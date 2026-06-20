// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DevObjectVersion.h"
#include "Containers/Map.h"

// Custom serialization version for changes made in the //Fortnite/Dev-FN-Sxx stream
struct FFortniteSeasonBranchObjectVersion
{
	enum Type
	{
		#define DEFINE_FORTNITE_SEASON_VERSIONS
		#include "UObject/FortniteSeasonBranchObjectVersions.inl"
		#undef DEFINE_FORTNITE_SEASON_VERSIONS

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	CORE_API const static FGuid GUID;

	static CORE_API TMap<FGuid, FGuid> GetSystemGuids();

private:
	FFortniteSeasonBranchObjectVersion() {}
};

#define CHECK_FORTNITE_SEASON_VERSIONS
#include "UObject/FortniteSeasonBranchObjectVersions.inl"
#undef CHECK_FORTNITE_SEASON_VERSIONS
