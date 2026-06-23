// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "AutoRTFM.h"
#include "BuildMacros.h"
#include "Utils.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdint.h>

namespace AutoRTFM
{
	struct FWriteLogEntry final
	{
		// The address of the write.
		std::byte* LogicalAddress = nullptr;
		
		// A pointer to the original data before the write occurred.
		std::byte* Data = nullptr;
		
		// The size of the write in bytes.
		size_t Size = 0;
		
		// If true, then this write will not be considered by the AutoRTFM
		// memory validator.
		bool bNoMemoryValidation = false;
	};

	// FWriteLog holds an ordered list of write records which can be iterated
	// forwards and backwards.
	// Ensure changes to this class are kept in sync with Unreal.natvis.
	class FWriteLog final
	{
	public:
		// Number of bits used by the FRecord to represent a write's size.
		static constexpr size_t RecordSizeBits = 15;

		// The maximum size for a single write log entry.
		// Writes will be split into multiple entries if the write is too large.
		static constexpr size_t RecordMaxSize = (1u << RecordSizeBits) - 1;

	private:
		struct FRecord
		{
			void Set(std::byte* Address, size_t Size, bool bNoMemoryValidation)
			{
				AUTORTFM_ASSERT_DEBUG((reinterpret_cast<uintptr_t>(Address) & 0xFFFF0000'00000000) == 0);
				AUTORTFM_ASSERT_DEBUG(Size <= 0x7FFF);
		
				Bits = reinterpret_cast<uint64_t>(Address);
				Bits <<= 1;
				Bits |= bNoMemoryValidation;
				Bits <<= 15;
				Bits |= Size;
			}

			void Grow(size_t Amount)
			{
				AUTORTFM_ASSERT_DEBUG(Size() + Amount <= 0x7FFF);
				Bits += Amount;
			}

			std::byte* Address() const
			{
				return reinterpret_cast<std::byte*>(Bits >> 16);
			}

			bool NoMemoryValidation() const
			{
				return Bits & 0x8000;
			}

			size_t Size() const
			{
				return Bits & 0x7FFF;
			}

			uint64_t Bits;
		};

		static_assert(sizeof(uintptr_t) == 8, "assumption: a pointer is 8 bytes");
		static_assert(sizeof(FRecord) == 8);

		// Ensure changes to this structure are kept in sync with Unreal.natvis.
		struct FBlock final
		{
			// ┌────────┬────┬────┬────┬────┬────────────────┬────┬────┬────┬────┐
			// │ FBlock │ D₀ │ D₁ │ D₂ │ D₃ │->            <-│ R₃ │ R₂ │ R₁ │ R₀ │
			// └────────┴────┴────┴────┴────┴────────────────┴────┴────┴────┴────┘
			//          ^                   ^                ^              ^
			//      DataStart()          DataEnd         LastRecord    FirstRecord
			// Where:
			//   Dₙ = Data n, Rₙ = Record n

			// Starting size of a heap-allocated block, including the FBlock struct header.
			static constexpr size_t DefaultSize = 2048;

			// Constructor
			// TotalSize is the total size of the allocated memory for the block including
			// the FBlock header.
			explicit FBlock(size_t TotalSize)
			{
				AUTORTFM_ENSURE((TotalSize & (alignof(FRecord) - 1)) == 0);
				std::byte* End = reinterpret_cast<std::byte*>(this) + TotalSize;
				DataEnd = DataStart();
				// Note: The initial empty state has LastRecord pointing one
				// FRecord beyond the immutable FirstRecord.
				LastRecord = reinterpret_cast<FRecord*>(End);
				FirstRecord = LastRecord - 1;
			}

			// Allocate performs a heap allocation of a new block.
			// TotalSize is the total size of the allocated memory for the block including
			// the FBlock header.
			static FBlock* Allocate(size_t TotalSize)
			{
				AUTORTFM_ASSERT(TotalSize > (sizeof(FBlock) + sizeof(FRecord)));
				void* Memory = AutoRTFM::Allocate(TotalSize, alignof(FBlock));
				// Disable false-positive warning C6386: Buffer overrun while writing to 'Memory'
				CA_SUPPRESS(6386)
				return new (Memory) FBlock(TotalSize);
			}

			// Free releases the heap-allocated memory for this block.
			// Note: This block must have been allocated with a call to Allocate().
			void Free()
			{
				AutoRTFM::Free(this);
			}

			// Returns a pointer to the data for the first entry
			std::byte* DataStart()
			{
				return reinterpret_cast<std::byte*>(this) + sizeof(FBlock);
			}

			// Returns a pointer to the data for the last entry
			std::byte* LastData()
			{
				return DataEnd - LastRecord->Size();
			}

			// Returns true if the block holds no entries.
			bool IsEmpty() const
			{
				return LastRecord > FirstRecord;
			}

			// This will return 0 if the passed-in entry can be folded into the previous write, and 1 if a new
			// write record will be required.
			UE_AUTORTFM_FORCEINLINE int NumRecordsNeededForWrite(std::byte* LogicalAddress, bool bNoMemoryValidation)
			{
				return static_cast<int>(
					IsEmpty() ||
					LogicalAddress != LastRecord->Address() + LastRecord->Size() ||
					LastRecord->NoMemoryValidation() != bNoMemoryValidation);
			}

			// This determines the number of bytes that can safely be passed to `Push` below, given the number of
			// bytes in the write entry (`EntrySize`) and whether or not those bytes can be folded into an existing
			// record (which can be determined by calling `NumRecordsNeededForWrite` above). This algorithm does
			// _not_ clamp the input size to RecordMaxSize. Instead, the block allocation logic in `Push` and
			// `PushSmall` avoids creating an FBlock larger than MaxSize at all. (When very large pushes occur, the
			// `Push` algorithm _will_ create a large FBlock to satisfy it, but this logic does not call
			// `CalculatePushBytes`, and will create a new tail block afterwards.)
			// 
			// Returns zero if the block is entirely full, `EntrySize` if there's enough space, or a value
			// somewhere in-between if the entry needs to be split.
			UE_AUTORTFM_FORCEINLINE size_t CalculatePushBytes(size_t EntrySize, int NumRecordsNeeded)
			{
				std::byte* BlockEdge = reinterpret_cast<std::byte*>(LastRecord - NumRecordsNeeded);
				constexpr ptrdiff_t MinimumWorthwhileSplitSize = 8;

				if (DataEnd + EntrySize <= BlockEdge)
				{
					// We will fit the entire entry in the block with room to spare.
					AUTORTFM_ASSERT_DEBUG(EntrySize <= RecordMaxSize);
					AUTORTFM_ASSERT_DEBUG((NumRecordsNeeded == 1) || (LastRecord->Size() + EntrySize <= ptrdiff_t(RecordMaxSize)));
					return EntrySize;
				}
				else if (ptrdiff_t BlockCapacity = BlockEdge - DataEnd; BlockCapacity >= MinimumWorthwhileSplitSize)
				{
					// We have enough data to fill up the entire block. This path will split the data across the end of this
					// block and the start of the next block. We avoid this path when the block has fewer than eight bytes
					// left, just as an efficiency measure, since it takes extra time to assemble two FRecords instead of one.
					AUTORTFM_ASSERT_DEBUG(BlockCapacity <= ptrdiff_t(RecordMaxSize));
					AUTORTFM_ASSERT_DEBUG((NumRecordsNeeded == 1) || (LastRecord->Size() + BlockCapacity <= ptrdiff_t(RecordMaxSize)));
					return BlockCapacity;
				}
				else
				{
					// The block is completely full.
					return 0;
				}
			}

			// Grows this block by copying `NumBytes` bytes from `DataIn`, representing data originally from `LogicalAddress`. 
			// Asserts if the block does not have enough capacity to hold `NumBytes`. The caller should determine the available
			// capacity ahead of time by calling `NumRecordsNeededForWrite` and `CalculatePushBytes`.
			UE_AUTORTFM_FORCEINLINE void Push(std::byte* LogicalAddress, std::byte* DataIn, size_t NumBytes, bool bNoMemoryValidation, int NumRecordsNeeded)
			{
				AUTORTFM_ASSERT_DEBUG(DataEnd + NumBytes <= reinterpret_cast<std::byte*>(LastRecord - NumRecordsNeeded));
				AUTORTFM_ASSERT_DEBUG(NumBytes <= RecordMaxSize);

				if (NumRecordsNeeded == 1)
				{
					LastRecord--;
					LastRecord->Set(LogicalAddress, NumBytes, bNoMemoryValidation);
				}
				else
				{
					AUTORTFM_ASSERT_DEBUG(NumRecordsNeeded == 0);
					LastRecord->Grow(NumBytes);
				}

				memcpy(DataEnd, DataIn, NumBytes);

				DataEnd += NumBytes;

				AUTORTFM_ASSERT_DEBUG(DataEnd <= reinterpret_cast<std::byte*>(LastRecord));
			}

			// The next block in the linked list.
			FBlock* NextBlock = nullptr;
			// The previous block in the linked list.
			FBlock* PrevBlock = nullptr;
			// The pointer to the first entry's record
			FRecord* FirstRecord = nullptr;
			// The pointer to the last entry's record
			FRecord* LastRecord = nullptr;
			// One byte beyond the end of the last entry's data
			std::byte* DataEnd = nullptr;
		private:
			~FBlock() = delete;
		};

	public:
		// Constructor
		FWriteLog()
		{
			new(HeadBlockMemory) FBlock(HeadBlockSize);
		}

		// Destructor
		~FWriteLog()
		{
			Reset();
		}

		// Adds the write log entry to the log.
		// The log will make a copy of the FWriteLogEntry's data.
		void Push(FWriteLogEntry Entry)
		{
			AUTORTFM_ASSERT_DEBUG((reinterpret_cast<uintptr_t>(Entry.LogicalAddress) & 0xffff0000'00000000) == 0);

			{
				TotalSizeBytes += Entry.Size;
				int NumRecordsNeeded = TailBlock->NumRecordsNeededForWrite(Entry.LogicalAddress, Entry.bNoMemoryValidation);
				size_t NumBytes = TailBlock->CalculatePushBytes(Entry.Size, NumRecordsNeeded);

				if (NumBytes == Entry.Size)
				{
					// This push fits into our existing block.
					TailBlock->Push(Entry.LogicalAddress, Entry.Data, NumBytes, Entry.bNoMemoryValidation, NumRecordsNeeded);
					NumEntries += NumRecordsNeeded;
					return;
				}

				// The push doesn't fit into the existing block...
				if (NumBytes > 0)
				{
					// ... but we can still use up the remainder of the block.
					TailBlock->Push(Entry.LogicalAddress, Entry.Data, NumBytes, Entry.bNoMemoryValidation, NumRecordsNeeded);
					NumEntries += NumRecordsNeeded;

					// Adjust the entry to point to the remaining unlogged bytes.
					Entry.Size -= NumBytes;
					Entry.LogicalAddress += NumBytes;
					Entry.Data += NumBytes;
				}
			}

			// Calculate how many maxed-out RecordMaxSize entries we can make.
			const size_t NumFullRecords = Entry.Size / RecordMaxSize;
			// Calculate how many bytes will be remaining once we have emitted the full-size entries.
			const size_t RemainingBytes = Entry.Size - (NumFullRecords * RecordMaxSize);
			// Calculate the exact required size of the block.
			const size_t RequiredSize =
				sizeof(FBlock) +                                                   // FBlock header
				(NumFullRecords * (RecordMaxSize + sizeof(FRecord))) +             // Bytes needed for full records
				(LIKELY(RemainingBytes) ? (RemainingBytes + sizeof(FRecord)) : 0); // Bytes needed for trailing partial record, if any
			// Add padding to the block to account for alignment.
			const size_t AlignedSize = AlignUp(RequiredSize, sizeof(FRecord));

			// Create a new empty tail block, large enough to hold the remainder of this push in its entirety, and
			// never smaller than the upcoming block size.
			const size_t BlockSize = std::max(AlignedSize, NextBlockSize);
			AllocateNewBlock(BlockSize);

			// Push all of the full records.
			for (size_t Index = NumFullRecords; Index--; )
			{
				constexpr int NumRecordsNeeded = 1;
				constexpr size_t NumBytes = RecordMaxSize;

				TailBlock->Push(Entry.LogicalAddress, Entry.Data, NumBytes, Entry.bNoMemoryValidation, NumRecordsNeeded);
				NumEntries += NumRecordsNeeded;

				Entry.Size -= NumBytes;
				Entry.LogicalAddress += NumBytes;
				Entry.Data += NumBytes;
			}

			// Push the final, partial record.
			if (LIKELY(RemainingBytes))
			{
				constexpr int NumRecordsNeeded = 1;

				TailBlock->Push(Entry.LogicalAddress, Entry.Data, RemainingBytes, Entry.bNoMemoryValidation, NumRecordsNeeded);
				NumEntries += NumRecordsNeeded;
			}

			// If we've just created an extra-large block, and alignment has caused it to be less than 100% full,
			// we preemptively allocate a new block here. This avoids a scenario where a future PushSmall could
			// accidentally overflow the record size. Normally this would be impossible because BlockMaxSize
			// isn't large enough to hold more than RecordMaxSize bytes, but in this case we are making an FBlock
			// which is potentially much larger than normal.
			if (AlignedSize > BlockMaxSize && AlignedSize > RequiredSize)
			{
				AllocateNewBlock(NextBlockSize);
			}
		}

		// Adds the write log entry to the log; assumes a payload small enough that splitting is not beneficial.
		// If you have large sizes, you should use `Push` instead so that splitting can occur.
		template <unsigned int SIZE>
		void PushSmall(std::byte* LogicalAddress)
		{
			static_assert(SIZE <= RecordMaxSize);  // This is a hard upper limit.
			AUTORTFM_ASSERT_DEBUG((reinterpret_cast<uintptr_t>(LogicalAddress) & 0xffff0000'00000000) == 0);

			TotalSizeBytes += SIZE;

			int NumRecordsNeeded = TailBlock->NumRecordsNeededForWrite(LogicalAddress, /*bNoMemoryValidation=*/false);
			size_t NumBytes = TailBlock->CalculatePushBytes(SIZE, NumRecordsNeeded);

			if (UNLIKELY(NumBytes != SIZE))
			{
				AllocateNewBlock(NextBlockSize);
				NumRecordsNeeded = 1;
			}

			TailBlock->Push(LogicalAddress, LogicalAddress, SIZE, /*bNoMemoryValidation=*/false, NumRecordsNeeded);
			NumEntries += NumRecordsNeeded;
		}

		// Iterator for enumerating the writes of the log.
		template<bool IS_FORWARD>
		struct TIterator final
		{
			TIterator() = default;

			TIterator(FBlock* StartBlock) : Block(StartBlock)
			{
				if (UNLIKELY(Block->IsEmpty()))
				{
					if (UNLIKELY(!AdvanceBlock()))
					{
						// The write log is entirely empty.
						return;
					}
				}

				Data = IS_FORWARD ? Block->DataStart() : Block->LastData();
				Record = IS_FORWARD ? Block->FirstRecord : Block->LastRecord;
			}

			// Returns the entry at the current iterator's position.
			FWriteLogEntry operator*() const
			{
				return FWriteLogEntry
				{
					.LogicalAddress = reinterpret_cast<std::byte*>(Record->Address()),
					.Data = Data,
					.Size = Record->Size(),
					.bNoMemoryValidation = Record->NoMemoryValidation(),
				};
			}

			// Progresses the iterator to the next entry
			void operator++()
			{
				if constexpr (IS_FORWARD)
				{
					if (Record == Block->LastRecord)
					{
						if (LIKELY(AdvanceBlock()))
						{
							Data = Block->DataStart();
							Record = Block->FirstRecord;
						}
					}
					else
					{
						Data += Record->Size();
						Record--;
					}
				}
				else
				{
					if (Record == Block->FirstRecord)
					{
						if (LIKELY(AdvanceBlock()))
						{
							Data = Block->LastData();
							Record = Block->LastRecord;
						}
					}
					else
					{
						Record++;
						Data -= Record->Size();
					}
				}
			}

			// Inequality operator
			bool operator!=(const TIterator& Other) const
			{
				return (Other.Block != Block) || (Other.Record != Record);
			}

		private:
			// Resets the iterator (compares equal to the write log's end())
			UE_AUTORTFM_FORCEINLINE void Reset()
			{
				Block = nullptr;
				Data = nullptr;
				Record = nullptr;
			}

			// Moves from this block to the next (if IS_FORWARD) or previous (if not IS_FORWARD),
			// skipping any empty blocks. Returns true on success. If no more blocks exist, resets 
			// the iterator and returns false.
			UE_AUTORTFM_FORCEINLINE bool AdvanceBlock()
			{
				do
				{
					Block = IS_FORWARD ? Block->NextBlock : Block->PrevBlock;
				}
				while (Block && Block->IsEmpty());

				if (!Block)
				{
					Reset();
					return false;
				}

				return true;
			}

			FBlock* Block = nullptr;
			std::byte* Data = nullptr;
			FRecord* Record = nullptr;
		};

		using Iterator = TIterator</* IS_FORWARD */ true>;
		using ReverseIterator = TIterator</* IS_FORWARD */ false>;

		Iterator begin() const
		{
			return (NumEntries > 0) ? Iterator(HeadBlock) : Iterator{};
		}
		ReverseIterator rbegin() const
		{
			return (NumEntries > 0) ? ReverseIterator(TailBlock) : ReverseIterator{};
		}
		Iterator end() const { return Iterator{}; }
		ReverseIterator rend() const { return ReverseIterator{}; }

		// Resets the write log to its initial state, freeing any allocated memory.
		void Reset()
		{
			// Skip HeadBlock, which is held as part of this structure.
			FBlock* Block = HeadBlock->NextBlock;
			while (nullptr != Block)
			{
				FBlock* const Next = Block->NextBlock;
				Block->Free();
				Block = Next;
			}
			new (HeadBlockMemory) FBlock(HeadBlockSize);
			HeadBlock = reinterpret_cast<FBlock*>(HeadBlockMemory);
			TailBlock = reinterpret_cast<FBlock*>(HeadBlockMemory);
			NumEntries = 0;
			TotalSizeBytes = 0;
			NextBlockSize = FBlock::DefaultSize;
		}

		// Returns true if the log holds no entries.
		UE_AUTORTFM_FORCEINLINE bool IsEmpty() const { return 0 == NumEntries; }

		// Return the number of entries in the log.
		UE_AUTORTFM_FORCEINLINE size_t Num() const { return NumEntries; }

		// Return the total size in bytes for all entries in the log.
		UE_AUTORTFM_FORCEINLINE size_t TotalSize() const { return TotalSizeBytes; }

		// Returns a hash of the first NumWriteEntries entries' logical memory
		// tracked by the write log. This is the memory post-write, not the
		// original memory that would be restored on abort.
		using FHash = uint64_t;
		FHash Hash(size_t NumWriteEntries) const;

	private:
		UE_AUTORTFM_FORCEINLINE void AllocateNewBlock(size_t Size)
		{
			FBlock* NewBlock = FBlock::Allocate(Size);
			NewBlock->PrevBlock = TailBlock;
			TailBlock->NextBlock = NewBlock;
			TailBlock = NewBlock;

			// Increase block sizes by 50% each time, capped at BlockMaxSize.
			NextBlockSize = std::min<size_t>((NextBlockSize * 3 / 2), BlockMaxSize);
		}

		template <size_t SIZE>
		static constexpr bool IsAlignedForFRecord = (SIZE & (alignof(FRecord) - 1)) == 0;

		// The size of the inline block, which is declared as a byte array (`HeadBlockMemory`)
		// inside the write log.
		static constexpr size_t HeadBlockSize = 256;

		// The upper bound on heap-allocated block size. We avoid making blocks larger 
		// than the maximum record size, so we don't need to insert size overflow 
		// checks throughout the push logic.
		static constexpr size_t BlockMaxSize = AlignDown(sizeof(FBlock) + sizeof(FRecord) + RecordMaxSize, alignof(FRecord));

		static_assert(IsAlignedForFRecord<HeadBlockSize>);
		static_assert(IsAlignedForFRecord<FBlock::DefaultSize>);
		static_assert(IsAlignedForFRecord<BlockMaxSize>);

		FHash HashAVX2(size_t NumWriteEntries) const;

		FBlock* HeadBlock = reinterpret_cast<FBlock*>(HeadBlockMemory);
		FBlock* TailBlock = reinterpret_cast<FBlock*>(HeadBlockMemory);
		size_t NumEntries = 0;
		size_t TotalSizeBytes = 0;
		size_t NextBlockSize = FBlock::DefaultSize;
		alignas(alignof(FBlock)) std::byte HeadBlockMemory[HeadBlockSize];
	};
}

#endif // (defined(__AUTORTFM) && __AUTORTFM)
