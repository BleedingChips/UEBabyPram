module;

#include <cassert>

export module UEBabyPramLogFilter;

import std;
import Potato;


export namespace UEBabyPram::LogFilter
{

	template<typename Type = std::size_t>
	using IndexSpan = Potato::Misc::IndexSpan<Type>;

	struct LineProperty
	{
		std::u8string_view time;
		std::u8string_view frame_count;
		std::u8string_view category;
		std::u8string_view level;
	};

	std::optional<LineProperty> GetLineProperty(std::u8string_view string);




	struct LogLine
	{
		using TimeT = std::chrono::system_clock::time_point;
		std::u8string_view time;
		std::u8string_view frame_count;
		std::u8string_view category;
		std::u8string_view level;
		std::u8string_view str;
		std::u8string_view total_str;
		IndexSpan<> line;
		std::optional<std::size_t> GetFrameCount() const;
		static std::optional<TimeT> GetSystemClockTimePoint(std::int32_t year, std::size_t month, std::size_t day, std::size_t hour, std::size_t min, std::size_t second, std::size_t milisecond);
		std::optional<TimeT> GetSystemClockTimePoint() const;
	};


	struct LogLineConsumer
	{
		std::optional<LogLine> GetLine();
		LogLineConsumer(std::u8string_view total_str = {});
		void Reset(std::u8string_view str = {}, bool reset_context = true);
		operator bool() const { return total_string_index.Size() != 0 || last_index.has_value(); }
	protected:
		std::u8string_view total_string;
		Potato::Misc::IndexSpan<> total_string_index;
		std::size_t last_string_offset = 0;
		struct LogLineIndex
		{
			IndexSpan<> time;
			IndexSpan<> frame_count;
			IndexSpan<> category;
			IndexSpan<> line;
			std::size_t str_offset;
		};
		std::optional<LogLineIndex> last_index;
		std::size_t line_count = 1;
		static LogLineIndex Translate(Potato::Reg::ProcessorAcceptRef const& Re, std::size_t LineOffset, std::size_t string_offset = 0);
		Potato::Reg::DfaProcessor processor;
	};

	template<typename Func>
	void ForeachLogLine(std::u8string_view str, Func&& fun) requires(std::is_invocable_v<Func&&, LogLine>)
	{
		LogLineConsumer consumer{str};
		while (true)
		{
			auto cur = consumer.GetLine();
			if (cur)
			{
				fun(*cur);
			}
			else {
				return;
			}
		}
	}
}