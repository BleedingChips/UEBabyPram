import UEBabyPram;
import Potato;
import std;

using namespace UEBabyPram;

int main(int argc, char* argv[])
{
	for (std::size_t i = 0; i < argc; ++i)
	{
		std::cout << argv[i] << std::endl;
	}
	return 0;
}