module;

export module UEBabyPramInsightParserContextSwitchStreamer;

import UEBabyPramInsightParser;
import Potato;
import std;

export namespace UEBabyPram::InsightParser
{
	// The streamer of the context switch events.
	//
	// The events of every thread are kept in the memory blocks of the thread, which are created with a fixed count
	// of events, and a block is only created when the last one is full, so the events which are already stored are
	// never moved. The blocks of a thread are only kept while the analysis needs them: after a block is full, the
	// streamer checks whether it keeps more blocks than Config::max_block_count and whether the time which the kept
	// blocks cover is longer than Config::min_time_span, and only when both of them hold, the earliest block is
	// dropped, so the memory of the streamer is bounded. A dropped block is never reused: a block which a listener
	// collected is held by the listener and it is destroyed when the last of them drops it, and a block which no
	// listener wants is destroyed right away.
	//
	// A listener of a thread is the time span which it is interested in, and the blocks which it collects: when a
	// block of the thread is completed and its time range overlaps the time span of the listener, the block is
	// appended to the listener, and the listener holds the block by a shared pointer, so a block which a listener
	// collected stays valid while the listener holds it. A listener is removed from the thread when the end of a
	// completed block passes the end of its time span, because none of the blocks which follow can overlap it any
	// more, and it is created by AddListener() and owned by the requester: the streamer only holds it weakly, so a
	// listener which the requester drops is not kept alive by the streamer, and the streamer removes the listeners
	// which are gone every Config::listener_prune_interval additions of a thread.
	struct ContextSwitchStreamer
	{
		// How many context switch events one memory block holds, every block has exactly this many events.
		static constexpr std::size_t block_event_capacity = 1024 * 16;

		// How many listeners one thread may add before the streamer removes the listeners which are already gone from
		// it: a listener is only held weakly, so the ones which the requester dropped stay in the list until they are
		// removed, and they are removed once per this count of additions.
		static constexpr std::size_t listener_prune_interval = 2048;

		struct Config
		{
			// How many memory blocks one thread keeps at most, the earliest block is only dropped when the count of
			// the blocks is over it.
			std::size_t max_block_count = 3;
			// How long the time of the kept blocks is at least, the earliest block is only dropped when the time of
			// the kept blocks is longer than it.
			DurationSec min_time_span = DurationSec{ 1 };
			// The resource which the blocks of the streamer are allocated from, the streamer keeps no memory of its
			// own.
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		// The event of one context switch, it is the same structure as ContextSwitchEventList::ThreadList::Event.
		struct Event
		{
			std::size_t active_core = std::numeric_limits<std::size_t>::max();
			Potato::Misc::IndexSpan<DurationSec> active_time_range;
		};

		// One memory block of the events of one thread, the events are kept in the order of the stream, and the time
		// range of the block is the range from the beginning of its first event to the end of its last event, which
		// is only valid when the block is completed.
		struct EventBlock
		{
			std::array<Event, block_event_capacity> events;
			// How many events are written into the block, the block is full when all of its events are written.
			std::size_t event_count = 0;
			Potato::Misc::IndexSpan<DurationSec> time_range;

			bool IsFull() const { return event_count >= block_event_capacity; }
		};

		// The listener of the context switch events of one thread, it collects the blocks whose time range overlaps
		// the time span which it is created with. It is created by AddListener(), and it is owned by the requester:
		// the streamer keeps a weak reference of it, so the listener is destroyed when the requester drops it, and
		// the blocks which it collected stay valid while it is alive, because every block is held by a shared
		// pointer as well.
		struct Listener
		{
			Listener(Potato::Misc::IndexSpan<DurationSec> time_range, std::pmr::memory_resource* resource = std::pmr::get_default_resource())
				: time_range(time_range), blocks(resource) {}

			struct Statis
			{
				// The time of the range which the thread really spent on the CPU, i.e. the time of the range with the
				// time which the thread spent out of the CPU removed.
				DurationSec removed_context_switch_time = DurationSec::zero();
				// How many context switch events of the thread are in the range.
				std::size_t switch_count = 0;
			};

			// The statistic of the context switch events which the listener collected in the time range: how much of
			// the range the thread spent on the CPU and how many context switch events it has in the range. A range
			// which carries no collected context switch event is a range in which the thread was on the CPU, so the
			// range is reported as it is.
			Statis GetContextSwitchStatis(Potato::Misc::IndexSpan<DurationSec> time_range) const;

			// The time span which the listener is interested in, a block is collected when the two of them overlap.
			Potato::Misc::IndexSpan<DurationSec> time_range;
			// The blocks which are collected, a collected block stays valid while the listener holds it.
			std::span<std::shared_ptr<EventBlock const> const> GetBlocks() const { return blocks; }

		protected:

			friend struct ContextSwitchStreamer;

