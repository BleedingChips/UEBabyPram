module UEBabyPramInsightParserMemoryTagsManager;

import UEBabyPramInsightParser;
import std;

namespace UEBabyPram::InsightParser
{
	MemoryTagsManager::MemoryTagsManager(Config in_config)
		: resource(in_config.resource)
		, string_blocks(&resource)
	{
	}

	MemoryTagsManager::~MemoryTagsManager()
	{
		// The blocks are owned by the manager, the storage of a block is freed to the resource which it
		// was allocated from.
		for (auto block : string_blocks)
		{
			block->~StringBlock();
			resource.deallocate(block, sizeof(StringBlock), alignof(StringBlock));
		}
	}

	std::u8string_view MemoryTagsManager::StoreString(std::u8string_view string)
	{
		if (string.empty())
		{
			return {};
		}

		// The characters are written after the characters which are already written into the block which
		// is being written, when the room of the block is enough for them.
		if (current_string_block != nullptr)
		{
			auto room = block_capacity - current_string_block_count;
			if (string.size() <= room)
			{
				auto offset = current_string_block_count;
				std::copy(string.begin(), string.end(), current_string_block->characters.data() + offset);
				current_string_block_count += string.size();
				return std::u8string_view{ current_string_block->characters.data() + offset, string.size() };
			}
		}

		// The block is allocated for the name, and the block becomes the block which is being written. The
		// characters of a name are always a slice of one block, so the characters which do not fit into the
		// new block are cut off.
		auto block = static_cast<StringBlock*>(resource.allocate(sizeof(StringBlock), alignof(StringBlock)));
		std::construct_at(block);
		string_blocks.push_back(block);
		current_string_block = block;
		current_string_block_count = 0;

		auto size = std::min(string.size(), block_capacity);
		std::copy_n(string.begin(), size, block->characters.data());
		current_string_block_count = size;
		return std::u8string_view{ block->characters.data(), size };
	}

	bool MemoryTagsManager::AddTag(MemoryTagId tag_id, MemoryTagId parent_id, MemoryTagSetId tag_set_id, std::u8string_view tag_name)
	{
		// An id which is not valid can not be referenced by a sample, so it is not stored. The spec of an id
		// which is already stored is kept, so the id is only stored once.
		if (!tag_id || Contains(tags, tag_id))
		{
			return false;
		}

		// Only the full name is stored, the name of the tag is a view into the full name: it is the last
		// segment after the last '/' when the tag has a parent, and it is the full name itself otherwise.
		std::u8string_view full_name = StoreString(tag_name);
		std::u8string_view name = full_name;
		if (parent_id)
		{
			auto slash = full_name.rfind(u8'/');
			if (slash != std::u8string_view::npos)
			{
				name = full_name.substr(slash + 1);
			}
		}

		auto& spec = Append(tags);
		spec.id = tag_id;
		spec.parent_id = parent_id;
		spec.tag_set_id = tag_set_id;
		spec.full_name = full_name;
		spec.name = name;
		return true;
	}

	bool MemoryTagsManager::AddTracker(MemoryTrackerId tracker_id, std::u8string_view tracker_name)
	{
		if (!tracker_id || Contains(trackers, tracker_id))
		{
			return false;
		}

		std::u8string_view name = StoreString(tracker_name);

		auto& spec = Append(trackers);
		spec.id = tracker_id;
		spec.name = name;
		return true;
	}

	bool MemoryTagsManager::AddTagSet(MemoryTagSetId tag_set_id, std::u8string_view tag_set_name)
	{
		if (!tag_set_id || Contains(tag_sets, tag_set_id))
		{
			return false;
		}

		std::u8string_view name = StoreString(tag_set_name);

		auto& spec = Append(tag_sets);
		spec.id = tag_set_id;
		spec.name = name;
		return true;
	}

	auto MemoryTagsManager::FindTag(MemoryTagId tag_id) const -> TagSpec const*
	{
		if (!tag_id)
		{
			return nullptr;
		}

		// The tags are walked block by block, the specs of a block are only read up to its count.
		for (auto const& block : tags)
		{
			for (std::size_t index = 0; index < block.count; ++index)
			{
				if (block.specs[index].id == tag_id)
				{
					return &block.specs[index];
				}
			}
		}
		return nullptr;
	}

	auto MemoryTagsManager::FindTracker(MemoryTrackerId tracker_id) const -> TrackerSpec const*
	{
		if (!tracker_id)
		{
			return nullptr;
		}

		for (auto const& block : trackers)
		{
			for (std::size_t index = 0; index < block.count; ++index)
			{
				if (block.specs[index].id == tracker_id)
				{
					return &block.specs[index];
				}
			}
		}
		return nullptr;
	}

	auto MemoryTagsManager::FindTagSets(MemoryTagSetId tag_set_id) const -> TagSetSpec const*
	{
		if (!tag_set_id)
		{
			return nullptr;
		}

		for (auto const& block : tag_sets)
		{
			for (std::size_t index = 0; index < block.count; ++index)
			{
				if (block.specs[index].id == tag_set_id)
				{
					return &block.specs[index];
				}
			}
		}
		return nullptr;
	}

	std::size_t MemoryTagsManager::GetTagCount() const
	{
		return GetCount(tags);
	}

	std::size_t MemoryTagsManager::GetTrackerCount() const
	{
		return GetCount(trackers);
	}

	std::size_t MemoryTagsManager::GetTagSetCount() const
	{
		return GetCount(tag_sets);
	}
}
