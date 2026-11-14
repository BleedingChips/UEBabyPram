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
			// The ratio of the time range which is analyzed, 0.9 is the default and analyzes the middle 90% of the range.
			// The beginning and the end of the trace are the time when the trace is saved, so the frames there are not
			// accurate, and the middle of the range is what the sampling is for. Only the frames whose time range
			// intersects the sampled range are counted and listed, see GameThreadParser::PrintToLog().
			double sampling_ratio = 0.9;
			// The path of the CSV file which the frames are written into instead of the text report, the file is
			// created and its previous content is emptied. Every analyzed frame is one row, ordered by the time, and
			// the frames are not cut to the longest ones, so the file carries the whole timeline of the frames before
			// the report does. The text report is not printed when the path is set.
			std::optional<std::filesystem::path> csv_path;
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

		// One analyzed frame of the default mode, it is what the CSV report of the frames is written from: the summary
		// is taken before the records are sorted and cut to the longest ones, so the file carries every analyzed frame
		// in the order of the time, while the text report only carries the longest ones.
		struct FrameSummary
		{
			Potato::Misc::IndexSpan<DurationSec> time_range;
			DurationSec duration = DurationSec::zero();
			std::shared_ptr<ContextSwitchStreamer::Listener> context_switch_listener;
		};

		// Write the frames of the default mode into the CSV file of Config::csv_path, the file is created or emptied.
		// One row carries one frame, and the rows are ordered by the time, which is what the text report does not do:
		// it only reports the longest frames.
		bool WriteCSV() const;

		std::pmr::vector<FrameRecord> event_records;
		std::pmr::vector<FrameSummary> frame_summaries;
		// The path of the CSV file of the frames, it is described by Config::csv_path.
		std::optional<std::filesystem::path> csv_path;
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
		// The ratio of the time range which is analyzed, it is Config::sampling_ratio and it is 0.9 by default.
		double sampling_ratio = 0.9;

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