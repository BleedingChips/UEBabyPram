// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericPlatform/GenericPlatformAffinity.h"

class FWindowsPlatformAffinity : public FGenericPlatformAffinity
{
public:
	static EThreadPriority GetRenderingThreadPriority()
	{
		return TPri_AboveNormal;
	}

	static EThreadPriority GetRHIThreadPriority()
	{
		return TPri_AboveNormal;
	}

	static EThreadPriority GetGameThreadPriority()
	{
		return TPri_AboveNormal;
	}
};

typedef FWindowsPlatformAffinity FPlatformAffinity;
