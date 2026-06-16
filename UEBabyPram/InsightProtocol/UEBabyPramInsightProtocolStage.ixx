module;
#include "Protocol7.h"
export module UEBabyPramInsightProtocolStage;

import std;
import Potato;
import UEBabyPramInsightDefine;


export namespace UEBabyPram::InsightParser
{
	class FAnalysisMachine
	{
	public:
		enum class EStatus
		{
			Error,
			Abort,
			NotEnoughData,
			EndOfStream,
			Continue,
			Sync,
		};

		struct FMachineContext
		{
			FAnalysisMachine& Machine;
			FAnalysisBridge& Bridge;
			FMessageDelegate& OnMessage;

			inline void EmitMessage(EAnalysisMessageSeverity Severity, FStringView Message) const
			{
				const bool _ = OnMessage.ExecuteIfBound(Severity, Message);
			}

			template<typename FormatType, typename... Types>
			inline void EmitMessagef(EAnalysisMessageSeverity Severity, const FormatType& Format, Types... Args) const
			{
				TStringBuilder<128> FormattedMessage;
				FormattedMessage.Appendf(Format, Forward<Types>(Args)...);
				EmitMessage(Severity, FormattedMessage.ToView());
			}
		};

		class FStage
		{
		public:
			typedef FAnalysisMachine::FMachineContext	FMachineContext;
			typedef FAnalysisMachine::EStatus			EStatus;

			virtual				~FStage() {}
			virtual EStatus		OnData(FStreamReader& Reader, const FMachineContext& Context) = 0;
			virtual void		EnterStage(const FMachineContext& Context) {};
			virtual void		ExitStage(const FMachineContext& Context) {};
		};

		FAnalysisMachine(FAnalysisBridge& InBridge, FMessageDelegate&& InMessage);
		~FAnalysisMachine();
		EStatus					OnData(FStreamReader& Reader);
		void					Transition();
		template <class StageType, typename... ArgsType>
		StageType* QueueStage(ArgsType... Args);

	private:
		void					CleanUp();
		FAnalysisBridge& Bridge;
		FStage* ActiveStage = nullptr;
		TArray<FStage*>			StageQueue;
		TArray<FStage*>			DeadStages;
		FMessageDelegate		OnMessage;
	};

	class FProtocol5Stage
		: public FAnalysisMachine::FStage
	{
	public:
		FProtocol5Stage(FTransport* InTransport);
		virtual				~FProtocol5Stage();

		virtual void		ExitStage(const FMachineContext& Context) override;

	protected:
		struct alignas(16) FEventDesc
		{
			union
			{
				struct
				{
					int32		Serial;
					uint16		Uid : 14;
					uint16		bTwoByteUid : 1;
					uint16		bHasAux : 1;
					uint16		AuxKey;
				};
				uint64			Meta = 0;
			};
			union
			{
				uint32			GapLength;
				const uint8* Data = nullptr;
			};
#if UE_TRACE_ANALYSIS_DEBUG
			uint32				EventSize = 0;
			uint32				AuxSize = 0;
			uint64				Reserved = 0;
#endif
		};
#if UE_TRACE_ANALYSIS_DEBUG
		static_assert(sizeof(FEventDesc) == 32, "");
#else
		static_assert(sizeof(FEventDesc) == 16, "");
#endif

		struct alignas(16) FEventDescStream
		{
			uint32					ThreadId;
			uint32					TransportIndex;
			union
			{
				uint32				ContainerIndex;
				const FEventDesc* EventDescs;
			};

			enum { GapThreadId = ~0u };
		};
		static_assert(sizeof(FEventDescStream) == 16, "");

		struct FSerialDistancePredicate
		{
			bool operator () (const FEventDescStream& Lhs, const FEventDescStream& Rhs) const
			{
				// Provided that less than approximately "SerialRange * BytesPerSerial"
				// is buffered there should never be more that "SerialRange / 2" serial
				// numbers. Thus if the distance between any two serial numbers is larger
				// than half the serial space, they have wrapped.
				uint32 Ld = Lhs.EventDescs->Serial - Origin;
				uint32 Rd = Rhs.EventDescs->Serial - Origin;
				return Ld < Rd;
			};
			uint32 Origin;
		};

		enum ESerial : int32
		{
			Bits = 24,
			Mask = (1 << Bits) - 1,
			Range = 1 << Bits,
			Ignored = Range << 2, // far away so proper serials always compare less-than
			Terminal,
		};

		using EventDescArray = TArray<FEventDesc>;
		using EKnownUids = Protocol5::EKnownEventUids;

		virtual EStatus			OnData(FStreamReader& Reader, const FMachineContext& Context) override;
		EStatus					OnDataNewEvents(const FMachineContext& Context);
		EStatus					OnDataImportant(const FMachineContext& Context);
		EStatus					OnDataNormal(const FMachineContext& Context);
		int32					ParseImportantEvents(FStreamReader& Reader, EventDescArray& OutEventDescs, const FMachineContext& Context);
		int32					ParseEvents(FStreamReader& Reader, EventDescArray& OutEventDescs, const FMachineContext& Context);
		int32					ParseEventsWithAux(FStreamReader& Reader, EventDescArray& OutEventDescs, const FMachineContext& Context);
		int32					ParseEvent(FStreamReader& Reader, FEventDesc& OutEventDesc, const FMachineContext& Context);
		virtual void			SetSizeIfKnownEvent(uint32 Uid, uint32& InOutEventSize);
		virtual bool			DispatchKnownEvent(const FMachineContext& Context, uint32 Uid, const FEventDesc* Cursor);
		int32					DispatchNormalEvents(const FMachineContext& Context, TArray<FEventDescStream>& EventDescHeap);
		int32					DispatchEvents(const FMachineContext& Context, TArray<FEventDescStream>& EventDescHeap);
		int32					DispatchEvents(const FMachineContext& Context, const FEventDesc* EventDesc, uint32 Count);
		void					DetectSerialGaps(TArray<FEventDescStream>& EventDescHeap);
		template <typename Callback>
		void					ForEachSerialGap(const TArray<FEventDescStream>& EventDescHeap, Callback&& InCallback);
#if UE_TRACE_ANALYSIS_DEBUG
		void					PrintParsedEvent(int EventIndex, const FEventDesc& EventDesc, int32 Size);
#endif // UE_TRACE_ANALYSIS_DEBUG

		FTypeRegistry			TypeRegistry;
		FTransport* BaseTransport;
		FTidPacketTransport& Transport;
		EventDescArray			EventDescs;
		EventDescArray			SerialGaps;
		uint32					NextSerial = ~0u;
		uint32					OldNextSerial = ~0u;
		uint32					NextSerialWaitCount = 0;
		uint32					SyncCount;
		uint32					EventVersion = 4; //Protocol version 5 uses the event version from protocol 4
		bool					bSkipSerialError = false;
		bool					bSkipSerial = false;
	};

	class FProtocol6Stage
		: public FProtocol5Stage
	{
	public:
		FProtocol6Stage(FTransport* InTransport);

		virtual void		ExitStage(const FMachineContext& Context) override;
	};

	class FProtocol7Stage
		: public FProtocol6Stage
	{
	public:
		FProtocol7Stage(FTransport* InTransport);

		virtual void		ExitStage(const FMachineContext& Context) override;
		virtual void		SetSizeIfKnownEvent(uint32 Uid, uint32& InOutEventSize) override;
		virtual bool		DispatchKnownEvent(const FMachineContext& Context, uint32 Uid, const FEventDesc* Cursor) override;

	protected:
		using EKnownUids = Protocol7::EKnownEventUids;
	};
}