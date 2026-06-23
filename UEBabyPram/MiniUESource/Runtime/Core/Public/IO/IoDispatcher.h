// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/Future.h"
#include "Async/TaskGraphInterfaces.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/Map.h"
#include "Containers/StringFwd.h"
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "CoreTypes.h"
#include "Delegates/Delegate.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformAtomics.h"
#include "HAL/PlatformFile.h"
#include "HAL/UnrealMemory.h"
#include "IO/IoBuffer.h"
#include "IO/IoChunkId.h"
#include "IO/IoContainerId.h"
#include "IO/IoDispatcherPriority.h"
#include "IO/IoHash.h"
#include "IO/IoStatus.h"
#include "Logging/LogMacros.h"
#include "Math/NumericLimits.h"
#include "Memory/MemoryFwd.h"
#include "Memory/MemoryView.h"
#include "Misc/AES.h"
#include "Misc/AssertionMacros.h"
#include "Misc/Build.h"
#include "Misc/ByteSwap.h"
#include "Misc/EnumClassFlags.h"
#include "Misc/Guid.h"
#include "Misc/IEngineCrypto.h"
#include "Misc/SecureHash.h"
#include "Serialization/Archive.h"
#include "Serialization/FileRegions.h"
#include "String/BytesToHex.h"
#include "Tasks/Task.h"
#include "Templates/Function.h"
#include "Templates/RefCounting.h"
#include "Templates/SharedPointer.h"
#include "Templates/TypeCompatibleBytes.h"
#include "Templates/UniquePtr.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/NameTypes.h"
#include "UObject/UnrealNames.h"

class FEvent;
class FIoBatchImpl;
class FIoDirectoryIndexReaderImpl;
class FIoDispatcher;
class FIoDispatcherImpl;
class FIoRequest;
class FIoRequestImpl;
class FIoStoreEnvironment;
class FIoStoreReader;
class FIoStoreReaderImpl;
class FPackageId;
class IMappedFileHandle;
class IMappedFileRegion;
struct FFileRegion;
struct IIoDispatcherBackend;
struct FIoOffsetAndLength;
template <typename CharType> class TStringBuilderBase;
template <typename OptionalType> struct TOptional;

CORE_API DECLARE_LOG_CATEGORY_EXTERN(LogIoDispatcher, Log, All);

//////////////////////////////////////////////////////////////////////////

/** Helper used to manage creation of I/O store file handles etc
  */
class FIoStoreEnvironment
{
public:
	CORE_API FIoStoreEnvironment();
	CORE_API ~FIoStoreEnvironment();

	CORE_API void InitializeFileEnvironment(FStringView InPath, int32 InOrder = 0);

	const FString& GetPath() const { return Path; }
	int32 GetOrder() const { return Order; }

private:
	FString			Path;
	int32			Order = 0;
};

class
UE_DEPRECATED(5.5, "FIoChunkHash is deprecated. Use FIoHash instead.")
FIoChunkHash
{
public:
	friend uint32 GetTypeHash(const FIoChunkHash& InChunkHash)
	{
		uint32 Result = 5381;
		for (int i = 0; i < sizeof Hash; ++i)
		{
			Result = Result * 33 + InChunkHash.Hash[i];
		}
		return Result;
	}

	friend FArchive& operator<<(FArchive& Ar, FIoChunkHash& ChunkHash)
	{
		Ar.Serialize(&ChunkHash.Hash, sizeof Hash);
		return Ar;
	}

	inline bool operator ==(const FIoChunkHash& Rhs) const
	{
		return 0 == FMemory::Memcmp(Hash, Rhs.Hash, sizeof Hash);
	}

	inline bool operator !=(const FIoChunkHash& Rhs) const
	{
		return !(*this == Rhs);
	}

	inline FString ToString() const
	{
		return BytesToHex(Hash, 20);
	}

	FIoHash ToIoHash() const
	{
		FIoHash IoHash;
		FMemory::Memcpy(IoHash.GetBytes(), Hash, sizeof(FIoHash));
		return IoHash;
	}

	static FIoChunkHash CreateFromIoHash(const FIoHash& IoHash)
	{
		FIoChunkHash Result;
		FMemory::Memcpy(Result.Hash, &IoHash, sizeof IoHash);
		FMemory::Memset(Result.Hash + 20, 0, 12);
		return Result;
	}

	static FIoChunkHash HashBuffer(const void* Data, uint64 DataSize)
	{
		return CreateFromIoHash(FIoHash::HashBuffer(Data, DataSize));
	}

private:
	uint8	Hash[32];
};

//////////////////////////////////////////////////////////////////////////

