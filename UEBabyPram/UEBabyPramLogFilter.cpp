module;

#include <cassert>

module UEBabyPramLogFilter;

import std;
import PotatoFormat;

namespace UEBabyPram::LogFilter
{

	using namespace Potato;

	Reg::DfaBinaryTableWrapper FastParsing() {
		static auto Buffer = Reg::CreateDfaBinaryTable(
			Reg::Dfa::FormatE::HeadMatch,
			UR"(((?:\[[0-9\.\-:]+?\]\[\s*?[0-9]+?\])?)([0-9a-zA-Z\-\_\z]+?)\: )"
		);
		return Reg::DfaBinaryTableWrapper{std::span(Buffer)};
	}

	LogLineProcessor::LogLineProcessor(std::u8string_view TotalStar) : TotalStr(TotalStar) {
		Pro.SetObserverTable(FastParsing());
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

	constexpr std::u8string_view Levels[] = {
		u8"Fatal: ",
		u8"Error: ",
		u8"Warning: ",
		u8"Display: ",
		u8"Verbose: ",
		u8"VeryVerbose: "
	};

	std::optional<LogLine> LogLineProcessor::Consume()
	{
		while (Offset < TotalStr.size())
		{
			std::size_t CurPos = Offset;
			auto Result = Document::StringSplitter::SplitLine(TotalStr, Offset);
			auto LineSpe = Result.SliceWithoutLine(TotalStr);
			Offset = Result.GetLineEnd();
			Pro.Clear();
			auto Re = Reg::Process(Pro, LineSpe);
			if (Re)
			{
				if (LastIndex.has_value())
				{
					std::size_t LastLineCount = LastIndex->LineIndex.End();
					LogLine ReLine;
					ReLine.Time = std::u8string_view{LastIndex->Time.Slice(TotalStr)};
					ReLine.Cagetory = std::u8string_view{ LastIndex->Category.Slice(TotalStr)};
					ReLine.Level = u8"Log";
					ReLine.Str = std::u8string_view{ LastIndex->Str.Slice(TotalStr) };
					for(auto ite : Levels)
					{
						if(ReLine.Str.starts_with(ite))
						{
							ReLine.Level = ReLine.Str.substr(0, ite.size() - 2);
							ReLine.Str = ReLine.Str.substr(ite.size());
							break;
						}
					}
					ReLine.LineIndex = LastIndex->LineIndex;
					IndexSpan<> New{LastIndex->Time.Begin(), LastIndex->Str.End() };
					ReLine.TotalStr = std::u8string_view{New.Slice(TotalStr)};
					LastIndex = Translate(Re, CurPos, Result.SizeWithoutLine(), LastLineCount);
					return ReLine;
				}
				else {
					assert(CurPos == 0);
					LastIndex = Translate(Re, CurPos, Result.SizeWithoutLine(), 1);
				}
			}
			else {
				if (LastIndex.has_value())
				{
					LastIndex->LineIndex.BackwardEnd(1);
					LastIndex->Str.BackwardEnd(Result.SizeWithoutLine());
				}
				else {
					LastIndex = {
						{0, 0},
						{0, 0},
						{0, Result.SizeWithoutLine()},
						{1, 2}
					};
				}
			}
		}
		if (LastIndex.has_value())
		{
			LogLine ReLine;
			ReLine.Time = std::u8string_view{LastIndex->Time.Slice(TotalStr)};
			ReLine.Cagetory = std::u8string_view{ LastIndex->Category.Slice(TotalStr) };
			ReLine.Level = u8"Log";
			ReLine.Str = std::u8string_view{ LastIndex->Str.Slice(TotalStr) };
			for(auto ite : Levels)
			{
				if(ReLine.Str.starts_with(ite))
				{
					ReLine.Level = ReLine.Str.substr(0, ite.size() - 2);
					ReLine.Str = ReLine.Str.substr(ite.size());
					break;
				}
			}
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
		Offset = 0;
	}

	Reg::DfaBinaryTableWrapper TimeParsing() {
		static auto Buffer = Reg::CreateDfaBinaryTable(
			Reg::Dfa::FormatE::HeadMatch,
			u8R"(.*?([0-9]+))"
		);
		return Reg::DfaBinaryTableWrapper{std::span(Buffer)};
	}

	std::optional<LogTime> LogLineProcessor::GetTime(LogLine Line)
	{
		LogTime Result;
		Reg::DfaProcessor Pro;
		Pro.SetObserverTable(TimeParsing());
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