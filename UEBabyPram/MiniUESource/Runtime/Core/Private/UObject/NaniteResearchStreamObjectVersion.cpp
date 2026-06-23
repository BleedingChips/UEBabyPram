// Copyright Epic Games, Inc. All Rights Reserved.
#include "UObject/NaniteResearchStreamObjectVersion.h"
#include "UObject/DevObjectVersion.h"
#include "Containers/Map.h"
#include "Misc/Guid.h"


TMap<FGuid, FGuid> FNaniteResearchStreamObjectVersion::GetSystemGuids()
{
	TMap<FGuid, FGuid> SystemGuids;
	const FDevSystemGuids& DevGuids = FDevSystemGuids::Get();

	SystemGuids.Add(DevGuids.NANITE_DERIVEDDATA_VER, FGuid("753A91EF-2107-4316-969A-1AA37578D5A8"));
	SystemGuids.Add(DevGuids.STATICMESH_DERIVEDDATA_VER, FGuid("E5FAD907-D19B-4754-8325-9C48E405CCCC"));
	SystemGuids.Add(DevGuids.SkeletalMeshDerivedDataVersion, FGuid("9B0FD28E-09DA-4E72-BFF8-7019EEE7270B"));

	return SystemGuids;
}
