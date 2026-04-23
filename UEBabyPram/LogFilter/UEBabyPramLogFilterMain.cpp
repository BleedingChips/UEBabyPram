import UEBabyPram;
import UEBabyPramLogFilter;
import std;

using namespace UEBabyPram;

int main(int argc, char* argv[])
{
#ifdef _WIN32

#endif
	LogFilter::Test();
	for (std::size_t i = 0; i < argc; ++i)
	{
		std::cout << argv[i] << std::endl;
	}
	return 0;
}