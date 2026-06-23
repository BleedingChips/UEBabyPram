// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if (defined(__AUTORTFM) && __AUTORTFM)

namespace AutoRTFM
{

// Enumerator used to control validity assertions for containers
// (bounds checking, etc).
enum class EContainerValidation
{
	Enabled,
	Disabled
};

}

#endif // (defined(__AUTORTFM) && __AUTORTFM)
