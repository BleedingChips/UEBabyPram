module;

module UEBabyPramInsightProtocolStage;

import std;
import Potato;
import UEBabyPramInsightDefine;


namespace UEBabyPram::InsightParser
{
	using namespace Potato;

	////////////////////////////////////////////////////////////////////////////////
	FAnalysisMachine::FAnalysisMachine(FAnalysisBridge& InBridge, FMessageDelegate&& InMessage)
		: Bridge(InBridge)
		, OnMessage(InMessage)
	{
	}

	////////////////////////////////////////////////////////////////////////////////
	FAnalysisMachine::~FAnalysisMachine()
	{
		CleanUp();
	}

	////////////////////////////////////////////////////////////////////////////////
	void FAnalysisMachine::CleanUp()
	{
		for (FStage* Stage : DeadStages)
		{
			delete Stage;
		}
		DeadStages.Reset();
	}

	////////////////////////////////////////////////////////////////////////////////
	template <class StageType, typename... ArgsType>
	StageType* FAnalysisMachine::QueueStage(ArgsType... Args)
	{
		StageType* Stage = new StageType(Args...);
		StageQueue.Insert(Stage, 0);
		return Stage;
	}

	////////////////////////////////////////////////////////////////////////////////
	void FAnalysisMachine::Transition()
	{
		if (ActiveStage != nullptr)
		{
			const FMachineContext Context = { *this, Bridge, OnMessage };
			ActiveStage->ExitStage(Context);

			DeadStages.Add(ActiveStage);
		}

		ActiveStage = (StageQueue.Num() > 0) ? StageQueue.Pop() : nullptr;

		if (ActiveStage != nullptr)
		{
			const FMachineContext Context = { *this, Bridge, OnMessage };
			ActiveStage->EnterStage(Context);
		}
	}

	////////////////////////////////////////////////////////////////////////////////
	FAnalysisMachine::EStatus FAnalysisMachine::OnData(FStreamReader& Reader)
	{
		const FMachineContext Context = { *this, Bridge, OnMessage };
		EStatus Ret;
		do
		{
			CleanUp();
			check(ActiveStage != nullptr);
			Ret = ActiveStage->OnData(Reader, Context);
		} while (Ret == EStatus::Continue);
		return Ret;
	}

	////////////////////////////////////////////////////////////////////////////////
	FProtocol5Stage::FProtocol5Stage(FTransport* InTransport)
		: BaseTransport(InTransport)
		, Transport(*(FTidPacketTransport*)InTransport)
		, SyncCount(Transport.GetSyncCount())
	{
		EventDescs.Reserve(8 << 10);
	}

	////////////////////////////////////////////////////////////////////////////////
	FProtocol5Stage::~FProtocol5Stage()
	{
		delete BaseTransport;
	}

	////////////////////////////////////////////////////////////////////////////////
	void FProtocol5Stage::ExitStage(const FMachineContext& Context)
	{
		// Ensure the transport does not have pending buffers (i.e. event data not yet processed).
		if (!Transport.IsEmpty())
		{
			Context.EmitMessage(EAnalysisMessageSeverity::Warning, TEXT("Transport buffers are not empty at end of analysis (protocol 5)!"));
		}

#if UE_TRACE_ANALYSIS_DEBUG
		Context.Bridge.GetSerial().Value = NextSerial;
		Transport.DebugEnd();
#endif
	}

	////////////////////////////////////////////////////////////////////////////////
	FProtocol5Stage::EStatus FProtocol5Stage::OnData(
		FStreamReader& Reader,
		const FMachineContext& Context)
	{
		Transport.SetReader(Reader);
		const FTidPacketTransport::ETransportResult Result = Transport.Update();
		if (Result == FTidPacketTransport::ETransportResult::Error)
		{
			Context.EmitMessage(
				EAnalysisMessageSeverity::Error,
				TEXT("An error was detected in the transport layer, most likely due to a corrupt trace file. See log for details.")
			);
			return EStatus::Error;
		}

		do
		{
			// New-events. They must be processed before anything else otherwise events
			// can not be interpreted.
			EStatus Ret = OnDataNewEvents(Context);
			if (Ret == EStatus::Error)
			{
				return Ret;
			}

			// Important events
			Ret = OnDataImportant(Context);
			if (Ret == EStatus::Error)
			{
				return Ret;
			}
			bool bNotEnoughData = (Ret == EStatus::NotEnoughData);

			// Normal events
			Ret = OnDataNormal(Context);
			if (Ret == EStatus::Error)
			{
				return Ret;
			}
			if (Ret == EStatus::Sync)
			{
				// After processing a SYNC packet, we need to read data once more.
				return OnData(Reader, Context);
			}
			bNotEnoughData |= (Ret == EStatus::NotEnoughData);

			if (bNotEnoughData && !bSkipSerial)
			{
				return EStatus::NotEnoughData;
			}
		} while (bSkipSerial);

		return Reader.CanMeetDemand() ? EStatus::Continue : EStatus::EndOfStream;
	}

	////////////////////////////////////////////////////////////////////////////////
	FProtocol5Stage::EStatus FProtocol5Stage::OnDataNewEvents(const FMachineContext& Context)
	{
		EventDescs.Reset();

		FStreamReader* ThreadReader = Transport.GetThreadStream(ETransportTid::Events);
		if (ThreadReader->IsEmpty())
		{
			return EStatus::EndOfStream;
		}

		if (ParseImportantEvents(*ThreadReader, EventDescs, Context) < 0)
		{
			UE_TRACE_ANALYSIS_DEBUG_LOG("Error: Failed to parse important events");
			return EStatus::Error;
		}

		for (const FEventDesc& EventDesc : EventDescs)
		{
			const FTypeRegistry::FTypeInfo* TypeInfo = TypeRegistry.Add(EventDesc.Data, EventVersion);
#if UE_TRACE_ANALYSIS_DEBUG
			Context.Bridge.DebugLogNewEvent(uint32(EventDesc.Uid), TypeInfo, EventDesc.EventSize + EventDesc.AuxSize);
#endif
			Context.Bridge.OnNewType(TypeInfo);
		}

		return EStatus::EndOfStream;
	}

	////////////////////////////////////////////////////////////////////////////////
	FProtocol5Stage::EStatus FProtocol5Stage::OnDataImportant(const FMachineContext& Context)
	{
		static_assert(ETransportTid::Importants == ETransportTid::Internal, "It is assumed there is only one 'important' thread stream");

		EventDescs.Reset();

		FStreamReader* ThreadReader = Transport.GetThreadStream(ETransportTid::Importants);
		if (ThreadReader->IsEmpty())
		{
			return EStatus::EndOfStream;
		}

		if (ParseImportantEvents(*ThreadReader, EventDescs, Context) < 0)
		{
			UE_TRACE_ANALYSIS_DEBUG_LOG("Error: Failed to parse important events");
			return EStatus::Error;
		}

		bool bNotEnoughData = !ThreadReader->IsEmpty();

		if (EventDescs.Num() <= 0)
		{
			return bNotEnoughData ? EStatus::NotEnoughData : EStatus::EndOfStream;
		}

		// Dispatch looks ahead to the next desc looking for runs of aux blobs. As
		// such we should add a terminal desc for it to read. Note the "- 1" too.
		FEventDesc& EventDesc = EventDescs.Emplace_GetRef();
		EventDesc.Serial = ESerial::Terminal;

		Context.Bridge.SetActiveThread(ETransportTid::Importants);
		if (DispatchEvents(Context, EventDescs.GetData(), EventDescs.Num() - 1) < 0)
		{
			UE_TRACE_ANALYSIS_DEBUG_LOG("Error: Failed to dispatch important events");
			return EStatus::Error;
		}

		return bNotEnoughData ? EStatus::NotEnoughData : EStatus::EndOfStream;
	}

