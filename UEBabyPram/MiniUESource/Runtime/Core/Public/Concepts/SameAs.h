// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>

namespace UE
{

/**
 * Concept which is satisfied if and only if T and U are the same type.
 *
 * We use this instead of std::same_as because <concepts> isn't a well supported header yet.
 */
template <typename T, typename U>
concept CSameAs = std::is_same_v<T, U> && std::is_same_v<U, T>;

} // UE
