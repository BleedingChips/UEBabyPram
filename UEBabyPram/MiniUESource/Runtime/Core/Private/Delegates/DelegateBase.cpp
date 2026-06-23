// Copyright Epic Games, Inc. All Rights Reserved.

#include "Delegates/DelegateBase.h"

#if UE_DELEGATE_CHECK_LIFETIME
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogDelegates, Log, All);

bool FTrackedDelegateInstanceExtras::IsValid() const
{
	return ModuleName == NAME_None || FModuleManager::Get().IsModuleSafeToUse(ModuleName);
}

bool FTrackedDelegateInstanceExtras::CheckValid() const
{
	if (!IsValid())
	{
		UE_LOG(LogDelegates, Fatal, TEXT("FTrackedDelegateInstanceExtras::CheckValid: bad delegate from %s (%s)"),
			*GetBoundFunctionName().ToString(), *ModuleName.ToString());

		return false;
	}
	else
	{
		return true;
	}
}

#endif //UE_DELEGATE_CHECK_LIFETIME

void* UE::Core::Private::DelegateAllocate(size_t Size, FDelegateAllocation& Allocation)
{
	int32 NewDelegateSize = FMath::DivideAndRoundUp((int32)Size, (int32)sizeof(FAlignedInlineDelegateType));
	if (Allocation.DelegateSize != NewDelegateSize)
	{
		Allocation.DelegateAllocator.ResizeAllocation(0, NewDelegateSize, sizeof(FAlignedInlineDelegateType));
		Allocation.DelegateSize = NewDelegateSize;
	}

	return Allocation.DelegateAllocator.GetAllocation();
}