enum class EIoReadOptionsFlags : uint32
{
	None = 0,
	/**
	 * Use this flag to inform the decompressor that the memory is uncached or write-combined and therefore the usage of staging might be needed if reading directly from the original memory
	 */
	HardwareTargetBuffer = 1 << 0,
};
ENUM_CLASS_FLAGS(EIoReadOptionsFlags);

class FIoReadOptions
{
public:
	FIoReadOptions() = default;

	FIoReadOptions(uint64 InOffset, uint64 InSize)
		: RequestedOffset(InOffset)
		, RequestedSize(InSize)
	{ }
	
	FIoReadOptions(uint64 InOffset, uint64 InSize, void* InTargetVa)
		: RequestedOffset(InOffset)
		, RequestedSize(InSize)
		, TargetVa(InTargetVa)
	{ }

	FIoReadOptions(uint64 InOffset, uint64 InSize, void* InTargetVa, EIoReadOptionsFlags InFlags)
		: RequestedOffset(InOffset)
		, RequestedSize(InSize)
		, TargetVa(InTargetVa)
		, Flags(InFlags)
	{ }

	~FIoReadOptions() = default;

	void SetRange(uint64 Offset, uint64 Size)
	{
		RequestedOffset = Offset;
		RequestedSize	= Size;
	}

	void SetTargetVa(void* InTargetVa)
	{
		TargetVa = InTargetVa;
	}

	void SetFlags(EIoReadOptionsFlags InValue)
	{
		Flags = InValue;
	}

	uint64 GetOffset() const
	{
		return RequestedOffset;
	}

	uint64 GetSize() const
	{
		return RequestedSize;
	}

	void* GetTargetVa() const
	{
		return TargetVa;
	}

	EIoReadOptionsFlags GetFlags() const
	{
		return Flags;
	}

private:
	uint64	RequestedOffset = 0;
	uint64	RequestedSize = ~uint64(0);
	void* TargetVa = nullptr;
	EIoReadOptionsFlags Flags = EIoReadOptionsFlags::None;
};

//////////////////////////////////////////////////////////////////////////

/**
  */
class FIoRequest final
{
public:
	FIoRequest() = default;
	CORE_API ~FIoRequest();

	CORE_API FIoRequest(const FIoRequest& Other);
	CORE_API FIoRequest(FIoRequest&& Other);
	CORE_API FIoRequest& operator=(const FIoRequest& Other);
	CORE_API FIoRequest& operator=(FIoRequest&& Other);
	CORE_API FIoStatus						Status() const;
	CORE_API const FIoBuffer*				GetResult() const;
	CORE_API const FIoBuffer&				GetResultOrDie() const;
	CORE_API void							Cancel();
	CORE_API void							UpdatePriority(int32 NewPriority);
	CORE_API void							Release();

private:
	FIoRequestImpl* Impl = nullptr;

	explicit FIoRequest(FIoRequestImpl* InImpl);

	friend class FIoDispatcher;
	friend class FIoDispatcherImpl;
	friend class FIoBatch;
};

using FIoReadCallback = TFunction<void(TIoStatusOr<FIoBuffer>)>;

inline int32 ConvertToIoDispatcherPriority(EAsyncIOPriorityAndFlags AIOP)
{
	int32 AIOPriorityToIoDispatcherPriorityMap[] = {
		IoDispatcherPriority_Min,
		IoDispatcherPriority_Low,
		IoDispatcherPriority_Medium - 1,
		IoDispatcherPriority_Medium,
		IoDispatcherPriority_High,
		IoDispatcherPriority_Max
	};
	static_assert(AIOP_NUM == UE_ARRAY_COUNT(AIOPriorityToIoDispatcherPriorityMap), "IoDispatcher and AIO priorities mismatch");
	return AIOPriorityToIoDispatcherPriorityMap[AIOP & AIOP_PRIORITY_MASK];
}

/** I/O batch

	This is a primitive used to group I/O requests for synchronization
	purposes
  */
class FIoBatch final
{
	friend class FIoDispatcher;
	friend class FIoDispatcherImpl;
	friend class FIoRequestStats;

public:
	CORE_API FIoBatch();
	CORE_API FIoBatch(FIoBatch&& Other);
	CORE_API ~FIoBatch();
	CORE_API FIoBatch& operator=(FIoBatch&& Other);
	CORE_API FIoRequest Read(const FIoChunkId& Chunk, FIoReadOptions Options, int32 Priority);
	CORE_API FIoRequest ReadWithCallback(const FIoChunkId& ChunkId, const FIoReadOptions& Options, int32 Priority, FIoReadCallback&& Callback);

