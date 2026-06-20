// Copyright Epic Games, Inc. All Rights Reserved. ** 

#pragma once

#include "Traits/IsContiguousContainer.h"
#include <type_traits>

namespace UE
{
	/**
	 * Concept which describes a contiguous range of elements.
	 */
	template <typename T>
	concept CContiguousRange =
		!std::is_reference_v<T> && // TIsContiguousContainer wrongly identifies references to containers as containers
		TIsContiguousContainer_V<T>;
}
