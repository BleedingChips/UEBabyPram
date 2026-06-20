// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"

namespace UE::String
{

/**
 * Checks if the passed in string contains only digits, at most one dot, and optionally a +/- sign at the start.
 *
 * @param View The string to check.
 *
 * @return True if the string looks like a valid floating point number.
 */
CORE_API bool IsNumeric(FWideStringView View);
CORE_API bool IsNumeric(FUtf8StringView View);

/**
 * Checks if the passed in string contains only digits.
 *
 * @param View The string to check.
 *
 * @return True if the string only contains numeric characters.
 */
CORE_API bool IsNumericOnlyDigits(FWideStringView View);
CORE_API bool IsNumericOnlyDigits(FUtf8StringView View);

}
