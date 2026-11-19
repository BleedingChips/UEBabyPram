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

`UEBabyPramInsightFilter` 用于分析 Unreal Insights `.utrace` 文件，支持以下模式（`-r`、`-es`、`-ed`、`-ef`、`-ts`、`-ms`、`-mv`、`-mt`、`-mtt` 互斥）：

| 模式 | 选项 | 说明 |
|------|------|------|
| 默认 | 无 | GameThread 帧耗时分析 |
| 事件位置 | `-r, --regex <regex>` | 按事件名正则搜索事件定义位置 |
| 事件统计 | `-es, --event_stat <query>` | 统计匹配事件的耗时与 Top N 最长 occurrence |
| 详细堆栈 | `-ed, --event_detail <query>` | 保存匹配的 stack tree，按 box-drawing 打印详细调用堆栈 |
| 火焰图 | `-ef, --event_flame_graph <query>` | 聚合匹配的调用路径生成火焰图 |
| 线程搜索 | `-ts, --thread_search <regex>` | 按线程名正则搜索线程 |
| 内存统计 | `-ms, --memory_stat <query>` | 按 EventQuery 选择内存 tag，按 tracker × tag 聚合该 tag 全部快照的值统计（首/末/最大/最小/平均，值带 B/KB/MB/GB 单位），可 `-csv` 导出 |
| 内存时间线 | `-mv, --memory_value <query>` | 每个 tag 的首点 + 峰值（斜率转向处）+ 末点，每点带时间/值/斜率；`--all_samples` 打印全部采样 |
| 内存 spec | `-mt, --memory_tag <regex>` | 按正则搜索内存 tag / tracker / tag set 的 spec（只解码 spec，不解析快照） |
| 仅 tracker | `-mtt, --memory_tracker <regex>` | 只搜索 memory tracker（与 `-mt` 互斥） |

#### 事件查询语法（`-es` / `-ed` / `-ef`）

