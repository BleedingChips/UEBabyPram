module;

#include <cassert>

export module UEBabyPramInsightTrace;

import std;
import Potato;
import UEBabyPramInsightDefine;

export namespace UEBabyPram::InsightParser
{
	// Field types
	enum AnsiString {};
	enum WideString {};

	// Reference to a definition event.
	template<typename IdType>
	struct TEventRef
	{
		using ReferenceType = IdType;

		TEventRef(IdType InId, uint32 InTypeId)
			: Id(InId)
			, RefTypeId(InTypeId)
		{
		}

		IdType Id;
		uint32 RefTypeId;

		uint64 GetHash() const
		{
			if constexpr (std::is_same_v<IdType, uint64>)
			{
				return (uint64(RefTypeId) << 32) ^ Id;
			}
			else
			{
				return (uint64(RefTypeId) << 32) | Id;
			}
		}

	private:
		TEventRef() = delete;
	};

	typedef TEventRef<uint8> FEventRef8;
	typedef TEventRef<uint16> FEventRef16;
	typedef TEventRef<uint32> FEventRef32;
	typedef TEventRef<uint64> FEventRef64;

	template<typename IdType>
	TEventRef<IdType> MakeEventRef(IdType InId, uint32 InTypeId)
	{
		return TEventRef<IdType>(InId, InTypeId);
	}

	enum class EMessageType : uint8
	{
		Reserved = 0,
		// Add to log
		Log,
		// For backwards compatibility
		Info = Log,
		// Display in console or similar
		Display,
		// Warnings to notify user
		WarningStart = 0x04,
		// Errors are critical to the user, but application
		// can continue to run.
		ErrorStart = 0x10,
		WriteError,
		ReadError,
		ConnectError,
		ListenError,
		EstablishError,
		FileOpenError,
		WriterError,
		CompressionError,
		// Fatal errors should cause application to stop
		FatalStart = 0x40,
		GenericFatal,
		OOMFatal,
	};

	struct FMessageEvent
	{
		/** Type of message */
		EMessageType		Type;
		/** Type of message stringified */
		const char* TypeStr;
		/** Clarifying message, may be null for some message types. Pointer only valide during callback. */
		const char* Description;
	};

	using OnMessageFunc = void(const FMessageEvent&);
	using OnConnectFunc = void(void);
	using OnUpdateFunc = void(void);
	using OnScopeBeginFunc = void(const ANSICHAR*);
	using OnScopeEndFunc = void(void);

	struct FInitializeDesc
	{
		uint32				TailSizeBytes = 4 << 20; // can be set to 0 to disable the tail buffer
		uint32				ThreadSleepTimeInMS = 0;
		uint32				BlockPoolMaxSize = UE_TRACE_BLOCK_POOL_MAXSIZE;
		bool				bUseWorkerThread = true;
		bool				bUseImportantCache = true;
		uint32				SessionGuid[4] = { 0,0,0,0 }; // leave as zero to generate random
		OnConnectFunc* OnConnectionFunc = nullptr;
		OnUpdateFunc* OnUpdateFunc = nullptr;
		OnScopeBeginFunc* OnScopeBeginFunc = nullptr;
		OnScopeEndFunc* OnScopeEndFunc = nullptr;
	};

	typedef uint32 FChannelId;

	struct FChannelInfo
	{
		const ANSICHAR* Name;
		const ANSICHAR* Desc;
		FChannelId Id;
		bool bIsEnabled;
		bool bIsReadOnly;
	};

	class FChannel;

	/**
	 * Allocate memory callback
	 * @param Size Size to allocate
	 * @param Alignment Alignment of memory
	 * @return Pointer to allocated memory
	 */
	typedef void* AllocFunc(SIZE_T Size, uint32 Alignment);

	/**
	 * Free memory callback
	 * @param Ptr Memory to free
	 * @param Size Size of memory to free
	 */
	typedef void		FreeFunc(void* Ptr, SIZE_T Size);

	/**
	 * The callback provides information about a channel and a user provided pointer.
	 * @param Name Name of channel
	 * @param State Enabled state of channel
	 * @param User User data passed to the callback
	 */
	typedef void		ChannelIterFunc(const ANSICHAR* Name, bool State, void* User);

	/**
	 * The callback provides information about a channel and a user provided pointer.
	 * @param Info Information about the channel
	 * @param User User data passed to the callback
	 * @return Returning false from the callback will stop the enumeration
	 */
	typedef bool		ChannelIterCallback(const FChannelInfo& Info, void*/*User*/);

	/**
	 * User defined write callback.
	 * @param Handle User defined handle passed to the function
	 * @param Data Pointer to data to write
	 * @param Size Size of data to write
	 * @return True if all data could be written correctly, false if error occurred.
	 */
	typedef bool		IoWriteFunc(UPTRINT Handle, const void* Data, uint32 Size);

	/**
	 * User defined close callback.
	 * @param Handle User defined handle passed to the function
	 */
	typedef void		IoCloseFunc(UPTRINT Handle);

	struct FStatistics
	{
		uint64	BytesSent = 0;	// Bytes sent/written to the current endpoint
		uint64	BytesTraced = 0;	// Bytes sent/written to the current endpoint (uncompressed)
		uint64	BytesEmitted = 0;	// Bytes emitted but potentially not yet written
		uint64	MemoryUsed = 0;	// Memory allocated by TraceLog allocator functions
		uint64	BlockPoolAllocated = 0;	// Memory allocated for the (TLS) block pool.
		uint32  BlockPoolFreeBlocks = 0;	// Number of available blocks
		uint32	BlockPoolAllocatedBlocks = 0;	// Number of allocated blocks
		uint32	SharedBufferAllocated = 0;	// Memory allocated for shared buffers
		uint32	FixedBufferAllocated = 0;	// Memory allocated for fixed buffers (tail, send)
		uint32	CacheAllocated = 0;	// Total memory allocated in cache buffers
		uint32	CacheUsed = 0;	// Used cache memory; Important-marked events are stored in the cache.
		uint32	CacheWaste = 0;	// Unused memory from retired cache buffers
	};

	struct FSendFlags
	{
		static const uint16 None = 0;
		static const uint16 ExcludeTail = 1 << 0;	// do not send the tail of historical events
		static const uint16 _Reserved = 1 << 15;	// this bit is used internally
	};

}