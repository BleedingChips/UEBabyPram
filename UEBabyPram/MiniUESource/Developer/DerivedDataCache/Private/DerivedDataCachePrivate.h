// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

class FCbWriter;
class FDerivedDataCacheInterface;

template <typename FuncType> class TUniqueFunction;

namespace UE::DerivedData { class FCacheRecord; }
namespace UE::DerivedData { class ICache; }
namespace UE::DerivedData { class IRequestOwner; }
namespace UE::DerivedData { struct FCacheGetChunkRequest; }
namespace UE::DerivedData { struct FCacheGetRequest; }
namespace UE::DerivedData { struct FCacheGetValueRequest; }

namespace UE::DerivedData::Private
{

// Implemented in DerivedDataBackends.cpp
int32 AddToAsyncTaskCounter(int32 Addend); // returns the previous value

// Implemented in DerivedDataCache.cpp
ICache* CreateCache(FDerivedDataCacheInterface** OutLegacyCache);
void LaunchTaskInCacheThreadPool(IRequestOwner& Owner, TUniqueFunction<void ()>&& TaskBody);
void SaveToCompactBinary(FCbWriter& Writer, const FCacheGetRequest& Request);
void SaveToCompactBinary(FCbWriter& Writer, const FCacheGetValueRequest& Request);
void SaveToCompactBinary(FCbWriter& Writer, const FCacheGetChunkRequest& Request);

// Implemented in DerivedDataCacheRecord.cpp
uint64 GetCacheRecordCompressedSize(const FCacheRecord& Record);
uint64 GetCacheRecordTotalRawSize(const FCacheRecord& Record);
uint64 GetCacheRecordRawSize(const FCacheRecord& Record);

} // UE::DerivedData::Private
