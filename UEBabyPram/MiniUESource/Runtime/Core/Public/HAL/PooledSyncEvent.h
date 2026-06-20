// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlatformProcess.h"

// An RAII wrapper around FPlatformProcess::GetSynchEventFromPool/ReturnSynchEventToPool.
struct FPooledSyncEvent
{
	explicit FPooledSyncEvent(bool bIsManualReset = false)
	{
		Event = FPlatformProcess::GetSynchEventFromPool(bIsManualReset);
	}
	~FPooledSyncEvent()
	{
		*this = nullptr;
	}

	// Non-copyable
	FPooledSyncEvent(FPooledSyncEvent&&) = delete;
	FPooledSyncEvent(const FPooledSyncEvent&) = delete;
	FPooledSyncEvent& operator=(FPooledSyncEvent&&) = delete;
	FPooledSyncEvent& operator=(const FPooledSyncEvent&) = delete;

	explicit operator bool() const
	{
		return !!this->Event;
	}

	FEvent* operator->() const
	{
		return this->Event;
	}

	FPooledSyncEvent& operator=(TYPE_OF_NULLPTR)
	{
		if (Event)
		{
			FPlatformProcess::ReturnSynchEventToPool(Event);
			Event = nullptr;
		}
		return *this;
	}

	FEvent* Event;
};