	////////////////////////////////////////////////////////////////////////////////
	int32 FProtocol5Stage::ParseImportantEvents(FStreamReader& Reader, EventDescArray& OutEventDescs, const FMachineContext& Context)
	{
		using namespace Protocol5;

		while (true)
		{
			uint32 Remaining = Reader.GetRemaining();
			if (Remaining < sizeof(FImportantEventHeader))
			{
				return 1;
			}

			const auto* Header = Reader.GetPointerUnchecked<FImportantEventHeader>();
			if (Remaining < uint32(Header->Size) + sizeof(FImportantEventHeader))
			{
				return 1;
			}

			uint32 Uid = Header->Uid;

			FEventDesc EventDesc;
			EventDesc.Serial = ESerial::Ignored;
			EventDesc.Uid = (uint16)Uid;
			EventDesc.Data = Header->Data;

			// Special case for new events. It would work to add a 0 type to the
			// registry but this way avoid raveling things together.
			if (Uid == EKnownUids::NewEvent)
			{
#if UE_TRACE_ANALYSIS_DEBUG
				EventDesc.EventSize = sizeof(*Header) + Header->Size;
#endif
				OutEventDescs.Add(EventDesc);
				Reader.Advance(sizeof(*Header) + Header->Size);
				continue;
			}

			const FTypeRegistry::FTypeInfo* TypeInfo = TypeRegistry.Get(Uid);
			if (TypeInfo == nullptr)
			{
				UE_TRACE_ANALYSIS_DEBUG_LOG("UID %u (0x%X) was not declared yet.", Uid, Uid);
				return 1;
			}

#if UE_TRACE_ANALYSIS_DEBUG
			EventDesc.EventSize = sizeof(*Header) + TypeInfo->EventSize;
#endif

			if (TypeInfo->Flags & FTypeRegistry::FTypeInfo::Flag_MaybeHasAux)
			{
				EventDesc.bHasAux = 1;
			}

			OutEventDescs.Add(EventDesc);

			if (TypeInfo->Flags & FTypeRegistry::FTypeInfo::Flag_MaybeHasAux)
			{
				const uint8* Cursor = Header->Data + TypeInfo->EventSize;
				const uint8* End = Header->Data + Header->Size;
				while (Cursor <= End)
				{
					if (Cursor[0] == uint8(EKnownUids::AuxDataTerminal))
					{
						break;
					}

					const auto* AuxHeader = (FAuxHeader*)Cursor;

					FEventDesc& AuxDesc = OutEventDescs.Emplace_GetRef();
					AuxDesc.Uid = uint8(EKnownUids::AuxData);
					AuxDesc.Data = AuxHeader->Data;
					AuxDesc.Serial = ESerial::Ignored;

					Cursor = AuxHeader->Data + (AuxHeader->Pack >> FAuxHeader::SizeShift);

#if UE_TRACE_ANALYSIS_DEBUG
					AuxDesc.EventSize = sizeof(FAuxHeader);
					AuxDesc.AuxSize = (AuxHeader->Pack >> FAuxHeader::SizeShift);
#endif
				}

				if (Cursor[0] != uint8(EKnownUids::AuxDataTerminal))
				{
					UE_TRACE_ANALYSIS_DEBUG_LOG("Error: Expecting AuxDataTerminal event");
					Context.EmitMessage(EAnalysisMessageSeverity::Warning, TEXT("Expected an aux data terminal in the stream."));
					return -1;
				}
			}

			Reader.Advance(sizeof(*Header) + Header->Size);
		}
	}

