// Copyright Epic Games, Inc. All Rights Reserved.

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "Context.h"
#include "ContextInlines.h"

#include "AutoRTFMMetrics.h"
#include "CallNestInlines.h"
#include "ExternAPI.h"
#include "FunctionMap.h"
#include "ScopedGuard.h"
#include "StackRange.h"
#include "Stats.h"
#include "Transaction.h"
#include "TransactionInlines.h"
#include "Utils.h"

#if AUTORTFM_PLATFORM_WINDOWS
#include "WindowsHeader.h"
#endif

namespace
{
AutoRTFM::FAutoRTFMMetrics GAutoRTFMMetrics;
}

namespace AutoRTFM
{

FContext* FContext::Instance = nullptr;

FContext* FContext::Create()
{
	AUTORTFM_ENSURE(Instance == nullptr);
	void* Memory = AutoRTFM::Allocate(sizeof(FContext), alignof(FContext));
	Instance = new (Memory) FContext();
	return Instance;
}

void ResetAutoRTFMMetrics()
{
	GAutoRTFMMetrics = FAutoRTFMMetrics{};
}

// get a snapshot of the current internal metrics
FAutoRTFMMetrics GetAutoRTFMMetrics()
{
	return GAutoRTFMMetrics;
}

bool FContext::IsTransactional() const
{
    return GetStatus() == EContextStatus::OnTrack;
}

bool FContext::IsCommitting() const
{
	switch (GetStatus())
	{
	default:
		return false;
	case EContextStatus::Committing:
		return true;
	}
}

bool FContext::IsCommittingOrAborting() const
{
	switch (GetStatus())
	{
	default:
		return true;
	case EContextStatus::Idle:
	case EContextStatus::OnTrack:
		return false;
	}
}

bool FContext::IsRetrying() const
{
	return GetStatus() == EContextStatus::AbortedByCascadingRetry;
}

void FContext::MaterializeDeferredTransactions()
{
	uint64_t NumToAllocate = GetNumDeferredTransactions();
	NumDeferredTransactions = 0;
	for (uint64_t I = 0; I < NumToAllocate; ++I)
	{
		StartNonDeferredTransaction(EMemoryValidationLevel::Disabled);
	}
}

void FContext::StartTransaction(EMemoryValidationLevel MemoryValidationLevel)
{
	if (MemoryValidationLevel != EMemoryValidationLevel::Disabled)
	{
		MaterializeDeferredTransactions();
		StartNonDeferredTransaction(MemoryValidationLevel);
		return;
	}

	NumDeferredTransactions += 1;
	AUTORTFM_ENSURE_MSG(CurrentTransaction, "FContext::StartTransaction() can only be called within a scoped transaction");
	AUTORTFM_ASSERT(Status == EContextStatus::OnTrack);

	GAutoRTFMMetrics.NumTransactionsStarted++;
}

void FContext::StartNonDeferredTransaction(EMemoryValidationLevel MemoryValidationLevel)
{
	AUTORTFM_ASSERT(GetNumDeferredTransactions() == 0);
	AUTORTFM_ENSURE_MSG(CurrentTransaction, "FContext::StartNonDeferredTransaction() can only be called within a scoped transaction");

	PushTransaction(
		/* Closed */ false,
		/* bIsScoped */ false,
		/* StackRange */ CurrentTransaction->GetStackRange(),
		/* MemoryValidationLevel */ MemoryValidationLevel);

	// This form of transaction is always ultimately within a scoped Transact 
	AUTORTFM_ASSERT(Status == EContextStatus::OnTrack);

	GAutoRTFMMetrics.NumTransactionsStarted++;
}

ETransactionResult FContext::CommitTransaction()
{
	AUTORTFM_ASSERT(Status == EContextStatus::OnTrack);

	ETransactionResult Result = ETransactionResult::Committed;

	if (GetNumDeferredTransactions())
	{
		// The optimization worked! We didn't need to allocate an FTransaction for this.
		NumDeferredTransactions -= 1;
	}
	else
	{
		// Scoped transactions commit on return, so committing explicitly isn't allowed
		AUTORTFM_ASSERT(CurrentTransaction->IsScopedTransaction() == false);

		if (CurrentTransaction->IsNested())
		{
			Result = ResolveNestedTransaction(CurrentTransaction);
		}
		else
		{
			AUTORTFM_VERBOSE("About to commit; my state is:");
			DumpState();
			AUTORTFM_VERBOSE("Committing...");

			if (AttemptToCommitTransaction(CurrentTransaction))
			{
				Result = ETransactionResult::Committed;
			}
			else
			{
				AUTORTFM_VERBOSE("Commit failed!");
				AUTORTFM_ASSERT(Status != EContextStatus::OnTrack);
				AUTORTFM_ASSERT(Status != EContextStatus::Idle);
			}
		}

		// Parent transaction is now the current transaction
		PopTransaction();
	}

	GAutoRTFMMetrics.NumTransactionsCommitted++;

	return Result;
}

void FContext::RollbackTransaction(EContextStatus NewStatus)
{
	GAutoRTFMMetrics.NumTransactionsAborted++;

	AUTORTFM_ASSERT(Status == EContextStatus::OnTrack);
	AUTORTFM_ASSERT(IsStatusAborting(NewStatus));

	Status = NewStatus;

	if (GetNumDeferredTransactions())
	{
		// The optimization worked! We didn't need to allocate an FTransaction for this.
		NumDeferredTransactions -= 1;
	}
	else
	{
		AUTORTFM_ASSERT(nullptr != CurrentTransaction);

		// Sort out how aborts work
		CurrentTransaction->AbortWithoutThrowing();

		// Non-scoped transactions are ended immediately, but scoped need to get to the end scope before being popped
		if (!CurrentTransaction->IsScopedTransaction())
		{
			PopTransaction();
		}
	}

}

void FContext::AbortTransaction(EContextStatus NewStatus)
{
	RollbackTransaction(NewStatus);
	Throw();
}

EContextStatus FContext::CallClosedNest(void (*ClosedFunction)(void* Arg), void* Arg)
{
	FTransaction* const Transaction = GetCurrentTransaction();
	AUTORTFM_ASSERT(Transaction != nullptr);
	AUTORTFM_ASSERT(FTransaction::EState::OpenActive == Transaction->State());
	EMemoryValidationLevel PreviousValidationLevel = Transaction->MemoryValidationLevel();
	const void* PreviousOpenReturnAddress = Transaction->OpenReturnAddress();
	Transaction->SetClosedActive();

	PushCallNest(CallNestPool.Take(this));

	CurrentNest->Try([&]() { ClosedFunction(Arg); });

	PopCallNest();

	if (Transaction == CurrentTransaction && Transaction->IsClosedActive())  // Transaction may have been aborted.
	{
		Transaction->SetOpenActive(PreviousValidationLevel, PreviousOpenReturnAddress);
	}

	return GetStatus();
}

void FContext::PushCallNest(FCallNest* NewCallNest)
{
	AUTORTFM_ASSERT(NewCallNest != nullptr);
	AUTORTFM_ASSERT(NewCallNest->Parent == nullptr);

	NewCallNest->Parent = CurrentNest;
	CurrentNest = NewCallNest;
}

void FContext::PopCallNest()
{
	AUTORTFM_ASSERT(CurrentNest != nullptr);
	FCallNest* OldCallNest = CurrentNest;
	CurrentNest = CurrentNest->Parent;

	CallNestPool.Return(OldCallNest);
}

FTransaction* FContext::PushTransaction(
	bool bClosed,
	bool bIsScoped,
	FStackRange StackRange,
	EMemoryValidationLevel MemoryValidationLevel)
{
	AUTORTFM_ASSERT(!GetNumDeferredTransactions());

	if (CurrentTransaction != nullptr)
	{
		AUTORTFM_ASSERT(CurrentTransaction->IsActive());
		CurrentTransaction->SetInactive();
	}

	FTransaction* NewTransaction = TransactionPool.Take(this);
	NewTransaction->Initialize(
		/* Parent */ CurrentTransaction, 
		/* bIsScoped */ bIsScoped, 
		/* StackRange */ StackRange);

	if (bClosed)
	{
		NewTransaction->SetClosedActive();
	}
	else
	{
		NewTransaction->SetOpenActive(MemoryValidationLevel, /* ReturnAddress */ nullptr);
	}

	CurrentTransaction = NewTransaction;

	// Collect stats that we've got a new transaction.
	Stats.Collect<EStatsKind::Transaction>();

	return NewTransaction;
}

void FContext::PopTransaction()
{
	AUTORTFM_ASSERT(!GetNumDeferredTransactions());
	AUTORTFM_ASSERT(CurrentTransaction != nullptr);
	AUTORTFM_ASSERT(CurrentTransaction->IsDone());
	FTransaction* OldTransaction = CurrentTransaction;
	CurrentTransaction = CurrentTransaction->GetParent();
	if (CurrentTransaction != nullptr)
	{
		AUTORTFM_ASSERT(CurrentTransaction->IsInactive());
		CurrentTransaction->SetActive();
	}
	TransactionPool.Return(OldTransaction);
}

void FContext::ClearTransactionStatus()
{
	switch (Status)
	{
	case EContextStatus::OnTrack:
		break;
	case EContextStatus::AbortedByLanguage:
	case EContextStatus::AbortedByRequest:
	case EContextStatus::AbortedByCascadingAbort:
	case EContextStatus::AbortedByCascadingRetry:
	case EContextStatus::AbortedByFailedLockAcquisition:
		Status = EContextStatus::OnTrack;
		break;
	default:
		AutoRTFM::InternalUnreachable();
	}
}

ETransactionResult FContext::ResolveNestedTransaction(FTransaction* Transaction)
{
	if (Status == EContextStatus::OnTrack)
	{
		bool bCommitResult = AttemptToCommitTransaction(Transaction);
		AUTORTFM_ASSERT(bCommitResult);
		AUTORTFM_ASSERT(Status == EContextStatus::OnTrack);
		return ETransactionResult::Committed;
	}

	AUTORTFM_ASSERT(Transaction->IsDone());

	switch (Status)
	{
	case EContextStatus::AbortedByRequest:
		return ETransactionResult::AbortedByRequest;
	case EContextStatus::AbortedByLanguage:
		return ETransactionResult::AbortedByLanguage;
	case EContextStatus::AbortedByCascadingAbort:
	case EContextStatus::AbortedByCascadingRetry:
		return ETransactionResult::AbortedByCascade;
	default:
		AutoRTFM::InternalUnreachable();
	}
}

AutoRTFM::FStackRange FContext::GetThreadStackRange()
{
	// On some platforms, looking up the stack range is quite expensive, so caching it
	// is important for performance. Linux glibc is particularly bad--see
	// https://github.com/golang/go/issues/68587 for a deep dive.
	thread_local FStackRange CachedStackRange = []
	{
		FStackRange Stack;

#if AUTORTFM_PLATFORM_WINDOWS
		GetCurrentThreadStackLimits(reinterpret_cast<PULONG_PTR>(&Stack.Low), reinterpret_cast<PULONG_PTR>(&Stack.High));
#elif defined(__APPLE__)         
		Stack.High = pthread_get_stackaddr_np(pthread_self());
		size_t StackSize = pthread_get_stacksize_np(pthread_self());
		Stack.Low = static_cast<char*>(Stack.High) - StackSize;
#else
		pthread_attr_t Attr{};
		pthread_getattr_np(pthread_self(), &Attr);
		Stack.Low = 0;
		size_t StackSize = 0;
		pthread_attr_getstack(&Attr, &Stack.Low, &StackSize);
		Stack.High = static_cast<char*>(Stack.Low) + StackSize;
#endif

		AUTORTFM_ASSERT(Stack.High > Stack.Low);
		return Stack;
	}();

	return CachedStackRange;
}

ETransactionResult FContext::Transact(void (*UninstrumentedFunction)(void*), void (*InstrumentedFunction)(void*), void* Arg)
{
    if (AUTORTFM_UNLIKELY(EContextStatus::Committing == Status))
    {
    	return ETransactionResult::AbortedByTransactDuringCommit;
    }

    if (AUTORTFM_UNLIKELY(IsAborting()))
    {
    	return ETransactionResult::AbortedByTransactDuringAbort;
    }
    
    AUTORTFM_ASSERT(Status == EContextStatus::Idle || Status == EContextStatus::OnTrack);

    if (!InstrumentedFunction)
    {
		AUTORTFM_WARN("Could not find function in AutoRTFM::FContext::Transact");
        return ETransactionResult::AbortedByLanguage;
    }

	// TODO: We could do better if we ever need to. There is no fundamental
	// reason we can't have a "range" of deferred transactions in the middle
	// of the transaction stack.
	MaterializeDeferredTransactions();
	AUTORTFM_ASSERT(!GetNumDeferredTransactions());
    
	FCallNest* NewNest = CallNestPool.Take(this);

	void* TransactStackStart = &NewNest;

	ETransactionResult Result = ETransactionResult::Committed; // Initialize to something to make the compiler happy.

	if (!CurrentTransaction)
	{
		// If exceptions are enabled, then ensure that the transaction is automatically committed if
		// an exception is thrown inside the transaction and the handler is outside the transaction.
		struct FAutoCommitter final
		{
			FAutoCommitter(FContext& Context) : Context{Context} {}
			~FAutoCommitter()
			{
				if (bCommitOnDestruct)
				{
					Commit();
				}
			}

			void Commit()
			{
				bCommitOnDestruct = false;

				if (!Context.CurrentTransaction->IsDone())
				{
					Context.CurrentTransaction->SetDone();
				}

				Context.PopCallNest();
				Context.PopTransaction();
				Context.ClearTransactionStatus();

				AUTORTFM_ASSERT(Context.CurrentNest == nullptr);
				AUTORTFM_ASSERT(Context.CurrentTransaction == nullptr);

				Context.Reset();
			}
		private:
			FContext& Context;
			bool bCommitOnDestruct = true;
		};
		FAutoCommitter AutoCommitter(*this);

		AUTORTFM_ASSERT(Status == EContextStatus::Idle);

		AUTORTFM_ASSERT(CurrentThreadId == FThreadID::Invalid);
		CurrentThreadId = FThreadID::GetCurrent();

		AUTORTFM_ASSERT(Stack == FStackRange{});
		Stack = GetThreadStackRange();

		AUTORTFM_ASSERT(Stack.Contains(TransactStackStart));

		FTransaction* NewTransaction = PushTransaction(
			/* Closed */ true,
			/* bIsScoped */ true,
			/* StackRange */ {Stack.Low, &TransactStackStart},
			/* MemoryValidationLevel */ EMemoryValidationLevel::Disabled);

		PushCallNest(NewNest);

		bool bTriedToRunOnce = false;

        for (;;)
        {
            Status = EContextStatus::OnTrack;
            AUTORTFM_ASSERT(CurrentTransaction->IsFresh());
			CurrentNest->Try([&] () { InstrumentedFunction(Arg); });
			AUTORTFM_ASSERT(CurrentTransaction == NewTransaction); // The transaction lambda should have unwound any nested transactions.
            AUTORTFM_ASSERT(Status != EContextStatus::Idle);

			switch (Status)
			{
			case EContextStatus::OnTrack:
				AUTORTFM_VERBOSE("About to commit; my state is:");
				DumpState();
				AUTORTFM_VERBOSE("Committing...");

				if (AUTORTFM_UNLIKELY(!bTriedToRunOnce && AutoRTFM::ForTheRuntime::ShouldRetryNonNestedTransactions()))
				{
					// We skip trying to commit this time, and instead re-run the transaction.
					Status = EContextStatus::AbortedByFailedLockAcquisition;
					CurrentTransaction->AbortWithoutThrowing();
					ClearTransactionStatus();

					// We've tried to run at least once if we get here!
					CurrentTransaction->Reset();
					CurrentTransaction->SetClosedActive();
					bTriedToRunOnce = true;
					continue;
				}

				if (AttemptToCommitTransaction(CurrentTransaction))
				{
					Result = ETransactionResult::Committed;
					break;
				}

				AUTORTFM_VERBOSE("Commit failed!");

				AUTORTFM_ASSERT(Status != EContextStatus::OnTrack);
				AUTORTFM_ASSERT(Status != EContextStatus::Idle);
				break;

			case EContextStatus::AbortedByRequest:
				Result = ETransactionResult::AbortedByRequest;
				break;

			case EContextStatus::AbortedByLanguage:
				Result = ETransactionResult::AbortedByLanguage;
				break;

			case EContextStatus::AbortedByCascadingAbort:
				Result = ETransactionResult::AbortedByCascade;
				break;

			case EContextStatus::AbortedByCascadingRetry:
				// Clean up the transaction to get it ready for re-execution.
				ClearTransactionStatus();
				CurrentTransaction->Reset();

				AUTORTFM_ASSERT(Status == EContextStatus::OnTrack);

				// Then get rolling!
				CurrentTransaction->SetClosedActive();

				// Lastly check whether the AutoRTFM runtime was disabled during
				// the call to `PostAbortCallback`, and if so just execute the
				// function without AutoRTFM as a fallback.
				if (!ForTheRuntime::IsAutoRTFMRuntimeEnabled())
				{
					UninstrumentedFunction(Arg);
					Result = ETransactionResult::Committed;
					break;
				}

				continue;

			case EContextStatus::AbortedByFailedLockAcquisition:
				continue; // Retry the transaction

			default:
				Unreachable();
			}

			break;
		}

		AutoCommitter.Commit();
	}
	else
	{
		// This transaction is within another transaction

		// If exceptions are enabled, then ensure that the transaction is automatically committed if
		// an exception is thrown inside the transaction and the handler is outside the transaction.
		struct FAutoCommitter final
		{
			FAutoCommitter(FContext& Context) : Context{Context} {}
			~FAutoCommitter()
			{
				if (bCommitOnDestruct)
				{
					Apply();
				}
			}

			ETransactionResult Apply()
			{
				bCommitOnDestruct = false;

				ETransactionResult Result = Context.ResolveNestedTransaction(Context.CurrentTransaction);
				
				Context.PopCallNest();
				Context.PopTransaction();

				AUTORTFM_ASSERT(Context.CurrentNest != nullptr);
				AUTORTFM_ASSERT(Context.CurrentTransaction != nullptr);

				if (Result == ETransactionResult::AbortedByCascade)
				{
					// Cascading aborts continue to abort transactions until reaching
					// a non-scoped transaction, or abort all transactions if the
					// transaction stack contains only scoped transactions.
					if (Context.CurrentTransaction->IsScopedTransaction())
					{
						Context.CurrentTransaction->AbortAndThrow(); // note: does not return
					}
				}
				else
				{
					Context.ClearTransactionStatus();
				}

				return Result;
			}
		private:
			FContext& Context;
			bool bCommitOnDestruct = true;
		};
		FAutoCommitter AutoCommitter(*this);

		AUTORTFM_ASSERT(Status == EContextStatus::OnTrack);

		AUTORTFM_ASSERT(CurrentThreadId == FThreadID::GetCurrent());

		AUTORTFM_ASSERT(Stack.Contains(TransactStackStart));

		FTransaction* NewTransaction = PushTransaction(
			/* Closed */ true,
			/* bIsScoped */ true,
			/* StackRange */ {Stack.Low, &TransactStackStart},
			/* MemoryValidationLevel */ EMemoryValidationLevel::Disabled);

		PushCallNest(NewNest);

		bool bTriedToRunOnce = false;

		for (;;)
		{
			CurrentNest->Try([&]() { InstrumentedFunction(Arg); });
			AUTORTFM_ASSERT(CurrentTransaction == NewTransaction);

			if (Status == EContextStatus::OnTrack)
			{
				if (AUTORTFM_UNLIKELY(!bTriedToRunOnce && AutoRTFM::ForTheRuntime::ShouldRetryNestedTransactionsToo()))
				{
					// We skip trying to commit this time, and instead re-run the transaction.
					Status = EContextStatus::AbortedByFailedLockAcquisition;
					NewTransaction->AbortWithoutThrowing();
					ClearTransactionStatus();

					// We've tried to run at least once if we get here!
					CurrentTransaction->Reset();
					CurrentTransaction->SetClosedActive();
					bTriedToRunOnce = true;
					continue;
				}
			}

			break;
		}

		Result = AutoCommitter.Apply();
	}

	return Result;
}

void FContext::AbortByRequestAndThrow()
{
    AUTORTFM_ASSERT(Status == EContextStatus::OnTrack);
	GAutoRTFMMetrics.NumTransactionsAbortedByRequest++;
	RollbackTransaction(EContextStatus::AbortedByRequest);
	Throw();
}

void FContext::AbortByLanguageAndThrow()
{
    AUTORTFM_ASSERT(Status == EContextStatus::OnTrack);
	GAutoRTFMMetrics.NumTransactionsAbortedByLanguage++;
	RollbackTransaction(EContextStatus::AbortedByLanguage);
	Throw();
}

void FContext::Reset()
{
	AUTORTFM_ASSERT(CurrentThreadId == FThreadID::GetCurrent() || CurrentThreadId == FThreadID::Invalid);

	CurrentThreadId = FThreadID::Invalid;
	Stack = {};
	CurrentTransaction = nullptr;
	CurrentNest = nullptr;
	Status = EContextStatus::Idle;
	StackLocalInitializerDepth = 0;
	TaskPool.Reset();
}

void FContext::Throw()
{
	GetCurrentNest()->AbortJump.Throw();
}

void FContext::DumpState() const
{
	AUTORTFM_VERBOSE("Context at %p", this);
}

} // namespace AutoRTFM

#endif // defined(__AUTORTFM) && __AUTORTFM
