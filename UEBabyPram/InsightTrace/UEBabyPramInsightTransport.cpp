module;
#include <cassert>
#include <lz4.h>

module UEBabyPramInsightTransport;

import Potato;
import UEBabyPramInsightDefine;

namespace UEBabyPram::InsightParser
{
	using namespace Potato;

	bool FTidPacketTransport::IsEmpty() const
	{
		if (Threads.size() > 0)
		{
			for (const FThreadStream& Thread : Threads)
			{
				if (!Thread.Buffer.empty())
				{
					return false;
				}
			}
		}
		return true;
	}

	////////////////////////////////////////////////////////////////////////////////
	FTidPacketTransport::EReadPacketResult FTidPacketTransport::ReadPacket()
	{
		FTidPacketBase packet_base;
		auto readed_size = Reader->StreamRead(&packet_base);

		if (readed_size != sizeof(FTidPacketBase))
		{
			return EReadPacketResult::NeedMoreData;
		}

		Reader->StreamSeek(-sizeof(FTidPacketBase));

		std::pmr::vector<std::byte> datas;
		datas.resize(packet_base.PacketSize);
		auto readed = Reader->StreamRead(datas.data(), datas.size());

		if (!readed || readed != datas.size())
		{
			return EReadPacketResult::NeedMoreData;
		}

		auto* PacketBase = reinterpret_cast<FTidPacketBase*>(datas.data());

#if UE_TRACE_ANALYSIS_DEBUG
		++NumPackets;
		TotalPacketHeaderSize += sizeof(FTidPacketBase);
		TotalPacketSize += PacketBase->PacketSize;
#endif // UE_TRACE_ANALYSIS_DEBUG

		uint32 ThreadId = PacketBase->ThreadId & FTidPacketBase::ThreadIdMask;

		if (ThreadId == ETransportTid::Sync)
		{
			++Synced;
#if UE_TRACE_ANALYSIS_DEBUG
			UE_TRACE_ANALYSIS_DEBUG_LOG("[SYNC %u]", Synced);
#endif // UE_TRACE_ANALYSIS_DEBUG
			return EReadPacketResult::NeedMoreData;	// Do not read any more packets. Gives consumers a
			// chance to sample the world at each known sync point.
		}

#if UE_TRACE_PACKET_VERIFICATION
		const bool bHasPacketSerial = !!(PacketBase->ThreadId & FTidPacketBase::Verification);
		FThreadStream* Thread = FindOrAddThread(ThreadId, true);
#else
		bool bIsPartial = !!(PacketBase->ThreadId & FTidPacketBase::PartialMarker);
		FThreadStream* Thread = FindOrAddThread(ThreadId, !bIsPartial);
#endif

		if (Thread == nullptr)
		{
			return EReadPacketResult::Continue;
		}

		uint32 DataSize = PacketBase->PacketSize - sizeof(FTidPacketBase);
		if (PacketBase->ThreadId & FTidPacketBase::EncodedMarker)
		{
			const FTidPacketEncoded* Packet = (const FTidPacketEncoded*)PacketBase;
			uint16 DecodedSize = Packet->DecodedSize;

#if UE_TRACE_ANALYSIS_DEBUG
#if UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 3
			UE_TRACE_ANALYSIS_DEBUG_LOG("[PACKET %u] Tid=%u, Size: %u + %u bytes, DecodedSize: %u bytes (%.0f%%)%s",
				NumPackets,
				ThreadId,
				uint32(sizeof(FTidPacketEncoded)),
				DataSize,
				DecodedSize,
				((double)DataSize * 100.0) / (double)DecodedSize - 100.0,
				((DataSize == 0) || (DataSize > DecodedSize)) ? " !!!" : "");
#endif // UE_TRACE_ANALYSIS_DEBUG_LEVEL
			TotalPacketHeaderSize += sizeof(FTidPacketEncoded) - sizeof(FTidPacketBase);
			TotalDecodedSize += DecodedSize;
			uint64& TotalDataSizePerThread = DataSizePerThread.FindOrAdd(ThreadId);
			TotalDataSizePerThread += DecodedSize;
#endif // UE_TRACE_ANALYSIS_DEBUG

			DataSize -= sizeof(DecodedSize);

			// for debugging purposes only
			//UE_LOG(LogCore, Log, TEXT("Decompress packet (tid=%u): %u --> %u bytes"), ThreadId, DataSize, uint32(DecodedSize));

			if (DataSize != 0)
			{
				auto old_size = Thread->Buffer.size();
				Thread->Buffer.resize(old_size + DecodedSize);
				uint8* Dest = reinterpret_cast<uint8*>(Thread->Buffer.data() + old_size);
				auto ResultSize = LZ4_decompress_safe(
					reinterpret_cast<char const*>(Packet->Data), 
					reinterpret_cast<char*>(Dest),
					DataSize, 
					DecodedSize
					);
				
				if (int32(DecodedSize) != ResultSize)
				{
					return EReadPacketResult::ReadError;
				}

#if UE_TRACE_ANALYSIS_DEBUG && UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 4
				UE_TRACE_ANALYSIS_DEBUG_BeginStringBuilder();
				for (uint32 i = 0; i < 32 && i < DecodedSize; ++i)
				{
					UE_TRACE_ANALYSIS_DEBUG_Appendf("%02X ", Dest[i]);
				}
				UE_TRACE_ANALYSIS_DEBUG_EndStringBuilder();
#endif // UE_TRACE_ANALYSIS_DEBUG
			}
			else // DataSize == 0
			{
			}
		}
		else // not encoded
		{
#if UE_TRACE_ANALYSIS_DEBUG
#if UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 3
			UE_TRACE_ANALYSIS_DEBUG_LOG("[PACKET %u] Tid=%u, Size: %u + %u bytes",
				NumPackets,
				ThreadId,
				uint32(sizeof(FTidPacket)),
				DataSize);
#endif // UE_TRACE_ANALYSIS_DEBUG_LEVEL
			TotalPacketHeaderSize += sizeof(FTidPacket) - sizeof(FTidPacketBase);
			TotalDecodedSize += DataSize;
			uint64& TotalDataSizePerThread = DataSizePerThread.FindOrAdd(ThreadId);
			TotalDataSizePerThread += DataSize;
#endif // UE_TRACE_ANALYSIS_DEBUG

			// for debugging purposes only
			//UE_LOG(LogCore, Log, TEXT("Uncompressed packet (tid=%u): %u bytes"), ThreadId, DataSize);

			Thread->Buffer.append_range(std::span((std::byte*)(PacketBase + 1), DataSize));

#if UE_TRACE_ANALYSIS_DEBUG && UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 4
			UE_TRACE_ANALYSIS_DEBUG_BeginStringBuilder();
			for (uint32 i = 0; i < 32 && i < DataSize; ++i)
			{
				UE_TRACE_ANALYSIS_DEBUG_Appendf("%02X ", ((uint8*)(PacketBase + 1))[i]);
			}
			UE_TRACE_ANALYSIS_DEBUG_EndStringBuilder();
#endif // UE_TRACE_ANALYSIS_DEBUG
		}

#if UE_TRACE_PACKET_VERIFICATION
		if (bHasPacketSerial)
		{
			uint64 PacketSerial = *GetPointer<uint64>();
			if ((LastPacketSerial + 1) != PacketSerial)
			{
				UE_LOG(LogCore, Error, TEXT("Found packet with index '%llu' when '%llu` was expected."), PacketSerial, LastPacketSerial);
				return EReadPacketResult::ReadError;
			}
			LastPacketSerial = PacketSerial;
			FTransport::Advance(sizeof(uint64));
		}
#endif

		return EReadPacketResult::Continue;
	}