	////////////////////////////////////////////////////////////////////////////////
	FProtocol5Stage::EStatus FProtocol5Stage::OnDataNormal(const FMachineContext& Context)
	{
		// Ordinary events

		EventDescs.Reset();
		bool bNotEnoughData = false;

		TArray<FEventDescStream> EventDescHeap;
		EventDescHeap.Reserve(Transport.GetThreadCount());

		bSkipSerial = false;

		for (uint32 i = ETransportTid::Bias, n = Transport.GetThreadCount(); i < n; ++i)
		{
			uint32 NumEventDescs = EventDescs.Num();

#if UE_TRACE_ANALYSIS_DEBUG && UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 3
			UE_TRACE_ANALYSIS_DEBUG_LOG("Thread:%u Id:%u", i, Transport.GetThreadId(i));
#endif

			// Extract all the events in the stream for this thread
			FStreamReader* ThreadReader = Transport.GetThreadStream(i);

			// Test if analysis has accumulated too much data for this thread.
			// This can happen on corrupted traces (ex. with serial sync events missing or out of order).
			constexpr uint32 MaxAccumulatedBytes = 2'000'000'000u;
			if (ThreadReader->GetRemaining() > MaxAccumulatedBytes)
			{
				UE_TRACE_ANALYSIS_DEBUG_LOG("Error: Trace analysis accumulated too much data (%.2f MiB on thread %u) and will start to skip the missing serial sync events!",
					(double)ThreadReader->GetRemaining() / (1024.0 * 1024.0),
					Transport.GetThreadId(i));
				if (!bSkipSerialError)
				{
					bSkipSerialError = true;
					Context.EmitMessagef(
						EAnalysisMessageSeverity::Error,
						TEXT("Trace analysis accumulated too much data (%.2f MiB on thread %u) and will start to skip the missing serial sync events!"),
						(double)ThreadReader->GetRemaining() / (1024.0 * 1024.0),
						Transport.GetThreadId(i)
					);
				}
				bSkipSerial = true;
			}

			if (ParseEvents(*ThreadReader, EventDescs, Context) < 0)
			{
				UE_TRACE_ANALYSIS_DEBUG_LOG("Error: Failed to parse events");
				return EStatus::Error;
			}

			bNotEnoughData |= !ThreadReader->IsEmpty();

			if (uint32(EventDescs.Num()) != NumEventDescs)
			{
				// Add a dummy event to delineate the end of this thread's events
				FEventDesc& EventDesc = EventDescs.Emplace_GetRef();
				EventDesc.Serial = ESerial::Terminal;

				FEventDescStream Out;
				Out.ThreadId = Transport.GetThreadId(i);
				Out.TransportIndex = i;
				Out.ContainerIndex = NumEventDescs;
				EventDescHeap.Add(Out);
			}

#if UE_TRACE_ANALYSIS_DEBUG && UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 3
			UE_TRACE_ANALYSIS_DEBUG_LOG("Thread:%u bNotEnoughData:%d", i, bNotEnoughData);
#endif
		}

		// Now EventDescs is stable we can convert the indices into pointers
		for (FEventDescStream& Stream : EventDescHeap)
		{
			Stream.EventDescs = EventDescs.GetData() + Stream.ContainerIndex;
		}

#if UE_TRACE_ANALYSIS_DEBUG
		FAnalysisState& State = Context.Bridge.GetState();
		if (EventDescs.Num() > State.MaxEventDescs)
		{
			State.MaxEventDescs = EventDescs.Num();
		}
#endif

		const bool bSync = (SyncCount != Transport.GetSyncCount());

		int32 NumAvailableEvents = EventDescs.Num();

		// Try to dispatch the parsed events.
		{
			int32 NumDispatchedEvents = DispatchNormalEvents(Context, EventDescHeap);
			if (NumDispatchedEvents < 0)
			{
				return EStatus::Error;
			}
			NumAvailableEvents += SerialGaps.Num(); // serial gaps are detected during DispatchNormalEvents
#if UE_TRACE_ANALYSIS_DEBUG && UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 2
			UE_TRACE_ANALYSIS_DEBUG_LOG("Dispatched %d normal events (%d --> %d)", NumDispatchedEvents, NumAvailableEvents, NumAvailableEvents - NumDispatchedEvents);
#endif
			NumAvailableEvents -= NumDispatchedEvents;
			check(NumAvailableEvents >= 0);

			// Count how many times we dispatched events, but without dispatching any "sync" event.
			if (OldNextSerial == NextSerial)
			{
				++NextSerialWaitCount;
			}
			else
			{
				OldNextSerial = NextSerial;
				NextSerialWaitCount = 0;
			}
		}

		// Test if analysis has accumulated too much data (parsed events not dispatched yet).
		// But, only enforce the limit after we have received at least one SYNC package
		// (e.g. server traces can accumulate large amounts of data before first SYNC event).
		constexpr int32 MaxAvailableEventsHighLimit = 90'000'000;
		constexpr int32 MaxAvailableEventsLowLimit = 50'000'000;
		constexpr uint32 MaxNextSerialWaitCount = 20;
		bool bSkipSerialNow = false;
		if (SyncCount > 0 &&
			!bSkipSerialError &&
			NumAvailableEvents > MaxAvailableEventsHighLimit &&
			NextSerialWaitCount > MaxNextSerialWaitCount)
		{
			UE_TRACE_ANALYSIS_DEBUG_LOG("Error: Trace analysis accumulated too much data (%d parsed events) and will start to skip the missing serial sync events!", NumAvailableEvents);
			UE_TRACE_ANALYSIS_DEBUG_LOG("NextSerial=%u", NextSerial);

			if (!bSkipSerialError)
			{
				bSkipSerialError = true;
#if UE_TRACE_ANALYSIS_DEBUG && UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 2
				for (const auto& Stream : EventDescHeap)
				{
					const FEventDesc* EventDesc = Stream.EventDescs;
					while (EventDesc->Serial != ESerial::Terminal)
					{
						if (EventDesc->Serial == NextSerial)
						{
							UE_TRACE_ANALYSIS_DEBUG_LOG("Found next serial on thread %u (event %d)", Stream.ThreadId, (int32)(EventDesc - Stream.EventDescs));
						}
						++EventDesc;
					}
				}
#endif
				Context.EmitMessagef(
					EAnalysisMessageSeverity::Error,
					TEXT("Trace analysis accumulated too much data (%d parsed events) and will start to skip the missing serial sync events!"), NumAvailableEvents);
			}
			bSkipSerialNow = true;
		}
		if (bSkipSerialNow ||
			(bSkipSerialError && NumAvailableEvents > MaxAvailableEventsLowLimit))
		{
			do
			{
				// Skip serials and continue to dispatch parsed events.
				bSkipSerial = true;
				NextSerialWaitCount = 0;
				int32 NumDispatchedEvents = DispatchNormalEvents(Context, EventDescHeap);
				if (NumDispatchedEvents < 0)
				{
					return EStatus::Error;
				}
#if UE_TRACE_ANALYSIS_DEBUG && UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 2
				UE_TRACE_ANALYSIS_DEBUG_LOG("Skipped serials and dispatched %d normal events (%d --> %d)", NumDispatchedEvents, NumAvailableEvents, NumAvailableEvents - NumDispatchedEvents);
#endif
				NumAvailableEvents -= NumDispatchedEvents;
				check(NumAvailableEvents >= 0);
			} while (NumAvailableEvents > MaxAvailableEventsLowLimit);
		}

#if 0
		// If the reader is empty (no more incoming trace data) and we still have parsed events not dispatched,
		// then we'll try to skip the missing serial sync events.
		if (Transport.GetReader()->IsEmpty() && NumAvailableEvents > 0)
		{
			UE_TRACE_ANALYSIS_DEBUG_LOG("Error: Trace reader is empty, but there are still %d parsed events not dispatched. Starting to skip the missing serial sync events!", NumAvailableEvents);
			Context.EmitMessagef(
				EAnalysisMessageSeverity::Error,
				TEXT("Trace reader is empty, but there are still %d parsed events not dispatched. Starting to skip the missing serial sync events!"), NumAvailableEvents);
			while (true)
			{
				// Skip serials and continue to dispatch parsed events.
				bSkipSerial = true;
				NextSerialWaitCount = 0;
				int32 NumDispatchedEvents = DispatchNormalEvents(Context, EventDescHeap);
				if (NumDispatchedEvents < 0)
				{
					return EStatus::Error;
				}
#if UE_TRACE_ANALYSIS_DEBUG && UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 2
				UE_TRACE_ANALYSIS_DEBUG_LOG("Skipped serials and dispatched %d normal events (%d --> %d)", NumDispatchedEvents, NumAvailableEvents, NumAvailableEvents - NumDispatchedEvents);
#endif
				NumAvailableEvents -= NumDispatchedEvents;
				check(NumAvailableEvents >= 0);

				if (NumAvailableEvents == 0)
				{
					break;
				}
				if (NumDispatchedEvents == 0)
				{
					UE_TRACE_ANALYSIS_DEBUG_LOG("Cannot dispatch the remaining %d (incomplete) parsed events!", NumAvailableEvents);
					break;
				}
			}
			bNotEnoughData = false;
			bSkipSerial = false;
		}
#endif

		// If there are any streams left in the heap then we are unable to proceed
		// until more data is received. We'll rewind the streams until more data is
		// available. It is not an efficient way to do things, but it is simple way.
		for (FEventDescStream& Stream : EventDescHeap)
		{
			const FEventDesc& EventDesc = Stream.EventDescs[0];
			uint32 HeaderSize = 1 + EventDesc.bTwoByteUid + (ESerial::Bits / 8);

			FStreamReader* Reader = Transport.GetThreadStream(Stream.TransportIndex);
			Reader->Backtrack(EventDesc.Data - HeaderSize);
		}

		if (bSync && SyncCount == Transport.GetSyncCount())
		{
			return EStatus::Sync;
		}
		if (bNotEnoughData)
		{
			return EStatus::NotEnoughData;
		}
		return EStatus::EndOfStream;
	}

