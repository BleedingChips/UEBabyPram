// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AutoRTFM.h"
#include "Async/RecursiveMutex.h"

#if UE_AUTORTFM
#include "AutoRTFM/OpenWrapper.h"
#include "Templates/SharedPointer.h"
#endif

namespace UE
{

#if UE_AUTORTFM

// A transactionally safe recursive mutex that works in the following novel ways:
// - In the open (non-transactional):
//   - Take the lock like before. Simple!
//   - Free the lock like before too.
// - In the closed (transactional):
//   - During locking we query `TransactionalLockCount`:
//     - 0 means we haven't taken the lock within our transaction nest and need to acquire the lock.
//     - Otherwise we already have the lock (and are preventing non-transactional code seeing any
//       modifications we've made while holding the lock), so just bump `TransactionalLockCount`.
//     - We also register an on-abort handler to release the lock should we abort (but we need to
//       query `TransactionalLockCount` even there because we could be aborting an inner transaction
//       and the parent transaction still wants to have the lock held!).
//   - During unlocking we defer doing the unlock until the transaction commits.
//
// Thus with this approach we will hold this lock for the *entirety* of the transactional nest should
// we take the lock during the transaction, thus preventing non-transactional code from seeing any
// modifications we should make.
struct FTransactionallySafeRecursiveMutexDefinition final
{
	FTransactionallySafeRecursiveMutexDefinition() : State(MakeShared<FState>())
	{
	}

	void Lock()
	{
		if (AutoRTFM::IsTransactional() || AutoRTFM::IsCommittingOrAborting())
		{
			UE_AUTORTFM_OPEN
			{
				// The transactional system which can increment TransactionalLockCount
				// is always single-threaded, thus this is safe to check without atomicity.
				if (0 == State->TransactionalLockCount)
				{
					State->Mutex.Lock();
				}

				State->TransactionalLockCount += 1;
			};

			// We explicitly copy the state here for the case that `this` was stack
			// allocated and has already died before the on-abort is hit.
			AutoRTFM::OnAbort([State = AutoRTFM::TOpenWrapper{this->State}]
			{
				ensure(0 != State.Object->TransactionalLockCount);

				State.Object->TransactionalLockCount -= 1;

				if (0 == State.Object->TransactionalLockCount)
				{
					State.Object->Mutex.Unlock();
				}
			});
		}
		else
		{
			State->Mutex.Lock();
			ensure(0 == State->TransactionalLockCount);
		}
	}

	void Unlock()
	{
		if (AutoRTFM::IsTransactional() || AutoRTFM::IsCommittingOrAborting())
		{
			// We explicitly copy the state here for the case that `this` was stack
			// allocated and has already died before the on-commit is hit.
			AutoRTFM::OnCommit([State = AutoRTFM::TOpenWrapper{this->State}]
			{
				ensure(0 != State.Object->TransactionalLockCount);

				State.Object->TransactionalLockCount -= 1;

				if (0 == State.Object->TransactionalLockCount)
				{
					State.Object->Mutex.Unlock();
				}
			});
		}
		else
		{
			ensure(0 == State->TransactionalLockCount);
			State->Mutex.Unlock();
		}
	}

	[[nodiscard]] bool TryLock()
	{
		if (AutoRTFM::IsTransactional() || AutoRTFM::IsCommittingOrAborting())
		{
			// TODO: implement transactional TryLock. #jira SOL-8303
			return false;
		}
		else
		{
			ensure(0 == State->TransactionalLockCount);
			return State->Mutex.TryLock();
		}
	}

private:
	UE_NONCOPYABLE(FTransactionallySafeRecursiveMutexDefinition)

	struct FState final
	{
		UE::FRecursiveMutex Mutex;
		uint32 TransactionalLockCount = 0;

		FState() = default;
		
		~FState()
		{
			ensure(0 == TransactionalLockCount);
		}
	};

	TSharedPtr<FState> State;
};

using FTransactionallySafeRecursiveMutex = ::UE::FTransactionallySafeRecursiveMutexDefinition;

#else
using FTransactionallySafeRecursiveMutex = ::UE::FRecursiveMutex;
#endif

} // namespace UE
