// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/UnrealString.h"
#include "Misc/AES.h"
#include "Misc/Guid.h"

struct FNamedAESKey
{
	FString Name;
	FGuid Guid;
	FAES::FAESKey Key;

	bool IsValid() const
	{
		return Key.IsValid();
	}
};