	////////////////////////////////////////////////////////////////////////////////
	int32 FProtocol5Stage::DispatchNormalEvents(const FMachineContext& Context, TArray<FEventDescStream>& EventDescHeap)
	{
#if UE_TRACE_ANALYSIS_DEBUG && UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 2
		UE_TRACE_ANALYSIS_DEBUG_LOG("Queued event descs: %d", EventDescs.Num());
#endif

		// Process leading unsynchronized events so that each stream starts with a synchronized event.
		int32 NumDispatchedUnsyncEvents = 0;
		for (FEventDescStream& Stream : EventDescHeap)
		{
			// Extract a run of consecutive unsynchronized events
			const FEventDesc* EndDesc = Stream.EventDescs;
			for (; EndDesc->Serial == ESerial::Ignored; ++EndDesc);

			// Dispatch.
			const FEventDesc* StartDesc = Stream.EventDescs;
			int32 DescNum = int32(UPTRINT(EndDesc - StartDesc));
			if (DescNum > 0)
			{
				NumDispatchedUnsyncEvents += DescNum;

				Context.Bridge.SetActiveThread(Stream.ThreadId);

				if (DispatchEvents(Context, StartDesc, DescNum) < 0)
				{
					UE_TRACE_ANALYSIS_DEBUG_LOG("Error: Failed to dispatch events");
					return -1;
				}

				Stream.EventDescs = EndDesc;
			}
		}

		// Trim off empty streams
		int32 NumTerminators = EventDescHeap.Num();
		EventDescHeap.RemoveAllSwap([](const FEventDescStream& Stream)
			{
				return (Stream.EventDescs->Serial == ESerial::Terminal);
			});
		NumTerminators -= EventDescHeap.Num();

		// Early out if there isn't any events available.
		if (UNLIKELY(EventDescHeap.IsEmpty()))
		{
			return NumDispatchedUnsyncEvents + NumTerminators;
		}

		// A min-heap is used to peel off groups of events by lowest serial
		EventDescHeap.Heapify(FSerialDistancePredicate{ NextSerial });

#if UE_TRACE_ANALYSIS_DEBUG && UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 2
		{
			const FEventDescStream& TopStream = EventDescHeap.HeapTop();
			UE_TRACE_ANALYSIS_DEBUG_LOG("NextSerial=%u LowestSerial=%d (Tid=%u)", NextSerial, TopStream.EventDescs[0].Serial, TopStream.ThreadId);
		}
#endif

		// Events must be consumed contiguously.
		if (bSkipSerial)
		{
			uint32 LowestSerial = EventDescHeap.HeapTop().EventDescs[0].Serial;
			if (LowestSerial != NextSerial)
			{
#if UE_TRACE_ANALYSIS_DEBUG
				uint32 SerialsToSkip = ((LowestSerial & ESerial::Mask) + ESerial::Range - (NextSerial & ESerial::Mask)) & ESerial::Mask;
				Context.Bridge.GetState().NumSkippedSerials += SerialsToSkip;
				UE_TRACE_ANALYSIS_DEBUG_LOG("Warning: NextSerial skips %u events (from %u to %u)", SerialsToSkip, NextSerial, LowestSerial);
#endif
				NextSerial = LowestSerial;
			}
		}
		else
			if (NextSerial == ~0u)
			{
				uint32 LowestSerial = EventDescHeap.HeapTop().EventDescs[0].Serial;
				if (Transport.GetSyncCount() || LowestSerial == 0)
				{
					NextSerial = LowestSerial;
					UE_TRACE_ANALYSIS_DEBUG_LOG("NextSerial=%u", NextSerial);
				}
			}

		DetectSerialGaps(EventDescHeap);

		int32 NumDispatchedEvents = DispatchEvents(Context, EventDescHeap);
		if (NumDispatchedEvents < 0)
		{
			UE_TRACE_ANALYSIS_DEBUG_LOG("Error: Failed to dispatch events");
			return -1;
		}

		return NumDispatchedUnsyncEvents + NumTerminators + NumDispatchedEvents;
	}

	////////////////////////////////////////////////////////////////////////////////
	int32 FProtocol5Stage::DispatchEvents(const FMachineContext& Context, TArray<FEventDescStream>& EventDescHeap)
	{
#if UE_TRACE_ANALYSIS_DEBUG
		FAnalysisState& State = Context.Bridge.GetState();
#endif

		auto UpdateHeap = [&](const FEventDescStream& Stream, const FEventDesc* EventDesc)
			{
				if (EventDesc->Serial != ESerial::Terminal)
				{
					// Stream reference is for an element of EventDescHeap array, so we copy it before modifying the array.
					FEventDescStream Next = Stream;
					Next.EventDescs = EventDesc;
					// We can now discard the top stream from the heap and push the updated stream to the heap.
					EventDescHeap.HeapPopDiscard(FSerialDistancePredicate{ NextSerial }, EAllowShrinking::No);
					EventDescHeap.HeapPush(Next, FSerialDistancePredicate{ NextSerial });
				}
				else
				{
					EventDescHeap.HeapPopDiscard(FSerialDistancePredicate{ NextSerial }, EAllowShrinking::No);
				}
			};

		int32 NumDispatchedEvents = 0;

		do
		{
			const FEventDescStream& Stream = EventDescHeap.HeapTop();
			const FEventDesc* StartDesc = Stream.EventDescs;
			const FEventDesc* EndDesc = StartDesc;

			// DetectSerialGaps() will add a special stream that communicates gaps
			// in serial numbers, gaps that will never be resolved. Thread IDs
			// are uint16 everywhere else so they will never collide with GapThreadId.
			if (Stream.ThreadId == FEventDescStream::GapThreadId)
			{
				NextSerial = EndDesc->Serial + EndDesc->GapLength;
				NextSerial &= ESerial::Mask;
#if UE_TRACE_ANALYSIS_DEBUG
				if (NextSerial != EndDesc->Serial + EndDesc->GapLength)
				{
					State.SerialWrappedCount++;
				}
				State.NumSkippedSerialGaps++;
				UE_TRACE_ANALYSIS_DEBUG_LOG("Skip serial gap (%u +%u) --> NextSerial=%u", EndDesc->Serial, EndDesc->GapLength, NextSerial);
#endif
				UpdateHeap(Stream, EndDesc + 1);
				NumDispatchedEvents += ((EndDesc + 1)->Serial == ESerial::Terminal) ? 2 : 1; // GapThreadId event + Terminal event
				continue;
			}

			// Extract a run of consecutive events (plus runs of unsynchronized ones).
			if (EndDesc->Serial == NextSerial)
			{
#if UE_TRACE_ANALYSIS_DEBUG
				const uint32 CurrentSerial = NextSerial;
#endif

				do
				{
					NextSerial = (NextSerial + 1) & ESerial::Mask;

					do
					{
						++EndDesc;
					} while (EndDesc->Serial == ESerial::Ignored);
				} while (EndDesc->Serial == NextSerial);

#if UE_TRACE_ANALYSIS_DEBUG
				if (NextSerial < CurrentSerial)
				{
					++State.SerialWrappedCount;
				}
#endif
			}
			else
			{
#if UE_TRACE_ANALYSIS_DEBUG
				if (uint32(EndDesc->Serial) < NextSerial &&
					NextSerial != ~0u &&
					NextSerial - uint32(EndDesc->Serial) < ESerial::Range / 2)
				{
					UE_TRACE_ANALYSIS_DEBUG_LOG("Error: Lowest serial %d (Tid=%u Uid=%u) is too low (NextSerial=%u; %d event descs) !!!", EndDesc->Serial, Stream.ThreadId, uint32(EndDesc->Uid), NextSerial, EventDescs.Num());
				}
				else
				{
#if UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 2
					UE_TRACE_ANALYSIS_DEBUG_LOG("Lowest serial %d (Tid=%u Uid=%u) is not low enough (NextSerial=%u; %d event descs)", EndDesc->Serial, Stream.ThreadId, uint32(EndDesc->Uid), NextSerial, EventDescs.Num());
#endif
				}
#if UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 2
				UE_TRACE_ANALYSIS_DEBUG_LOG("Available streams (SerialWrappedCount=%u):", State.SerialWrappedCount);
				for (const FEventDescStream& EventDescStream : EventDescHeap)
				{
					uint32 BufferSize = 0;
					uint32 DataSize = 0; // parsed data size + remaining data size
					uint32 RemainingSize = 0;
					for (uint32 i = 0, n = Transport.GetThreadCount(); i < n; ++i)
					{
						FStreamBuffer* ThreadReader = (FStreamBuffer*)Transport.GetThreadStream(i);
						uint32 ThreadId = Transport.GetThreadId(i);
						if (ThreadId == EventDescStream.ThreadId)
						{
							BufferSize = ThreadReader->GetBufferSize();
							const FEventDesc& EventDesc = EventDescStream.EventDescs[0];
							uint32 HeaderSize = 1 + EventDesc.bTwoByteUid + (ESerial::Bits / 8);
							DataSize = ThreadReader->GetBacktrackSize(EventDesc.Data - HeaderSize);
							RemainingSize = ThreadReader->GetRemaining();
							break;
						}
					}
					UE_TRACE_ANALYSIS_DEBUG_BeginStringBuilder();
					UE_TRACE_ANALYSIS_DEBUG_Appendf("  Tid=%u : Serial=%u BufferSize=%u DataSize=%u", EventDescStream.ThreadId, EventDescStream.EventDescs->Serial, BufferSize, DataSize);
					if (RemainingSize != 0)
					{
						UE_TRACE_ANALYSIS_DEBUG_Appendf(" (%u + %u)", DataSize - RemainingSize, RemainingSize);
					}
					if (EventDescStream.EventDescs->Serial == NextSerial)
					{
						UE_TRACE_ANALYSIS_DEBUG_Append(" (next)");
					}
					UE_TRACE_ANALYSIS_DEBUG_EndStringBuilder();
				}
#endif // UE_TRACE_ANALYSIS_DEBUG_LEVEL
#endif // UE_TRACE_ANALYSIS_DEBUG

#if 0
				int32 MinSerial = EndDesc->Serial;
				EventDescHeap.Heapify(FSerialDistancePredicate{ NextSerial });
				const FEventDescStream& MinStream = EventDescHeap.HeapTop();
				const FEventDesc* MinDesc = MinStream.EventDescs;
				if (MinDesc->Serial < MinSerial)
				{
					UE_TRACE_ANALYSIS_DEBUG_LOG("Try one more time with lowest serial %d (Tid=%u Uid=%u)", MinDesc->Serial, MinStream.ThreadId, uint32(MinDesc->Uid));
					continue;
				}
#endif

				// The lowest known serial number is not low enough so we are unable to proceed any further.
				break;
			}

			// Dispatch.
			Context.Bridge.SetActiveThread(Stream.ThreadId);
			int32 DescNum = int32(UPTRINT(EndDesc - StartDesc));
			check(DescNum > 0);
			NumDispatchedEvents += DescNum;
			if (DispatchEvents(Context, StartDesc, DescNum) < 0)
			{
				UE_TRACE_ANALYSIS_DEBUG_LOG("Error: Failed to dispatch events");
				return -1;
			}

			UpdateHeap(Stream, EndDesc);
			NumDispatchedEvents += (EndDesc->Serial == ESerial::Terminal) ? 1 : 0; // Terminal event
		} while (!EventDescHeap.IsEmpty());

		return NumDispatchedEvents;
	}

