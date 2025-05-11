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
			LR"(((?:\[[0-9\.\-:]+?\]\[\s*?[0-9]+?\])?)([0-9a-zA-Z\-\_\z]+?)\: )"
		);
		return Reg::DfaBinaryTableWrapper{std::span(Buffer)};
	}


	LogLineProcessor::LogLineProcessor() {
		processor.SetObserverTable(FastParsing());
		processor.Clear();
	}

	auto LogLineProcessor::Translate(Potato::Reg::ProcessorAcceptRef const& result, std::size_t line_offset)
		-> LogLineIndex
	{
		LogLineIndex line_index;
		line_index.time = result[0];
		line_index.category = result[1];
		line_index.str_offset = result.MainCapture.End();
		line_index.line = {line_offset, line_offset + 1};
		return line_index;
	}

	constexpr std::wstring_view Levels[] = {
		L"Fatal: ",
		L"Error: ",
		L"Warning: ",
		L"Display: ",
		L"Verbose: ",
		L"VeryVerbose: "
	};

	std::optional<LogLine> LogLineProcessor::ConsumeLinedString(std::wstring_view lined_string)
	{
		processor.Clear();
		auto re = Reg::Process(processor, lined_string);
		if (re)
		{
			if (LastIndex.has_value())
			{
				finished_string = temporary_buffer;
				temporary_buffer = lined_string;

				std::size_t LastLineCount = LastIndex->line.End();
				LogLine ReLine;
				ReLine.time = std::wstring_view{ LastIndex->time.Slice(std::wstring_view{finished_string}) };
				ReLine.category = std::wstring_view{ LastIndex->category.Slice(std::wstring_view{finished_string}) };
				ReLine.level = L"Log";
				ReLine.str = std::wstring_view{ finished_string.substr(LastIndex->str_offset)};
				for (auto ite : Levels)
				{
					if (ReLine.str.starts_with(ite))
					{
						ReLine.level = ReLine.str.substr(0, ite.size() - 2);
						ReLine.str = ReLine.str.substr(ite.size());
						break;
					}
				}
				ReLine.line = LastIndex->line;
				ReLine.total_str = std::wstring_view{ finished_string };
				LastIndex = Translate(re, LastLineCount);
				return ReLine;
			}
			else {
				LastIndex = Translate(re, 1);
				temporary_buffer = lined_string;
			}
		}
		else {
			if (LastIndex.has_value())
			{
				LastIndex->line.BackwardEnd(1);
				temporary_buffer.append(lined_string);
			}
			else {
				LastIndex = {
					{0, 0},
					{0, 0},
					{1, 2}
				};
				temporary_buffer = lined_string;
			}
		}
		return std::nullopt;
	}

	std::optional<LogLine> LogLineProcessor::End()
	{
		if (LastIndex.has_value())
		{
			finished_string = temporary_buffer;
			temporary_buffer.clear();

			std::size_t LastLineCount = LastIndex->line.End();
			LogLine ReLine;
			ReLine.time = std::wstring_view{ LastIndex->time.Slice(std::wstring_view{finished_string}) };
			ReLine.category = std::wstring_view{ LastIndex->category.Slice(std::wstring_view{finished_string}) };
			ReLine.level = L"Log";
			ReLine.str = std::wstring_view{ finished_string.substr(LastIndex->str_offset) };
			for (auto ite : Levels)
			{
				if (ReLine.str.starts_with(ite))
				{
					ReLine.level = ReLine.str.substr(0, ite.size() - 2);
					ReLine.str = ReLine.str.substr(ite.size());
					break;
				}
			}
			ReLine.line = LastIndex->line;
			ReLine.total_str = std::wstring_view{ finished_string };
			LastIndex.reset();
			return ReLine;
		}
		return std::nullopt;
	}

	void LogLineProcessor::Clear()
	{
		processor.Clear();
		temporary_buffer.clear();
		finished_string.clear();
		LastIndex.reset();
	}

	Reg::DfaBinaryTableWrapper TimeParsing() {
		static auto Buffer = Reg::CreateDfaBinaryTable(
			Reg::Dfa::FormatE::HeadMatch,
			LR"(.*?([0-9]+))"
		);
		return Reg::DfaBinaryTableWrapper{std::span(Buffer)};
	}

	std::optional<LogTime> LogLineProcessor::GetTime(LogLine Line)
	{
		LogTime Result;
		Reg::DfaProcessor Pro;
		Pro.SetObserverTable(TimeParsing());
		std::wstring_view Str = Line.time;
		std::array<std::size_t, 8> Buffer;
		for (std::size_t I = 0; I < 8; ++I)
		{
			Pro.Clear();
			auto Re = Reg::Process(Pro, Str);
			if (Re)
			{
				std::wstring_view Cur = Re[0].Slice(Str);
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