module;

#include <cassert>

export module UEBabyPramLogCLI;

import std;
import Potato;
import UEBabyPramLogFilter;


export namespace UEBabyPram::LogFilter
{
	using namespace Potato;

	constexpr auto comment_log = TMP::TypeString{u"CLI"};

	int HandleComment(int argc, char* argv[], FilterSetting& setting, LogFilterProcessor& processor);
}