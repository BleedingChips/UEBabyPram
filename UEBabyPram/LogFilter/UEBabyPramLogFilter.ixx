module;

#include <cassert>

export module UEBabyPramLogFilter;

import std;
import Potato;
import UEBabyPram;


export namespace UEBabyPram::LogFilter
{
	enum class OutputTarget
	{
		FILE,
		STD,
	};

	enum class OutputMode
	{
		FILTER,
		FILTER_WITH_FRAME_SPERATE,
		CUSTOM,
	};

	constexpr std::size_t max_cache_size_gb = 1;

	struct FilterSetting
	{
		std::vector<std::filesystem::path> input_files;
		std::vector<std::filesystem::path> input_paths;
		std::filesystem::path output_paths;
		std::filesystem::path output_expand;
		OutputTarget target = OutputTarget::FILE;
		OutputMode mode = OutputMode::FILTER;
		std::size_t max_cache_size = 1024 * 1204 * 1024 * max_cache_size_gb;
	};

	void Test();
}