	////////////////////////////////////////////////////////////////////////////////
#if UE_TRACE_ANALYSIS_DEBUG
	void FProtocol5Stage::PrintParsedEvent(int EventIndex, const FEventDesc& EventDesc, int32 Size)
	{
		UE_TRACE_ANALYSIS_DEBUG_BeginStringBuilder();
		UE_TRACE_ANALYSIS_DEBUG_Appendf("Event=%-6d Uid=%-4u ", EventIndex, uint32(EventDesc.Uid));
		if (EventDesc.Serial >= ESerial::Range)
		{
			UE_TRACE_ANALYSIS_DEBUG_Appendf("Serial=0x%07X ", EventDesc.Serial);
		}
		else
		{
			UE_TRACE_ANALYSIS_DEBUG_Appendf("Serial=%-9d ", EventDesc.Serial);
		}
		UE_TRACE_ANALYSIS_DEBUG_Appendf("Size=%d", Size);
		if (EventDesc.bHasAux)
		{
			UE_TRACE_ANALYSIS_DEBUG_Append(" aux");
		}
		if (EventDesc.Uid == EKnownUids::AuxData)
		{
			UE_TRACE_ANALYSIS_DEBUG_Append(" data");
		}
		else if (EventDesc.Uid == EKnownUids::AuxDataTerminal)
		{
			UE_TRACE_ANALYSIS_DEBUG_Append(" end");
		}
		UE_TRACE_ANALYSIS_DEBUG_EndStringBuilder();
	}
#endif // UE_TRACE_ANALYSIS_DEBUG

	////////////////////////////////////////////////////////////////////////////////
	int32 FProtocol5Stage::ParseEvents(FStreamReader& Reader, EventDescArray& OutEventDescs, const FMachineContext& Context)
	{
		while (!Reader.IsEmpty())
		{
			FEventDesc EventDesc;

			int32 Size = ParseEvent(Reader, EventDesc, Context);
			if (Size <= 0)
			{
				return Size;
			}

#if UE_TRACE_ANALYSIS_DEBUG && UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 3
			PrintParsedEvent(OutEventDescs.Num(), EventDesc, Size);
#endif // UE_TRACE_ANALYSIS_DEBUG

			OutEventDescs.Add(EventDesc);

			if (EventDesc.bHasAux)
			{
				uint32 RewindDescsNum = OutEventDescs.Num() - 1;
				auto RewindMark = Reader.SaveMark();

				Reader.Advance(Size);

				int Ok = ParseEventsWithAux(Reader, OutEventDescs, Context);
				if (Ok < 0)
				{
					return Ok;
				}

				if (Ok == 0)
				{
#if UE_TRACE_ANALYSIS_DEBUG && UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 2
					UE_TRACE_ANALYSIS_DEBUG_LOG("Warning: Incomplete aux stack (Uid=%u)! Rewind %d parsed events.", EventDesc.Uid, OutEventDescs.Num() - RewindDescsNum);
#endif
					OutEventDescs.SetNum(RewindDescsNum, EAllowShrinking::No);
					Reader.RestoreMark(RewindMark);
					break;
				}

				continue;
			}

			Reader.Advance(Size);
		}

		return 0;
	}

