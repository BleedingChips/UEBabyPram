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
			u8R"((?:\[([0-9\.\-:]+?)\]\[\s*?([0-9]+?)\])?([0-9a-zA-Z\-\_\z]+?)\: )"
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
		line_index.frame_count = result[1];
		line_index.category = result[2];
		line_index.str_offset = result.MainCapture.End();
		line_index.line = {line_offset, line_offset + 1};
		return line_index;
	}

	constexpr std::u8string_view Levels[] = {
		u8"Fatal: ",
		u8"Error: ",
		u8"Warning: ",
		u8"Display: ",
		u8"Verbose: ",
		u8"VeryVerbose: "
	};

	std::optional<LogLine> LogLineProcessor::ConsumeLinedString(std::u8string_view lined_string)
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
				ReLine.time = std::u8string_view{ LastIndex->time.Slice(std::u8string_view{finished_string}) };
				ReLine.frame_count = std::u8string_view{ LastIndex->frame_count.Slice(std::u8string_view{finished_string}) };
				ReLine.category = std::u8string_view{ LastIndex->category.Slice(std::u8string_view{finished_string}) };
				ReLine.level = u8"Log";
				ReLine.str = std::u8string_view{ finished_string }.substr(LastIndex->str_offset);
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
				ReLine.total_str = std::u8string_view{ finished_string };
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
			ReLine.time = std::u8string_view{ LastIndex->time.Slice(std::u8string_view{finished_string}) };
			ReLine.category = std::u8string_view{ LastIndex->category.Slice(std::u8string_view{finished_string}) };
			ReLine.level = u8"Log";
			ReLine.str = std::u8string_view{ finished_string.substr(LastIndex->str_offset) };
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
			ReLine.total_str = std::u8string_view{ finished_string };
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

	std::optional<std::size_t> LogLine::GetFrameCount() const
	{
		if (!frame_count.empty())
		{
			std::size_t number = 0;
			if (Format::DirectScan(frame_count, number))
			{
				return number;
			}
		}
		return std::nullopt;
	}

	std::optional<LogLine::TimeT> LogLine::GetSystemClockTimePoint(std::int32_t year, std::size_t month, std::size_t day, std::size_t hour, std::size_t min, std::size_t second, std::size_t milisecond)
	{
		if (
			year >= 1900
			&& month > 1 && month <= 12
			&& day > 0 && day <= 31
			&& hour < 24
			&& min < 60
			&& second < 60
			&& milisecond < 1000
			)
		{
			std::tm time;
			time.tm_year = year - 1900;
			time.tm_mon = month - 1;
			time.tm_mday = day;
			time.tm_hour = hour;
			time.tm_min = min;
			time.tm_sec = second;
			time.tm_wday = 0;
			time.tm_isdst = -1;

			std::time_t local_time_t = std::mktime(&time);

			auto system_time_point = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::from_time_t(local_time_t)) + std::chrono::milliseconds{ milisecond };
			return system_time_point;
		}
		assert(false);
		return std::nullopt;
	}

	std::optional<LogLine::TimeT> LogLine::GetSystemClockTimePoint() const
	{
		if (!time.empty())
		{
			Reg::DfaProcessor Pro;
			Pro.SetObserverTable(TimeParsing());
			std::u8string_view ite_time = time;
			std::array<std::size_t, 7> Buffer;
			for (std::size_t I = 0; I < 7; ++I)
			{
				Pro.Clear();
				auto Re = Reg::Process(Pro, ite_time);
				if (Re)
				{
					std::u8string_view cur = Re[0].Slice(ite_time);
					std::size_t Index = 0;
					Format::DirectScan(cur, Index);
					Buffer[I] = Index;
					ite_time = ite_time.substr(Re.MainCapture.End());
				}
				else {
					return std::nullopt;;
				}
			}

			return LogLine::GetSystemClockTimePoint(
				static_cast<std::int32_t>(Buffer[0]),
				Buffer[1],
				Buffer[2],
				Buffer[3],
				Buffer[4],
				Buffer[5],
				Buffer[6]
			);
		}
		return std::nullopt;
	}
}