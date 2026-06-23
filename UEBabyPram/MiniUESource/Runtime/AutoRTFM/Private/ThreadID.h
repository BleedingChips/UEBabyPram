// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if (defined(__AUTORTFM) && __AUTORTFM)

#include "AutoRTFM.h"

#include <thread>

namespace AutoRTFM
{

// A unique identifier for a thread of execution.
// Constructed as FThreadID::Invalid.
struct FThreadID 
{
	// An invalid thread identifier.
	UE_AUTORTFM_API static const FThreadID Invalid;

	// Returns the currently executing thread's unique identifier.
	UE_AUTORTFM_API
	static FThreadID GetCurrent();
	
	// Equality operator
	inline bool operator == (const FThreadID& Other) const
	{
		return Value == Other.Value;
	}

	// Inequality operator
	inline bool operator != (const FThreadID& Other) const
	{
		return Value != Other.Value;
	}

	std::thread::id Value;
};

}

#endif // (defined(__AUTORTFM) && __AUTORTFM)
