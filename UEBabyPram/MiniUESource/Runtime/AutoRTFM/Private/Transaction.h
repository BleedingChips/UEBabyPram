// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "AutoRTFM.h"
#include "HitSet.h"
#include "IntervalTree.h"
#include "IntrusivePool.h"
#include "Stack.h"
#include "StackRange.h"
#include "Stats.h"
#include "TaskArray.h"
#include "Utils.h"
#include "WriteLog.h"

#include <optional>

namespace AutoRTFM
{
class FContext;

class AUTORTFM_DISABLE FTransaction final
{
	friend class TIntrusivePool<FTransaction, 16>;

	void Resurrect(FContext* const InContext);
	void Suppress();
	FTransaction** GetIntrusiveAddress();

	// Constructor.
	// The transaction is constructed in an initial Closed state.
	FTransaction(FContext* const Context);

	// Destructor.
	// The transaction must we in a Done state.
	~FTransaction();

public:
	// State flow diagram for a transaction:
	//                       ┌──────────────────────────────────────┐
	//                       │             Uninitialized            │
	//                       └──────────────────────────────────────┘
	//                               │                       │
	//                               ▼                       ▼
	// ┌────────────────┐    ┌────────────────┐    ┌────────────────┐    ┌────────────────┐
	// │  OpenInactive  │ ←→ │   OpenActive   │ ←→ │  ClosedActive  │ ←→ │ ClosedInactive │
	// └────────────────┘    └────────────────┘    └────────────────┘    └────────────────┘
	//                               │                       │
	//                               ▼                       ▼
	//                       ┌──────────────────────────────────────┐
	//                       │                 Done                 │
	//                       └──────────────────────────────────────┘
	enum class EState
	{
		// The initial state for the transaction.
		// Can only transition to OpenActive or ClosedActive.
		Uninitialized,
		// The transaction is open (not recording writes) and is the current transaction.
		// Can only transition to OpenInactive, ClosedActive or Done.
		OpenActive,
		// The transaction is closed (recording writes) and is the current transaction.
		// Can only transition to ClosedInactive, OpenActive or Done.
		ClosedActive,
		// The transaction is open and the current transaction is a descendant.
		// Can only transition to OpenActive.
		OpenInactive,
		// The transaction is closed and the current transaction is a descendant.
		// Can only transition to ClosedActive.
		ClosedInactive,
		// The transaction is committed or aborted.
		// Once in this state the transaction must be reset with Reset() or destructed.
		Done,
	};

	void Initialize(FTransaction* InParent, bool bInIsStackScoped, FStackRange InStackRange);

	// Clears the tracked transaction state and resets back to the default Uninitialized state.
	void Reset();

	bool IsNested() const { return !!Parent; }
	FTransaction* GetParent() const { return Parent; }

	// This should just use type displays or ranges. Maybe ranges could even work out great.
	bool IsNestedWithin(const FTransaction* Other) const
	{
		for (const FTransaction* Current = this; ; Current = Current->Parent)
		{
			if (!Current)
			{
				return false;
			}
			else if (Current == Other)
			{
				return true;
			}
		}
	}

	inline bool IsScopedTransaction() const { return bIsStackScoped; }

	void DeferUntilCommit(TTask<void()>&&);
	void DeferUntilPreAbort(TTask<void()>&&);
	void DeferUntilAbort(TTask<void()>&&);
	void DeferUntilComplete(TTask<void()>&&);
	void PushDeferUntilCommitHandler(const void* Key, TTask<void()>&&);
	void PopDeferUntilCommitHandler(const void* Key);
	void PopAllDeferUntilCommitHandlers(const void* Key);
	void PushDeferUntilAbortHandler(const void* Key, TTask<void()>&&);
	void PopDeferUntilAbortHandler(const void* Key);
	void PopAllDeferUntilAbortHandlers(const void* Key);

	[[noreturn]] void AbortAndThrow();
	void AbortWithoutThrowing();
	bool AttemptToCommit();

	// Record that a write is about to occur at the given LogicalAddress of Size bytes.
	void RecordWrite(void* LogicalAddress, size_t Size, bool bNoMemoryValidation = false);
	template<unsigned SIZE> void RecordWrite(void* LogicalAddress);
	template<unsigned SIZE> void RecordWriteInsertedSlow(void* LogicalAddress);
	template<unsigned SIZE> void RecordWriteNotInsertedSlow(void* LogicalAddress);

	void DidAllocate(void* LogicalAddress, size_t Size);
	void DidFree(void* LogicalAddress);

	void SetOpenActive(EMemoryValidationLevel MemoryValidationLevel, const void* ReturnAddress);
	void SetClosedActive();
	void SetOpenInactive();
	void SetClosedInactive();
	void SetActive();
	void SetInactive();
	void SetDone();

	// A debug helper that will break to the debugger if the memory validation
	// hash no longer matches. Useful for isolating where the open write happened.
	void DebugBreakIfMemoryValidationFails();