	////////////////////////////////////////////////////////////////////////////////
	int32 FProtocol5Stage::ParseEventsWithAux(FStreamReader& Reader, EventDescArray& OutEventDescs, const FMachineContext& Context)
	{
		// We are now "in" the scope of an event with zero or more aux-data blocks.
		// We will consume events until we leave this scope (a aux-data-terminal).
		// A running key is assigned to each event with a gap left following events
		// that may have aux-data blocks. Aux-data blocks are assigned a key that
		// fits in these gaps. Once sorted by this key, events maintain their order
		// while aux-data blocks are moved to directly follow their owners.

		TArray<uint16, TInlineAllocator<8>> AuxKeyStack = { 0 };
		uint32 AuxKey = 2;

		uint32 FirstDescIndex = OutEventDescs.Num();
		bool bUnsorted = false;

		while (!Reader.IsEmpty())
		{
			FEventDesc EventDesc;
			EventDesc.Serial = ESerial::Ignored;

			int32 Size = ParseEvent(Reader, EventDesc, Context);
			if (Size <= 0)
			{
				return Size;
			}

#if UE_TRACE_ANALYSIS_DEBUG && UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 3
			PrintParsedEvent(OutEventDescs.Num(), EventDesc, Size);
#endif // UE_TRACE_ANALYSIS_DEBUG

			Reader.Advance(Size);

			if (EventDesc.Uid == EKnownUids::AuxDataTerminal)
			{
				// Leave the scope of an aux-owning event.
				if (AuxKeyStack.Pop() == 0)
				{
					break;
				}
				continue;
			}
			else if (EventDesc.Uid == EKnownUids::AuxData)
			{
				// Move an aux-data block to follow its owning event
				EventDesc.AuxKey = AuxKeyStack.Last() + 1;
			}
			else
			{
				EventDesc.AuxKey = uint16(AuxKey);

				// Maybe it is time to create a new aux-data owner scope
				if (EventDesc.bHasAux)
				{
					AuxKeyStack.Add(uint16(AuxKey));
				}

				// This event may be in the middle of an earlier event's aux data blocks.
				bUnsorted = true;
			}

			OutEventDescs.Add(EventDesc);

			++AuxKey;

			constexpr uint32 MaxAuxKey = 0x7fff;
			if (AuxKeyStack.Num() == 1 && AuxKey > MaxAuxKey)
			{
				// If an "aux terminal" for the initial event was not detected after
				// many intermediate events, we can assume it is lost.
				check(FirstDescIndex > 0);
				uint32 NumParsedEvents = uint32(OutEventDescs.Num()) - FirstDescIndex;
				UE_TRACE_ANALYSIS_DEBUG_LOG("Error: Ignoring lost aux terminal for event with uid %u (desc %d), after parsing %u events",
					OutEventDescs[FirstDescIndex - 1].Uid,
					FirstDescIndex - 1,
					NumParsedEvents);
				Context.EmitMessagef(
					EAnalysisMessageSeverity::Error,
					TEXT("Ignoring lost aux terminal for event with uid %u, after parsing %u events."),
					OutEventDescs[FirstDescIndex - 1].Uid,
					NumParsedEvents);
				AuxKeyStack.Pop();
				break;
			}
		}

		if (AuxKeyStack.Num() > 0)
		{
			// There was not enough data available to complete the outer most scope
			return 0;
		}

		checkf((AuxKey & 0xffff0000) == 0, TEXT("AuxKey overflow (0x%X)"), AuxKey);

		// Sort to get all aux-blocks contiguous with their owning event
		if (bUnsorted)
		{
			uint32 NumDescs = OutEventDescs.Num() - FirstDescIndex;
#if UE_TRACE_ANALYSIS_DEBUG && UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 3
			UE_TRACE_ANALYSIS_DEBUG_LOG("Sorting %u event descs", NumDescs);
#endif
			TArrayView<FEventDesc> DescsView(OutEventDescs.GetData() + FirstDescIndex, NumDescs);
			Algo::StableSort(
				DescsView,
				[](const FEventDesc& Lhs, const FEventDesc& Rhs)
				{
					return Lhs.AuxKey < Rhs.AuxKey;
				}
			);
		}

		return 1;
	}

	////////////////////////////////////////////////////////////////////////////////
	int32 FProtocol5Stage::ParseEvent(FStreamReader& Reader, FEventDesc& EventDesc, const FMachineContext& Context)
	{
		using namespace Protocol5;

		// No need to aggressively bounds check here. Events are never fragmented
		// due to the way that data is transported (aux payloads can be though).
		const uint8* Cursor = Reader.GetPointerUnchecked<uint8>();

		// Parse the event's ID
		uint32 Uid = *Cursor;
		if (Uid & EKnownUids::Flag_TwoByteUid)
		{
			EventDesc.bTwoByteUid = 1;
			Uid = *(uint16*)Cursor;
			++Cursor;
		}
		Uid >>= EKnownUids::_UidShift;
		++Cursor;

		// Calculate the size of the event
		uint32 Serial = uint32(ESerial::Ignored);
		uint32 EventSize = 0;
		if (Uid < EKnownUids::User)
		{
			/* Well-known event */

			if (Uid == Protocol5::EKnownEventUids::AuxData)
			{
				--Cursor; // FAuxHeader includes the one-byte Uid
				const auto* AuxHeader = (FAuxHeader*)Cursor;

				uint32 Remaining = Reader.GetRemaining();
				uint32 Size = AuxHeader->Pack >> FAuxHeader::SizeShift;
				if (Remaining < Size + sizeof(FAuxHeader))
				{
					return 0;
				}

				EventSize = Size;
				Cursor += sizeof(FAuxHeader);
			}
			else
			{
				SetSizeIfKnownEvent(Uid, EventSize);
			}

			EventDesc.bHasAux = 0;
		}
		else
		{
			/* Ordinary events */

			const FTypeRegistry::FTypeInfo* TypeInfo = TypeRegistry.Get(Uid);
			if (TypeInfo == nullptr)
			{
				UE_TRACE_ANALYSIS_DEBUG_LOG("Warning: UID %u (0x%X) was not declared yet!", Uid, Uid);
				return 0;
			}

			EventSize = TypeInfo->EventSize;
			EventDesc.bHasAux = !!(TypeInfo->Flags & FTypeRegistry::FTypeInfo::Flag_MaybeHasAux);

			if ((TypeInfo->Flags & FDispatch::Flag_NoSync) == 0)
			{
				memcpy(&Serial, Cursor, sizeof(int32));
				Serial &= ESerial::Mask;
				Cursor += 3;
			}
		}

		EventDesc.Serial = Serial;
		EventDesc.Uid = (uint16)Uid;
		EventDesc.Data = Cursor;

		uint32 HeaderSize = uint32(UPTRINT(Cursor - Reader.GetPointer<uint8>()));
		uint32 TotalEventSize = HeaderSize + EventSize;
#if UE_TRACE_ANALYSIS_DEBUG
		EventDesc.EventSize = TotalEventSize;
#endif
		return TotalEventSize;
	}

	////////////////////////////////////////////////////////////////////////////////
	template <typename Callback>
	void FProtocol5Stage::ForEachSerialGap(const TArray<FEventDescStream>& EventDescHeap, Callback&& InCallback)
	{
		TArray<FEventDescStream> HeapCopy(EventDescHeap);
		int Serial = HeapCopy.HeapTop().EventDescs[0].Serial;

		// There might be a gap at the beginning of the heap if some events have
		// already been consumed.
		if (NextSerial != Serial)
		{
			if (!InCallback(NextSerial, Serial))
			{
				return;
			}
		}

		// A min-heap is used to peel off each stream (thread) with the lowest serial
		// numbered event.
		do
		{
			const FEventDescStream& Stream = HeapCopy.HeapTop();
			const FEventDesc* EventDesc = Stream.EventDescs;

			// If the next lowest serial number doesn't match where we got up to in
			// the previous stream we have found a gap. Celebration ensues.
			if (Serial != EventDesc->Serial)
			{
				if (!InCallback(Serial, EventDesc->Serial))
				{
					return;
				}
			}

			// Consume consecutive events (including unsynchronized ones).
			Serial = EventDesc->Serial;
			do
			{
				do
				{
					++EventDesc;
				} while (EventDesc->Serial == ESerial::Ignored);

				Serial = (Serial + 1) & ESerial::Mask;
			} while (EventDesc->Serial == Serial);

			// Update the heap
			if (EventDesc->Serial != ESerial::Terminal)
			{
				auto& Out = HeapCopy.Add_GetRef({ Stream.ThreadId, Stream.TransportIndex });
				Out.EventDescs = EventDesc;
			}
			HeapCopy.HeapPopDiscard(FSerialDistancePredicate{ NextSerial }, EAllowShrinking::No);
		} while (!HeapCopy.IsEmpty());
	}

