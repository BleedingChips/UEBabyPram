// Copyright Epic Games, Inc. All Rights Reserved.

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "Transaction.h"
#include "TransactionInlines.h"

#include "CallNestInlines.h"
#include "ContextStatus.h"
#include "HitSet.h"
#include "Utils.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>

namespace AutoRTFM
{

void FTransaction::Resurrect(FContext* const InContext)
{
	AUTORTFM_ASSERT(Context == InContext)
	Parent = nullptr;
	StatDepth = 1;
	RecordedWriteHash = 0;
	NumWriteLogsHashed = 0;
	CurrentMemoryValidationLevel = EMemoryValidationLevel::Disabled;
	CurrentOpenReturnAddress = nullptr;
	CurrentState = EState::Uninitialized;
	bIsStackScoped = false;
	bIsInAllocateFn = false;
}

void FTransaction::Suppress()
{
	CurrentState = EState::Done;
	Reset();
}

FTransaction** FTransaction::GetIntrusiveAddress()
{
	return &Parent;
}

FTransaction::FTransaction(FContext* InContext)
    : Context(InContext)
	, CommitTasks(Context->GetTaskPool())
	, PreAbortTasks(Context->GetTaskPool())
	, AbortTasks(Context->GetTaskPool())
{
	Resurrect(Context);
}

FTransaction::~FTransaction()
{
	Suppress();
}

void FTransaction::Initialize(FTransaction* InParent, bool bInIsStackScoped, FStackRange InStackRange)
{
	AUTORTFM_ASSERT(CurrentState == EState::Uninitialized);

	Parent = InParent;
	bIsStackScoped = bInIsStackScoped;
	StackRange = InStackRange;

	if (Parent)
	{
		// For stats, record the nested depth of the transaction.
		StatDepth = Parent->StatDepth + 1;
	}
	else
	{
		// Root-level transactions require a completion task list.
		CompletionTasks.emplace(Context->GetTaskPool());
	}

	Stats.Collect<EStatsKind::AverageTransactionDepth>(StatDepth);
	Stats.Collect<EStatsKind::MaximumTransactionDepth>(StatDepth);
}

void FTransaction::Reset()
{
	AUTORTFM_ASSERT(IsDone());

	CommitTasks.Reset();
	PreAbortTasks.Reset();
	AbortTasks.Reset();
	if (CompletionTasks != std::nullopt)
	{
		CompletionTasks->Reset();
		CompletionTasks = std::nullopt;
	}

	HitSet.Reset();
	NewMemoryTracker.Reset();
	WriteLog.Reset();
	CurrentMemoryValidationLevel = EMemoryValidationLevel::Disabled;

	// Reset to the initial state.
	CurrentState = EState::Uninitialized;

	DeferredPopOnCommitHandlers.Reset();
	DeferredPopOnAbortHandlers.Reset();
	DeferredPopAllOnCommitHandlers.Reset();
	DeferredPopAllOnAbortHandlers.Reset();

	AUTORTFM_ASSERT(IsFresh());
}

bool FTransaction::IsFresh() const
{
    return HitSet.IsEmpty()
        && NewMemoryTracker.IsEmpty()
        && WriteLog.IsEmpty()
        && CommitTasks.IsEmpty()
        && PreAbortTasks.IsEmpty()
        && AbortTasks.IsEmpty()
		&& (CompletionTasks == std::nullopt || CompletionTasks->IsEmpty())
        && !IsDone()
		&& DeferredPopOnCommitHandlers.IsEmpty()
		&& DeferredPopOnAbortHandlers.IsEmpty()
		&& DeferredPopAllOnCommitHandlers.IsEmpty()
		&& DeferredPopAllOnAbortHandlers.IsEmpty();
}

void FTransaction::AbortWithoutThrowing()
{
	AUTORTFM_VERBOSE("Aborting '%hs'!", GetContextStatusName(Context->GetStatus()));

	AUTORTFM_ASSERT(Context->IsAborting());

	Stats.Collect<EStatsKind::Abort>();
	CollectStats();

	// Ensure that we enter the done state before applying the commit, as this
	// will ensure the open-memory validation is performed before the write log
	// is cleared.
	SetDone();

	const EMemoryValidationLevel MemoryValidationLevel = ForTheRuntime::GetMemoryValidationLevel();

	// Call the destructors of all the OnCommit functors before undoing the transactional memory and
	// calling the OnAbort callbacks. This is important as the callback functions may have captured
	// variables that are depending on the allocated memory.
	if (AUTORTFM_LIKELY(MemoryValidationLevel == EMemoryValidationLevel::Disabled))
	{
		CommitTasks.Reset();
	}
	else
	{
		ResetCommitTasksWithValidation();
	}

	// Call each of the pre memory rollback on-abort handlers.
	if (AUTORTFM_LIKELY(MemoryValidationLevel == EMemoryValidationLevel::Disabled))
	{
		PreAbortTasks.RemoveEachBackward([&](TTask<void()>& Task)
		{ 
			Task();
		});
	}
	else
	{
		CallAbortTasksWithValidation(PreAbortTasks);
	}

	// Revert all memory in the write-log.
	Undo();

	// Call each of the post memory rollback on-abort handlers.
	if (AUTORTFM_LIKELY(MemoryValidationLevel == EMemoryValidationLevel::Disabled))
	{
		AbortTasks.RemoveEachBackward([&](TTask<void()>& Task)
		{ 
			Task();
		});
	}
	else
	{
		CallAbortTasksWithValidation(AbortTasks);
	}

	if (IsNested())
	{
		AUTORTFM_ASSERT(Parent);
	}
	else
	{
		// Completion tasks don't use memory validation; by definition, they happen after the transaction is over.
		CompletionTasks->RemoveEachForward([&](TTask<void()>& Task)
		{ 
			Task();
		});

		AUTORTFM_ASSERT(Context->IsAborting());
	}
}


void FTransaction::ResetCommitTasksWithValidation()
{
	FOpenHashThrottler& OpenHashThrottler = Context->GetOpenHashThrottler();
	const bool bThrottlingEnabled = AutoRTFM::ForTheRuntime::GetMemoryValidationThrottlingEnabled();
	std::optional<FWriteHash> OldHash;
	CommitTasks.RemoveEachBackward([&](TTask<void()>& Task)
	{
		void* const FunctionAddress = Task.FunctionAddress();

		const bool bShouldHash = !bThrottlingEnabled || OpenHashThrottler.ShouldHashFor(FunctionAddress);
		if (bShouldHash && !OldHash)
		{
			OldHash = CalculateNestedWriteHash(FunctionAddress);
		}

		Task.Reset();

		if (bShouldHash)
		{
			const FWriteHash NewHash = CalculateNestedWriteHash(FunctionAddress);
			if (OldHash != NewHash)
			{
				OnValidatorWriteHashMismatch(ForTheRuntime::GetMemoryValidationLevel(), 
					"Memory modified in a transaction was also modified in the destructor of the on-commit handler: %s\n"
					"This may lead to memory corruption if the transaction is aborted.",
					GetFunctionDescription(FunctionAddress).c_str());
				OldHash = NewHash;
			}
		}
		else
		{
			// Task was reset without validation, so invalidate the snapshot hash.
			OldHash = std::nullopt;
		}
	});
}

void FTransaction::CallAbortTasksWithValidation(FTaskArray& Tasks)
{
	FOpenHashThrottler& OpenHashThrottler = Context->GetOpenHashThrottler();
	const bool bThrottlingEnabled = AutoRTFM::ForTheRuntime::GetMemoryValidationThrottlingEnabled();
	std::optional<FWriteHash> OldHash;
	auto MaybeValidate = [&](void* FunctionAddress, const char* Message, auto&& Fn)
	{
		const bool bShouldHash = !bThrottlingEnabled || OpenHashThrottler.ShouldHashFor(FunctionAddress);
		if (bShouldHash && !OldHash)
		{
			OldHash = CalculateNestedWriteHash(FunctionAddress);
		}
		
		Fn();

		if (bShouldHash)
		{
			const FWriteHash NewHash = CalculateNestedWriteHash(FunctionAddress);
			if (OldHash != NewHash)
			{
				OnValidatorWriteHashMismatch(ForTheRuntime::GetMemoryValidationLevel(), Message,
					GetFunctionDescription(FunctionAddress).c_str());
				OldHash = NewHash;
			}
		}
		else
		{
			// Fn() was run without validation, so invalidate the snapshot hash.
			OldHash = std::nullopt;
		}
	};

	Tasks.RemoveEachBackward([&](TTask<void()>& Task)
	{
		void* const FunctionAddress = Task.FunctionAddress();

		MaybeValidate(FunctionAddress,
			"Memory modified in a transaction was also modified in the on-abort handler: %s\n"
			"This may lead to memory corruption.",
			[&]{ Task(); });

		MaybeValidate(FunctionAddress,
			"Memory modified in a transaction was also modified in the destructor of the on-abort handler: %s\n"
			"This may lead to memory corruption if the transaction is aborted.",
			[&]{ Task.Reset(); });
	});
}

void FTransaction::AbortAndThrow()
{
    AbortWithoutThrowing();
	Context->Throw();
}

bool FTransaction::AttemptToCommit()
{
    AUTORTFM_ASSERT(Context->GetStatus() == EContextStatus::Committing);
    AUTORTFM_ASSERT(Context->GetCurrentTransaction() == this);

    Stats.Collect<EStatsKind::Commit>();
    CollectStats();

	// Ensure that we enter the done state before applying the commit, as this
	// will ensure the open-memory validation is performed before the write log
	// is cleared.
	SetDone();

    bool bResult;
    if (IsNested())
    {
        CommitNested();
        bResult = true;
    }
    else
    {
        bResult = AttemptToCommitOuterNest();
    }

    return bResult;
}

void FTransaction::Undo()
{
	AUTORTFM_VERBOSE("Undoing a transaction...");
	AUTORTFM_ASSERT(IsDone());

	for(auto Iter = WriteLog.rbegin(); Iter != WriteLog.rend(); ++Iter)
    {
		FWriteLogEntry Entry = *Iter;
		// No write records should be within the transaction's stack range.
		AUTORTFM_ENSURE(!IsOnStack(Entry.LogicalAddress));

		memcpy(Entry.LogicalAddress, Entry.Data, Entry.Size);
    }

	AUTORTFM_VERBOSE("Undone a transaction!");
}

void FTransaction::CommitNested()
{
    AUTORTFM_ASSERT(Parent);
    AUTORTFM_ASSERT(CompletionTasks == std::nullopt);

	// We need to pass our write log to our parent transaction, but with care!
	// We need to discard any writes if the memory location is on the parent
	// transaction's stack range.
	for (FWriteLogEntry Write : WriteLog)
	{
		if (Parent->IsOnStack(Write.LogicalAddress))
		{
			continue;
		}

		if (Write.Size <= FHitSet::MaxSize)
		{
			FHitSetEntry HitSetEntry{};
			HitSetEntry.Address = reinterpret_cast<uintptr_t>(Write.LogicalAddress);
			HitSetEntry.Size = static_cast<uint16_t>(Write.Size);
			HitSetEntry.bNoMemoryValidation = Write.bNoMemoryValidation;

			if (Parent->HitSet.FindOrTryInsert(HitSetEntry))
			{
				continue; // Don't duplicate the write-log entry.
			}
		}

		Parent->WriteLog.Push(Write);
	}

	// For all the deferred calls to `PopOnCommitHandler` that we couldn't
	// process (because our transaction nest didn't `PushOnCommitHandler`)
	// we need to move these to the parent now to handle them.
	for (const void* Key : DeferredPopOnCommitHandlers)
	{
		Parent->PopDeferUntilCommitHandler(Key);
	}
	DeferredPopOnCommitHandlers.Reset();

	// For all the deferred calls to `PopOnAbortHandler` that we couldn't
	// process (because our transaction nest didn't `PushOnAbortHandler`)
	// we need to move these to the parent now to handle them.
	for (const void* Key : DeferredPopOnAbortHandlers)
	{
		Parent->PopDeferUntilAbortHandler(Key);
	}
	DeferredPopOnAbortHandlers.Reset();

	// For all the calls to `PopAllOnCommitHandlers` we need to run these
	// again on parent now to handle them there too.
	for (const void* Key : DeferredPopAllOnCommitHandlers)
	{
		Parent->PopAllDeferUntilCommitHandlers(Key);
	}
	DeferredPopAllOnCommitHandlers.Reset();

	// For all the calls to `PopAllOnAbortHandlers` we need to run these
	// again on parent now to handle them there too.
	for (const void* Key : DeferredPopAllOnAbortHandlers)
	{
		Parent->PopAllDeferUntilAbortHandlers(Key);
	}
	DeferredPopAllOnAbortHandlers.Reset();

    Parent->CommitTasks.AddAll(std::move(CommitTasks));
    Parent->PreAbortTasks.AddAll(std::move(PreAbortTasks));
    Parent->AbortTasks.AddAll(std::move(AbortTasks));

    Parent->NewMemoryTracker.Merge(NewMemoryTracker);
}

bool FTransaction::AttemptToCommitOuterNest()
{
    AUTORTFM_ASSERT(!Parent);

	AUTORTFM_VERBOSE("About to run commit tasks!");
	Context->DumpState();
	AUTORTFM_VERBOSE("Running commit tasks...");

    PreAbortTasks.Reset();
    AbortTasks.Reset();

    CommitTasks.RemoveEachForward([] (TTask<void()>& Task)
    { 
        Task();
    });

    CompletionTasks->RemoveEachForward([] (TTask<void()>& Task)
    { 
        Task();
    });

    return true;
}

void FTransaction::SetOpenActiveValidatorEnabled(EMemoryValidationLevel NewMemoryValidationLevel, const void* ReturnAddress)
{
	AUTORTFM_ASSERT(NewMemoryValidationLevel != EMemoryValidationLevel::Disabled);
	CurrentMemoryValidationLevel = NewMemoryValidationLevel;
	CurrentOpenReturnAddress = ReturnAddress;

	if (AutoRTFM::ForTheRuntime::GetMemoryValidationThrottlingEnabled())
	{
		FOpenHashThrottler& Throttler = Context->GetOpenHashThrottler();
		if (!Throttler.ShouldHashFor(ReturnAddress))
		{
			CurrentMemoryValidationLevel = EMemoryValidationLevel::Disabled;
		}
		Throttler.Update();
	}

	SetState<EState::OpenActive>();
}

void FTransaction::SetOpenActive(EMemoryValidationLevel NewMemoryValidationLevel, const void* ReturnAddress)
{
	if (AUTORTFM_UNLIKELY(NewMemoryValidationLevel != EMemoryValidationLevel::Disabled))
	{
		AUTORTFM_MUST_TAIL return SetOpenActiveValidatorEnabled(NewMemoryValidationLevel, ReturnAddress);
	}

	CurrentMemoryValidationLevel = NewMemoryValidationLevel;
	CurrentOpenReturnAddress = ReturnAddress;
	
	// TODO: Validate if open -> open with different validation levels.
	RecordedWriteHash = 0;
	NumWriteLogsHashed = 0;
	CurrentOpenReturnAddress = nullptr;

	SetState<EState::OpenActive>();
}

void FTransaction::SetClosedActive()
{
	SetState<EState::ClosedActive>();
}

void FTransaction::SetOpenInactive()
{
	SetState<EState::OpenInactive>();
}

void FTransaction::SetClosedInactive()
{
	SetState<EState::ClosedInactive>();
}

void FTransaction::SetActive()
{
	switch (CurrentState)
	{
		case EState::OpenActive:
		case EState::ClosedActive:
			break;
		case EState::OpenInactive:
			SetState<EState::OpenActive>();
			break;
		case EState::ClosedInactive:
			SetState<EState::ClosedActive>();
			break;
		default:
			AUTORTFM_FATAL("Invalid state");
	}
}

void FTransaction::SetInactive()
{
	switch (CurrentState)
	{
		case EState::OpenInactive:
		case EState::ClosedInactive:
			break;
		case EState::OpenActive:
			SetState<EState::OpenInactive>();
			break;
		case EState::ClosedActive:
			SetState<EState::ClosedInactive>();
			break;
		default:
			AUTORTFM_FATAL("Invalid state");
	}
}

void FTransaction::SetDone()
{
	SetState<EState::Done>();
}

template<FTransaction::EState NewState>
void FTransaction::SetState()
{
	AUTORTFM_ASSERT(NewState != CurrentState);

	switch (CurrentState)
	{
		case EState::Uninitialized:
			AUTORTFM_ASSERT(NewState == EState::OpenActive || NewState == EState::ClosedActive);
			break;

		// OpenActive -> OpenInactive, ClosedActive or Done
		case EState::OpenActive:
			AUTORTFM_ASSERT(NewState == EState::OpenInactive || NewState == EState::ClosedActive || NewState == EState::Done);
			if (CurrentMemoryValidationLevel != EMemoryValidationLevel::Disabled)
			{
				ValidateWriteHash();
				RecordedWriteHash = 0;
				NumWriteLogsHashed = 0;
			}
			else
			{
				AUTORTFM_ASSERT(RecordedWriteHash == 0 && NumWriteLogsHashed == 0);
			}
			break;

		// ClosedActive -> ClosedInactive, OpenActive or Done
		case EState::ClosedActive:
			AUTORTFM_ASSERT(NewState == EState::ClosedInactive || NewState == EState::OpenActive || NewState == EState::Done);
			break;

		// ClosedActive -> OpenActive
		case EState::OpenInactive:
			AUTORTFM_ASSERT(NewState == EState::OpenActive);
			break;

		// ClosedInactive -> ClosedActive
		case EState::ClosedInactive:
			AUTORTFM_ASSERT(NewState == EState::ClosedActive);
			break;

		case EState::Done:
			AUTORTFM_FATAL("Once Done, the transaction cannot change state without a call to Reset()");
			break;

		default:
			AUTORTFM_FATAL("Invalid state");
			break;
	}

	// OpenInactive, ClosedActive or Done -> OpenActive
	if (NewState == EState::OpenActive) {
		if (CurrentMemoryValidationLevel != EMemoryValidationLevel::Disabled)
		{
			AUTORTFM_ASSERT(RecordedWriteHash == 0 && NumWriteLogsHashed == 0);
			RecordWriteHash();
		}
	}

	CurrentState = NewState;
}

void FTransaction::DebugBreakIfMemoryValidationFails()
{
	if (CurrentMemoryValidationLevel != EMemoryValidationLevel::Disabled)
	{
		FWriteHash OldHash = RecordedWriteHash;
		FWriteHash NewHash = CalculateNestedWriteHash(CurrentOpenReturnAddress);
		if (OldHash != NewHash)
		{
			AUTORTFM_WARN("DebugBreakIfInvalidMemoryHash() detected a change in hash");
			__builtin_debugtrap();
		}
	}
}

void FTransaction::RecordWriteHash()
{
	NumWriteLogsHashed = WriteLog.Num();
	RecordedWriteHash = CalculateNestedWriteHash(CurrentOpenReturnAddress);
}

void FTransaction::ValidateWriteHash() const
{
	const FWriteHash OldHash = RecordedWriteHash;
	const FWriteHash NewHash = CalculateNestedWriteHash(CurrentOpenReturnAddress);

	if (OldHash != NewHash)
	{
		OnValidatorWriteHashMismatch(CurrentMemoryValidationLevel,
			"Memory modified in a transaction was also modified in an call to AutoRTFM::Open().\n"
			"This may lead to memory corruption if the transaction is aborted.");
	}
}

void FTransaction::OnValidatorWriteHashMismatch(EMemoryValidationLevel MemoryValidationLevel, const char* Message, ...) const
{
	va_list Args;
	va_start(Args, Message);
	switch (MemoryValidationLevel)
	{
		case EMemoryValidationLevel::Default:
		case EMemoryValidationLevel::Disabled:
			break;
		case EMemoryValidationLevel::Warn:
			AUTORTFM_WARN_V(Message, Args);
			break;
		case EMemoryValidationLevel::Error:
			if (!ForTheRuntime::GetEnsureOnInternalAbort())
			{
				AUTORTFM_FATAL_V(Message, Args);
			}
			else
			{
				AUTORTFM_ENSURE_MSG_V(false, Message, Args);
			}
			break;
	}
	va_end(Args);
}

FTransaction::FWriteHash FTransaction::CalculateNestedWriteHash(const void *FunctionAddress) const
{
	const FWriteHash Hash = CalculateNestedWriteHashWithLimit(NumWriteLogsHashed, FunctionAddress);
	Context->GetOpenHashThrottler().Update();
	return Hash;
}

FTransaction::FWriteHash FTransaction::CalculateNestedWriteHashWithLimit(size_t NumWriteEntries, const void *FunctionAddress) const
{
	FWriteHash Hash = 0;
	if (nullptr != Parent)
	{
		Hash = 31 * Parent->CalculateNestedWriteHashWithLimit(Parent->WriteLog.Num(), FunctionAddress);
	}
	{
		FOpenHashThrottler::FHashScope Profile(Context->GetOpenHashThrottler(), FunctionAddress, WriteLog);
		Hash ^= WriteLog.Hash(NumWriteEntries);
	}
	return Hash;
}

} // namespace AutoRTFM

#endif // defined(__AUTORTFM) && __AUTORTFM
