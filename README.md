# UEBabyPram

一些 Unreal Engine（UE4/UE5）外部辅助工具：解析 UE `.log` 日志、分析 Unreal Insights `.utrace` 文件。

## 仓库组成

| 目录 | 说明 |
|------|------|
| `UEBabyPram/` | 核心静态库 `UEBabyPram`，以及 `.utrace` 数据解析库 `UEBabyPramInsightParser` |
| `UEBabyPramLogFilter/` | UE 日志过滤 CLI `UEBabyPramLogFilter` 及其 skill 模板 |
| `UEBabyPramInsightFilter/` | `.utrace` 分析 CLI `UEBabyPramInsightFilter` 及其 skill 模板 |
| `Test/` | 测试程序，每个 `Test/*.cpp` 自动生成一个 target |
| `xmake.lua` | xmake 构建入口 |
| `xmake_install.ps1` | 安装 xmake |
| `xmake_generate_vs_project.ps1` | 在 `vsxmake2022/` 下生成 VS 工程 |

## 环境要求

- Windows x64
- Visual Studio 2022（MSVC）
- [xmake](https://xmake.io/)
- C++ latest（项目使用 C++20 modules，源码为 `.ixx` + `.cpp`）

## 安装

从 GitHub 上将以下库 Clone 到本地，并使其保持在同一个路径下：

```
https://github.com/BleedingChips/UEBabyPram.git
https://github.com/BleedingChips/Potato.git
```

在包含该项目的 `xmake.lua` 上，添加如下代码即可作为子项目使用：

```lua
includes("../Potato/")
includes("../UEBabyPram/")

target(xxx)
	...
	add_deps("UEBabyPram")
target_end()
```

运行 `xmake_install.ps1` 安装 xmake；运行 `xmake_generate_vs_project.ps1` 将在 `vsxmake2022` 下产生 VS 项目文件。也可以直接使用 `xmake build-xxx-skill` 在 `.\Skills\` 下生成对应的 SKILL。

常用构建命令：

```powershell
xmake build UEBabyPram
xmake build UEBabyPramLogFilter
xmake build UEBabyPramInsightFilter
```

## 功能

### LogFilter

根据时间、类别、行号、日志内容对 UE 日志进行筛选、查找以及结构化输出。

#### 特点

1. 流式加载，支持大文件
2. 支持多条件组合查询
3. 支持自定义格式化输出
4. AI 友好，一键生成对应的 SKILL

#### 构建方法

```powershell
xmake build-logfilter-skill
```

#### 常用示例

```powershell
UEBabyPramLogFilter -f "D:\Logs\MyGame.log" -c "Level >= Warning"
UEBabyPramLogFilter -f "D:\Logs\MyGame.log" -c "Level >= Error" -ostd -or 0 50

# 查看版本（输出 UEBabyPramLogFilter <1.0>）
UEBabyPramLogFilter -v
```

### InsightFilter

`UEBabyPramInsightFilter` 用于分析 Unreal Insights `.utrace` 文件，支持以下模式（`-r`、`-es`、`-ed`、`-ef`、`-ts` 互斥）：

| 模式 | 选项 | 说明 |
|------|------|------|
| 默认 | 无 | GameThread 帧耗时分析 |
| 事件位置 | `-r, --regex <regex>` | 按事件名正则搜索事件定义位置 |
| 事件统计 | `-es, --event_stat <query>` | 统计匹配事件的耗时与 Top N 最长 occurrence |
| 详细堆栈 | `-ed, --event_detail <query>` | 保存匹配的 stack tree，按 box-drawing 打印详细调用堆栈 |
| 火焰图 | `-ef, --event_flame_graph <query>` | 聚合匹配的调用路径生成火焰图 |
| 线程搜索 | `-ts, --thread_search <regex>` | 按线程名正则搜索线程 |

#### 事件查询语法（`-es` / `-ed` / `-ef`）

```text
chain [ '&' ( 'Time' | 'ITime' ) '=' [TimePoint, TimePoint]+ ] [ '&' 'Thread' '=' (^Regex^) ]
chain     := elem ( '->' elem )*
elem      := (^Regex^)     只作为调用链的一部分，不统计
          |  [^Regex^]     匹配该事件并统计其耗时
          |  %[^Regex^]    匹配该事件并统计其耗时，且该元素匹配到的所有事件合并成同一个统计口径
          |  %            匹配任意事件，只作为调用链的一部分
TimePoint := Integer 'm' Decimal 's' | Decimal 's' | Integer 's' | Integer 'm' Integer 's'
```
- 调用链按调用顺序匹配，后一个元素必须是前一个元素的后代；
- **单个查询节点在一棵子树里只会匹配一次**：某节点匹配成功后，同一节点的下一个候选从它整棵子树之后继续查找，所以同一节点的候选之间不会嵌套；用 `[^.*^]`/`(^.*^)` 这类"匹配全部名字"的写法时只会统计到调用树的**根节点**（根是第一个候选，其余匹配都在根的子树内），不能用来枚举所有事件——要统计具体事件请写明确的名字，只用于补全调用链的节点用 `%`；
- 只有 `[^...^]`/`%[^...^]` 元素会被统计（`%[^...^]` 合并该元素命中的所有事件为一个统计口径），`(^...^)` 与 `%` 仅补全调用链；
- 正则由一对 `^` 界定：`[`、`]`、`(`、`)`、`%`、`->` 等语法字符可直接出现在正则里，正则内表示 `^` 需写成 `\^`；
- **时间片可连写合并**：同一条件的多个时间片直接连写、无分隔符，例如 `&Time=[0s,10s][20s,30s]`；
- `Time` 条件按**相交**过滤被统计的 occurrence，例如 `&Time=[0s,10s]`；
- `ITime` 条件按**包含或相等**过滤：事件的时间范围必须整段落在某个时间片内（完全相等也算），跨边界或只有部分交集都不通过，例如 `&ITime=[0s,10s][20s,30s]`；`Time` 与 `ITime` 只能二选一；
- `-es` 的 `Time`/`ITime` 过滤的是被统计的 occurrence；`-ed`/`-ef` 报告整棵树，因此时间片选择的是整棵树（`ITime` 只选整棵树都在时间片内的树）；
- `Thread` 条件匹配完整线程名，例如 `&Thread=(^.*GameThread.*^)`。

示例：
```text
(^FEngineLoop::Tick^)->[^.*TickActor.*^]
[^UWorld::Tick^]&Time=[0s,10s]&Thread=(^.*GameThread.*^)
[^UWorld::Tick^]&ITime=[0s,10s][20s,30s]
%[^UWorld::Tick^]
```

> **性能提示**：`-r`、`-es`、`-ed`、`-ef` 里的每个正则都要对 trace 中的每一个事件名（真实 trace 常有数千个）做完整匹配，`.*`、`(^.*^)`、`[^.*Tick.*^]` 这类宽正则会让整次分析变慢、报告变大；请写名字里确定的部分，query 里若"任意事件"即可，用不带正则的 `%` 元素。
#### 常用参数

| 选项 | 说明 |
|------|------|
| `-f, --file <path>` | 必填，`.utrace` 文件路径 |
| `-o, --output <path>` | 报告写入的文件（存在则清空重建），不给则写标准输出；写文件时每条消息的字节数与累计字节数会直接打印到终端 |
| `-a, --append <path>` | 报告追加到该文件末尾（不存在则创建、原有内容保留，仅纯文本）；不能与 `-o` 同时使用 |
| `-v, --version` | 输出工具的版本并退出（输出形如 `UEBabyPramInsightFilter <1.0>`） |
| `-tx, --top_count <count>` | `-es` 每个事件名保留的 Top N 最长 occurrence，默认 5 |
| `-dbc, --duration_bucket_count <count>` | `-es` 耗时分布直方图的分组数，默认 5 |
| `-csv, --csv <path>` | 把报告写成 CSV 文件（UTF-8，RFC 4180）而不是文本报告：`-es` 时每行一个事件统计；**默认模式**时每行一帧（`FrameIndex, Begin_s, End_s, Duration_ms, ENCS_ms, FPS`，覆盖采样区间内全部帧并按时间排序）。不能与 `-o`/`-a` 同时使用 |
| `-fs, --folded_stack <path>` | `-ef` 把火焰图写成 folded stacks 文件（`帧;帧 <微秒>`）而不是文本报告；不能与 `-o`/`-a` 同时使用，只能与 `-ef` 一起用 |
| `-mvc, --max_view_count <count>` | `-ed` 最多保存多少个 stack tree，默认 10 |
| `-mnc, --max_node_count <count>` | `-ed` 单棵 stack tree 最多打印多少个节点，超出不打印并以 `...` 标记，0 表示不限，默认 100 |
| `-md, --max_depth <depth>` | `-ed` 单棵 stack tree 最多打印多少层，超出不打印并以 `...` 标记，0 表示不限，默认 8（根节点为第 1 层） |
| `-mlc, --max_location_count <count>` | `-r` 最多报告多少个事件位置，默认 256 |
| `-mrf, --max_record_frame <count>` | 默认模式记录的最长帧数量，默认 10 |
| `-metc, --max_exclude_time_count <count>` | 每个 Top 列表保留的最长 Exclude Time 事件数量，默认 30 |
| `-sr, --sampling_ratio <ratio>` | 默认模式采样比例，`[0.0, 1.0]`，默认 **0.9**（只分析中间 90%）；报告只统计与采样时间范围有交集的帧 |

#### 构建与运行

```powershell
xmake run UEBabyPramInsightFilter -f .\Capture.utrace -es "[^UWorld::Tick^]"
xmake run UEBabyPramInsightFilter -f .\Capture.utrace -es "[^UWorld::Tick^]"
```

#### 常用示例

```powershell
# 默认 GameThread 帧耗时分析
UEBabyPramInsightFilter -f .\Capture.utrace

# 搜索事件定义位置
UEBabyPramInsightFilter -f .\Capture.utrace -r "FEngineLoop::.*"

UEBabyPramInsightFilter -f .\Capture.utrace -es "(^FEngineLoop::Tick^)->[^.*TickActor.*^]" -tx 10

# 把事件耗时统计导出为 CSV 文件（不打印文本报告）
UEBabyPramInsightFilter -f .\Capture.utrace -es "[^UWorld::Tick^]" -csv .\result.csv

# 把默认模式的逐帧数据导出为 CSV（每行一帧，覆盖采样区间内全部帧）
UEBabyPramInsightFilter -f .\Capture.utrace -csv .\frames.csv

UEBabyPramInsightFilter -f .\Capture.utrace -ed "[^UWorld::Tick^]" -mvc 5

# 每棵树最多打印 200 个节点、20 层，超出部分以 ... 标记
UEBabyPramInsightFilter -f .\Capture.utrace -ed "[^UWorld::Tick^]" -mvc 5 -mnc 200 -md 20

UEBabyPramInsightFilter -f .\Capture.utrace -ef "[^UWorld::Tick^]"

# 把火焰图导出为 folded stacks，可直接用 flamegraph.pl 出图
UEBabyPramInsightFilter -f .\Capture.utrace -ef "[^UWorld::Tick^]" -fs .\result.folded

# 搜索 GameThread
UEBabyPramInsightFilter -f .\Capture.utrace -ts ".*GameThread.*"
```

#### 输出字段说明

- 默认模式：`Total GameThread Time`／`Total GameFrame`／`Avg GameThread Time` 给出总耗时、帧数与平均帧耗时；`Frame Rate` 段落用表格给出 5 档帧率（`> 120`、`60 ~ 120`、`30 ~ 60`、`15 ~ 30`、`<= 15` FPS）的帧耗时区间、帧数与占比（占比之和 100%），`Frames Above X FPS` 给出累计占比（帧率高于 X FPS 的帧），`Mean FPS` 由平均帧耗时换算；其后是 `Time Range`／`Sampled Range`／`Sampled Ratio` 与最长帧、最长 Exclude Time 列表。报告顶部有一行口径声明：**所有帧数、帧率与 Top 列表只统计与采样时间范围（`-sr`，默认 0.9）有交集的帧**，完全在范围外的帧不统计、跨边界的帧按整帧耗时统计。没有记录到 GameFrame 时只输出一行提示。
- 上下文切换：若 trace 未采集 context switch 通道（没有任何 context switch 事件），默认模式 / `-es` / `-ed` 会在报告标题下打印 `No Context Switch event is recorded in the trace ...`，此时 `CS` 恒为 0、`ENCS` 等于 `E`（耗时是线程被调度的时间，不是真正占用 CPU 的时间）。
- `-r`：文件路径是产生 utrace 的包体打包时的源文件路径，指向打包机器；需要用户自行转换为本地工作目录/源码树路径。
- `-es`：`Count`、`Total D/Mean D/Median D/Std D`、`Total E/Mean E`；Top occurrence 行的 `D`（耗时）、`E`（Exclude Time）、`ENCS`（去除离核时间后的 Exclude Time）、`CS`（Context Switch Time）、`TR`（时间范围）、`TName`（线程名）；Call Context 以 box-drawing 打印父链与子树。加 `-csv <path>` 时改为写 CSV 文件，列顺序为 `QueryIndex, Event, Kind, EventIdCount, OccurrenceCount, TotalD_ms, MeanD_ms, MedianD_ms, StdD_ms, TotalE_ms, MeanE_ms, Query`（`Kind` 为 `Name` 或 `Regex`，耗时单位 ms、6 位小数），按查询与总耗时从大到小排序，UTF-8 无 BOM、字段符合 RFC 4180；CSV 只含事件汇总，Top occurrence／GameFrame／直方图仍只在文本报告里。
- `-ed`：`View` 行的 `E` 为整棵 stack tree 总耗时；节点行的 `TR` 为时间范围、`D` 为耗时、`E` 为 Exclude Time、`CS` 为 Exclude Time 中线程不在 CPU 上的时间（为 0 时不打印）。打印的是**完整**调用树：查询的 `Time` 条件只决定保留哪些树，时间片外的事件照样打印；单棵树规模由 `-mnc`（节点数，默认 100）与 `-md`（层数，默认 8）限制，超出的节点不打印并以 `...` 标记，两个限制对每棵树分别生效（`0` 表示不限）。
- `-ef`：`D` 为调用路径的 inclusive 耗时，`Percent` 为占全部匹配时间的百分比。时间片只决定聚合哪些树：与时间片相交的 stack tree 会**整棵**参与统计（树下所有事件都计入、时间不被裁剪），但报告只打印"至少有一个 occurrence 落在时间片内"的调用路径，完全在时间片外的调用路径不打印，其时间仍属于父节点的 inclusive 耗时。加 `-fs <path>` 时改为写 folded stacks 文件（每行 `帧1;帧2;...;帧N <微秒>`，权重为该路径的自身耗时，各行之和等于报告 `Total`；只导出报告会打印的路径，帧名里的 `;`/换行/制表符会被替换），可直接用 `flamegraph.pl result.folded > result.svg` 出图。

#### 构建 SKILL

```powershell
xmake build-insight-filter-skill
```

生成在 `.\Skills\uebabypram-insight-filter\`，包含 `SKILL.md` 与 `scripts\` 下的可执行文件及运行时 DLL。打包通过 `xmake install` 完成，依赖的运行时库会一起部署。

## SKILL 生成

```powershell
# 日志过滤 skill
xmake build-logfilter-skill

# Insights 分析 skill
xmake build-insight-filter-skill
```

打包流程会先以 release 配置构建对应 target，再用 `xmake install -o <staging>` 安装到临时目录，最后把 `bin` 下的可执行文件与运行时 DLL 一起复制到 skill 的 `scripts` 目录。

也可以直接运行仓库根目录下的两个脚本，它们分别执行上面两条指令（多给的参数会透传，例如 `--output=<dir>`）：

```powershell
.\xmake_build_logfilter_skill.ps1
.\xmake_build_insight_filter_skill.ps1
```

生成目录可通过 `--output=<dir>` 指定，默认为仓库根目录下的 `Skills`。

## 许可证

[MIT](LICENSE)