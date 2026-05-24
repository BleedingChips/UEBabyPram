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
		std::size_t max_output_count = std::numeric_limits<std::size_t>::max();
	};

	constexpr auto comment_log = TMP::TypeString{u"CLI"};

	int HandleComment(int argc, char* argv[], FilterSetting& setting, LogFilterProcessor& processor);
}