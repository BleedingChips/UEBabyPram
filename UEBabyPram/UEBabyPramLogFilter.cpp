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

	auto LogLineConsumer::Translate(Potato::Reg::ProcessorAcceptRef const& result, std::size_t line_offset, std::size_t string_offset)
		-> LogLineIndex
	{
		LogLineIndex line_index;
		line_index.time = result[0].WholeOffset(string_offset);
		line_index.frame_count = result[1].WholeOffset(string_offset);
		line_index.category = result[2].WholeOffset(string_offset);
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

	void LogLineConsumer::Reset(std::u8string_view str, bool reset_context)
	{
		total_string = str;
		total_string_index = {0, total_string.size()};
		last_string_offset = 0;
		last_index.reset();
		processor.Clear();
		line_count = 1;
	}

	LogLineConsumer::LogLineConsumer(std::u8string_view total_str) 
	{
		processor.SetObserverTable(FastParsing());
		processor.Clear(); 
		Reset(total_str);
	}

	std::optional<LogLine> LogLineConsumer::GetLine()
	{
		while (total_string_index.Size() > 0)
		{
			auto current_str = total_string_index.Slice(total_string);
			std::u8string_view lined_string = current_str;
			auto fined_end = lined_string.find(u8'\n');
			auto current_string_offset = total_string_index.Begin();
			if (fined_end != decltype(lined_string)::npos)
			{
				lined_string = lined_string.substr(0, fined_end);
				total_string_index = total_string_index.SubIndex(
					lined_string.size() + 1
				);
			}
			else {
				total_string_index = total_string_index.SubIndex(
					lined_string.size()
				);
			}
			auto current_line = line_count++;
			
			processor.Clear();
			auto re = Reg::Process(processor, lined_string);

			if (re)
			{
				auto index_info = Translate(re, current_line, current_string_offset);
				if (last_index.has_value())
				{
					std::size_t LastLineCount = last_index->line.End();
					LogLine ReLine;
					ReLine.time = last_index->time.Slice(total_string);
					ReLine.frame_count = last_index->frame_count.Slice(total_string);
					ReLine.category = last_index->category.Slice(total_string);
					ReLine.level = u8"Log";
					ReLine.str = Potato::Misc::IndexSpan<>{ last_string_offset + last_index->str_offset, current_string_offset - 1 }.Slice(total_string);
					for (auto ite : Levels)
					{
						if (ReLine.str.starts_with(ite))
						{
							ReLine.level = ReLine.str.substr(0, ite.size() - 2);
							ReLine.str = ReLine.str.substr(ite.size());
							break;
						}
					}
					ReLine.line = last_index->line;
					ReLine.total_str = Potato::Misc::IndexSpan<>{ last_string_offset, current_string_offset - 1 }.Slice(total_string);
					last_index = index_info;
					last_string_offset = current_string_offset;
					return ReLine;
				}
				else {
					last_index = index_info;
					last_string_offset = current_string_offset;
					continue;
				}
			}
			else {
				if (last_index.has_value())
				{
					last_index->line.BackwardEnd(1);
				}
				else {
					last_index = {
						{0, 0},
						{0, 0},
						{1, 2}
					};
					last_string_offset = current_string_offset;
				}
			}
		}

		if (last_index.has_value())
		{
			std::size_t LastLineCount = last_index->line.End();
			LogLine ReLine;
			ReLine.time = last_index->time.Slice(total_string);
			ReLine.frame_count = last_index->frame_count.Slice(total_string);
			ReLine.category = last_index->category.Slice(total_string);
			ReLine.level = u8"Log";
			ReLine.str = Potato::Misc::IndexSpan<>{ last_string_offset + last_index->str_offset, total_string_index.End() }.Slice(total_string);
			for (auto ite : Levels)
			{
				if (ReLine.str.starts_with(ite))
				{
					ReLine.level = ReLine.str.substr(0, ite.size() - 2);
					ReLine.str = ReLine.str.substr(ite.size());
					break;
				}
			}
			ReLine.line = last_index->line;
			ReLine.total_str = total_string.substr(last_string_offset);
			if (!ReLine.str.empty() && *ReLine.str.rbegin() == u8'\n')
			{
				ReLine.str = ReLine.str.substr(0, ReLine.str.size() - 1);
				ReLine.total_str = ReLine.total_str.substr(0, ReLine.total_str.size() - 1);
			}
			last_string_offset = total_string.size();
			last_index.reset();
			return ReLine;
		}
		return std::nullopt;
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
			&& month >= 1 && month <= 12
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