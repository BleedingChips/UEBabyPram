// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreFwd.h"
#include "HAL/Platform.h"

#define UE_API TRACEANALYSIS_API

class IFileHandle;
class FSocket;

namespace UE {
namespace Trace {

class IInDataStream
{
public:
	virtual ~IInDataStream() = default;

	/**
	 * Read bytes from the stream.
	 * @param Data Pointer to buffer to read into
	 * @param Size Maximum size that can be read in bytes
	 * @return Number of bytes read from the stream. Zero indicates end of stream and negative values indicate errors.
	 */
	virtual int32 Read(void* Data, uint32 Size) = 0;

	/**
	 * Close the stream. Reading from a closed stream
	 * is considered an error.
	 */
	virtual void Close() {}

	/**
	 * Query if the stream is ready to read. Some streams may need to
	 * establish the data stream before reading can begin. A stream may not
	 * block indefinitely.
	 *
	 * @return if the stream is ready to be read from
	 */
	virtual bool WaitUntilReady() { return true; }
};

} // namespace Trace
} // namespace UE

#undef UE_API