			std::pmr::vector<std::shared_ptr<EventBlock const>> blocks;
		};

		ContextSwitchStreamer(Config config = {});
		ContextSwitchStreamer(ContextSwitchStreamer const&) = delete;
		ContextSwitchStreamer(ContextSwitchStreamer&&) = default;
		~ContextSwitchStreamer();

		// Store one context switch event of the thread, the event is appended to the block which is being written,
		// and the block is completed when it is full.
		void AddContextSwitchEvent(ThreadSystemID thread_id, std::size_t active_core, Potato::Misc::IndexSpan<DurationSec> active_time);

		// Whether any context switch event is stored, i.e. whether the trace carries the context switch channel at
		// all. A trace which carries no such event carries no time which a thread spent out of the CPU, so the
		// reporters which use the streamer report the Context Switch Time of every event as 0, and they tell the
		// user that the time is missing instead of the time which the thread was really on the CPU.
		bool HasContextSwitchEvent() const { return context_switch_event_count != 0; }

		// How many context switch events are stored, 0 when the trace carries none of them.
		std::size_t GetContextSwitchEventCount() const { return context_switch_event_count; }

		// Create and register a listener of one thread for a time span, and return it. Every block of that thread
		// which is completed while its time range overlaps the span is collected by the listener, and the blocks
		// which are completed before the listener is created are collected as well, so a listener which is created
		// after the events which it is interested in are stored still collects them. The listener is removed when the
		// end of the completed block passes the end of the span. The listener is only held weakly by the thread, so it
		// collects the blocks while the requester holds it, and a listener which the requester drops is removed the
		// next time the listeners of the thread are pruned.
		std::shared_ptr<Listener> AddListener(ThreadSystemID thread_id, Potato::Misc::IndexSpan<DurationSec> time_range);

		// Give the block which is being written of every thread to the listeners of the thread as a completed block,
		// and drop every block of every thread. The blocks which are collected by a listener are kept by the
		// listener, the blocks which no listener wants are destroyed here.
		void Flush();

	protected:

		// The stream of one thread.
		struct ThreadStream
		{
			ThreadStream(ThreadSystemID thread_id, std::pmr::memory_resource* resource)
				: thread_system_id(thread_id), blocks(resource), listeners(resource) {}

			ThreadStream(ThreadStream const&) = delete;
			ThreadStream(ThreadStream&&) = default;

			ThreadSystemID thread_system_id;
			// The completed blocks which the streamer keeps, the earliest block is the first one and the newest is
			// the last. A block which the retention policy drops is destroyed here when no listener holds it, and a
			// block which a listener collected is only kept by the listeners which collected it.
			std::pmr::vector<std::shared_ptr<EventBlock>> blocks;
			// The block which the next event is appended to, it is empty after the whole stream is completed.
			std::shared_ptr<EventBlock> current_block;
			// The listeners of the thread, the thread only holds them weakly, so a listener which its requester
			// dropped goes away by itself, and the entry which is left is removed by PruneListeners().
			std::pmr::vector<std::weak_ptr<Listener>> listeners;
			// How many listeners the thread has added, the expired listeners are removed from the stream every
			// ContextSwitchStreamer::listener_prune_interval additions, so the list does not grow with the listeners
			// which are never collected again.
			std::size_t listener_add_count = 0;
		};

		ThreadStream& GetOrCreateStream(ThreadSystemID thread_id);
		// Allocate a block of block_event_capacity events out of the resource of the config.
		std::shared_ptr<EventBlock> CreateBlock();
		// Complete the block which is being written: give it to the listeners of the thread, and drop the earliest
		// block when the retention policy allows it, a dropped block is never reused.
		void CompleteBlock(ThreadStream& stream);
		// Set the time range of the block, append it to the block list of every listener of the thread which it
		// overlaps, and remove the listeners which the block has passed.
		void CollectBlock(ThreadStream& stream, std::shared_ptr<EventBlock> const& block);
		// Remove the listeners of the thread which are already gone: the stream only holds them weakly, so an entry
		// whose listener was dropped by its requester is removed here. It is called every
		// listener_prune_interval additions of the thread, and the walk of the blocks removes the expired entries as
		// well.
		static void PruneListeners(ThreadStream& stream);
		// The time which the kept blocks of the thread cover, from the beginning of the earliest block to the end of
		// the newest one.
		DurationSec GetRetainedTimeSpan(ThreadStream const& stream) const;

		std::pmr::vector<ThreadStream> thread_streams;
		std::pmr::memory_resource* resource;
		std::size_t max_block_count = 3;
		DurationSec min_time_span = DurationSec{ 1 };
		// How many context switch events are stored, it is what HasContextSwitchEvent() is answered from.
		std::size_t context_switch_event_count = 0;
	};
}