```text
chain [ '&' ( 'Time' | 'ITime' ) '=' [TimePoint, TimePoint]+ ] [ '&' 'Thread' '=' (^Regex^) ]
                 [ '&' 'Tracker' '=' (^Regex^) ]
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
- **`Time` 与 `ITime` 在 CPU 事件模式下不同**：`Time` 只要求事件时间范围与时间片**相交**（跨边界也计入、按整段计），`ITime` 要求事件时间范围**完全落在某个时间片内**（或正好相等）；在**内存模式**（`-ms`/`-mv`）下两者**等价**，因为那里用的是快照的**时间点**（含端点）；
- `-es` 的时间条件过滤被统计的 occurrence（`Time` 相交即计、`ITime` 整段在内）；`-ed`/`-ef` 里时间片只决定保留哪些 stack tree（`Time` 保留相交的树、`ITime` 只保留整棵落在切片内的树），不裁剪被保留树的报告内容；
- `-ed`/`-ef` 的 query 若不含 `[^...^]`/`%[^...^]`（没有需要捕捉的事件），query 只作为筛选条件，打印/聚合并打印匹配的整棵调用树；若含捕捉元素，则每次匹配只报告被捕捉的节点，并在最后一个被捕捉节点下打印它的整棵子树，被捕捉节点之上的补全链节点与祖先会被省略；
- `Thread` 条件匹配**完整线程名**（如 `&Thread=(^.*GameThread.*^)`），`Tracker` 条件匹配**完整 tracker 名**（内存模式）；每个模式只接受它读的条件，其余条件会**报错退出**：`-es`/`-ed`/`-ef` 接受 chain + Time + Thread，`-ms`/`-mv` 接受 chain + Time + Tracker。

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
| `-f, --file <path>` | 必填，`.utrace` 文件路径；每累计读取超过 1 GB 在终端打印一次 `Input <本段字节数> byte(s), Total <累计字节数> byte(s)`，读取结束时打印 `Input Read Complete, Total <累计字节数> byte(s)` |
| `-o, --output <path>` | 报告写入的文件（存在则清空重建），不给则写标准输出 |
| `-a, --append <path>` | 报告追加到该文件末尾（不存在则创建、原有内容保留，仅纯文本）；不能与 `-o` 同时使用 |
| `-v, --version` | 输出工具的版本并退出（输出形如 `UEBabyPramInsightFilter <1.2>`） |
| `-tx, --top_count <count>` | `-es` 每个事件名保留的 Top N 最长 occurrence，默认 5 |
| `-dbc, --duration_bucket_count <count>` | `-es` 耗时分布直方图的分组数，默认 5 |
| `-csv, --csv <path>` | 把报告写成 CSV 文件（UTF-8，RFC 4180）而不是文本报告：`-es` 时每行一个事件统计；`-ms` 时每行一个内存 tag 统计（**固定按 tracker → tag 排序**）；`-mv` 时每行一个打印点（`Tracker, MemoryTag, PointIndex, Kind, Time_s, Value_B, Slope_Bps`）（`Tracker, MemoryTag, Count, FirstTime_s, FirstValue_B, LastTime_s, LastValue_B, MaxTime_s, MaxValue_B, MinTime_s, MinValue_B, Mean_B, TrendStart_s, Trend20_s, Trend20_B, Trend40_s, Trend40_B, Trend60_s, Trend60_B, Trend80_s, Trend80_B, TrendEnd_s, Slope20_Bps, Slope40_Bps, Slope60_Bps, Slope80_Bps, SlopeStdDev_Bps`，值为字节）；**默认模式**时每行一帧（`FrameIndex, Begin_s, End_s, Duration_ms, ENCS_ms, FPS`，覆盖采样区间内全部帧）。只能用于 `-es`/`-ms`/`-mv` 或默认模式；不能与 `-o`/`-a` 同时使用 |
| `-fs, --folded_stack <path>` | `-ef` 把火焰图写成 folded stacks 文件（每行 `调用路径 <整数>`，整数是该路径的**自耗时、单位微秒**）而不是文本报告；不能与 `-o`/`-a` 同时使用，只能与 `-ef` 一起用 |
| `-mvc, --max_view_count <count>` | `-ed` 最多保存多少个 stack tree，默认 10 |
| `-mvc, --max_view_count <count>` | `-ed`/`-ef` 保留多少个最长的 stack tree，默认 10；注意 **`-mvc 0` 表示一个都不保留**（与 `-mnc`/`-md` 的 0=不限不同） |
| `-mnc, --max_node_count <count>` | `-ed` 单棵报告树最多打印多少个节点，超出时**整棵后续子树都不打印、只留一个 `...`**，0 表示不限，默认 100。注意 `0` 是「不限」：1.5 s 的尖峰帧单视图实测 55,582 行，常规用 100~500 |
| `-md, --max_depth <depth>` | `-ed` 单棵报告树最多打印多少层，超出时**每层**各留一个 `...`，0 表示不限，默认 8（根节点为第 1 层） |
| `-mlc, --max_location_count <count>` | `-r` 最多报告多少个事件位置，默认 256 |
| `-mrf, --max_record_frame <count>` | 默认模式记录的最长帧数量，默认 10 |
| `-metc, --max_exclude_time_count <count>` | 每个 Top 列表保留的最长 Exclude Time 事件数量，默认 30 |
| `--all_samples` | `-mv` 打印全部采样点（不只峰值），`-mvp`/`-mvs` 失效，输出很大，建议配 `-csv` |
| `-mvp, --max_peak_count <count>` | `-mv` 每个内存 tag 最多打印多少个峰值，默认 20，0 表示不限 |
| `-mvs, --min_peak_sample_count <count>` | `-mv` 两个打印峰值之间至少隔多少个采样点，默认 5 |
| `-msr, --memory_sort <name\|mean\|max>` | `-ms` 的行排序：`name`（默认）、`mean`（平均值从大到小）、`max`（最大值从大到小） |
| `-mst, --max_tag_count <count>` | `-ms` 最多打印多少个统计，0 表示不限，默认 0 |
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

# 按 query 聚合内存 tag 的值统计（chain 匹配 tag 父级链，Thread 匹配 tracker 名），值带单位
UEBabyPramInsightFilter -f .\Capture.utrace -ms "(^Mass^)"

# 导出为 CSV（值为字节，不带单位，便于计算）
UEBabyPramInsightFilter -f .\Capture.utrace -ms "%&Tracker=(^Platform^)" -csv .\memory.csv
UEBabyPramInsightFilter -f .\Capture.utrace -ms "%&Tracker=(^Platform^)" -msr max

# 打印内存 tag 的时间线（每个 tag 最多 20 个采样点），并导出 CSV
UEBabyPramInsightFilter -f .\Capture.utrace -mv "(^Mass^)" -mvp 10
UEBabyPramInsightFilter -f .\Capture.utrace -mv "(^Mass^)" -csv .\timeline.csv
```

#### 输出字段说明

