// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "IO/IoDispatcher.h"
#include "IO/IoChunkId.h"

// This header is used for IoDispatcher functionality we don't want to expose outside of the engine

class FIoDispatcherInternal final
{
public:
	static bool HasPackageData()
	{
		// Checking for the global script objects chunk is currently the best means 
		// to know if the IoDispatcher will be loading packaged data
		const FIoDispatcher& Dispatcher = FIoDispatcher::Get();
		static const bool HasScriptObjectsChunk = Dispatcher.DoesChunkExist(CreateIoChunkId(0, 0, EIoChunkType::ScriptObjects));
		return HasScriptObjectsChunk;
	}
};