// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DevObjectVersion.h"
#include "Containers/Map.h"

// System Guids for changes made in the Dev-ParallelRendering stream
struct FParallelRenderingObjectVersion
{
	static CORE_API TMap<FGuid, FGuid> GetSystemGuids();

private:
	FParallelRenderingObjectVersion() {}
};
