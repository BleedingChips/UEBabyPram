module;

#include <cassert>

export module UEBabyPramLogCLI;

import std;
import Potato;
import UEBabyPramLogFilter;


export namespace UEBabyPram::LogFilter
{
	using namespace Potato;

	enum class OutputTarget
	{
		FILE,
		STD,
	};

	enum class OutputMode
	{
		NORMAL,
		NORMAL_WITH_LINE,
		ONLY_TIME_AND_LINE,
		CUSTOM
	};

	struct FilterSetting
	{
		std::pmr::vector<std::filesystem::path> input_file;
		std::filesystem::path output_path;
		std::filesystem::path output_expand = ".filterout";
		OutputTarget target = OutputTarget::FILE;
		OutputMode mode = OutputMode::NORMAL;
		bool output_with_separate_frame = false;
		Potato::Misc::IndexSpan<> output_span = { 0, std::numeric_limits<std::size_t>::max() };
	};

	constexpr auto comment_log = TMP::TypeString{u"CLI"};

	int HandleComment(int argc, char* argv[], FilterSetting& setting, LogFilterProcessor& processor, LogFilterFormatter& fomatter);
}

namespace Potato::Log
{
	template<Potato::Log::LogLevel level>
	struct LogCategoryFormatter<UEBabyPram::LogFilter::comment_log, level>
	{
		template<typename OutputIterator, typename ...Parameters>
		OutputIterator operator()(OutputIterator iterator, std::basic_format_string<wchar_t, std::type_identity_t<Parameters>...> const& pattern, Parameters&& ...parameters)
		{
			if constexpr (level == Potato::Log::LogLevel::Error)
			{
				Potato::Log::FormatedSystemTime time;
				iterator = std::format_to(
					std::move(iterator),
					L"{}:",
					level
				);
			}
			return std::format_to(
				std::move(iterator),
				pattern,
				std::forward<Parameters>(parameters)...
			);
		}
	};
}