	////////////////////////////////////////////////////////////////////////////////
	FTidPacketTransport::FThreadStream* FTidPacketTransport::FindOrAddThread(
		uint32	ThreadId,
		bool	bAddIfNotFound)
	{
		uint32 ThreadCount = Threads.size();
		for (uint32 i = 0; i < ThreadCount; ++i)
		{
			if (Threads[i].ThreadId == ThreadId)
			{
				return &(Threads[i]);
			}
		}

		if (!bAddIfNotFound)
		{
			return nullptr;
		}

		FThreadStream Thread;
		Thread.ThreadId = ThreadId;
		Threads.push_back(Thread);
		return &(Threads[ThreadCount]);
	}

	////////////////////////////////////////////////////////////////////////////////
	FTidPacketTransport::ETransportResult FTidPacketTransport::Update()
	{
		EReadPacketResult Result;
		do
		{
			Result = ReadPacket();
		} while (Result == EReadPacketResult::Continue);

		Threads.erase(
			std::remove_if(Threads.begin(), Threads.end(), [](const FThreadStream& Thread)
				{
					return (Thread.ThreadId <= ETransportTid::Importants) ? false : Thread.Buffer.empty();
				}),
			Threads.end()
		);

#if UE_TRACE_ANALYSIS_DEBUG
		DebugUpdate();
#endif // UE_TRACE_ANALYSIS_DEBUG

		return Result == EReadPacketResult::ReadError ? ETransportResult::Error : ETransportResult::Ok;
	}

