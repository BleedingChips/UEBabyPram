// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VersePath.h: Forward declarations of VersePath-related types
=============================================================================*/

#pragma once

#include "CoreTypes.h"
#include "Containers/StringFwd.h"

class FArchive;
class FString;

namespace UE::Core
{
	class FVersePath;

	[[nodiscard]] bool operator==(const FVersePath& Lhs, const FVersePath& Rhs);
	[[nodiscard]] bool operator!=(const FVersePath& Lhs, const FVersePath& Rhs);

	FArchive& operator<<(FArchive& Ar, FVersePath& VersePath);

	[[nodiscard]] uint32 GetTypeHash(const UE::Core::FVersePath& VersePath);

	UE_DEPRECATED(5.5, "Use MakeValidVerseIdentifier instead")
	[[nodiscard]] CORE_API FString MangleGuidToVerseIdent(const FString& Guid);

	[[nodiscard]] CORE_API FString MakeValidVerseIdentifier(FStringView Str);
}
