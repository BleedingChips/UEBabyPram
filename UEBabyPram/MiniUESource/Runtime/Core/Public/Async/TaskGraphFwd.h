// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	TaskGraphFwd.h: TaskGraph library
=============================================================================*/

#pragma once

#include "Templates/RefCounting.h"

class FBaseGraphTask;

/** Generic task used by the task graph system */
using FGraphEvent = FBaseGraphTask;

/** Convenience typedef for a reference counted pointer to a graph event */
using FGraphEventRef = TRefCountPtr<FBaseGraphTask>;