	////////////////////////////////////////////////////////////////////////////////
	void FTidPacketTransport::DebugUpdate()
	{
#if UE_TRACE_ANALYSIS_DEBUG
		const int32 ThreadCount = Threads.Num();
		for (int32 ThreadIndex = 0; ThreadIndex < ThreadCount; ++ThreadIndex)
		{
			const FThreadStream& Thread = Threads[ThreadIndex];
			const uint32 BufferSize = Thread.Buffer.GetBufferSize();
			const uint32 DataSize = Thread.Buffer.GetRemaining();

			if (BufferSize > MaxBufferSize)
			{
				MaxBufferSize = BufferSize;
				MaxBufferSizeThreadId = Thread.ThreadId;
			}

			if (DataSize > MaxDataSizePerBuffer)
			{
				MaxDataSizePerBuffer = DataSize;
				MaxDataSizePerBufferThreadId = Thread.ThreadId;
			}
		}
#endif // UE_TRACE_ANALYSIS_DEBUG
	}

	////////////////////////////////////////////////////////////////////////////////
	void FTidPacketTransport::DebugBegin(Log::LogPrinter&)
	{
#if UE_TRACE_ANALYSIS_DEBUG
		UE_TRACE_ANALYSIS_DEBUG_LOG("FTidPacketTransport::DebugBegin()");
#endif // UE_TRACE_ANALYSIS_DEBUG
	}

