// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DevObjectVersion.h"
#include "Containers/Map.h"

// Custom serialization version for changes made in the //Fortnite/Release-XX.XX stream
struct FFortniteReleaseBranchCustomObjectVersion
{
	enum Type
	{	
		#define DEFINE_FORTNITE_RELEASE_VERSIONS
		#include "UObject/FortniteReleaseBranchCustomObjectVersions.inl"
		#undef DEFINE_FORTNITE_RELEASE_VERSIONS

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	CORE_API const static FGuid GUID;

	static CORE_API TMap<FGuid, FGuid> GetSystemGuids();

private:
	FFortniteReleaseBranchCustomObjectVersion() {}
};

#define CHECK_FORTNITE_RELEASE_VERSIONS
#include "UObject/FortniteReleaseBranchCustomObjectVersions.inl"
#undef CHECK_FORTNITE_RELEASE_VERSIONS