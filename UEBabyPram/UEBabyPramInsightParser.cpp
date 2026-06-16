module;
#include <cassert>
#include "InsightProtocol/Protocol7.h"
module UEBabyPramInsightParser;

import Potato;
import UEBabyPramInsightDefine;

namespace UEBabyPram::InsightParser
{
	using namespace Potato;
	using namespace UE::Trace;

	bool MetaDataStage(Potato::Streamer::StreamRandomReader& reader, InsightContext& context, Potato::Log::LogPrinter& printer)
	{
		uint16 meta_data_size = 0;
		auto readed_size = reader.StreamRead(&meta_data_size);
		if (readed_size < sizeof(meta_data_size))
		{
			Log::LogTo<insight_log, Log::LogLevel::Error, u"MetaData size is not enough">(printer);
			return false;
		}
		auto offset = reader.StreamSeek(meta_data_size);
		if (!offset.has_value() || *offset < meta_data_size)
		{
			Log::LogTo<insight_log, Log::LogLevel::Error, u"MetaData size is not enough">(printer);
			return false;
		}
		Log::LogTo<insight_log, Log::LogLevel::Log, u"MetaData size is {}">(printer, meta_data_size);
		return EstablishTransportStage(reader, context, printer);
	}

	bool EstablishTransportStage(Potato::Streamer::StreamRandomReader& reader, InsightContext& context, Potato::Log::LogPrinter& printer)
	{
		struct HeaderT
		{
			uint8 TransportVersion = 0;
			uint8 ProtocolVersion = 0;
		}header;

		auto readed = reader.StreamRead(&header);

		if (readed != sizeof(header))
		{
			Log::LogTo<insight_log, Log::LogLevel::Error, u"Header size is not enough">(printer);
			return false;
		}

		Log::LogTo<insight_log, Log::LogLevel::Display, u"TransportVersion: {}, ProtocolVersion: {}">(
			printer,
			header.TransportVersion,
			header.ProtocolVersion
		);
		std::shared_ptr<FTransport> Transport;
		switch (header.TransportVersion)
		{
		case ETransport::Raw:			Transport = std::make_shared<FTransport>(); break;
		//case ETransport::Packet:		Transport = new FPacketTransport(); break;
		case ETransport::TidPacket:		Transport = std::make_shared<FTidPacketTransport>(); break;
		case ETransport::TidPacketSync:	Transport = std::make_shared<FTidPacketTransportSync>(); break;
		default:
		{
			Log::LogTo<insight_log, Log::LogLevel::Error, u"UnSupport TransportVersion {}">(printer, header.TransportVersion);
			return false;
		}
#if UE_TRACE_ANALYSIS_DEBUG
		Transport->DebugBegin();
#endif

		switch (header.ProtocolVersion)
		{
		case Protocol0::EProtocol::Id:
			Context.Machine.QueueStage<FProtocol0Stage>(Transport);
			Context.Machine.Transition();
			break;

		case Protocol1::EProtocol::Id:
		case Protocol2::EProtocol::Id:
		case Protocol3::EProtocol::Id:
			Context.Machine.QueueStage<FProtocol2Stage>(ProtocolVersion, Transport);
			Context.Machine.Transition();
			break;

		case Protocol4::EProtocol::Id:
			Context.Machine.QueueStage<FProtocol4Stage>(ProtocolVersion, Transport);
			Context.Machine.Transition();
			break;

		case Protocol5::EProtocol::Id:
			Context.Machine.QueueStage<FProtocol5Stage>(Transport);
			Context.Machine.Transition();
			break;

		case Protocol6::EProtocol::Id:
			Context.Machine.QueueStage<FProtocol6Stage>(Transport);
			Context.Machine.Transition();
			break;

		case Protocol7::EProtocol::Id:
			Context.Machine.QueueStage<FProtocol7Stage>(Transport);
			Context.Machine.Transition();
			break;

		default:
		{
			UE_TRACE_ANALYSIS_DEBUG_LOG("Error: Invalid protocol version %u", ProtocolVersion);
			Context.EmitMessagef(
				EAnalysisMessageSeverity::Error,
				TEXT("Unknown protocol version: %u. You may need to recompile this application"),
				ProtocolVersion
			);
			return EStatus::Error;
		}
		}

		UE_TRACE_ANALYSIS_DEBUG_LOG("");

		Reader.Advance(sizeof(*Header));

#if UE_TRACE_ANALYSIS_DEBUG_API
		Context.Bridge.OnVersion(TransportVersion, ProtocolVersion);
#endif // UE_TRACE_ANALYSIS_DEBUG_API

		return EStatus::Continue;
		reader.StreamRead();
		*/
		return true;
	}


	bool ForEachInsight(Potato::Streamer::StreamRandomReader& reader, InsightContext& context, Potato::Log::LogPrinter& printer)
	{
		if (std::endian::native == std::endian::big)
		{
			Log::LogTo<insight_log, Log::LogLevel::Error, u"Big endian traces are currently not supported.">(printer);
			return false;
		}

		std::array<char8_t, 4> temporary;
		reader.StreamRead(temporary.data(), temporary.size());
		if (std::u8string_view{ temporary.data(), temporary.size() } == u8"2CRT")
		{
			if (!MetaDataStage(reader, context, printer))
			{
				return false;
			}
			volatile int i = 0;
		}
		else {
			Log::LogTo<insight_log, Log::LogLevel::Error, u"The file or stream was not recognized as trace stream.">(printer);
		}
		return false;
	}
}