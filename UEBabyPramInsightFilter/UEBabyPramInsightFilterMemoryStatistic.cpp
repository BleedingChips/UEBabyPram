module UEBabyPramInsightFilterMemoryStatistic;

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
		// The name of a spec which the trace declares with an empty name is reported as unknown instead of
		// an empty pair of angle brackets.
		std::u8string_view DisplayName(std::u8string_view name)
		{
			return name.empty()
				? std::u8string_view{ u8"<Unknown>" }
				: name;
		}

		// Build the text of a reference to the spec of an id: the id and the name of the spec are written
		// together, ex: <5:Memory/Texture>, and an invalid id is reported as none, which is the case of a
		// tag which has no parent or no tag set. An id which the trace does not declare is reported as
		// Unknown instead, because the id is still a valid reference to a spec which the trace omits.
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

	MemoryStatisticParser::MemoryStatisticParser(Config in_config)
		: memory_tags_manager(MemoryTagsManager::Config{ in_config.resource })
	{
	}

	void MemoryStatisticParser::OnMemoryTagSpecId(MemoryTagId tag_id, MemoryTagId parent_id, MemoryTagSetId tag_set_id, std::u8string_view tag_name)
	{
		memory_tags_manager.AddTag(tag_id, parent_id, tag_set_id, tag_name);
	}

	void MemoryStatisticParser::OnMemoryTrackerSpec(MemoryTrackerId tracker_id, std::u8string_view tracker_name)
	{
		memory_tags_manager.AddTracker(tracker_id, tracker_name);
	}

	void MemoryStatisticParser::OnMemoryTagSetSpec(MemoryTagSetId tag_set_id, std::u8string_view tag_set_name)
	{
		memory_tags_manager.AddTagSet(tag_set_id, tag_set_name);
	}

	bool MemoryStatisticParser::IsParserRequired(ParserRequireFlag flag) const
	{
		// The LLM channel of the trace carries the memory specs, which is the only part of the trace which
		// this parser reads. The CPU event, the event spec, the context switch and the meta data are not
		// needed, so their routes are not registered.
		return flag == ParserRequireFlag::MEMORY;
	}

	void MemoryStatisticParser::AllAnalyzeDone()
	{
		// The manager keeps the specs in the order of the trace, so only the base parser is completed.
		Parser::AllAnalyzeDone();
	}

	bool MemoryStatisticParser::PrintToLog(Potato::Log::LogPrinter& printer)
	{
		using InsightFilter::OutputLog;

		OutputLog<L"MemoryStatisticParser Output:">(printer);
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

		// The manager keeps the specs in the order of the trace, so the report of the same trace is always
		// the same. The blocks of the specs are walked directly, every block carries the count of the specs
		// which it uses.
		for (auto const& block : memory_tags_manager.GetTagSets())
		{
			for (std::size_t index = 0; index < block.count; ++index)
			{
				auto const& spec = block.specs[index];

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

				// The parent tag and the tag set are resolved from the manager, so the report shows the name
				// of the association instead of its id only. The name of a spec is passed without the angle
				// brackets, which MakeReference adds around the whole reference.
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

		return true;
	}
}