	////////////////////////////////////////////////////////////////////////////////
	void FProtocol5Stage::DetectSerialGaps(TArray<FEventDescStream>& EventDescHeap)
	{
		// Events that should be synchronized across threads are assigned serial
		// numbers so they can be analyzed in the correct order. Gaps in the
		// serials can occur under two scenarios; 1) when packets are dropped from
		// the trace tail to make space for new trace events, and 2) when Trace's
		// worker thread ticks, samples all the trace buffers and sends their data.
		// In late-connect scenarios these gaps need to be skipped over in order to
		// successfully re-serialize events in the data stream. To further complicate
		// matters, most of the gaps from (2) will get filled by the following update,
		// leading to initial false positive gaps. By embedding sync points in the
		// stream we can reliably differentiate genuine gaps from temporary ones.
		//
		// Note that this could be done without sync points but it is an altogether
		// more complex solution. So unsightly embedded syncs it is...

		if (SyncCount == Transport.GetSyncCount())
		{
			return;
		}

		SyncCount = Transport.GetSyncCount();
		UE_TRACE_ANALYSIS_DEBUG_LOG("SyncCount: %d (%d previous serial gaps)", SyncCount, SerialGaps.Num());

		// We expect 3 syncpoints before we consider the stream complete. Once the second
		// sync is parsed we can collect the gaps in serials
		if (SyncCount == 2)
		{
			// On the first update we will just collect gaps.
			auto GatherGap = [this](int32 Lhs, int32 Rhs)
				{
					FEventDesc& Gap = SerialGaps.Emplace_GetRef();
					Gap.Serial = Lhs;
					Gap.GapLength = (Rhs - Lhs) & ESerial::Mask;
					return true;
				};
			ForEachSerialGap(EventDescHeap, GatherGap);
		}
		else if (SyncCount == 3)
		{
			// On the third update we detect where gaps from the previous update
			// start getting filled in. Any gaps preceding that point are genuine.
			uint32 GapCount = 0;
			auto RecordGap = [this, &GapCount](int32 Lhs, int32 Rhs)
				{
					if (SerialGaps.IsEmpty() || GapCount >= (uint32)SerialGaps.Num())
					{
						return false;
					}

					const FEventDesc& SerialGap = SerialGaps[GapCount];

					if (SerialGap.Serial == Lhs)
					{
						/* This is the expected case */
						++GapCount;
						return true;
					}

					if (SerialGap.Serial > Lhs)
					{
						/* We've started receiving new gaps that are exist because not all
						* data has been received yet. They're false positives. No need to process
						* any further */
						return false;
					}

					// If we're here something's probably gone wrong
					UE_TRACE_ANALYSIS_DEBUG_LOG("Error: Serial gaps detection failed (SerialGap.Serial=%d, Lhs=%d)!", SerialGap.Serial, Lhs);
					return false;
				};

			ForEachSerialGap(EventDescHeap, RecordGap);

			UE_TRACE_ANALYSIS_DEBUG_LOG("Serial gaps: %d", GapCount);

			if (GapCount == 0) //-V547
			{
				SerialGaps.Empty();
				return;
			}

#if UE_TRACE_ANALYSIS_DEBUG && UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 2
			for (uint32 GapIndex = 0; GapIndex < GapCount; ++GapIndex)
			{
				const FEventDesc& SerialGap = SerialGaps[GapIndex];
				UE_TRACE_ANALYSIS_DEBUG_LOG("  gap %u +%u", SerialGap.Serial, SerialGap.GapLength);
			}
#endif

			// Turn the genuine gaps into a stream that DispatchEvents() can handle
			// and use to skip over them.

			if (GapCount == uint32(SerialGaps.Num()))
			{
				SerialGaps.Emplace();
			}
			FEventDesc& Terminator = SerialGaps[GapCount];
			Terminator.Serial = ESerial::Terminal;

			FEventDescStream Out = EventDescHeap[0];
			Out.ThreadId = FEventDescStream::GapThreadId;
			Out.EventDescs = SerialGaps.GetData();
			EventDescHeap.HeapPush(Out, FSerialDistancePredicate{ NextSerial });
		}
	}

	////////////////////////////////////////////////////////////////////////////////
	void FProtocol5Stage::SetSizeIfKnownEvent(uint32 Uid, uint32& InOutEventSize)
	{
		switch (Uid)
		{
		case EKnownUids::EnterScope_T:
		case EKnownUids::LeaveScope_T:
			InOutEventSize = 7;
			break;
		};
	}

	////////////////////////////////////////////////////////////////////////////////
	bool FProtocol5Stage::DispatchKnownEvent(const FMachineContext& Context, uint32 Uid, const FEventDesc* Cursor)
	{
		// Maybe this is a "well-known" event that is handled a little different?
		switch (Uid)
		{
		case EKnownUids::EnterScope:
#if UE_TRACE_ANALYSIS_DEBUG
			Context.Bridge.DebugLogEnterScopeEvent(Uid, Cursor->EventSize + Cursor->AuxSize);
#endif
			Context.Bridge.EnterScope();
			return true;

		case EKnownUids::LeaveScope:
#if UE_TRACE_ANALYSIS_DEBUG
			Context.Bridge.DebugLogLeaveScopeEvent(Uid, Cursor->EventSize + Cursor->AuxSize);
#endif
			Context.Bridge.LeaveScope();
			return true;

		case EKnownUids::EnterScope_T:
		{
			uint64 RelativeTimestamp = *(uint64*)(Cursor->Data - 1) >> 8;
#if UE_TRACE_ANALYSIS_DEBUG
			Context.Bridge.DebugLogEnterScopeEvent(Uid, RelativeTimestamp, Cursor->EventSize + Cursor->AuxSize);
#endif
			Context.Bridge.EnterScope(RelativeTimestamp);
			return true;
		}

		case EKnownUids::LeaveScope_T:
		{
			uint64 RelativeTimestamp = *(uint64*)(Cursor->Data - 1) >> 8;
#if UE_TRACE_ANALYSIS_DEBUG
			Context.Bridge.DebugLogLeaveScopeEvent(Uid, RelativeTimestamp, Cursor->EventSize + Cursor->AuxSize);
#endif
			Context.Bridge.LeaveScope(RelativeTimestamp);
			return true;
		}

		case EKnownUids::AuxData:
		case EKnownUids::AuxDataTerminal:
			return true;

		default:
			return false;
		}
	}

