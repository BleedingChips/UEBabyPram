// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "Context.h"
#include "HitSet.h"
#include "ScopedGuard.h"
#include "Transaction.h"
#include "Utils.h"
#include "WriteLog.h"

#include <utility>

#if __has_include(<sanitizer/asan_interface.h>)
#	include <sanitizer/asan_interface.h>
#	if defined(__SANITIZE_ADDRESS__)
#		define AUTORTFM_CHECK_ASAN_FAKE_STACKS 1
#	elif defined(__has_feature)
#		if __has_feature(address_sanitizer)
#			define AUTORTFM_CHECK_ASAN_FAKE_STACKS 1
#		endif
#	endif
#endif

#ifndef AUTORTFM_CHECK_ASAN_FAKE_STACKS
#define AUTORTFM_CHECK_ASAN_FAKE_STACKS 0
#endif

namespace AutoRTFM
{

UE_AUTORTFM_FORCEINLINE bool FTransaction::IsOnStack(const void* LogicalAddress) const
{
    if (StackRange.Contains(LogicalAddress))
    {
        return true;
    }

#if AUTORTFM_CHECK_ASAN_FAKE_STACKS
    if (void* FakeStack = __asan_get_current_fake_stack())
    {
        void* Beg = nullptr;
        void* End = nullptr;
        void* RealAddress = __asan_addr_is_in_fake_stack(FakeStack, const_cast<void*>(LogicalAddress), &Beg, &End);
        return RealAddress && StackRange.Contains(RealAddress);
    }
#endif  // AUTORTFM_CHECK_ASAN_FAKE_STACKS

    return false;
}

UE_AUTORTFM_FORCEINLINE bool FTransaction::ShouldRecordWrite(void* LogicalAddress) const
{
	// We cannot record writes to stack memory used within the transaction, as
	// undoing the writes may corrupt stack memory that has been unwound or
	// is now being used for a different variable from the one the write was
	// made.
	return !IsOnStack(LogicalAddress);
}

AUTORTFM_NO_ASAN UE_AUTORTFM_FORCEINLINE
void FTransaction::RecordWrite(void* LogicalAddress, size_t Size, bool bNoMemoryValidation /* = false */)
{
	if (AUTORTFM_UNLIKELY(0 == Size))
	{
		return;
	}

	if (!ShouldRecordWrite(LogicalAddress))
	{
		Stats.Collect<EStatsKind::HitSetSkippedBecauseOfStackLocalMemory>();
		return;
	}

	if (Size <= FHitSet::MaxSize)
	{
		FHitSetEntry HitSetEntry{};
		HitSetEntry.Address = reinterpret_cast<uintptr_t>(LogicalAddress);
		HitSetEntry.Size = static_cast<uint16_t>(Size);
		HitSetEntry.bNoMemoryValidation = bNoMemoryValidation;

		if (HitSet.FindOrTryInsert(HitSetEntry))
		{
			Stats.Collect<EStatsKind::HitSetHit>();
			return;
		}

		Stats.Collect<EStatsKind::HitSetMiss>();
	}

	if (NewMemoryTracker.Contains(LogicalAddress, Size))
	{
		Stats.Collect<EStatsKind::NewMemoryTrackerHit>();
		return;
	}

	Stats.Collect<EStatsKind::NewMemoryTrackerMiss>();

	WriteLog.Push(FWriteLogEntry
	{
		.LogicalAddress = static_cast<std::byte*>(LogicalAddress),
		.Data = static_cast<std::byte*>(LogicalAddress),
		.Size = Size,
		.bNoMemoryValidation = bNoMemoryValidation
	});
}

template<unsigned SIZE> AUTORTFM_NO_ASAN UE_AUTORTFM_FORCEINLINE void FTransaction::RecordWrite(void* LogicalAddress)
{
    static_assert(SIZE <= 8);

    if (!ShouldRecordWrite(LogicalAddress))
    {
        Stats.Collect<EStatsKind::HitSetSkippedBecauseOfStackLocalMemory>();
        return;
    }

    FHitSetEntry Entry{};
    Entry.Address = reinterpret_cast<uintptr_t>(LogicalAddress);
    Entry.Size = static_cast<uint16_t>(SIZE);

	switch (HitSet.FindOrTryInsertNoResize(Entry))
	{
	case FHitSet::EInsertResult::Exists:
		Stats.Collect<EStatsKind::HitSetHit>();
		return;
	case FHitSet::EInsertResult::Inserted:
		AUTORTFM_MUST_TAIL return FTransaction::RecordWriteInsertedSlow<SIZE>(LogicalAddress);
	case FHitSet::EInsertResult::NotInserted:
		AUTORTFM_MUST_TAIL return FTransaction::RecordWriteNotInsertedSlow<SIZE>(LogicalAddress);
	}
}

template<unsigned SIZE> AUTORTFM_NO_ASAN UE_AUTORTFM_FORCENOINLINE void FTransaction::RecordWriteNotInsertedSlow(void* LogicalAddress)
{
    FHitSetEntry Entry{};
    Entry.Address = reinterpret_cast<uintptr_t>(LogicalAddress);
    Entry.Size = static_cast<uint16_t>(SIZE);

	if (HitSet.FindOrTryInsert(Entry))
	{
		Stats.Collect<EStatsKind::HitSetHit>();
		return;
	}

	Stats.Collect<EStatsKind::HitSetMiss>();

	return RecordWriteInsertedSlow<SIZE>(LogicalAddress);
}

template<unsigned SIZE> AUTORTFM_NO_ASAN UE_AUTORTFM_FORCENOINLINE void FTransaction::RecordWriteInsertedSlow(void* LogicalAddress)
{
	if (NewMemoryTracker.Contains(LogicalAddress, SIZE))
	{
		Stats.Collect<EStatsKind::NewMemoryTrackerHit>();
		return;
	}

	Stats.Collect<EStatsKind::NewMemoryTrackerMiss>();

	WriteLog.PushSmall<SIZE>(static_cast<std::byte*>(LogicalAddress));
}

UE_AUTORTFM_FORCEINLINE void FTransaction::DidAllocate(void* LogicalAddress, const size_t Size)
{
	if (0 == Size || bIsInAllocateFn)
	{
		return;
	}

	AutoRTFM::TScopedGuard<bool> RecursionGuard(bIsInAllocateFn, true);
	const bool DidInsert = NewMemoryTracker.Insert(LogicalAddress, Size);
	AUTORTFM_ASSERT(DidInsert);
}

UE_AUTORTFM_FORCEINLINE void FTransaction::DidFree(void* LogicalAddress)
{
	AUTORTFM_ASSERT(bTrackAllocationLocations);

	// Checking if one byte is in the interval map is enough to ascertain if it
	// is new memory and we should be worried.
	if (!bIsInAllocateFn)
	{
		AutoRTFM::TScopedGuard<bool> RecursionGuard(bIsInAllocateFn, true);
		AUTORTFM_ASSERT(!NewMemoryTracker.Contains(LogicalAddress, 1));
	}
}

UE_AUTORTFM_FORCEINLINE void FTransaction::DeferUntilCommit(TTask<void()>&& Callback)
{
	// We explicitly must copy the function here because the original was allocated
	// within a transactional context, and thus the memory is allocating under
	// transactionalized conditions. By copying, we create an open copy of the callback.
	TTask<void()> Copy(Callback);
    CommitTasks.Add(std::move(Copy));
}

UE_AUTORTFM_FORCEINLINE void FTransaction::DeferUntilPreAbort(TTask<void()>&& Callback)
{
	// We explicitly must copy the function here because the original was allocated
	// within a transactional context, and thus the memory is allocating under
	// transactionalized conditions. By copying, we create an open copy of the callback.
	TTask<void()> Copy(Callback);
    PreAbortTasks.Add(std::move(Copy));
}

UE_AUTORTFM_FORCEINLINE void FTransaction::DeferUntilAbort(TTask<void()>&& Callback)
{
	// We explicitly must copy the function here because the original was allocated
	// within a transactional context, and thus the memory is allocating under
	// transactionalized conditions. By copying, we create an open copy of the callback.
	TTask<void()> Copy(Callback);
    AbortTasks.Add(std::move(Copy));
}

UE_AUTORTFM_FORCEINLINE void FTransaction::DeferUntilComplete(TTask<void()>&& Callback)
{
	// Completion tasks are always stored directly on the root level of a transaction.
	FTransaction* Transaction = this;
	while (FTransaction* Above = Transaction->Parent)
	{
		Transaction = Above;
	}

	// We explicitly must copy the function here because the original was allocated
	// within a transactional context, and thus the memory is allocating under
	// transactionalized conditions. By copying, we create an open copy of the callback.
	TTask<void()> Copy(Callback);
	Transaction->CompletionTasks->Add(std::move(Copy));
}

UE_AUTORTFM_FORCEINLINE void FTransaction::PushDeferUntilCommitHandler(const void* Key, TTask<void()>&& Callback)
{
	// We explicitly must copy the function here because the original was allocated
	// within a transactional context, and thus the memory was allocated under
	// transactionalized conditions. By copying, we create an open copy of the callback.
	TTask<void()> Copy(Callback);
	CommitTasks.AddKeyed(Key, std::move(Copy));
}

UE_AUTORTFM_FORCEINLINE void FTransaction::PopDeferUntilCommitHandler(const void* Key)
{
	if (AUTORTFM_LIKELY(CommitTasks.DeleteKey(Key)))
	{
		return;
	}

	DeferredPopOnCommitHandlers.Push(Key);
}

UE_AUTORTFM_FORCEINLINE void FTransaction::PopAllDeferUntilCommitHandlers(const void* Key)
{
	CommitTasks.DeleteAllMatchingKeys(Key);

	// We also need to remember to run this on our parent's nest if our transaction commits.
	DeferredPopAllOnCommitHandlers.Push(Key);
}

UE_AUTORTFM_FORCEINLINE void FTransaction::PushDeferUntilAbortHandler(const void* Key, TTask<void()>&& Callback)
{
	// We explicitly must copy the function here because the original was allocated
	// within a transactional context, and thus the memory is allocating under
	// transactionalized conditions. By copying, we create an open copy of the callback.
	TTask<void()> Copy(Callback);
    AbortTasks.AddKeyed(Key, std::move(Copy));
}

UE_AUTORTFM_FORCEINLINE void FTransaction::PopDeferUntilAbortHandler(const void* Key)
{
	if (AUTORTFM_LIKELY(AbortTasks.DeleteKey(Key)))
	{
		return;
	}

	DeferredPopOnAbortHandlers.Push(Key);
}

UE_AUTORTFM_FORCEINLINE void FTransaction::PopAllDeferUntilAbortHandlers(const void* Key)
{
	AbortTasks.DeleteAllMatchingKeys(Key);

	// We also need to remember to run this on our parent's nest if our transaction commits.
	DeferredPopAllOnAbortHandlers.Push(Key);
}

UE_AUTORTFM_FORCEINLINE void FTransaction::CollectStats() const
{
    Stats.Collect<EStatsKind::AverageWriteLogEntries>(WriteLog.Num());
    Stats.Collect<EStatsKind::MaximumWriteLogEntries>(WriteLog.Num());

    Stats.Collect<EStatsKind::AverageWriteLogBytes>(WriteLog.TotalSize());
    Stats.Collect<EStatsKind::MaximumWriteLogBytes>(WriteLog.TotalSize());

    Stats.Collect<EStatsKind::AverageCommitTasks>(CommitTasks.Num());
    Stats.Collect<EStatsKind::MaximumCommitTasks>(CommitTasks.Num());

    Stats.Collect<EStatsKind::AveragePreAbortTasks>(PreAbortTasks.Num());
    Stats.Collect<EStatsKind::MaximumPreAbortTasks>(PreAbortTasks.Num());

    Stats.Collect<EStatsKind::AverageAbortTasks>(AbortTasks.Num());
    Stats.Collect<EStatsKind::MaximumAbortTasks>(AbortTasks.Num());

    Stats.Collect<EStatsKind::AverageHitSetSize>(HitSet.GetCount());
    Stats.Collect<EStatsKind::AverageHitSetCapacity>(HitSet.GetCapacity());
}

} // namespace AutoRTFM

#undef AUTORTFM_CHECK_ASAN_FAKE_STACKS

#endif // (defined(__AUTORTFM) && __AUTORTFM)
