module;

#include <cassert>

export module UEBabyPramLogFilter;

import std;
import Potato;


export namespace UEBabyPram::LogFilter
{

	template<typename Type = std::size_t>
	using IndexSpan = Potato::Misc::IndexSpan<Type>;

	struct LogLine
	{
		using TimeT = std::chrono::system_clock::time_point;
		std::string_view time;
		std::string_view frame_count;
		std::string_view category;
		std::string_view level;
		std::string_view str;
		std::string_view total_str;
		IndexSpan<> line;
		std::optional<std::size_t> GetFrameCount() const;
		std::optional<TimeT> GetTimePoint() const;
	};

	struct LogLineProcessor
	{
		std::optional<LogLine> ConsumeLinedString(std::string_view lined_string);
		std::optional<LogLine> End();
		void Clear();
		LogLineProcessor();
	protected:
		struct LogLineIndex
		{
			IndexSpan<> time;
			IndexSpan<> frame_count;
			IndexSpan<> category;
			IndexSpan<> line;
			std::size_t str_offset;
		};
		static LogLineIndex Translate(Potato::Reg::ProcessorAcceptRef const& Re, std::size_t LineOffset);
		Potato::Reg::DfaProcessor processor;
		std::string temporary_buffer;
		std::string finished_string;
		std::optional<LogLineIndex> LastIndex;
	};

	template<typename Func>
	void ForeachLogLine(std::string_view str, Func&& fun) requires(std::is_invocable_v<Func&&, LogLine>)
	{
		LogLineProcessor processor;
		auto ite = str;
		while (!ite.empty())
		{
			auto offset = ite.find('\n');
			if (offset != ite.size())
				offset += 1;
			auto line = processor.ConsumeLinedString(ite.substr(0, offset));
			if (line)
				fun(*line);
			ite = ite.substr(offset);
		}
		auto line = processor.End();
		if (line)
			fun(*line);
	}
}