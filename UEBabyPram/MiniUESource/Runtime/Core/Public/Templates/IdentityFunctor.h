// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

/**
 * A functor which returns whatever is passed to it.  Mainly used for generic composition.
 */
struct FIdentityFunctor
{
	template <typename T>
	UE_FORCEINLINE_HINT constexpr T&& operator()(T&& Val) const
	{
		return (T&&)Val;
	}
};