- 默认模式：`Total GameThread Time`／`Total GameFrame`／`Avg GameThread Time` 给出总耗时、帧数与平均帧耗时；`Frame Rate` 段落用表格给出 5 档帧率（`> 120`、`60 ~ 120`、`30 ~ 60`、`15 ~ 30`、`<= 15` FPS）的帧耗时区间、帧数与占比（占比之和 100%），`Frames Above X FPS` 给出累计占比（帧率高于 X FPS 的帧），`Mean FPS` 由平均帧耗时换算；其后是 `Time Range`／`Sampled Range`／`Sampled Ratio` 与最长帧、最长 Exclude Time 列表。报告顶部有一行口径声明：**所有帧数、帧率与 Top 列表只统计与采样时间范围（`-sr`，默认 0.9）有交集的帧**，完全在范围外的帧不统计、跨边界的帧按整帧耗时统计。没有记录到 GameFrame 时只输出一行提示。
- 上下文切换：若 trace 未采集 context switch 通道（没有任何 context switch 事件），默认模式 / `-es` / `-ed` 会在报告标题下打印 `No Context Switch event is recorded in the trace ...`，此时 `CS` 恒为 0、`ENCS` 等于 `E`（耗时是线程被调度的时间，不是真正占用 CPU 的时间）。
- `-r`：文件路径是产生 utrace 的包体打包时的源文件路径，指向打包机器；需要用户自行转换为本地工作目录/源码树路径。
- `-es`：`Count`、`Total D/Mean D/Median D/Std D`、`Total E/Mean E`；Top occurrence 行的 `D`（耗时）、`E`（Exclude Time）、`ENCS`（去除离核时间后的 Exclude Time）、`CS`（Context Switch Time）、`TR`（时间范围）、`TName`（线程名）；Call Context 以 box-drawing 打印父链与子树。加 `-csv <path>` 时改为写 CSV 文件，列顺序为 `QueryIndex, Event, Kind, EventIdCount, OccurrenceCount, TotalD_ms, MeanD_ms, MedianD_ms, StdD_ms, TotalE_ms, MeanE_ms, Query`（`Kind` 为 `Name` 或 `Regex`，耗时单位 ms、6 位小数），按查询与总耗时从大到小排序，UTF-8 无 BOM、字段符合 RFC 4180；CSV 只含事件汇总，Top occurrence／GameFrame／直方图仍只在文本报告里。
- `-ed`：`View` 行的 `E` 为整棵 stack tree 总耗时；节点行的 `TR` 为时间范围、`D` 为耗时、`E` 为 Exclude Time、`CS` 为 Exclude Time 中线程不在 CPU 上的时间（为 0 时不打印）。报告内容由 query 决定：query 不含 `[^...^]`/`%[^...^]` 时打印**完整**调用树（`Time` 条件只决定保留哪些树，时间片外的事件照样打印）；query 含捕捉元素时，每次匹配输出一组，以 `Match <n>: <A -> B>` 开头，只打印被 `[^...^]`/`%[^...^]` 命中的节点（按调用顺序），并在最后一个被捕捉节点下打印它的整棵子树，被捕捉节点之上的补全链节点与祖先省略。单组规模由 `-mnc`（节点数，默认 100）与 `-md`（层数，默认 8）限制，超出的节点不打印并以 `...` 标记，两个限制对每组分别生效（`0` 表示不限）。
- `-ef`：`D` 为调用路径的 inclusive 耗时，`Percent` 为占全部匹配时间的百分比。聚合范围由 query 决定：query 不含 `[^...^]`/`%[^...^]` 时整棵树参与聚合并打印全部调用路径；query 含捕捉元素时只聚合被捕捉节点与最后一个被捕捉节点的整棵子树（按调用顺序压缩未捕获的祖先），每次匹配都会并入图中。时间片只决定聚合哪些树，不裁剪事件时间，聚合出的所有调用路径都会打印。加 `-fs <path>` 时改为写 folded stacks 文件（每行 `帧1;帧2;...;帧N <微秒>`，权重为该路径的自身耗时，各行之和等于报告 `Total`；导出全部打印路径，帧名里的 `;`/换行/制表符会被替换），可直接用 `flamegraph.pl result.folded > result.svg` 出图。
- `-ms`：按 **EventQuery** 选择内存 tag，并按「每个 tracker × 每个内存 tag」聚合该 tag 的全部快照值（**不区分 QueryIndex**，同一 tag 被多个 query 命中只出现一次，重复 query 不会翻倍）。query 语义随内存数据形状而变：`&Tracker=(^...^)` 匹配产生快照的 **tracker 名**（如 `Default`/`Platform`，不带则选全部；`Thread` 在内存模式会被拒绝）；链的每个元素从根开始依次匹配 tag 名字（父级路径 `A/B/C`）的**一段**，并且**必须覆盖整个名字**才选中（`(^A^)->(^B^)` 只选 `A/B`，不再默认包含子节点；用不带括号的 `%` 多走一层，如 `(^A^)->(^B^)->%`、`(^Mass^)->%`）；时间条件是**时间点**语义：`Time`/`ITime` 等价，均取 `a <= t <= b`（含边界）。每行输出 `Count`（携带该 tag 的快照数）、`First`/`Last`（首/末快照时间与该时刻的值）、`Max`/`Min`（最大值/最小值及其所在时刻）、`Mean`（全部值的平均）；**值按 1024 进制带单位**（`B`/`KB`/`MB`/`GB`，≥1 GB 用 GB，保留 2 位小数）；每个 tag 另有一行 `Trend`：区间为「第一次达到最大值」到「第一次达到最小值」，按时间等分取 20%/40%/60%/80% 四个点（最近采样点）的时间、值、以及每段的斜率（字节/秒），并给出四个斜率的 `Slope StdDev`；`-mst <count>` 限制打印条数（默认 0 = 不限，截断时报告末尾提示）。行顺序为 **tracker 名 → tag 名**（`-msr` 可改为按平均值/最大值从大到小，同值时仍按 tracker → tag）；文本报告按 tracker 分块。加 `-csv <path>` 时写 CSV（值为字节，单位由列名 `_B` 表示）。只累加快照里实际带 value 的 tag；trace 未采集 memory 通道或 query 未选中任何 tag 时输出 `No memory tag is selected by the query, or the trace carries no memory tag snapshot.`。
- `-mv`：按 **EventQuery** 选择内存 tag（语义与 `-ms` 相同），打印每个 tag 的**曲线形状**：**首点**、**峰值**（斜率由正变负、由负变正、或变为 0 的采样点）、**末点**，每点给出时间、值与该点斜率（字节/秒，带单位）。同样不区分 QueryIndex、不含子节点，行顺序 tracker → tag、文本按 tracker 分块。峰值与上一个保留峰值的差 < 两者最大值 **5%** 时视为小波丢弃（首末点除外）；`-mvs`（默认 **5**）限制两个峰值之间的最小采样间隔；`-mvp`（默认 **20**）限制每个 tag 的峰值数量，超出时等距抽取；加 `--all_samples` 则跳过峰值、逐点打印全部采样（`Kind` 为 `First`/`Sample`/`Last`）。每行标题给出 `Point <打印数> of <总数>`，打印量 > 1000 时报告头提示。加 `-csv <path>` 时每行一个打印点，列为 `Tracker, MemoryTag, PointIndex, Kind, Time_s, Value_B, Slope_Bps`（`Kind` 为 `First`/`Peak`/`Last`）。- `-mt`：用一条正则同时搜索 memory tag、memory tracker 与 memory tag set（`-mtt` 只搜 tracker，两者互斥），命中的 spec 输出 id 与名字：tag 给出 `FullName`、`Name`、父 tag 与所属 tag set 的 `id:名字`，末尾汇总命中数量。正则匹配完整名字，`-mt ".*"` 导出全部 spec。只解码 spec（`ParserRequireFlag::MEMORY_TAG`），快照事件跳过，但 trace 文件仍需完整读取，耗时主要由读文件决定。

