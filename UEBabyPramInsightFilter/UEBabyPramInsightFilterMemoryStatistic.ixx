module;

export module UEBabyPramInsightFilterMemoryStatistic;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightParser;
import UEBabyPramInsightParserMemoryTagsManager;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace UEBabyPram::InsightParser;
}

export namespace UEBabyPram::InsightFilter
{
	// Collect the memory specs of an insight trace and report the id and the name of every one of them.
	//
	// The LLM channel of the trace declares the memory tags, the trackers and the tag sets by their id,
	// while a memory sample only carries the ids, so the specs are the only place where the name of an id
	// is found. The specs and their association, which is the parent tag and the tag set of a tag, are
	// stored by MemoryTagsManager, and the parser only forwards the callbacks of the trace to the manager
	// and reports what the manager carries.
	//
	// Only ParserRequireFlag::MEMORY is required, which is what makes the memory analyzer of the trace
	// parser register the LLM routes. The CPU event, the event spec, the context switch and the meta data
	// routes are not required, so the parser reads no event of them.
	//
	// The memory samples are not collected yet: OnMemoryTagSnap() is left empty, so the report carries
	// the specs of the trace instead of the samples of the trackers.
	struct MemoryStatisticParser : public Parser
	{
		struct Config
		{
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		MemoryStatisticParser(Config config = {});

		// Store the tag, the tracker and the tag set in the manager of the memory specs, which keeps the
		// association of the id and the name of the spec, and the parent tag and the tag set of a tag. The
		// same id is only stored once, the spec which is stored first is kept.
		virtual void OnMemoryTagSpecId(MemoryTagId tag_id, MemoryTagId parent_id, MemoryTagSetId tag_set_id, std::u8string_view tag_name) override;
		virtual void OnMemoryTrackerSpec(MemoryTrackerId tracker_id, std::u8string_view tracker_name) override;
		virtual void OnMemoryTagSetSpec(MemoryTagSetId tag_set_id, std::u8string_view tag_set_name) override;
		// The sample of the memory tags is not collected yet, only the specs of the trace are reported.
		virtual void OnMemoryTagSnap(MemoryTagSnapView) override {}
		// The memory analyzer registers the LLM routes only when the memory channel is required, and no
		// CPU event, event spec, context switch or meta data is needed by this parser.
		virtual bool IsParserRequired(ParserRequireFlag flag) const override;
		virtual void AllAnalyzeDone() override;
		bool PrintToLog(Potato::Log::LogPrinter& printer) override;

		// The manager of the specs which the trace declares, the report is written from it.
		MemoryTagsManager const& GetMemoryTagsManager() const { return memory_tags_manager; }

	protected:

		MemoryTagsManager memory_tags_manager;
	};
}
