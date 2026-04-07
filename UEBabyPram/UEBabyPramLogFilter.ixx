module;

#include <cassert>

export module UEBabyPramLogFilter;

import std;
import Potato;


export namespace UEBabyPram::LogFilter
{

	template<typename Type = std::size_t>
	using IndexSpan = Potato::Misc::IndexSpan<Type>;

	struct TimeStringView
	{
		std::u8string_view year;
		std::u8string_view month;
		std::u8string_view day;
		std::u8string_view hour;
		std::u8string_view minute;
		std::u8string_view second;
		std::u8string_view millisecond;
	};

	struct LineProperty
	{
		TimeStringView time;
		std::u8string_view frame_count;
		std::u8string_view category;
		std::u8string_view level;
	};

	struct LinePropertyResult
	{
		LineProperty property;
		std::size_t offset = 0;
		operator bool() const { return offset != 0; }
	};

	LinePropertyResult GetLineProperty(std::u8string_view string);

	struct LogLine
	{
		using TimeT = std::chrono::system_clock::time_point;
		LineProperty property;
		std::u8string_view str;
		std::u8string_view total_str;
		IndexSpan<> line;
		std::optional<std::size_t> GetFrameCount() const;
		static std::optional<TimeT> GetSystemClockTimePoint(std::int32_t year, std::size_t month, std::size_t day, std::size_t hour, std::size_t min, std::size_t second, std::size_t milisecond);
		std::optional<TimeT> GetSystemClockTimePoint() const;
	};

	struct LineContext
	{
		std::optional<LinePropertyResult> property;
		std::size_t property_line = 0;
		std::size_t property_line_offset = 0;

		std::size_t total_line = 0;
		std::size_t next_line_offset = 0;
	};

	std::optional<LogLine> GetLogLine(std::u8string_view log, LineContext& context);

	template<typename Func>
	void ForeachLogLine(std::u8string_view str, Func&& fun) requires(std::is_invocable_r_v<void, Func&&, LogLine>)
	{
		LineContext context;
		while (true)
		{
			auto log_line = GetLogLine(str, context);
			if (log_line.has_value())
			{
				fun(*log_line);
			}
			else {
				return;
			}
		}
		return;
	}

	template<typename Func>
	std::u8string_view ForeachLogLine(std::u8string_view str, Func&& fun) requires(std::is_invocable_r_v<bool, Func&&, LogLine>)
	{
		LineContext context;
		while (true)
		{
			auto log_line = GetLogLine(str, context);
			if (log_line.has_value() && fun(*log_line))
			{
				continue;
			}
			else {
				return;
			}
		}
		return;
	}
}