	// State querying
	EState State() const { return CurrentState; }
	bool IsFresh() const;
	bool IsOpenActive() const { return CurrentState == EState::OpenActive; }
	bool IsClosedActive() const { return CurrentState == EState::ClosedActive; }
	bool IsOpenInactive() const { return CurrentState == EState::OpenInactive; }
	bool IsClosedInactive() const { return CurrentState == EState::ClosedInactive; }
	bool IsOpen() const { return IsOpenActive() || IsOpenInactive(); }
	bool IsClosed() const { return IsClosedActive() || IsClosedInactive(); }
	bool IsActive() const { return IsOpenActive() || IsClosedActive(); }
	bool IsInactive() const { return IsOpenInactive() || IsClosedInactive(); }
	bool IsDone() const { return CurrentState == EState::Done; }

	EMemoryValidationLevel MemoryValidationLevel() const { return CurrentMemoryValidationLevel; }
	const void* OpenReturnAddress() const { return CurrentOpenReturnAddress; }

	// The stack range represents all stack memory inside the transaction scope
	inline FStackRange GetStackRange() const { return StackRange; }

	// Returns true if the LogicalAddress is within the stack of the transaction.
	inline bool IsOnStack(const void* LogicalAddress) const;

private:
	using FWriteHash = FWriteLog::FHash;
	using FTaskArray = TTaskArray<TTask<void()>>;

	void Undo();

	void CommitNested();
	bool AttemptToCommitOuterNest();

	void ResetCommitTasksWithValidation();
	void CallAbortTasksWithValidation(FTaskArray& Tasks);

	// Calculates the hash of all the memory written during the transaction and stores it to
	// RecordedWriteHash.
	void RecordWriteHash();

	// Compares the hash of all the memory that will be written on abort against the
	// RecordedWriteHash, raising a warning or error if the hashes are not equal.
	// See CalculateNestedWriteHash().
	void ValidateWriteHash() const;

	// Warns / errors when a validation hash mismatch is detected.
	void OnValidatorWriteHashMismatch(EMemoryValidationLevel MemoryValidationLevel, const char* Message, ...) const;
	
	// Returns a hash of all the memory written during this transaction and all parent transactions.
	// Can be used to ensure that no transaction written memory is modified during an open.
	// FunctionAddress is the address of the function that triggered this hash.
	FWriteHash CalculateNestedWriteHash(const void *FunctionAddress) const;
	
	// Returns a hash of all the memory written during this transaction and all parent transactions.
	// Can be used to ensure that no transaction written memory is modified during an open.
	// NumWriteEntries is the number of write entries to hash before stopping.
	// FunctionAddress is the address of the function that triggered this hash.
	FWriteHash CalculateNestedWriteHashWithLimit(size_t NumWriteEntries, const void *FunctionAddress) const;

	void CollectStats() const;

	bool ShouldRecordWrite(void* LogicalAddress) const;

	void SetOpenActiveValidatorEnabled(EMemoryValidationLevel MemoryValidationLevel, const void* ReturnAddress);

	template<EState NewState>
	void SetState();

	FContext* Context;

	// This field is used for two purposes:
	// - When the transaction is active, the Parent field is used to support transaction nesting.
	//   This field points to our immediate outer nest; it's null when this is the top level.
	// - When the transaction is "freed"--sitting in the FTransactionPool free list--this field is
	//   used to point to the next free item, forming a linked list of reusable FTransactions. 
	//   (See GetIntrusiveAddress.)
	FTransaction* Parent;

	// Commit tasks run on commit in forward order.
	FTaskArray CommitTasks;

	// Abort tasks run on abort (pre memory rollback) in reverse order.
	FTaskArray PreAbortTasks;

	// Abort tasks run on abort (post memory rollback) in reverse order.
	FTaskArray AbortTasks;

	// Completion tasks run at the end of the transaction, in forward order.
	std::optional<FTaskArray> CompletionTasks;

	// If a call to `PopOnCommitHandler` could not find a commit to pop, it is deferred
	// and tried again on the parent transaction.
	TStack<const void*, 8> DeferredPopOnCommitHandlers;

	// If a call to `PopOnAbortHandler` could not find an abort to pop, it is deferred
	// and tried again on the parent transaction.
	TStack<const void*, 8> DeferredPopOnAbortHandlers;

	// If a call to `PopAllOnCommitHandlers` was used and our transaction successfully
	// commits, we need to propagate this to the parent too.
	TStack<const void*, 1> DeferredPopAllOnCommitHandlers;

	// If a call to `PopAllOnAbortHandlers` was used and our transaction successfully
	// commits, we need to propagate this to the parent too.
	TStack<const void*, 1> DeferredPopAllOnAbortHandlers;

	FHitSet HitSet;
	FIntervalTree NewMemoryTracker;
	FWriteLog WriteLog;
	TStatStorage<uint64_t> StatDepth = 1;
	FWriteHash RecordedWriteHash;
	size_t NumWriteLogsHashed;
	EMemoryValidationLevel CurrentMemoryValidationLevel;
	const void* CurrentOpenReturnAddress;
	EState CurrentState;
	FStackRange StackRange;
	bool bIsStackScoped;
	bool bIsInAllocateFn;
};

} // namespace AutoRTFM

#endif // (defined(__AUTORTFM) && __AUTORTFM)
