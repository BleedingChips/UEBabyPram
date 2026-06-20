// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Traits/IsCharType.h"

namespace UE
{
	/**
	 * Concept which describes a character encoding type.
	 */
	template <typename T>
	concept CCharType = TIsCharType_V<T>;
}
