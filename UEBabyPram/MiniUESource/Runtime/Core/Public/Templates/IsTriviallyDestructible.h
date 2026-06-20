// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include <type_traits>

/**
 * Traits class which tests if a type has a trivial destructor.
 */
template <typename T>
struct UE_DEPRECATED(5.5, "TIsTriviallyDestructible has been deprecated, please use std::is_trivially_destructible_v instead.") TIsTriviallyDestructible
{
	enum { Value = std::is_trivially_destructible_v<T> };
};
