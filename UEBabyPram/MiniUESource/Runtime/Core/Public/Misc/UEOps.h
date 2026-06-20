// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Concepts/SameAs.h"
#include "HAL/Platform.h"

namespace UE::Core::Private
{
	// This only exists to bind to NULL, which is not perfectly-forwardable but which is commonly used in comparison operators.
	// The type is unique so that it can't be confused with a type that will actually be used by real user code, and it used to
	// create a pointer-to-member type which is highly unlikely to be deduced.
	struct FIncomplete;

	template <typename LhsType, typename RhsType>
	concept CWithUEOpEquals = requires(const LhsType & Lhs, const RhsType & Rhs)
	{
		{ Lhs.UEOpEquals(Rhs) } -> UE::CSameAs<bool>;
	};

	template <typename LhsType, typename RhsType>
	concept CWithUEOpLessThan = requires(const LhsType & Lhs, const RhsType & Rhs)
	{
		{ Lhs.UEOpLessThan(Rhs) } -> UE::CSameAs<bool>;
	};

	template <typename LhsType, typename RhsType>
	concept CWithUEOpGreaterThan = requires(const LhsType & Lhs, const RhsType & Rhs)
	{
		{ Lhs.UEOpGreaterThan(Rhs) } -> UE::CSameAs<bool>;
	};
}

/**
 * This file defines global comparison operators for all types containing certain named member functions.
 * These ensure that the operators and their variants are defined correctly based on a single customization
 * point and should result in faster overload resolution.
 *
 * bool FMyType::UEOpEquals(const FMyType&) const;
 * bool FMyType::UEOpEquals(const OtherType&) const;
 *   Should be defined to opt into operator== and operator!=.
 *
 * bool FMyType::UEOpLessThan(const FMyType&) const;
 * bool FMyType::UEOpLessThan(const OtherType&) const;
 * bool FMyType::UEOpGreaterThan(const OtherType&) const;
 *   Should be defined to opt into operator<, operator<=, operator> and operator>=.
 *
 * bool FMyType::UEOpGreaterThan(const FMyType&) const;
 *   Will never be called if FMyType::UEOpLessThan is defined, so is redundant.
 */

template <typename LhsType, typename RhsType>
	requires UE::Core::Private::CWithUEOpEquals<LhsType, RhsType> || UE::Core::Private::CWithUEOpEquals<RhsType, LhsType>
[[nodiscard]] UE_REWRITE constexpr bool operator==(const LhsType& Lhs, const RhsType& Rhs)
{
	if constexpr (UE::Core::Private::CWithUEOpEquals<LhsType, RhsType>)
	{
		return Lhs.UEOpEquals(Rhs);
	}
	else
	{
		return Rhs.UEOpEquals(Lhs);
	}
}

// This overload exists to support A == NULL, which is common in comparison operators, but NULL is not perfectly-forwardable.
template <typename LhsType>
	requires UE::Core::Private::CWithUEOpEquals<LhsType, decltype(nullptr)>
[[nodiscard]] UE_REWRITE constexpr bool operator==(const LhsType& Lhs, int UE::Core::Private::FIncomplete::* Rhs)
{
	return Lhs.UEOpEquals(nullptr);
}

template <typename LhsType, typename RhsType>
	requires UE::Core::Private::CWithUEOpLessThan<LhsType, RhsType> || UE::Core::Private::CWithUEOpGreaterThan<RhsType, LhsType>
[[nodiscard]] UE_REWRITE constexpr bool operator<(const LhsType& Lhs, const RhsType& Rhs)
{
	if constexpr (UE::Core::Private::CWithUEOpLessThan<LhsType, RhsType>)
	{
		return Lhs.UEOpLessThan(Rhs);
	}
	else
	{
		return Rhs.UEOpGreaterThan(Lhs);
	}
}

template <typename LhsType, typename RhsType>
	requires UE::Core::Private::CWithUEOpLessThan<RhsType, LhsType> || UE::Core::Private::CWithUEOpGreaterThan<LhsType, RhsType>
[[nodiscard]] UE_REWRITE constexpr bool operator>(const LhsType& Lhs, const RhsType& Rhs)
{
	if constexpr (UE::Core::Private::CWithUEOpLessThan<RhsType, LhsType>)
	{
		return Rhs.UEOpLessThan(Lhs);
	}
	else
	{
		return Lhs.UEOpGreaterThan(Rhs);
	}
}

template <typename LhsType, typename RhsType>
	requires UE::Core::Private::CWithUEOpLessThan<LhsType, RhsType> || UE::Core::Private::CWithUEOpGreaterThan<RhsType, LhsType>
[[nodiscard]] UE_REWRITE constexpr bool operator>=(const LhsType& Lhs, const RhsType& Rhs)
{
	if constexpr (UE::Core::Private::CWithUEOpLessThan<LhsType, RhsType>)
	{
		return !Lhs.UEOpLessThan(Rhs);
	}
	else
	{
		return !Rhs.UEOpGreaterThan(Lhs);
	}
}

template <typename LhsType, typename RhsType>
	requires UE::Core::Private::CWithUEOpLessThan<RhsType, LhsType> || UE::Core::Private::CWithUEOpGreaterThan<LhsType, RhsType>
[[nodiscard]] UE_REWRITE constexpr bool operator<=(const LhsType& Lhs, const RhsType& Rhs)
{
	if constexpr (UE::Core::Private::CWithUEOpLessThan<RhsType, LhsType>)
	{
		return !Rhs.UEOpLessThan(Lhs);
	}
	else
	{
		return !Lhs.UEOpGreaterThan(Rhs);
	}
}

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_7
#include "Templates/Requires.h"
#endif
