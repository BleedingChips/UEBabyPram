// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VersePath.h: A type which holds a VersePath
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Misc/PathViews.h"
#include "VersePathFwd.h"

class UE::Core::FVersePath
{
	friend bool operator==(const FVersePath& Lhs, const FVersePath& Rhs);
	friend bool operator!=(const FVersePath& Lhs, const FVersePath& Rhs);

	friend FArchive& operator<<(FArchive& Ar, FVersePath& VersePath);

public:
	[[nodiscard]] FVersePath() = default;
	[[nodiscard]] FVersePath(FVersePath&&) = default;
	[[nodiscard]] FVersePath(const FVersePath&) = default;
	FVersePath& operator=(FVersePath&&) = default;
	FVersePath& operator=(const FVersePath&) = default;
	~FVersePath() = default;

	[[nodiscard]] const TCHAR* operator*() const
	{
		return *PathString;
	}

	[[nodiscard]] bool IsValid() const
	{
		return !PathString.IsEmpty();
	}

	[[nodiscard]] explicit operator bool() const
	{
		return !PathString.IsEmpty();
	}

	/**
	 * Lexicographically tests how this Verse path compares to the Other given Verse path
	 *
	 * @param Other The Verse path to test against
	 * @return 0 if equal, negative if less than, positive if greater than
	 */
	[[nodiscard]] int32 Compare(const FVersePath& Other) const
	{
		return PathString.Compare(Other.PathString, ESearchCase::CaseSensitive);
	}

	/**
	 * Tests whether this Verse path is a base Verse path of Other
	 * 
	 * Examples:
	 * "/domain1/path1", "/domain1/path1" -> true, ""
	 * "/domain1/path1", "/domain1/path1/leaf" -> true, "leaf"
	 * "/domain1/path1", "/domain1/path1/path2/leaf" -> true, "path2/leaf"
	 * "/domain1/path1", "/domain1/path2/leaf" -> false, ""
	 * "/domain1/path1", "/domain2/path1/leaf" -> false, ""
	 *
	 * @param Other The Verse path to test against
	 * @param OutLeafPath The path segment of Other that is relative to this Verse path, ommiting the leading '/'. Empty if the two Verse paths are equal
	 * @return true if Other is relative or equal to this Verse path
	 */
	[[nodiscard]] CORE_API bool IsBaseOf(const FVersePath& Other, FStringView* OutLeafPath = nullptr) const;

	[[nodiscard]] const FString& ToString() const &
	{
		return PathString;
	}

	[[nodiscard]] FString ToString() &&
	{
		return MoveTemp(PathString);
	}

	[[nodiscard]] FStringView AsStringView() const
	{
		return PathString;
	}

	[[nodiscard]] FText AsText() const &
	{
		return FText::AsCultureInvariant(AsStringView());
	}

	[[nodiscard]] FText AsText() &&
	{
		return FText::AsCultureInvariant(MoveTemp(PathString));
	}

	[[nodiscard]] FString GetVerseDomain() const
	{
		return FString(FPathViews::GetMountPointNameFromPath(PathString));
	}

	[[nodiscard]] FStringView GetVerseDomainAsStringView() const
	{
		return FPathViews::GetMountPointNameFromPath(PathString);
	}

	[[nodiscard]] FString GetPathExceptVerseDomain() const
	{
		FStringView VerseDomain = GetVerseDomainAsStringView();
		return PathString.RightChop(VerseDomain.Len()+1);
	}

	CORE_API static bool TryMake(FVersePath& OutPath, const FString& Path, FText* OutErrorMessage = nullptr);
	CORE_API static bool TryMake(FVersePath& OutPath, FString&& Path, FText* OutErrorMessage = nullptr);

	[[nodiscard]] CORE_API static bool IsValidFullPath(const TCHAR* String, FText* OutErrorMessage = nullptr);
	[[nodiscard]] CORE_API static bool IsValidFullPath(const TCHAR* String, int32 Len, FText* OutErrorMessage = nullptr);
	[[nodiscard]] CORE_API static bool IsValidDomain(const TCHAR* String, FText* OutErrorMessage = nullptr);
	[[nodiscard]] CORE_API static bool IsValidDomain(const TCHAR* String, int32 Len, FText* OutErrorMessage = nullptr);
	[[nodiscard]] CORE_API static bool IsValidSubpath(const TCHAR* String, FText* OutErrorMessage = nullptr);
	[[nodiscard]] CORE_API static bool IsValidSubpath(const TCHAR* String, int32 Len, FText* OutErrorMessage = nullptr);
	[[nodiscard]] CORE_API static bool IsValidIdent(const TCHAR* String, FText* OutErrorMessage = nullptr, const FText* IdentTermReplacement = nullptr);
	[[nodiscard]] CORE_API static bool IsValidIdent(const TCHAR* String, int32 Len, FText* OutErrorMessage = nullptr, const FText* IdentTermReplacement = nullptr);

private:
	FString PathString;
};

[[nodiscard]] UE_FORCEINLINE_HINT bool UE::Core::operator==(const FVersePath& Lhs, const FVersePath& Rhs)
{
	return Lhs.PathString.Equals(Rhs.PathString, ESearchCase::CaseSensitive);
}

[[nodiscard]] UE_FORCEINLINE_HINT bool UE::Core::operator!=(const FVersePath& Lhs, const FVersePath& Rhs)
{
	return !(Lhs == Rhs);
}

UE_FORCEINLINE_HINT FArchive& UE::Core::operator<<(FArchive& Ar, FVersePath& VersePath)
{
	return Ar << VersePath.PathString;
}

[[nodiscard]] UE_FORCEINLINE_HINT uint32 UE::Core::GetTypeHash(const FVersePath& VersePath)
{
	return FCrc::StrCrc32<TCHAR>(*VersePath);
}
