// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Experimental/UnifiedError/UnifiedError.h"


class FIoStatus;

UE_DECLARE_ERROR_MODULE(CORE_API, IoStore);


namespace UE::UnifiedError::IoStore
{
	CORE_API UE::UnifiedError::FError ConvertError(const FIoStatus& Status);
}