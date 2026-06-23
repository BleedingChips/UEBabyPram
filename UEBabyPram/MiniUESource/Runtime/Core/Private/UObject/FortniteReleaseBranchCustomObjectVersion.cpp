// Copyright Epic Games, Inc. All Rights Reserved.
#include "UObject/FortniteReleaseBranchCustomObjectVersion.h"

TMap<FGuid, FGuid> FFortniteReleaseBranchCustomObjectVersion::GetSystemGuids()
{
	TMap<FGuid, FGuid> SystemGuids;
	const FDevSystemGuids& DevGuids = FDevSystemGuids::Get();

	SystemGuids.Add(DevGuids.Niagara_LatestScriptCompileVersion, FGuid("213B319D239D438C82A946BB164152DE"));
	SystemGuids.Add(DevGuids.SkeletalMeshDerivedDataVersion, FGuid("C96DEBAD45E3443D804017D4C29F7C4B"));
	SystemGuids.Add(DevGuids.MaterialTranslationDDCVersion, FGuid("3CD389BC871B4FFF8351228666EAE54F"));
	SystemGuids.Add(DevGuids.GLOBALSHADERMAP_DERIVEDDATA_VER, FGuid("43D841E68BB14DF4A45F6D09B35501F7"));
	SystemGuids.Add(DevGuids.MATERIALSHADERMAP_DERIVEDDATA_VER, FGuid("D52C1D148C294A6A8C6CD75505081A31"));
	SystemGuids.Add(DevGuids.STATICMESH_DERIVEDDATA_VER, FGuid("0D7C30DE6E5F489A831CEBF1346725C4"));
	SystemGuids.Add(DevGuids.NANITE_DERIVEDDATA_VER, FGuid("B08A8584408545A88E5ED193054085E3"));
	return SystemGuids;
}