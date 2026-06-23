// Copyright Epic Games, Inc. All Rights Reserved.

#include "Experimental/UnifiedError/CoreErrorTypes.h"

UE_DEFINE_ERROR_MODULE(Core);
UE_DEFINE_ERROR(ArgumentError, Core);
UE_DEFINE_ERROR(CancellationError, Core);

namespace UE::UnifiedError
{

bool IsCancellationError(const FError& Error)
{
	return Core::CancellationError::OfType(Error);
}

} // namespace UE::UnifiedError

