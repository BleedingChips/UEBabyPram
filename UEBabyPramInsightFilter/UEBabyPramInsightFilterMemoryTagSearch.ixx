module;

#include <re2/re2.h>

export module UEBabyPramInsightFilterMemoryTagSearch;

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
	// Search the memory specs which the LLM channel of the trace declares by the regular expression of their name: the
	// memory tags, the memory trackers and the memory tag sets are searched by one expression, and every spec whose name is
	// matched is reported with its id and its name. A tag is reported with the name of its parent tag and of its tag set as
	// well, which is the association which the name of a tag is built from.
	//
	// The search is the way from a name to the id which a snapshot carries: the report of the values of the memory tags
	// carries the name of a tag, and the id of that name is what a snapshot is read by, ex: through
	// MemoryTagSnapView::GetMemoryTagValues(), which answers the value of every tag of an id.
	//
	// Only ParserRequireFlag::MEMORY_TAG is required: the specs are the only part of the memory channel which this parser
	// reads, so the events of the snapshots are not registered and every one of them is skipped by the trace parser instead
	// of being decoded and accumulated. The trace file is still read from its first byte to its last one, so the elapsed
	// time of the search is dominated by reading the file, and only the parsing of the snapshots is saved.
	struct MemoryTagSearchParser : public Parser
	{
		struct Config
		{
			// The regular expression of the name, the same syntax as the one of -r and -ts: the whole name has to be matched,
			// and an empty expression matches every spec. The expression is compiled by the command line from its text, so
			// the parser only receives the matcher which is ready to be used.
			std::shared_ptr<re2::RE2> matcher;
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		MemoryTagSearchParser(Config config = {});

		// Store the tag, the tracker and the tag set in the manager of the memory specs, which keeps the association of the
		// id and the name of the spec, and the parent tag and the tag set of a tag.
		virtual void OnMemoryTagSpecId(MemoryTagId tag_id, MemoryTagId parent_id, MemoryTagSetId tag_set_id, std::u8string_view tag_name) override;
		virtual void OnMemoryTrackerSpec(MemoryTrackerId tracker_id, std::u8string_view tracker_name) override;
		virtual void OnMemoryTagSetSpec(MemoryTagSetId tag_set_id, std::u8string_view tag_set_name) override;
		// The snapshots are not read by this parser, so the memory analyzer does not register their route for it.
		virtual bool IsParserRequired(ParserRequireFlag flag) const override;
		virtual void AllAnalyzeDone() override;
		bool PrintToLog(Potato::Log::LogPrinter& printer) override;

		// The manager of the specs which the trace declares, the report is written from it.
		MemoryTagsManager const& GetMemoryTagsManager() const { return memory_tags_manager; }

	protected:

		// Whether the name of a spec is selected by the regular expression of the search.
		bool IsNameMatched(std::u8string_view name) const;

		std::pmr::unsynchronized_pool_resource resource;
		MemoryTagsManager memory_tags_manager;
		std::shared_ptr<re2::RE2> matcher;
	};
}
