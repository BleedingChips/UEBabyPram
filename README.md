# UEBabyPram

一些UE外部工具

## 安装

从 Github 上将以下库Clone到本地，并使其保持在同一个路径下：

	```
	https://github.com/BleedingChips/UEBabyPram.git
	https://github.com/BleedingChips/Potato.git
	```

在包含该项目的 xmake.lua 上，添加如下代码即可：

	```lua
	includes("../Potato/")
	includes("../UEBabyPram/")

	target(xxx)
		...
		add_deps("UEBabyPram")
	target_end()
	```

运行 `xmake_install.ps1` 安装 `xmake`，运行`xmake_generate_vs_project.ps1`将在`vsxmake2022`下产生vs的项目文件。

## 功能

### LogFilter 

    将UE4日志结构化拆解:

    ```cpp
    std::u8string_view Source = 
        u8R"(LogConfig: Setting CVar [[net.AllowAsyncLoading:1]]
        [2021.10.11-11.53.12:082][  0]LogConfig: Setting CVar [[con.DebugEarlyDefault:1]]
        [2021.10.11-11.53.12:082][  0]LogConfig: Display: Setting CVar [[con.DebugEarlyDefault:1]]
        )";
    UEBabyPram::LogFilter::ForeachLogLine(Source, [](UEBabyPram::LogFilter::LogLine Line){
        // 通过读取Line可以获取改行的日志信息
    });
    ```

    LogLine 结构体包含以下变量：

    ```cpp
    struct LogLine
    {
        // 该行日志打印的时间，可以通过 LogLineProcessor::GetTime(Line) 来获得整数形式的月-日-时-分-秒-毫秒-帧形式的时间。
        std::u8string_view Time;

        // 该日志所属的类别，比如LogDemo, LogConfig
        std::u8string_view Cagetory;

        // 该日志等级，如 log，Display，Error
        std::u8string_view Level;

        // 该日志不包含上述信息的文本
        std::u8string_view Str;

        // 该日志包含上述信息的问题
        std::u8string_view TotalStr;

        // 该日志所在的行数
        IndexSpan<> LineIndex;
    };
    ```
