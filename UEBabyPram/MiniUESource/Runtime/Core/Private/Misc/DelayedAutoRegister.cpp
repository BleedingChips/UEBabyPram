// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/DelayedAutoRegister.h"

#include "Containers/Set.h"
#include "Delegates/Delegate.h"
#include "Delegates/DelegateBase.h"
#include "Delegates/DelegateInstancesImpl.h"
#include "Delegates/IDelegateInstance.h"
#include "Misc/AssertionMacros.h"
#include "Templates/Function.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FDelayedAutoRegisterDelegate, EDelayedRegisterRunPhase);

static TSet<EDelayedRegisterRunPhase> GPhasesAlreadyRun;

static FDelayedAutoRegisterDelegate& GetDelayedAutoRegisterDelegate(EDelayedRegisterRunPhase Phase)
{
	static FDelayedAutoRegisterDelegate Singleton[(uint8)EDelayedRegisterRunPhase::NumPhases];
	return Singleton[(uint8)Phase];
}

FDelayedAutoRegisterHelper::FDelayedAutoRegisterHelper(EDelayedRegisterRunPhase Phase, TFunction<void()> RegistrationFunction, const bool bRerunOnLiveCodingReload)
	: FDelayedAutoRegisterHelper(
		Phase,
		[RegistrationFunction](const EDelayedRegisterRunPhase) { RegistrationFunction(); },
		bRerunOnLiveCodingReload)
{
}

FDelayedAutoRegisterHelper::FDelayedAutoRegisterHelper(
	EDelayedRegisterRunPhase Phase,
	TFunction<void(const EDelayedRegisterRunPhase)> RegistrationFunction,
	const bool bRerunOnLiveCodingReload)
{
#if WITH_EDITOR && WITH_LIVE_CODING
	// We can use both the provided phase AND LiveCodingReload
	if (bRerunOnLiveCodingReload && Phase != EDelayedRegisterRunPhase::LiveCodingReload)
	{
		GetDelayedAutoRegisterDelegate(EDelayedRegisterRunPhase::LiveCodingReload).AddLambda(RegistrationFunction);
	}
#endif

	// if the phase has already passed, we just run the function immediately
	if (Phase < EDelayedRegisterRunPhase::NumRunOncePhases && GPhasesAlreadyRun.Contains(Phase))
	{
		RegistrationFunction(Phase);
	}
	else
	{
		GetDelayedAutoRegisterDelegate(Phase).AddLambda(RegistrationFunction);
	}
}

void FDelayedAutoRegisterHelper::RunAndClearDelayedAutoRegisterDelegates(EDelayedRegisterRunPhase RunPhase)
{
	const bool bIsRunOncePhase = RunPhase < EDelayedRegisterRunPhase::NumRunOncePhases;
	if (bIsRunOncePhase)
	{
		checkf(!GPhasesAlreadyRun.Contains(RunPhase), TEXT("Delayed Startup phase %d has already run - it is not expected to be run again!"), (int32)RunPhase);
	}

	// run all the delayed functions!
	GetDelayedAutoRegisterDelegate(RunPhase).Broadcast(RunPhase);

	if (bIsRunOncePhase)
	{
		GetDelayedAutoRegisterDelegate(RunPhase).Clear();
		GPhasesAlreadyRun.Add(RunPhase);
	}
}