	CORE_API void Issue();
	CORE_API void IssueWithCallback(TFunction<void()>&& Callback);
	CORE_API void IssueAndTriggerEvent(FEvent* Event);
	CORE_API void IssueAndDispatchSubsequents(FGraphEventRef Event);

private:
	FIoBatch(FIoDispatcherImpl& InDispatcher);
	FIoRequestImpl* ReadInternal(const FIoChunkId& ChunkId, const FIoReadOptions& Options, int32 Priority);

	FIoDispatcherImpl*	Dispatcher;
	FIoRequestImpl*		HeadRequest = nullptr;
	FIoRequestImpl*		TailRequest = nullptr;
};

/**
 * Mapped region.
 */
struct FIoMappedRegion
{
	IMappedFileHandle* MappedFileHandle = nullptr;
	IMappedFileRegion* MappedFileRegion = nullptr;
};

struct FIoDispatcherMountedContainer
{
	FIoStoreEnvironment Environment;
	FIoContainerId ContainerId;
};

struct FIoSignatureError
{
	FString ContainerName;
	int32 BlockIndex = INDEX_NONE;
	FSHAHash ExpectedHash;
	FSHAHash ActualHash;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FIoSignatureErrorDelegate, const FIoSignatureError&);

struct FIoSignatureErrorEvent
{
	FCriticalSection CriticalSection;
	FIoSignatureErrorDelegate SignatureErrorDelegate;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FIoSignatureErrorDelegate, const FIoSignatureError&);

DECLARE_MULTICAST_DELEGATE_OneParam(FIoContainerMountedDelegate, const FIoContainerId&);

/** I/O dispatcher
  */
class FIoDispatcher final
{
public:
	DECLARE_EVENT_OneParam(FIoDispatcher, FIoContainerMountedEvent, const FIoDispatcherMountedContainer&);
	DECLARE_EVENT_OneParam(FIoDispatcher, FIoContainerUnmountedEvent, const FIoDispatcherMountedContainer&);

	CORE_API						FIoDispatcher();
	CORE_API						~FIoDispatcher();

	CORE_API void					Mount(TSharedRef<IIoDispatcherBackend> Backend, int32 Priority = 0);

	CORE_API FIoBatch				NewBatch();

	CORE_API TIoStatusOr<FIoMappedRegion> OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options);

	// Polling methods
	CORE_API bool					DoesChunkExist(const FIoChunkId& ChunkId) const;
	CORE_API TIoStatusOr<uint64>	GetSizeForChunk(const FIoChunkId& ChunkId) const;
	CORE_API int64					GetTotalLoaded() const;


	// Events
	CORE_API FIoSignatureErrorDelegate& OnSignatureError();

	FIoDispatcher(const FIoDispatcher&) = default;
	FIoDispatcher& operator=(const FIoDispatcher&) = delete;

	static CORE_API bool IsInitialized();
	static CORE_API FIoStatus Initialize();
	static CORE_API void InitializePostSettings();
	static CORE_API void Shutdown();
	static CORE_API FIoDispatcher& Get();

private:
	CORE_API bool					DoesChunkExist(const FIoChunkId& ChunkId, const FIoOffsetAndLength& ChunkRange) const;
	CORE_API TIoStatusOr<uint64>	GetSizeForChunk(const FIoChunkId& ChunkId, const FIoOffsetAndLength& ChunkRange, uint64& OutAvailable) const;

	FIoDispatcherImpl* Impl = nullptr;

	friend class FIoRequest;
	friend class FIoBatch;
	friend class FIoQueue;
	friend class FBulkData;
};

//////////////////////////////////////////////////////////////////////////

class FIoDirectoryIndexHandle
{
	static constexpr uint32 InvalidHandle = ~uint32(0);
	static constexpr uint32 RootHandle = 0;

public:
	FIoDirectoryIndexHandle() = default;

	inline bool IsValid() const
	{
		return Handle != InvalidHandle;
	}

	inline bool operator<(FIoDirectoryIndexHandle Other) const
	{
		return Handle < Other.Handle;
	}

	inline bool operator==(FIoDirectoryIndexHandle Other) const
	{
		return Handle == Other.Handle;
	}

	inline friend uint32 GetTypeHash(FIoDirectoryIndexHandle InHandle)
	{
		return InHandle.Handle;
	}

	inline uint32 ToIndex() const
	{
		return Handle;
	}

	static inline FIoDirectoryIndexHandle FromIndex(uint32 Index)
	{
		return FIoDirectoryIndexHandle(Index);
	}

