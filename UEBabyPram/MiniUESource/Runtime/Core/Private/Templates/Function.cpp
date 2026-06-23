// Copyright Epic Games, Inc. All Rights Reserved.

#include "Templates/Function.h"

namespace UE::Core::Private::Function
{
	FORCENOINLINE void CheckCallable(void* Callable)
	{
		checkf(Callable, TEXT("Attempting to call an unbound TFunction!"));
	}
}
