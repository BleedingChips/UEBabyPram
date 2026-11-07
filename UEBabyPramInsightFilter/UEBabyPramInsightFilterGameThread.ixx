module;

export module UEBabyPramInsightFilterGameThread;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightParser;
import UEBabyPramInsightParserContextSwitchStreamer;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace UEBabyPram::InsightParser;
}

export namespace UEBabyPram::InsightFilter
{

	struct GameThreadParser : public Parser
	{
		struct Config
		{
			std::size_t max_record_frame = 10;
			std::size_t max_exclude_time_count = 30;
			double sampling_ratio = 0.8;
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		void OnThreadDiscovered(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name) override;
		virtual void OnCPUEventDiscovered(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line) override;
		virtual void OnCPUScopeEventDiscovered(EventID id, std::u8string_view event_name) override;
		virtual void OnCPUStackTree(ThreadCPUEventView event_scope) override;
		
		void ContextSwitchEvent(ThreadSystemID thread_id, std::size_t core_name, Potato::Misc::IndexSpan<DurationSec> duration) override;
		virtual bool IsThreadRequired(ThreadID thread_id) const override;
		// The thread is only required when it is the game thread, which is the thread which the frames are recorded on.
		virtual bool IsThreadRequired(ThreadSystemID thread_system_id) const override;
		virtual bool IsParserRequired(ParserRequireFlag flag) const override { return true; }
		virtual void AllAnalyzeDone() override;
		bool PrintToLog(Potato::Log::LogPrinter& printer) override;
		GameThreadParser(Config config = {});
	
	protected:

		std::pmr::unsynchronized_pool_resource resource;
		// The streamer of the context switch events, the blocks of the events are streamed to the listener which every
		// recorded frame creates for its own time range.
		ContextSwitchStreamer context_switch_streamer;
		EventSpecManager events_manager;

		struct FrameRecord
		{
			FrameRecord(std::pmr::memory_resource* resource) : events(resource) {}
			FrameRecord(FrameRecord const&) = default;
			FrameRecord(FrameRecord&&) = default;
			FrameRecord& operator=(FrameRecord&&) = default;
			DurationSec duration = DurationSec::zero();
			Potato::Misc::IndexSpan<DurationSec> time_range;
			// The listener which collects the context switch blocks of the frame, it is created with the time range of the
			// frame, so the frame reads the time which the game thread spent out of the CPU inside the frame.
			std::shared_ptr<ContextSwitchStreamer::Listener> context_switch_listener;
			std::pmr::vector<ThreadCPUEvent> events;
		};

		std::pmr::vector<FrameRecord> event_records;
		DurationSec min_duration = DurationSec::zero();
		std::size_t max_record_frame = 10;
		std::size_t max_exclude_time_count = 30;
		std::array<std::size_t, 5> fps_frame_record = {0, 0, 0, 0, 0};
		InsightParser::DurationSec total_time = InsightParser::DurationSec::zero();
		Potato::Misc::IndexSpan<DurationSec> total_range;
		Potato::Misc::IndexSpan<DurationSec> sampled_range;
		std::size_t total_count = 0;
		ThreadID game_thread_id;
		ThreadSystemID game_thread_system_id;
		std::vector<EventID> tick_event_id;
		double sampling_ratio = 0.8;

		struct CrossFrameEventRecord
		{
			std::u8string_view event_name;
			std::size_t count = 0;
			DurationSec durations = DurationSec::zero();
			DurationSec durations_without_context_switch = DurationSec::zero();
			Potato::Misc::IndexSpan<DurationSec> max_time_range;
			std::size_t from_frame_index = 0;
		};

		std::pmr::vector<CrossFrameEventRecord> longest_frame_records;
		std::pmr::vector<CrossFrameEventRecord> longest_frame_records_in_top;
	};
}