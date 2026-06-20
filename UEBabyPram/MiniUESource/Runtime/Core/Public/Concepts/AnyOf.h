// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>

template <typename T, typename... Options>
constexpr bool CAnyOf = (std::is_same_v<T, Options> || ...);
