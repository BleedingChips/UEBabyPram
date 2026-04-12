import UEBabyPram;
import UEBabyPramLogFilter;
import std;

int main(int argc, char* argv[])
{
#ifdef _WIN32

#endif
	for (std::size_t i = 0; i < argc; ++i)
	{
		std::cout << argv[i] << std::endl;
	}
	return 0;
}