	////////////////////////////////////////////////////////////////////////////////
	void FTidPacketTransport::DebugEnd(Log::LogPrinter&)
	{
#if UE_TRACE_ANALYSIS_DEBUG
		Threads.RemoveAll([](const FThreadStream& Thread)
			{
				return Thread.Buffer.IsEmpty();
			});

		DebugUpdate();

		constexpr double MiB = 1024.0 * 1024.0;

		UE_TRACE_ANALYSIS_DEBUG_LOG("");
		UE_TRACE_ANALYSIS_DEBUG_LOG("TotalPacketSize: %llu bytes (%.1f MiB)", TotalPacketSize, (double)TotalPacketSize / MiB);
		const uint64 AdjustedTotalDecodedSize = TotalPacketHeaderSize + TotalDecodedSize;
		UE_TRACE_ANALYSIS_DEBUG_LOG("TotalDecodedSize: %llu bytes + %llu bytes = %llu bytes (%.1f MiB)", TotalPacketHeaderSize, TotalDecodedSize, AdjustedTotalDecodedSize, (double)AdjustedTotalDecodedSize / MiB);
		const int64 Saving = (int64)AdjustedTotalDecodedSize - (int64)TotalPacketSize;
		const double SavingPercent = ((double)TotalPacketSize * 100.0) / (double)AdjustedTotalDecodedSize - 100.0;
		UE_TRACE_ANALYSIS_DEBUG_LOG("Compression Savings: %lli bytes (%.1f MiB; %.2f%%)", Saving, (double)Saving / MiB, SavingPercent);

		UE_TRACE_ANALYSIS_DEBUG_LOG("NumPackets: %u", NumPackets);

		UE_TRACE_ANALYSIS_DEBUG_LOG("MaxBufferSize: %u bytes (%.1f MiB; for thread %u)", MaxBufferSize, (double)MaxBufferSize / MiB, MaxBufferSizeThreadId);
		const double MaxDataSizePerBufferPercent = ((double)MaxDataSizePerBuffer * 100.0) / (double)TotalDecodedSize;
		UE_TRACE_ANALYSIS_DEBUG_LOG("MaxDataSizePerBuffer: %u bytes (%.1f%%; for thread %u)", MaxDataSizePerBuffer, MaxDataSizePerBufferPercent, MaxDataSizePerBufferThreadId);

		uint64 MaxDataSizePerThread = 0;
		uint32 MaxDataSizePerThreadId = 0;
		for (auto& KV : DataSizePerThread)
		{
			if (KV.Value > MaxDataSizePerThread)
			{
				MaxDataSizePerThread = KV.Value;
				MaxDataSizePerThreadId = KV.Key;
			}
		}
		const double MaxDataSizePerThreadPercent = ((double)MaxDataSizePerThread * 100.0) / (double)TotalDecodedSize;
		UE_TRACE_ANALYSIS_DEBUG_LOG("MaxDataSizePerThread: %llu bytes (%.1f MiB; %.1f%%; for thread %u)", MaxDataSizePerThread, (double)MaxDataSizePerThread / MiB, MaxDataSizePerThreadPercent, MaxDataSizePerThreadId);

		const int32 ThreadCount = Threads.Num();
		UE_TRACE_ANALYSIS_DEBUG_LOG("Remaining Streaming Threads: %d", ThreadCount);
		uint64 TotalBufferSize = 0;
		uint64 UnprocessedDataSize = 0;
		for (int32 ThreadIndex = 0; ThreadIndex < ThreadCount; ++ThreadIndex)
		{
			const FThreadStream& Thread = Threads[ThreadIndex];
			const uint32 BufferSize = Thread.Buffer.GetBufferSize();
			const uint32 DataSize = Thread.Buffer.GetRemaining();
			const double DataSizePercent = ((double)DataSize * 100.0) / (double)TotalDecodedSize;
			UE_TRACE_ANALYSIS_DEBUG_LOG("[THREAD %d] Tid=%u BufferSize: %u bytes, DataSize: %u bytes (%.1f%%)", ThreadIndex, Thread.ThreadId, BufferSize, DataSize, DataSizePercent);
			TotalBufferSize += BufferSize;
			UnprocessedDataSize += DataSize;
		}
		UE_TRACE_ANALYSIS_DEBUG_LOG("TotalBufferSize: %llu bytes (%.1f MiB)", TotalBufferSize, (double)TotalBufferSize / MiB);

		const int64 ProcessedDataSize = (int64)TotalDecodedSize - (int64)UnprocessedDataSize;
		const double ProcessedPercent = ((double)ProcessedDataSize * 100.0) / (double)TotalDecodedSize;

		UE_TRACE_ANALYSIS_DEBUG_LOG("TotalDataSize (processed): %llu bytes (%.1f MiB; %.1f%%)", ProcessedDataSize, (double)ProcessedDataSize / MiB, ProcessedPercent);
		UE_TRACE_ANALYSIS_DEBUG_LOG("TotalDataSize (unprocessed): %llu bytes (%.1f MiB; %.1f%%)", UnprocessedDataSize, (double)UnprocessedDataSize / MiB, 100.0 - ProcessedPercent);

		const bool bIsEmpty = IsEmpty();
		check((UnprocessedDataSize == 0 && bIsEmpty) || (UnprocessedDataSize != 0 && !bIsEmpty));

		if (!bIsEmpty)
		{
			UE_TRACE_ANALYSIS_DEBUG_LOG("Error: FTidPacketTransport is not empty!");
		}

		UE_TRACE_ANALYSIS_DEBUG_LOG("FTidPacketTransport::DebugEnd()");
#endif // UE_TRACE_ANALYSIS_DEBUG
	}

	////////////////////////////////////////////////////////////////////////////////
	uint32 FTidPacketTransport::GetThreadCount() const
	{
		return uint32(Threads.size());
	}

	////////////////////////////////////////////////////////////////////////////////
	std::span<std::byte> FTidPacketTransport::GetThreadStream(uint32 Index)
	{
		auto& ref = Threads[Index].Buffer;
		return std::span<std::byte>(ref.data(), ref.size());
	}

	////////////////////////////////////////////////////////////////////////////////
	uint32 FTidPacketTransport::GetThreadId(uint32 Index) const
	{
		return Threads[Index].ThreadId;
	}
}