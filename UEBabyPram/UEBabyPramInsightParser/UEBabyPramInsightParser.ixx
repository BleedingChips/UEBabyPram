module;

#include "UEBabyPramInsightParserInterface.h"

export module UEBabyPramInsightParser;
import std;

export namespace UEBabyPram::InsightParser
{
	using UEBabyPram::InsightParser::DataResourceInterface;

	void Test(DataResourceInterface& resource)
	{
		TestImp(resource);
	}
}


//export import UEBabyPramInsightInterface;