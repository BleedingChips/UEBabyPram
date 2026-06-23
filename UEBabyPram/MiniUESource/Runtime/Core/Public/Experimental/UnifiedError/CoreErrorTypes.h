// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UnifiedError.h"

UE_DECLARE_ERROR_MODULE(	CORE_API, Core);
UE_DECLARE_ERROR_TWOPARAM(	CORE_API, ArgumentError,		1,	Core, NSLOCTEXT("Core", "ArgumentError", "Invalid argument '{ArgumentName}', reason '{ArgumentError}'"), FString, ArgumentName, TEXT(""), FString, ArgumentError, TEXT(""));
UE_DECLARE_ERROR(			CORE_API, CancellationError,	2,	Core, NSLOCTEXT("Core", "CancellationError", "The operation was cancelled"));

namespace UE::UnifiedError
{

CORE_API bool IsCancellationError(const FError& Error);

} // namespace UE::UnifiedError

