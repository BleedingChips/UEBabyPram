// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>

namespace UE
{
	/**
	 * Concept which describes an arithmetic type.
	 */
	template <typename T>
	concept CArithmetic = std::is_arithmetic_v<T>;
}
