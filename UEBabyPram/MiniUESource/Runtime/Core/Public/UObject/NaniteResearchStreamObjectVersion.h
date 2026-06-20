// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "Misc/Guid.h"

struct FNaniteResearchStreamObjectVersion
{
	enum Type
	{
		// Before any version changes were made
		BeforeCustomVersionWasAdded = 0,

		// Various global shader values converted to LWC types
		LWCTypesInShaders,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	CORE_API const static FGuid GUID;

	static CORE_API TMap<FGuid, FGuid> GetSystemGuids();

	FNaniteResearchStreamObjectVersion() = delete;
};
