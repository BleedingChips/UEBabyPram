module;

#include <cassert>

module UEBabyPramInsightParserContextSwitchStreamer;

import UEBabyPramInsightParserContextSwitchStreamer;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightParser
{
	ContextSwitchStreamer::ContextSwitchStreamer(Config config)
		: thread_streams(config.resource)
		, resource(config.resource)
		, max_block_count(config.max_block_count)
		, min_time_span(config.min_time_span)
	{
	}

	ContextSwitchStreamer::~ContextSwitchStreamer() = default;

	void ContextSwitchStreamer::AddContextSwitchEvent(ThreadSystemID thread_id, std::size_t active_core, Potato::Misc::IndexSpan<DurationSec> active_time)
	{
		// The event is counted, so a parser tells whether the trace carries the context switch channel at all.
		context_switch_event_count += 1;

		auto& stream = GetOrCreateStream(thread_id);

		// The block which is being written is only empty after the whole stream is completed, so the block which the
		// events which follow belong to is created here.
		if (!stream.current_block)
		{
			stream.current_block = CreateBlock();
		}

		auto& block = *stream.current_block;
		assert(!block.IsFull());

		block.events[block.event_count] = Event{ active_core, active_time };
		block.event_count += 1;

		// The block is completed when it is full: it is given to the listeners, and the retention policy makes the
		// room for the events which follow.
		if (block.IsFull())
		{
			CompleteBlock(stream);
		}
	}

	auto ContextSwitchStreamer::AddListener(ThreadSystemID thread_id, Potato::Misc::IndexSpan<DurationSec> time_range) -> std::shared_ptr<Listener>
	{
		auto listener = std::make_shared<Listener>(time_range, resource);

		auto& stream = GetOrCreateStream(thread_id);

		// The blocks which are already completed are given to the new listener as well, so a listener which is
		// created after the events which it is interested in are stored still collects them.
		for (auto& block : stream.blocks)
		{
			if (listener->time_range.IsOverlap(block->time_range))
			{
				listener->blocks.push_back(block);
			}
		}

		// The listener is held by the stream of the thread weakly, so it collects the blocks while the requester holds
		// its pointer, and the entries of the listeners which the requester dropped are removed by the prune below and
		// by the walk of the blocks.
		stream.listeners.push_back(listener);

		// The expired listeners are removed from the stream every listener_prune_interval additions, so the list does
		// not grow with the listeners which are never collected again: a parser creates a listener for every event
		// which it records, and most of them are dropped before the end of the analysis.
		stream.listener_add_count += 1;
		if (stream.listener_add_count % listener_prune_interval == 0)
		{
			PruneListeners(stream);
		}

		return listener;
	}

	void ContextSwitchStreamer::PruneListeners(ThreadStream& stream)
	{
		stream.listeners.erase(
			std::remove_if(
				stream.listeners.begin(),
				stream.listeners.end(),
				[](std::weak_ptr<Listener> const& listener) { return listener.expired(); }
			),
			stream.listeners.end()
		);
	}

	void ContextSwitchStreamer::Flush()
	{
		for (auto& stream : thread_streams)
		{
			// The block which is being written is completed as well, so the events which are stored after the last
			// full block are given to the listeners as a completed block of the thread.
			if (stream.current_block && stream.current_block->event_count > 0)
			{
				CompleteBlock(stream);
			}

			// Every block of the thread is dropped, the blocks which a listener collected are kept by the listener,
			// and the blocks which no listener wants are destroyed here.
			stream.current_block.reset();
			stream.blocks.clear();
		}
	}

	auto ContextSwitchStreamer::GetOrCreateStream(ThreadSystemID thread_id) -> ThreadStream&
	{
		for (auto& stream : thread_streams)
		{
			if (stream.thread_system_id == thread_id)
			{
				return stream;
			}
		}

		return thread_streams.emplace_back(thread_id, resource);
	}

	auto ContextSwitchStreamer::CreateBlock() -> std::shared_ptr<EventBlock>
	{
		// The block is allocated out of the resource of the config, so the streamer keeps no memory of its own, and
		// the block is owned by a shared pointer, so a block which is given to a listener stays valid while it is
		// collected.
		return std::allocate_shared<EventBlock>(std::pmr::polymorphic_allocator<EventBlock>{ resource });
	}

	void ContextSwitchStreamer::CompleteBlock(ThreadStream& stream)
	{
		// The block which is being written is completed here, so the events which follow belong to a new block, which
		// is created by the next AddContextSwitchEvent().
		auto block = std::move(stream.current_block);
		assert(block && block->event_count > 0);

		CollectBlock(stream, block);

		// The completed block is kept by the thread, and the thread checks whether the earliest block of the blocks
		// which it keeps may be dropped, so both of the conditions of the retention policy are checked here.
		stream.blocks.push_back(block);

		while (stream.blocks.size() > max_block_count && GetRetainedTimeSpan(stream) > min_time_span)
		{
			// The earliest block is dropped, and a dropped block is never reused: the block which a listener
			// collected stays valid while the listener holds it, and the block which no listener wants is destroyed
			// here.
			stream.blocks.erase(stream.blocks.begin());
		}
	}

	void ContextSwitchStreamer::CollectBlock(ThreadStream& stream, std::shared_ptr<EventBlock> const& block)
	{
		// The time range of the block is the range from the beginning of its first event to the end of its last event,
		// which is what the listeners are checked against.
		block->time_range = Potato::Misc::IndexSpan<DurationSec>{
			block->events.front().active_time_range.Begin(),
			block->events[block->event_count - 1].active_time_range.End()
		};

		for (auto ite = stream.listeners.begin(); ite != stream.listeners.end(); )
		{
			// The listener is only held weakly, so a listener which its requester dropped is removed here: it can not
			// collect the block any more, and there is no reason to keep the entry.
			auto listener = ite->lock();
			if (!listener)
			{
				ite = stream.listeners.erase(ite);
				continue;
			}

			// The block overlaps the time span of the listener, so the listener collects it, and the block stays valid
			// while the listener holds it.
			if (listener->time_range.IsOverlap(block->time_range))
			{
				listener->blocks.push_back(block);
			}

			// The block is completed past the end of the time span of the listener, so none of the blocks which
			// follow can overlap the listener any more, and the listener is removed from the thread.
			if (block->time_range.End() > listener->time_range.End())
			{
				ite = stream.listeners.erase(ite);
				continue;
			}

			++ite;
		}
	}

	auto ContextSwitchStreamer::Listener::GetContextSwitchStatis(Potato::Misc::IndexSpan<DurationSec> time_range) const -> Statis
	{
		DurationSec active_time = DurationSec::zero();
		std::size_t switch_count = 0;

		// The blocks are collected in the order of the stream, so the events are visited once, and the part of an
		// event which is inside the range is the time which the thread really spent on the CPU in the range.
		for (auto& block : blocks)
		{
			// A block which is entirely out of the range carries no event of the range, so it is skipped.
			if (block->time_range.IsDisjoint(time_range))
			{
				continue;
			}

			for (std::size_t index = 0; index < block->event_count; ++index)
			{
				auto const& event = block->events[index];
				auto begin = std::max(event.active_time_range.Begin(), time_range.Begin());
				auto end = std::min(event.active_time_range.End(), time_range.End());

				// The event is counted when its range reaches the range of the analysis, an event which only touches
				// the boundary of the range has no time inside it, but it is a context switch of the range anyway.
				if (end >= begin)
				{
					active_time += end - begin;
					switch_count += 1;
				}
			}
		}

		// A range which carries no collected context switch event is a range in which the thread was on the CPU, so
		// the time which it spent on the CPU is the whole range.
		if (switch_count == 0)
		{
			return Statis{ time_range.Size(), 0 };
		}

		return Statis{ active_time, switch_count };
	}

	auto ContextSwitchStreamer::GetRetainedTimeSpan(ThreadStream const& stream) const -> DurationSec
	{
		assert(!stream.blocks.empty());

		// The time of the kept blocks is the range from the beginning of the earliest block to the end of the newest
		// one, and the newest block is the one which is completed last.
		return Potato::Misc::IndexSpan<DurationSec>{
			stream.blocks.front()->time_range.Begin(),
			stream.blocks.back()->time_range.End()
		}.Size();
	}
}
