module;

export module UEBabyPramInsightParserMemoryTagsManager;

import UEBabyPramInsightParser;
import std;

export namespace UEBabyPram::InsightParser
{
	// The manager of the memory tags, the memory trackers and the memory tag sets which an insight trace
	// declares.
	//
	// The LLM channel of the trace declares the three kinds of the spec by their id, while a memory sample
	// only carries the ids, so the manager keeps the association of the id and the name of every spec, and
	// the association of a tag with its parent tag and with its tag set, so the id which a sample carries is
	// resolved to its name.
	//
	// The names are UTF-8 and are written into the blocks of block_capacity characters, and every kind of
	// the spec is stored in the blocks of block_capacity specs, which is the layout of SubEventSpecManager:
	// a block is only allocated when the last one is full, and a spec holds the views of its names instead
	// of a copy, so the memory of a name is only taken once. The blocks of the names are never moved, so the
	// views of the names stay valid while the manager is alive.
	//
	// A tag only stores its full name: the name of the tag is the last segment of the full name after the
	// last '/', and it is the full name itself when the tag has no parent. The name view therefore points
	// into the full name, and no second copy of it is stored.
	//
	// The manager returns the blocks of the specs only, never the names of a spec directly: the caller walks
	// the blocks and reads the views of the name from the specs. The lookups which the manager provides are
	// FindTag, FindTracker and FindTagSets, which search the blocks of their kind by walking them. The
	// pointer which a find returns stays valid until the next spec of that kind is added, because the vector
	// of the blocks may move its blocks when it grows.
	struct MemoryTagsManager
	{
		// The count of the characters which one block of the names holds, and the count of the specs which
		// one block of the specs holds.
		static constexpr std::size_t block_capacity = 1024;

		struct Config
		{
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		// The spec of one memory tag. Both names are views into the blocks of the names of the manager:
		// full_name is the name which the trace declares, and name is the last segment of it after the last
		// '/'. When the tag has no parent, name is the same view as full_name.
		struct TagSpec
		{
			MemoryTagId id{};
			// The id of the parent tag, an invalid id means that the tag has no parent.
			MemoryTagId parent_id{};
			// The id of the tag set which the tag belongs to, an invalid id means that the tag has no tag
			// set.
			MemoryTagSetId tag_set_id{};
			std::u8string_view full_name{};
			std::u8string_view name{};
		};

		// The spec of one memory tracker, the name is a view into the blocks of the names of the manager.
		struct TrackerSpec
		{
			MemoryTrackerId id{};
			std::u8string_view name{};
		};

		// The spec of one memory tag set, the name is a view into the blocks of the names of the manager.
		struct TagSetSpec
		{
			MemoryTagSetId id{};
			std::u8string_view name{};
		};

		// One block of the specs of one kind, the specs of the block are filled from its beginning, so the
		// count of the block tells how many of them are used.
		template<typename SpecType>
		struct Block
		{
			std::array<SpecType, block_capacity> specs;
			std::size_t count = 0;
		};

		MemoryTagsManager(Config config = {});
		MemoryTagsManager(MemoryTagsManager const&) = delete;
		~MemoryTagsManager();

		// Store the full name of a tag in the blocks of the names, and append the spec of the tag to the
		// blocks of the specs with the views of its names. The name of the tag is the last segment of the
		// full name when the tag has a parent, and it is the full name itself otherwise. True is returned
		// when the spec is stored for the first time, and false when the id is invalid or already stored.
		bool AddTag(MemoryTagId tag_id, MemoryTagId parent_id, MemoryTagSetId tag_set_id, std::u8string_view tag_name);
		// Store the name of a tracker and append its spec, the return value is the same as the one of
		// AddTag.
		bool AddTracker(MemoryTrackerId tracker_id, std::u8string_view tracker_name);
		// Store the name of a tag set and append its spec, the return value is the same as the one of
		// AddTag.
		bool AddTagSet(MemoryTagSetId tag_set_id, std::u8string_view tag_set_name);