	static inline FIoDirectoryIndexHandle RootDirectory()
	{
		return FIoDirectoryIndexHandle(RootHandle);
	}

	static inline FIoDirectoryIndexHandle Invalid()
	{
		return FIoDirectoryIndexHandle(InvalidHandle);
	}

private:
	FIoDirectoryIndexHandle(uint32 InHandle)
		: Handle(InHandle) { }

	uint32 Handle = InvalidHandle;
};

using FDirectoryIndexVisitorFunction = TFunctionRef<bool(FStringView, const uint32)>;

class FIoDirectoryIndexReader
{
public:
	CORE_API FIoDirectoryIndexReader();
	CORE_API ~FIoDirectoryIndexReader();
	CORE_API FIoStatus Initialize(TConstArrayView<uint8> InBuffer, FAES::FAESKey InDecryptionKey);

	CORE_API const FString& GetMountPoint() const;
	CORE_API FIoDirectoryIndexHandle GetChildDirectory(FIoDirectoryIndexHandle Directory) const;
	CORE_API FIoDirectoryIndexHandle GetNextDirectory(FIoDirectoryIndexHandle Directory) const;
	CORE_API FIoDirectoryIndexHandle GetFile(FIoDirectoryIndexHandle Directory) const;
	CORE_API FIoDirectoryIndexHandle GetNextFile(FIoDirectoryIndexHandle File) const;
	CORE_API FStringView GetDirectoryName(FIoDirectoryIndexHandle Directory) const;
	CORE_API FStringView GetFileName(FIoDirectoryIndexHandle File) const;
	CORE_API uint32 GetFileData(FIoDirectoryIndexHandle File) const;

	CORE_API bool IterateDirectoryIndex(FIoDirectoryIndexHandle Directory, FStringView Path, FDirectoryIndexVisitorFunction Visit) const;

private:
	UE_NONCOPYABLE(FIoDirectoryIndexReader);

	FIoDirectoryIndexReaderImpl* Impl;
};

//////////////////////////////////////////////////////////////////////////

enum class EIoContainerFlags : uint8
{
	None,
	Compressed	= (1 << 0),
	Encrypted	= (1 << 1),
	Signed		= (1 << 2),
	Indexed		= (1 << 3),
	OnDemand	= (1 << 4),
};
ENUM_CLASS_FLAGS(EIoContainerFlags);

struct FIoContainerSettings
{
	FIoContainerId ContainerId;
	EIoContainerFlags ContainerFlags = EIoContainerFlags::None;
	FGuid EncryptionKeyGuid;
	FAES::FAESKey EncryptionKey;
	FRSAKeyHandle SigningKey;
	bool bGenerateDiffPatch = false;

	bool IsCompressed() const
	{
		return !!(ContainerFlags & EIoContainerFlags::Compressed);
	}

	bool IsEncrypted() const
	{
		return !!(ContainerFlags & EIoContainerFlags::Encrypted);
	}

	bool IsSigned() const
	{
		return !!(ContainerFlags & EIoContainerFlags::Signed);
	}

	bool IsIndexed() const
	{
		return !!(ContainerFlags & EIoContainerFlags::Indexed);
	}
	
	bool IsOnDemand() const
	{
		return !!(ContainerFlags & EIoContainerFlags::OnDemand);
	}
};

struct FIoStoreTocChunkInfo
{
	FIoChunkId Id;
	FIoHash ChunkHash;
	UE_DEPRECATED(5.5, "Hash of type FIoChunkHash is deprecated. Use ChunkHash of type FIoHash instead.")
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FIoChunkHash Hash;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	FString FileName;
	uint64 Offset;
	uint64 OffsetOnDisk;
	uint64 Size;
	uint64 CompressedSize;
	uint32 NumCompressedBlocks;
	int32 PartitionIndex;
	EIoChunkType ChunkType;
	bool bHasValidFileName;
	bool bForceUncompressed;
	bool bIsMemoryMapped;
	bool bIsCompressed;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS // Compilers can complain about deprecated members in compiler generated code
	FIoStoreTocChunkInfo() = default;
	FIoStoreTocChunkInfo(const FIoStoreTocChunkInfo&) = default;
	FIoStoreTocChunkInfo(FIoStoreTocChunkInfo&&) = default;
	FIoStoreTocChunkInfo& operator=(FIoStoreTocChunkInfo&) = default;
	FIoStoreTocChunkInfo& operator=(FIoStoreTocChunkInfo&&) = default;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
};

struct FIoStoreTocCompressedBlockInfo
{
	uint64 Offset;
	uint32 CompressedSize;
	uint32 UncompressedSize;
	uint8 CompressionMethodIndex;
};

