// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>

namespace UE
{

/**
 * Concept which describes convertibility from one type to another.
 *
 * We use this instead of std::convertible_to because <concepts> isn't a well supported header yet.
 */
template <typename From, typename To>
concept CConvertibleTo = std::is_convertible_v<From, To> && requires { static_cast<To>(std::declval<From>()); };

} // UE
