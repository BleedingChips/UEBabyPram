module;

export module UEBabyPramInsightTransport;

import std;
import Potato;
import UEBabyPramInsightDefine;


export namespace UEBabyPram::InsightParser
{
	using namespace Potato;

	struct FTidPacketBase
	{
		enum : uint16
		{
			EncodedMarker = 0x8000,
			PartialMarker = 0x4000, // now unused. fragmented aux-data has an event header
			Verification = 0x4000, // when set the packet data is immediately followed by 64-bit verification value, see UE_TRACE_PACKET_VERIFICATION
			ThreadIdMask = PartialMarker - 1,
		};

		uint16 PacketSize;
		uint16 ThreadId;
	};

	template <uint32 DataSize>
	struct TTidPacket
		: public FTidPacketBase
	{
		uint8	Data[DataSize];
	};

	template <uint32 DataSize>
	struct TTidPacketEncoded
		: public FTidPacketBase
	{
		uint16	DecodedSize;
		uint8	Data[DataSize];
	};

	using FTidPacket = TTidPacket<0>;
	using FTidPacketEncoded = TTidPacketEncoded<0>;

	////////////////////////////////////////////////////////////////////////////////
	// Some assumptions are made about 0-sized arrays in the packet structs so we
	// will casually make assertions about those assumptions here.
	static_assert(sizeof(FTidPacket) == 4, "");
	static_assert(sizeof(FTidPacketEncoded) == 6, "");

	class FTransport
	{
	public:
		
		using StreamRandomReader = Streamer::StreamRandomReader;
		
		FTransport() {}
		virtual					~FTransport() {}
		void					SetReader(StreamRandomReader& InReader) { Reader = &InReader; }
		StreamRandomReader* GetReader() const { return Reader; }
		template<typename Type>
		std::size_t Read(Type* out, std::size_t array_count = 1) { return Reader->StreamRead(out, array_count); }
		virtual bool			IsEmpty() const { return true; }
		virtual void			DebugBegin(Log::LogPrinter& printer) {}
		virtual void			DebugEnd(Log::LogPrinter& printer) {}
	protected:
		StreamRandomReader* Reader = nullptr;
	};

	class FTidPacketTransport
		: public FTransport
	{
	public:
		enum class ETransportResult : uint8
		{
			Ok,
			Error
		};

		FTidPacketTransport() {}
		virtual					~FTidPacketTransport() {}

		virtual bool			IsEmpty() const override;
		virtual void			DebugBegin(Log::LogPrinter& printer) override;
		virtual void			DebugEnd(Log::LogPrinter& printer) override;

		ETransportResult		Update();
		uint32					GetThreadCount() const;
		std::span<std::byte>	GetThreadStream(uint32 Index);
		uint32					GetThreadId(uint32 Index) const;
		uint32					GetSyncCount() const;

	protected:
		uint32					Synced = 0x7fff'ffff;
		uint32					NumPackets = 0;
#if UE_TRACE_PACKET_VERIFICATION
		uint64					LastPacketSerial = 0;
#endif

	private:
		struct FThreadStream
		{
			std::vector<std::byte>		Buffer;
			uint32				ThreadId;
		};

		enum class EReadPacketResult : uint8
		{
			NeedMoreData,
			Continue,
			ReadError
		};

		EReadPacketResult		ReadPacket();
		FThreadStream* FindOrAddThread(uint32 ThreadId, bool bAddIfNotFound);
		void					DebugUpdate();

		std::vector<FThreadStream>	Threads = {
									{ {}, ETransportTid::Events },
									{ {}, ETransportTid::Importants },
		};

#if UE_TRACE_ANALYSIS_DEBUG
		uint64					TotalPacketHeaderSize = 0;
		uint64					TotalPacketSize = 0;
		uint64					TotalDecodedSize = 0;
		uint32					MaxBufferSize = 0;
		uint32					MaxBufferSizeThreadId = 0;
		uint32					MaxDataSizePerBuffer = 0;
		uint32					MaxDataSizePerBufferThreadId = 0;
		TMap<uint32, uint64>	DataSizePerThread;
#endif // UE_TRACE_ANALYSIS_DEBUG
	};

	class FTidPacketTransportSync
		: public FTidPacketTransport
	{
	public:
		FTidPacketTransportSync()
		{
			Synced = 0;
		}
		virtual ~FTidPacketTransportSync() {}
	};
}