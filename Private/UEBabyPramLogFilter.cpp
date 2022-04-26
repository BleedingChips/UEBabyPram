#include "../Public/UEBabyPramLogFilter.h"
#include "Potato/Public/PotatoReg.h"

namespace UEBabyPram::LogFilter
{
	bool LoadUE4Log(std::filesystem::path Path, bool (*Func)(LogLine, void*), void* Data)
	{
		using namespace Potato;

		static Reg::Table StartupTimeTab(UR"(\[([0-9]{4})\.([0-9]{2})\.([0-9]{2})\-([0-9]{2})\.([0-9]{2})\.([0-9]{2})\:([0-9]{3})\]\[\s*?([0-9]{0,3})\])");
		static Reg::Table StartupNoTimeTab(UR"(([0-9a-zA-Z\-\_\z]+?)\:)");

		StrEncode::DocumentReader Wrap(std::move(Path));
		if (Wrap)
		{
			std::vector<std::byte> Tembuffer;
			Tembuffer.resize(Wrap.RecalculateLastSize());
			std::u32string Buffer;
			std::optional<LogLine> LastLine;
			auto Reader = Wrap.CreateWrapper(Tembuffer);
			auto R1 = Wrap.Flush(Reader);
			std::size_t LineCount = 0;
			while (Reader)
			{
				++LineCount;

				auto Re = Reader.ReadLine(Buffer);

				auto Ite = std::u32string_view(Buffer);

				std::optional<LogTime> Time;
				std::optional<std::u32string_view> Catory;

				auto Re1 = Reg::ProcessFrontMarch(StartupTimeTab, Buffer);
				if (Re1.has_value())
				{
					LogTime TempTime;
					auto ScanResult = StrFormat::CaptureScan(Re1->GetCaptureWrapper(),
						Ite,
						TempTime.Year,
						TempTime.Month,
						TempTime.Day,
						TempTime.Hour,
						TempTime.Min,
						TempTime.Sec,
						TempTime.MSec,
						TempTime.FrameCount
					);
					Time = TempTime;
					Ite = Ite.substr(Re1->MainCapture.Count());
				}

				auto Re2 = Reg::ProcessFrontMarch(StartupNoTimeTab, Ite);
				if (Re2.has_value())
				{
					Catory = Ite.substr(0, Re2->MainCapture.Count());
					Ite = Ite.substr(Re2->MainCapture.Count());
				}

				if (Time.has_value() || Catory.has_value())
				{
					if (LastLine.has_value())
						if (!Func(std::move(*LastLine), Data))
						{
							LastLine.reset();
							return true;
						}
					LogLine Line;
					if (Time.has_value())
						Line.Time = std::move(Time);
					if (Catory.has_value())
						Line.Cagetory = *Catory;
					Line.Line = LineCount;
					Line.Str = std::move(Buffer);
					LastLine = std::move(Line);
				}
				else {
					if (LastLine.has_value())
					{
						LastLine->Str.append(Buffer);
						Buffer.clear();
					}
					else {
						LogLine Line;
						Line.Line = LineCount;
						Line.Str = std::move(Buffer);
						LastLine = std::move(Line);
					}
						
				}
			}
			if (LastLine.has_value())
				if (!Func(std::move(*LastLine), Data))
				{
					LastLine.reset();
					return true;
				}
			return true;
		}
		return false;
	}
}