	////////////////////////////////////////////////////////////////////////////////
	int32 FProtocol5Stage::DispatchEvents(const FMachineContext& Context, const FEventDesc* EventDesc, uint32 Count)
	{
		using namespace Protocol5;

		FAuxDataCollector AuxCollector;

#if UE_TRACE_ANALYSIS_DEBUG && UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 3
		UE_TRACE_ANALYSIS_DEBUG_LOG("Dispatch run of %u consecutive events (Tid=%u)", Count, Context.Bridge.GetActiveThreadId());
#endif

		for (const FEventDesc* Cursor = EventDesc, *End = EventDesc + Count; Cursor < End; ++Cursor)
		{
			uint32 Uid = uint32(Cursor->Uid);

			if (DispatchKnownEvent(Context, Uid, Cursor))
			{
				continue;
			}

			if (!TypeRegistry.IsUidValid(Uid))
			{
				UE_TRACE_ANALYSIS_DEBUG_LOG("Warning: Unexpected event with UID %u (0x%X)", Uid, Uid);
				Context.EmitMessagef(EAnalysisMessageSeverity::Warning, TEXT("An unknown event UID (%u) was encountered."), Uid);
#if UE_TRACE_ANALYSIS_DEBUG && UE_TRACE_ANALYSIS_DEBUG_LEVEL >= 3
				UE_TRACE_ANALYSIS_DEBUG_BeginStringBuilder();
				const uint8* StartCursor = Cursor->Data;
				for (uint32 i = 0; i < 32 && i < Cursor->EventSize; ++i)
				{
					UE_TRACE_ANALYSIS_DEBUG_Appendf("%02X ", StartCursor[i]);
				}
				UE_TRACE_ANALYSIS_DEBUG_EndStringBuilder();
				UE_TRACE_ANALYSIS_DEBUG_ResetStringBuilder();
				UE_TRACE_ANALYSIS_DEBUG_Append("[[[");
				for (uint32 i = 0; i < 128 && i < Cursor->EventSize; ++i)
				{
					UE_TRACE_ANALYSIS_DEBUG_AppendChar((char)StartCursor[i]);
				}
				UE_TRACE_ANALYSIS_DEBUG_Append("]]]");
				UE_TRACE_ANALYSIS_DEBUG_EndStringBuilder();
#endif // UE_TRACE_ANALYSIS_DEBUG

				return -1;
			}

			// It is a normal event.
			const FTypeRegistry::FTypeInfo* TypeInfo = TypeRegistry.Get(Uid);
			FEventDataInfo EventDataInfo = {
				Cursor->Data,
				*TypeInfo,
				&AuxCollector,
				TypeInfo->EventSize,
			};

#if UE_TRACE_ANALYSIS_DEBUG
			uint32 FixedSize = Cursor->EventSize;
			uint32 AuxSize = Cursor->AuxSize;
			const FEventDesc* EventCursor = Cursor;
#endif

			// Gather its auxiliary data blocks into a collector.
			if (Cursor->bHasAux)
			{
				while (true)
				{
					++Cursor;

					if (Cursor->Uid != EKnownUids::AuxData)
					{
						--Cursor; // Read off too much. Put it back.
						break;
					}

					const auto* AuxHeader = ((FAuxHeader*)(Cursor->Data)) - 1;

					FAuxData AuxData = {};
					AuxData.Data = AuxHeader->Data;
					AuxData.DataSize = (AuxHeader->Pack >> FAuxHeader::SizeShift);
					AuxData.FieldIndex = AuxHeader->FieldIndex_Size & FAuxHeader::FieldMask;
					// AuxData.FieldSizeAndType = ... - this is assigned on demand in GetData()
					AuxCollector.Add(AuxData);

#if UE_TRACE_ANALYSIS_DEBUG
					AuxSize += Cursor->EventSize + Cursor->AuxSize;
#endif
				}
#if UE_TRACE_ANALYSIS_DEBUG
				AuxSize += 1; // for AuxDataTerminal
#endif
			}

#if UE_TRACE_ANALYSIS_DEBUG
			Context.Bridge.DebugLogEvent(TypeInfo, FixedSize, AuxSize, EventCursor->Serial);
#endif

			Context.Bridge.OnEvent(EventDataInfo);

			AuxCollector.Reset();
		}

		return 0;
	}

	////////////////////////////////////////////////////////////////////////////////
	FProtocol6Stage::FProtocol6Stage(FTransport* InTransport)
		: FProtocol5Stage(InTransport)
	{
		EventVersion = 6;
	}

	////////////////////////////////////////////////////////////////////////////////
	void FProtocol6Stage::ExitStage(const FMachineContext& Context)
	{
		// Ensure the transport does not have pending buffers (i.e. event data not yet processed).
		if (!Transport.IsEmpty())
		{
			Context.EmitMessage(EAnalysisMessageSeverity::Warning, TEXT("Transport buffers are not empty at end of analysis (protocol 6)!"));
		}

#if UE_TRACE_ANALYSIS_DEBUG
		Context.Bridge.GetSerial().Value = NextSerial;
		Transport.DebugEnd();
#endif
	}

	FProtocol7Stage::FProtocol7Stage(FTransport* InTransport)
		: FProtocol6Stage(InTransport)
	{
		EventVersion = 7;
	}

	////////////////////////////////////////////////////////////////////////////////
	void FProtocol7Stage::ExitStage(const FMachineContext& Context)
	{
		// Ensure the transport does not have pending buffers (i.e. event data not yet processed).
		if (!Transport.IsEmpty())
		{
			uint64 TotalRemainingDataSize = 0;
			for (int32 ThreadIndex = 0, ThreadCount = Transport.GetThreadCount(); ThreadIndex < ThreadCount; ++ThreadIndex)
			{
				TotalRemainingDataSize += Transport.GetThreadStream(ThreadIndex)->GetRemaining();
			}
			Context.EmitMessagef(
				EAnalysisMessageSeverity::Warning,
				TEXT("Transport buffers are not empty at end of analysis (%llu bytes; protocol 7)!"), TotalRemainingDataSize);
		}

#if UE_TRACE_ANALYSIS_DEBUG
		Context.Bridge.GetSerial().Value = NextSerial;
		Transport.DebugEnd();
#endif
	}

	////////////////////////////////////////////////////////////////////////////////
	void FProtocol7Stage::SetSizeIfKnownEvent(uint32 Uid, uint32& InOutEventSize)
	{
		switch (Uid)
		{
		case EKnownUids::EnterScope_TA:
		case EKnownUids::LeaveScope_TA:
			InOutEventSize = 8;
			break;

		case EKnownUids::EnterScope_TB:
		case EKnownUids::LeaveScope_TB:
			InOutEventSize = 7;
			break;
		};
	}

	////////////////////////////////////////////////////////////////////////////////
	bool FProtocol7Stage::DispatchKnownEvent(const FMachineContext& Context, uint32 Uid, const FEventDesc* Cursor)
	{
		// Maybe this is a "well-known" event that is handled a little different?
		switch (Uid)
		{
		case EKnownUids::EnterScope:
#if UE_TRACE_ANALYSIS_DEBUG
			Context.Bridge.DebugLogEnterScopeEvent(Uid, Cursor->EventSize + Cursor->AuxSize);
#endif
			Context.Bridge.EnterScope();
			return true;

		case EKnownUids::LeaveScope:
#if UE_TRACE_ANALYSIS_DEBUG
			Context.Bridge.DebugLogLeaveScopeEvent(Uid, Cursor->EventSize + Cursor->AuxSize);
#endif
			Context.Bridge.LeaveScope();
			return true;

		case EKnownUids::EnterScope_TA:
		{
			uint64 AbsoluteTimestamp = *(uint64*)Cursor->Data;
#if UE_TRACE_ANALYSIS_DEBUG
			Context.Bridge.DebugLogEnterScopeAEvent(Uid, AbsoluteTimestamp, Cursor->EventSize + Cursor->AuxSize);
#endif
			Context.Bridge.EnterScopeA(AbsoluteTimestamp);
			return true;
		}

		case EKnownUids::LeaveScope_TA:
		{
			uint64 AbsoluteTimestamp = *(uint64*)Cursor->Data;
#if UE_TRACE_ANALYSIS_DEBUG
			Context.Bridge.DebugLogLeaveScopeAEvent(Uid, AbsoluteTimestamp, Cursor->EventSize + Cursor->AuxSize);
#endif
			Context.Bridge.LeaveScopeA(AbsoluteTimestamp);
			return true;
		}

		case EKnownUids::EnterScope_TB:
		{
			uint64 BaseRelativeTimestamp = *(uint64*)(Cursor->Data - 1) >> 8;
#if UE_TRACE_ANALYSIS_DEBUG
			Context.Bridge.DebugLogEnterScopeBEvent(Uid, BaseRelativeTimestamp, Cursor->EventSize + Cursor->AuxSize);
#endif
			Context.Bridge.EnterScopeB(BaseRelativeTimestamp);
			return true;
		}

		case EKnownUids::LeaveScope_TB:
		{
			uint64 BaseRelativeTimestamp = *(uint64*)(Cursor->Data - 1) >> 8;
#if UE_TRACE_ANALYSIS_DEBUG
			Context.Bridge.DebugLogLeaveScopeBEvent(Uid, BaseRelativeTimestamp, Cursor->EventSize + Cursor->AuxSize);
#endif
			Context.Bridge.LeaveScopeB(BaseRelativeTimestamp);
			return true;
		}

		case EKnownUids::AuxData:
		case EKnownUids::AuxDataTerminal:
			return true;

		default:
			return false;
		}
	}
}