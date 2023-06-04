module;

#include <cassert>

module UEBabyPram.LogFilter;

import Potato.Format;

namespace UEBabyPram::LogFilter
{

	using namespace Potato;

	Reg::DfaBinaryTableWrapper const& FastParsing() {
		static auto Buffer = Reg::CreateDfaBinaryTable(
			Reg::Dfa::FormatE::HeadMatch,
			UR"(((?:\[[0-9\.\-:]+?\]\[\s*?[0-9]+?\])?)([0-9a-zA-Z\-\_\z]+?)\: )"
		);
		static Reg::DfaBinaryTableWrapper Wrap{Buffer};
		return Wrap;
	}

	LogLineProcessor::LogLineProcessor(std::u8string_view TotalStar) : Sperater(TotalStar) {
		Pro.SetObserverTable(&FastParsing());
		Pro.Clear();
	}

	auto LogLineProcessor::Translate(Potato::Reg::ProcessorAcceptRef const& Re, std::size_t Offset, std::size_t StrSize, std::size_t LineOffset)
		-> LogLineIndex
	{
		LogLineIndex Result;
		Result.Time = Re[0].WholeOffset(Offset);
		Result.Category = Re[1].WholeOffset(Offset);
		Result.Str = Misc::IndexSpan<>{Re.MainCapture.End(), StrSize}.WholeOffset(Offset);
		Result.LineIndex = {LineOffset, LineOffset + 1};
		return Result;
	}

	std::optional<LogLine> LogLineProcessor::Consume()
	{
		while (Sperater)
		{
			std::size_t CurPos = Sperater.GetItePosition();
			auto LineSpe = Sperater.Consume();
			Pro.Clear();
			auto Re = Reg::Process(Pro, LineSpe.Str);
			if (Re)
			{
				if (LastIndex.has_value())
				{
					std::size_t LastLineCount = LastIndex->LineIndex.End();
					auto TotalStr = Sperater.GetTotalStr();
					LogLine ReLine;
					ReLine.Time = std::u8string_view{LastIndex->Time.Slice(TotalStr)};
					ReLine.Cagetory = std::u8string_view{ LastIndex->Category.Slice(TotalStr)};
					ReLine.Str = std::u8string_view{ LastIndex->Str.Slice(TotalStr) };
					ReLine.LineIndex = LastIndex->LineIndex;
					IndexSpan<> New{LastIndex->Time.Begin(), LastIndex->Str.End() };
					ReLine.TotalStr = std::u8string_view{New.Slice(TotalStr)};
					LastIndex = Translate(Re, CurPos, LineSpe.Str.size(), LastLineCount + 1);
					return ReLine;
				}
				else {
					assert(CurPos == 0);
					LastIndex = Translate(Re, CurPos, LineSpe.Str.size(), 0);
				}
			}
			else {
				if (LastIndex.has_value())
				{
					LastIndex->LineIndex.BackwardEnd(1);
					LastIndex->Str.BackwardEnd(LineSpe.Str.size());
				}
				else {
					LastIndex = {
						{0, 0},
						{0, 0},
						{0, LineSpe.Str.size()},
						{0, 1}
					};
				}
			}
		}
		if (LastIndex.has_value())
		{
			LogLine ReLine;
			auto TotalStr = Sperater.GetTotalStr();
			ReLine.Time = std::u8string_view{LastIndex->Time.Slice(TotalStr)};
			ReLine.Cagetory = std::u8string_view{ LastIndex->Category.Slice(TotalStr) };
			ReLine.Str = std::u8string_view{ LastIndex->Str.Slice(TotalStr) };
			ReLine.LineIndex = LastIndex->LineIndex; 
			IndexSpan<> New{ LastIndex->Time.Begin(), LastIndex->Str.End() };
			ReLine.TotalStr = std::u8string_view{ New.Slice(TotalStr) };
			LastIndex = {};
			return ReLine;
		}
		return {};
	}

	void LogLineProcessor::Clear()
	{
		LastIndex = {};
		Pro.Clear();
		Sperater.Clear();
	}

	Reg::DfaBinaryTableWrapper const& TimeParsing() {
		static auto Buffer = Reg::CreateDfaBinaryTable(
			Reg::Dfa::FormatE::HeadMatch,
			u8R"(.*?([0-9]+))"
		);
		static Reg::DfaBinaryTableWrapper Wrap{Buffer};
		return Wrap;
	}

	std::optional<LogTime> LogLineProcessor::GetTime(LogLine Line)
	{
		LogTime Result;
		Reg::DfaProcessor Pro;
		Pro.SetObserverTable(&TimeParsing());
		std::u8string_view Str = Line.Time;
		std::array<std::size_t, 8> Buffer;
		for (std::size_t I = 0; I < 8; ++I)
		{
			Pro.Clear();
			auto Re = Reg::Process(Pro, Str);
			if (Re)
			{
				std::u8string_view Cur = Re[0].Slice(Str);
				std::size_t Index = 0;
				Format::DirectScan(Cur, Index);
				Buffer[I] = Index;
				Str = Str.substr(Re.MainCapture.End());
			}
			else {
				return {};
			}
		}

		Result.Year = Buffer[0];
		Result.Month = Buffer[1];
		Result.Day = Buffer[2];
		Result.Hour = Buffer[3];
		Result.Min = Buffer[4];
		Result.Sec = Buffer[5];
		Result.MSec = Buffer[6];
		Result.FrameCount = Buffer[7];
		return Result;
	}
	
}