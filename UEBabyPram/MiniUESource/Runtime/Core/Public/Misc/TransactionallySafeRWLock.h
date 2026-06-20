// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AutoRTFM.h"

#if UE_AUTORTFM
#include "Async/TransactionallySafeSharedMutex.h"
#else
#include "HAL/CriticalSection.h"
#endif // UE_AUTORTFM


#if UE_AUTORTFM

// Transactionally-safe RWLocks are implemented in terms of FTransactionallySafeSharedMutex.
// Unfortunately, the method names differ slightly between RWLock and SharedMutex, so we adapt the names here.
class FTransactionallySafeRWLock : ::UE::FTransactionallySafeSharedMutex
{
private:
	using Super = ::UE::FTransactionallySafeSharedMutex;

public:
	void ReadLock()
	{
		Super::LockShared();
	}

	void ReadUnlock()
	{
		Super::UnlockShared();
	}

	void WriteLock()
	{
		Super::Lock();
	}

	void WriteUnlock()
	{
		Super::Unlock();
	}

	bool TryWriteLock()
	{
		return Super::TryLock();
	}
};

#else
using FTransactionallySafeRWLock = ::FRWLock;
#endif