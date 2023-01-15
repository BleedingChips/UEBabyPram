#include "UEBabyPram/UEBabyPramLogFilter.h"

namespace UEBabyPram::LogFilter
{

	using namespace Potato;

	Reg::TableWrapper FastParsing() {
		static auto Buffer = Reg::TableWrapper::Create(
			u8R"(((?:\[[0-9\.\-:]+?\]\[\s*?[0-9]+?\])?)([0-9a-zA-Z\-\_\z]+?)\: )"
		);
		return Reg::TableWrapper{Buffer};
	}

	LogLineProcessor::LogLineProcessor(std::u8string_view TotalStar) : Sperater(TotalStar), Pro(FastParsing()) {}

	auto LogLineProcessor::Translate(Reg::HeadMatchProcessor::Result const& Re, std::size_t Offset, std::size_t StrSize, std::size_t LineOffset)
		-> LogLineIndex
	{
		LogLineIndex Result;
		auto Wra = Re.GetCaptureWrapper();
		Wra = Wra.GetTopSubCapture();
		Result.Time = Wra.GetCapture();
		Result.Time.Offset += Offset;
		Wra = Wra.GetNextCapture();
		Result.Category = Wra.GetCapture();
		Result.Category.Offset += Offset;
		auto MainCapture = Re.GetCaptureWrapper().GetCapture();
		Result.Str = { MainCapture.End() + Offset, StrSize - MainCapture.End()};
		Result.LineIndex = {LineOffset, 1};
		return Result;
	}

	std::optional<LogLine> LogLineProcessor::Consume()
	{
		while (Sperater)
		{
			std::size_t CurPos = Sperater.GetItePosition();
			auto LineSpe = Sperater.Consume();
			Pro.Clear();
			auto Re = Reg::HeadMatch(Pro, LineSpe.Str);
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
					IndexSpan<> New{LastIndex->Time.Begin(), LastIndex->Str.End() - LastIndex->Time.Begin() };
					ReLine.TotalStr = std::u8string_view{New.Slice(TotalStr)};
					LastIndex = Translate(*Re, CurPos, LineSpe.Str.size(), LastLineCount + 1);
					return ReLine;
				}
				else {
					assert(CurPos == 0);
					LastIndex = Translate(*Re, CurPos, LineSpe.Str.size(), 0);
				}
			}
			else {
				if (LastIndex.has_value())
				{
					LastIndex->LineIndex.Length += 1;
					LastIndex->Str.Length += LineSpe.Str.size();
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
			IndexSpan<> New{ LastIndex->Time.Begin(), LastIndex->Str.End() - LastIndex->Time.Begin() };
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

	Reg::TableWrapper TimeParsing() {
		static auto Buffer =
			Reg::TableWrapper::Create(
			u8R"(.*?([0-9]+))"
		);
		return Reg::TableWrapper{Buffer};
	}

	std::optional<LogTime> LogLineProcessor::GetTime(LogLine Line)
	{
		LogTime Result;
		Reg::HeadMatchProcessor Pro(TimeParsing());
		std::u8string_view Str = Line.Time;
		std::array<std::size_t, 8> Buffer;
		for (std::size_t I = 0; I < 8; ++I)
		{
			Pro.Clear();
			auto Re = Reg::HeadMatch(Pro, Str);
			if (Re)
			{
				std::u8string_view Cur = Re->GetCaptureWrapper().GetTopSubCapture().Slice(Str);
				std::size_t Index = 0;
				StrFormat::DirectScan(Cur, Index);
				Buffer[I] = Index;
				Str = Str.substr(Re->GetCaptureWrapper().GetCapture().End());
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