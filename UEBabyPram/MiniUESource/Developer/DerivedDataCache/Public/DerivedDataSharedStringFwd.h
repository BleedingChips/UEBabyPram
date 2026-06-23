// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"

UE_DEPRECATED_HEADER(5.5, "Include Containers/StringFwd.h.")

namespace UE::DerivedData
{

template <typename CharType> using TSharedString = UE::TSharedString<CharType>;

using FSharedString UE_DEPRECATED(5.5, "This moved to Core. Use FSharedString from the UE namespace.") = UE::FSharedString;
using FAnsiSharedString UE_DEPRECATED(5.5, "This moved to Core. Use FAnsiSharedString from the UE namespace.") = UE::FAnsiSharedString;
using FWideSharedString UE_DEPRECATED(5.5, "This moved to Core. Use FWideSharedString from the UE namespace.") = UE::FWideSharedString;
using FUtf8SharedString UE_DEPRECATED(5.5, "This moved to Core. Use FUtf8SharedString from the UE namespace.") = UE::FUtf8SharedString;

} // UE::DerivedData
