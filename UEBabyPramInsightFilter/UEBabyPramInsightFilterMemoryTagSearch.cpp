module;

#include <re2/re2.h>

module UEBabyPramInsightFilterMemoryTagSearch;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightParser;
import UEBabyPramInsightParserMemoryTagsManager;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace InsightParser;

	namespace
	{
		// The name of a spec which the trace declares with an empty name is reported as unknown instead of an empty pair of
		// angle brackets.
		std::u8string_view DisplayName(std::u8string_view name)
		{
			return name.empty()
				? std::u8string_view{ u8"<Unknown>" }
				: name;
		}

		// Build the text of a reference to the spec of an id: the id and the name of the spec are written together, ex:
		// <5:Memory/Texture>, and an invalid id is reported as none, which is the case of a tag which has no parent or no tag
		// set. An id which the trace does not declare is reported as Unknown instead, because the id is still a valid
		// reference to a spec which the trace omits.
		template<typename IdType>
		std::u8string MakeReference(IdType id, std::u8string_view name)
		{
			if (!id)
			{
				return u8"<None>";
			}

			std::string id_text = std::to_string(id.id);

			std::u8string reference = u8"<";
			reference.append(reinterpret_cast<char8_t const*>(id_text.data()), id_text.size());
			reference += u8":";
			reference.append(name.data(), name.size());
			reference += u8">";
			return reference;
		}
	}

	MemoryTagSearchParser::MemoryTagSearchParser(Config in_config)
		: resource(in_config.resource)
		, memory_tags_manager(MemoryTagsManager::Config{ &resource })
		, matcher(std::move(in_config.matcher))
	{
	}

	bool MemoryTagSearchParser::IsNameMatched(std::u8string_view name) const
	{
		// An empty expression matches every spec, which is what a search with no pattern means: the user wants every spec
		// which the trace declares, and it is also the way to dump the whole memory tag tree of a trace. An empty re2
		// expression only matches the empty name, so it is answered here instead of by the expression.
		if (!matcher || matcher->pattern().empty())
		{
			return true;
		}

		return IsEventNameMatched(*matcher, name);
	}

	void MemoryTagSearchParser::OnMemoryTagSpecId(MemoryTagId tag_id, MemoryTagId parent_id, MemoryTagSetId tag_set_id, std::u8string_view tag_name)
	{
		memory_tags_manager.AddTag(tag_id, parent_id, tag_set_id, tag_name);
	}

	void MemoryTagSearchParser::OnMemoryTrackerSpec(MemoryTrackerId tracker_id, std::u8string_view tracker_name)
	{
		memory_tags_manager.AddTracker(tracker_id, tracker_name);
	}

	void MemoryTagSearchParser::OnMemoryTagSetSpec(MemoryTagSetId tag_set_id, std::u8string_view tag_set_name)
	{
		memory_tags_manager.AddTagSet(tag_set_id, tag_set_name);
	}

	bool MemoryTagSearchParser::IsParserRequired(ParserRequireFlag flag) const
	{
		// The specs are the only part of the memory channel which the search reads, so the routes of the snapshots are not
		// registered and their events are skipped by the trace parser instead of being decoded. The file itself is still
		// read end to end, so the search saves the parsing of the snapshots and not the reading of the trace.
		return flag == ParserRequireFlag::MEMORY_TAG;
	}

	void MemoryTagSearchParser::AllAnalyzeDone()
	{
		// The manager keeps the specs in the order of the trace, so only the base parser is completed.
		Parser::AllAnalyzeDone();
	}

	bool MemoryTagSearchParser::PrintToLog(Potato::Log::LogPrinter& printer)
	{
		using InsightFilter::OutputLog;

		OutputLog<L"MemoryTagSearchParser Output:">(printer);
		OutputLog<L"    (Memory Tag: the tag which the LLM channel of the trace declares; Memory Tracker: the tracker which the LLM channel declares; Memory Tag Set: the tag set which the LLM channel declares)">(
			printer
		);
		OutputLog<L"    (FullName: the name which the trace declares; Name: the last segment of the FullName after the last '/', which is the FullName itself when the tag has no parent; Parent: the id and the FullName of the parent tag; Tag Set: the id and the name of the tag set which a tag belongs to)">(
			printer
		);
		OutputLog<L"    Memory Tag <{}>, Memory Tracker <{}>, Memory Tag Set <{}>">(
			printer,
			memory_tags_manager.GetTagCount(),
			memory_tags_manager.GetTrackerCount(),
			memory_tags_manager.GetTagSetCount()
		);

		if (memory_tags_manager.IsEmpty())
		{
			OutputLog<L"    No memory spec is recorded in the trace.">(printer);
			return true;
		}

		// The specs are reported in the order of the trace, which is the order in which the manager keeps them, and every
		// kind is reported in its own section. A spec which the expression does not match is left out, so the report of a
		// search only carries the specs which were searched for.
		std::size_t matched_tag_set_count = 0;
		std::size_t matched_tracker_count = 0;
		std::size_t matched_tag_count = 0;

		for (auto const& block : memory_tags_manager.GetTagSets())
		{
			for (std::size_t index = 0; index < block.count; ++index)
			{
				auto const& spec = block.specs[index];
				if (!IsNameMatched(spec.name))
				{
					continue;
				}

				++matched_tag_set_count;
				OutputLog<L"    Tag Set <{}>: <{:}>">(
					printer,
					spec.id.id,
					DisplayName(spec.name)
				);
			}
		}

		for (auto const& block : memory_tags_manager.GetTrackers())
		{
			for (std::size_t index = 0; index < block.count; ++index)
			{
				auto const& spec = block.specs[index];
				if (!IsNameMatched(spec.name))
				{
					continue;
				}

				++matched_tracker_count;
				OutputLog<L"    Tracker <{}>: <{:}>">(
					printer,
					spec.id.id,
					DisplayName(spec.name)
				);
			}
		}

		for (auto const& block : memory_tags_manager.GetTags())
		{
			for (std::size_t index = 0; index < block.count; ++index)
			{
				auto const& spec = block.specs[index];
				if (!IsNameMatched(spec.full_name))
				{
					continue;
				}

				++matched_tag_count;

				// The parent tag and the tag set are resolved from the manager, so the report shows the name of the
				// association instead of its id only. The name of a spec is passed without the angle brackets, which
				// MakeReference adds around the whole reference.
				std::u8string_view parent_name = u8"Unknown";
				if (!spec.parent_id)
				{
					parent_name = u8"None";
				}
				else if (auto parent = memory_tags_manager.FindTag(spec.parent_id); parent != nullptr && !parent->full_name.empty())
				{
					parent_name = parent->full_name;
				}

				std::u8string_view tag_set_name = u8"Unknown";
				if (!spec.tag_set_id)
				{
					tag_set_name = u8"None";
				}
				else if (auto tag_set = memory_tags_manager.FindTagSets(spec.tag_set_id); tag_set != nullptr && !tag_set->name.empty())
				{
					tag_set_name = tag_set->name;
				}

				std::u8string parent_reference = MakeReference(spec.parent_id, parent_name);
				std::u8string tag_set_reference = MakeReference(spec.tag_set_id, tag_set_name);

				OutputLog<L"    Tag <{}>: <{:}>, Name:<{:}>, Parent:{}, Tag Set:{}">(
					printer,
					spec.id.id,
					DisplayName(spec.full_name),
					DisplayName(spec.name),
					std::u8string_view{ parent_reference },
					std::u8string_view{ tag_set_reference }
				);
			}
		}

		OutputLog<L"    Matched <{}> Tag Set(s), <{}> Tracker(s), <{}> Tag(s)">(
			printer,
			matched_tag_set_count,
			matched_tracker_count,
			matched_tag_count
		);

		return true;
	}
}