		// Find the spec of the id by walking the blocks of the tags. Nullptr is returned when the id is
		// invalid or when the trace does not declare it. The pointer stays valid until the next tag is
		// added.
		TagSpec const* FindTag(MemoryTagId tag_id) const;
		// Find the spec of the id by walking the blocks of the trackers, the return value and the lifetime
		// of the pointer are the same as the ones of FindTag.
		TrackerSpec const* FindTracker(MemoryTrackerId tracker_id) const;
		// Find the spec of the id by walking the blocks of the tag sets, the return value and the lifetime
		// of the pointer are the same as the ones of FindTag.
		TagSetSpec const* FindTagSets(MemoryTagSetId tag_set_id) const;

		// The blocks of the specs of every kind, in the order in which the trace declares them. The count
		// of every block tells how many specs of it are used.
		std::vector<Block<TagSpec>> const& GetTags() const { return tags; }
		std::vector<Block<TrackerSpec>> const& GetTrackers() const { return trackers; }
		std::vector<Block<TagSetSpec>> const& GetTagSets() const { return tag_sets; }

		// How many specs of every kind the trace declares.
		std::size_t GetTagCount() const;
		std::size_t GetTrackerCount() const;
		std::size_t GetTagSetCount() const;
		// Whether the trace declares no spec at all.
		bool IsEmpty() const { return tags.empty() && trackers.empty() && tag_sets.empty(); }

	protected:

		// Append one spec to the last block of the vector, and allocate a new block when the last one is
		// full.
		template<typename SpecType>
		static SpecType& Append(std::vector<Block<SpecType>>& blocks)
		{
			if (blocks.empty() || blocks.back().count == block_capacity)
			{
				blocks.emplace_back();
			}

			Block<SpecType>& block = blocks.back();
			return block.specs[block.count++];
		}

		// Count the specs which the blocks of one kind carry.
		template<typename SpecType>
		static std::size_t GetCount(std::vector<Block<SpecType>> const& blocks)
		{
			std::size_t count = 0;
			for (auto const& block : blocks)
			{
				count += block.count;
			}
			return count;
		}

		// Whether the blocks of one kind already carry the spec of the id, which is found by walking the
		// blocks the same way as FindTag does.
		template<typename SpecType, typename IdType>
		static bool Contains(std::vector<Block<SpecType>> const& blocks, IdType id)
		{
			for (auto const& block : blocks)
			{
				for (std::size_t index = 0; index < block.count; ++index)
				{
					if (block.specs[index].id == id)
					{
						return true;
					}
				}
			}
			return false;
		}

		// One block of the characters of the names, a name never crosses the boundary of a block, so the
		// characters of a name are always a slice of one block.
		struct StringBlock
		{
			std::array<char8_t, block_capacity> characters;
		};

		// Write the name into the blocks of the names and return the view of it. The name is written into
		// the room which is left in the block which is being written when the room is enough, and a new
		// block is created otherwise. The characters which do not fit into one block are cut off, because
		// the characters of a name are always a slice of one block.
		std::u8string_view StoreString(std::u8string_view string);

		std::pmr::unsynchronized_pool_resource resource;
		// The blocks of the characters, they own the characters of every name of the manager.
		std::pmr::vector<StringBlock*> string_blocks;
		// The block which is being written, it is where the next name is written into, and it is empty
		// when no name is stored yet.
		StringBlock* current_string_block = nullptr;
		// How many characters are written into current_string_block, which is where the next character is
		// written to.
		std::size_t current_string_block_count = 0;

		// The blocks of the specs of the three kinds, in the order of the trace.
		std::vector<Block<TagSpec>> tags;
		std::vector<Block<TrackerSpec>> trackers;
		std::vector<Block<TagSetSpec>> tag_sets;
	};
}