#### 限制与实测注意事项

| 组合 / 行为 | 规则 |
|-------------|------|
| 模式 | `-r`、`-es`、`-ed`、`-ef`、`-ts`、`-ms`、`-mv`、`-mt`、`-mtt` 只能给一个 |
| `-csv` | 只能用于 `-es`、`-ms`、`-mv` 或默认模式；不能与 `-o`/`-a` 同时用 |
| `-fs` | 只能与 `-ef` 一起用；不能与 `-o`/`-a` 同时用 |
| `-mt` / `-mtt` | 二者互斥 |
| `-a` / `-o` | 二者互斥 |
| query 条件 | `-es`/`-ed`/`-ef` 拒绝 `Tracker`，`-ms`/`-mv` 拒绝 `Thread`；条件顺序固定 Time → Thread → Tracker |

- 违反上表时工具**报错并直接退出**（在读 trace 之前，0.0–0.2 s），不会静默忽略。
- `-r` / `-ts` / `-mt` / `-mtt` 的正则与 query 里的 `^...^` 都是**整名匹配**：`-r "Tick"` 只命中名字正好是 `Tick` 的事件，搜包含要写 `-r ".*Tick.*"`。
- `-ed -mnc 0` / `-md 0` 是「不限」：本仓库测试 trace 的 1.5 s 尖峰帧单视图输出 55,582 行；常规用 `-mnc 100~500 -md 8~20`。
- `-ms`/`-mv` 的统计单位是 **per tracker × per tag**，多 query 命中同一 tag 只出现一次；`-ms` 的 `-csv` 固定按 tracker → tag 排序（文本仍按 `-msr`）。
- `-mv` 峰值 = 斜率由正变负 / 由负变正 / 变为 0 的采样；与上一保留峰值差 < 两者最大值 5% 的小波丢弃（首末点总是打印）。
- `-ef -fs` 的 folded 权重是**该路径的自耗时、单位微秒**，与 `perf`/flamegraph 工具的口径一致。
- 实测耗时（622 MB / 778 帧 trace，release）：内存类模式 7–10 s，事件类 8–16 s，默认全帧分析 30–90 s。
- 运行前需把 `Skills\uebabypram-insight-filter\scripts` 加入 `PATH`（`tbbmalloc.dll` 等运行时库在该目录）。

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
