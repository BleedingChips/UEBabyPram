// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DevObjectVersion.h"
#include "Containers/Map.h"

// Custom serialization version for changes made in the //Fortnite/Main stream
struct FFortniteMainBranchObjectVersion
{
	enum Type
	{
		#define DEFINE_FORTNITE_MAIN_VERSIONS
		#include "UObject/FortniteMainBranchObjectVersions.inl"
		#undef DEFINE_FORTNITE_MAIN_VERSIONS

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	CORE_API const static FGuid GUID;

	static CORE_API TMap<FGuid, FGuid> GetSystemGuids();

private:
	FFortniteMainBranchObjectVersion() {}
};

#define CHECK_FORTNITE_MAIN_VERSIONS
#include "UObject/FortniteMainBranchObjectVersions.inl"
#undef CHECK_FORTNITE_MAIN_VERSIONS
