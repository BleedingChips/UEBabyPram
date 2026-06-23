// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "Context.h"
#include "TransactionInlines.h"

namespace AutoRTFM
{

UE_AUTORTFM_FORCEINLINE FContext* FContext::Get()
{
	return FContext::Instance;
}

AUTORTFM_NO_ASAN UE_AUTORTFM_FORCEINLINE void FContext::RecordWrite(void* LogicalAddress, size_t Size)
{
    GetCurrentTransaction()->RecordWrite(LogicalAddress, Size);
}

template<unsigned SIZE> AUTORTFM_NO_ASAN UE_AUTORTFM_FORCEINLINE void FContext::RecordWrite(void* LogicalAddress)
{
	if (MustMaterializeDeferredTransactions())
	{
		AUTORTFM_MUST_TAIL return RecordWriteSlow<SIZE>(LogicalAddress);
	}
	else
	{
		GetMaterializedTransaction()->RecordWrite<SIZE>(LogicalAddress);
	}
}

template<unsigned SIZE> AUTORTFM_NO_ASAN UE_AUTORTFM_FORCENOINLINE void FContext::RecordWriteSlow(void* LogicalAddress)
{
	// Going through get current transaction will do the materialization of deferred transactions, but it is slow
	// so that is why we use this carve out.
	GetCurrentTransaction()->RecordWrite<SIZE>(LogicalAddress);
}

UE_AUTORTFM_FORCEINLINE void FContext::DidAllocate(void* LogicalAddress, size_t Size)
{
    GetCurrentTransaction()->DidAllocate(LogicalAddress, Size);
}

UE_AUTORTFM_FORCEINLINE void FContext::DidFree(void* LogicalAddress)
{
    // We can do free's in the open within a transaction *during* when the
    // transaction itself is being destroyed, so we need to check for that case.
	FTransaction* Transaction = GetCurrentTransaction();
	if (AUTORTFM_LIKELY(Transaction))
	{
		Transaction->DidFree(LogicalAddress);
	}
}

UE_AUTORTFM_FORCEINLINE bool FContext::AttemptToCommitTransaction(FTransaction* const Transaction)
{
    AUTORTFM_ASSERT(EContextStatus::OnTrack == Status);

    Status = EContextStatus::Committing;

    const bool bResult = Transaction->AttemptToCommit();

    if (bResult)
    {
        Status = EContextStatus::OnTrack;    
    }

    return bResult;
}

} // namespace AutoRTFM

#endif // (defined(__AUTORTFM) && __AUTORTFM)