struct FIoStoreCompressedBlockInfo
{
	/** Name of the method used to compress the block. */
	FName CompressionMethod;
	/** The size of relevant data in the block (i.e. what you pass to decompress). */
	uint32 CompressedSize;
	/** The size of the _block_ after decompression. This is not adjusted for any FIoReadOptions used. */
	uint32 UncompressedSize;
	/** The size of the data this block takes in IoBuffer (i.e. after padding for decryption). */
	uint32 AlignedSize;
	/** Where in IoBuffer this block starts. */
	uint64 OffsetInBuffer;
};

struct FIoStoreCompressedReadResult
{
	/** The buffer containing the chunk. */
	FIoBuffer IoBuffer;

	/** Info about the blocks that the chunk is split up into. */
	TArray<FIoStoreCompressedBlockInfo> Blocks;
	// There is where the data starts in IoBuffer (for when you pass in a data range via FIoReadOptions)
	uint64 UncompressedOffset = 0;
	// This is the total size requested via FIoReadOptions. Notably, if you requested a narrow range, you could
	// add up all the block uncompressed sizes and it would be larger than this.
	uint64 UncompressedSize = 0;
	// This is the total size of compressed data, which is less than IoBuffer size due to padding for decryption.
	uint64 TotalCompressedSize = 0;
};

class FIoStoreReader
{
public:
	CORE_API FIoStoreReader();
	CORE_API ~FIoStoreReader();

	[[nodiscard]] CORE_API FIoStatus Initialize(FStringView ContainerPath, const TMap<FGuid, FAES::FAESKey>& InDecryptionKeys);
	CORE_API FIoContainerId GetContainerId() const;
	CORE_API uint32 GetVersion() const;
	CORE_API EIoContainerFlags GetContainerFlags() const;
	CORE_API FGuid GetEncryptionKeyGuid() const;
	CORE_API int32 GetChunkCount() const;
	CORE_API FString GetContainerName() const; // The container name is the base filename of ContainerPath, e.g. "global".

	CORE_API void EnumerateChunks(TFunction<bool(FIoStoreTocChunkInfo&&)>&& Callback) const;
	CORE_API TIoStatusOr<FIoStoreTocChunkInfo> GetChunkInfo(const FIoChunkId& Chunk) const;
	CORE_API TIoStatusOr<FIoStoreTocChunkInfo> GetChunkInfo(const uint32 TocEntryIndex) const;

	// Reads the chunk off the disk, decrypting/decompressing as necessary.
	CORE_API TIoStatusOr<FIoBuffer> Read(const FIoChunkId& Chunk, const FIoReadOptions& Options) const;
	
	// As Read(), except returns a task that will contain the result after a .Wait/.BusyWait.
	CORE_API UE::Tasks::TTask<TIoStatusOr<FIoBuffer>> ReadAsync(const FIoChunkId& Chunk, const FIoReadOptions& Options) const;

	// Reads and decrypts if necessary the compressed blocks, but does _not_ decompress them. The totality of the data is stored
	// in FIoStoreCompressedReadResult::FIoBuffer as a contiguous buffer, however each block is padded during encryption, so
	// either use FIoStoreCompressedBlockInfo::AlignedSize to advance through the buffer, or use FIoStoreCompressedBlockInfo::OffsetInBuffer
	// directly.
	CORE_API TIoStatusOr<FIoStoreCompressedReadResult> ReadCompressed(const FIoChunkId& Chunk, const FIoReadOptions& Options, bool bDecrypt = true) const;

	CORE_API const FIoDirectoryIndexReader& GetDirectoryIndexReader() const;

	CORE_API void GetFilenamesByBlockIndex(const TArray<int32>& InBlockIndexList, TArray<FString>& OutFileList) const;
	CORE_API void GetFilenames(TArray<FString>& OutFileList) const;

	CORE_API uint32 GetCompressionBlockSize() const;
	CORE_API const TArray<FName>& GetCompressionMethods() const;
	CORE_API void EnumerateCompressedBlocks(TFunction<bool(const FIoStoreTocCompressedBlockInfo&)>&& Callback) const;
	CORE_API void EnumerateCompressedBlocksForChunk(const FIoChunkId& Chunk, TFunction<bool(const FIoStoreTocCompressedBlockInfo&)>&& Callback) const;

	// Returns the .ucas file path and all partition(s) ({containername}_s1.ucas, {containername}_s2.ucas)
	CORE_API void GetContainerFilePaths(TArray<FString>& OutPaths);

private:
	FIoStoreReaderImpl* Impl;
};
//////////////////////////////////////////////////////////////////////////
