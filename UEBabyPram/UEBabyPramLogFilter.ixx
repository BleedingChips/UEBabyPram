module;

#include <cassert>

export module UEBabyPram.LogFilter;

export import Potato.Encode;
export import Potato.Reg;
export import Potato.Document;

export namespace UEBabyPram::LogFilter
{

	template<typename Type = std::size_t>
	using IndexSpan = Potato::Misc::IndexSpan<Type>;

	struct LogTime
	{
		std::size_t Year = 0;
		std::size_t Month = 0;
		std::size_t Day = 0;
		std::size_t Hour = 0;
		std::size_t Min = 0;
		std::size_t Sec = 0;
		std::size_t MSec = 0;
		std::size_t FrameCount = 0;
	};

	struct LogLine
	{
		std::u8string_view Time;
		std::u8string_view Cagetory;
		std::u8string_view Str;
		std::u8string_view TotalStr;
		IndexSpan<> LineIndex;
	};

	struct LogLineProcessor
	{
		struct Result
		{
			std::optional<LogLine> CaptureLine;
			bool IsEnd;
		};

		std::optional<LogLine> Consume();

		LogLineProcessor(std::u8string_view TotalStr);

		operator bool() const {return Sperater || LastIndex.has_value(); }

		static std::optional<LogTime> GetTime(LogLine InputLine);

		void Clear();

	private:

		struct LogLineIndex
		{
			IndexSpan<> Time;
			IndexSpan<> Category;
			IndexSpan<> Str;
			IndexSpan<> LineIndex;
		};

		static LogLineIndex Translate(Potato::Reg::HeadMatchProcessor::Result const& Re, std::size_t Offset, std::size_t StrSize, std::size_t LineOffset);

		std::optional<LogLineIndex> LastIndex;
		Potato::Reg::HeadMatchProcessor Pro;
		Potato::Document::LineSperater Sperater;
	};

	template<typename Func>
	std::size_t ForeachLogLine(LogLineProcessor& Pro, Func&& Fun) requires(std::is_invocable_v<Func&&, LogLine>)
	{
		Pro.Clear();
		std::size_t Count = 0;
		while (Pro)
		{
			auto Re = Pro.Consume();
			assert(Re);
			std::forward<Func>(Fun)(*Re);
			++Count;
		}
		return Count;
	}

	template<typename Func>
	std::size_t ForeachLogLine(std::u8string_view Str, Func&& Fun) requires(std::is_invocable_v<Func&&, LogLine>)
	{
		LogLineProcessor Pro(Str);
		return ForeachLogLine(Pro, std::forward<Func>(Fun));
	}
}