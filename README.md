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
chain [ '&' 'Time' '=' [TimePoint, TimePoint]+ ] [ '&' 'Thread' '=' (^Regex^) ]
chain     := elem ( '->' elem )*
elem      := (^Regex^)     只作为调用链的一部分，不统计
          |  [^Regex^]     匹配该事件并统计其耗时
          |  %[^Regex^]    匹配该事件并统计其耗时，且该元素匹配到的所有事件合并成同一个统计口径
          |  %            匹配任意事件，只作为调用链的一部分
TimePoint := Integer 'm' Decimal 's' | Decimal 's' | Integer 's' | Integer 'm' Integer 's'
```
- 调用链按调用顺序匹配，后一个元素必须是前一个元素的后代；
- 只有 `[^...^]`/`%[^...^]` 元素会被统计（`%[^...^]` 合并该元素命中的所有事件为一个统计口径），`(^...^)` 与 `%` 仅补全调用链；
- 正则由一对 `^` 界定：`[`、`]`、`(`、`)`、`%`、`->` 等语法字符可直接出现在正则里，正则内表示 `^` 需写成 `\^`；
- `Time` 条件只过滤被统计的 occurrence，例如 `&Time=[0s,10s]`；
- `Thread` 条件匹配完整线程名，例如 `&Thread=(^.*GameThread.*^)`。

示例：
```text
(^FEngineLoop::Tick^)->[^.*TickActor.*^]
[^UWorld::Tick^]&Time=[0s,10s]&Thread=(^.*GameThread.*^)
%[^UWorld::Tick^]
```

> **性能提示**：`-r`、`-es`、`-ed`、`-ef` 里的每个正则都要对 trace 中的每一个事件名（真实 trace 常有数千个）做完整匹配，`.*`、`(^.*^)`、`[^.*Tick.*^]` 这类宽正则会让整次分析变慢、报告变大；请写名字里确定的部分，query 里若"任意事件"即可，用不带正则的 `%` 元素。
#### 常用参数

| 选项 | 说明 |
|------|------|
| `-f, --file <path>` | 必填，`.utrace` 文件路径 |
| `-tx, --top_count <count>` | `-es` 每个事件名保留的 Top N 最长 occurrence，默认 5 |
| `-dbc, --duration_bucket_count <count>` | `-es` 耗时分布直方图的分组数，默认 5 |
| `-mvc, --max_view_count <count>` | `-ed` 最多保存多少个 stack tree，默认 10 |
| `-mnc, --max_node_count <count>` | `-ed` 单棵 stack tree 最多打印多少个节点，超出不打印并以 `...` 标记，0 表示不限，默认 100 |
| `-md, --max_depth <depth>` | `-ed` 单棵 stack tree 最多打印多少层，超出不打印并以 `...` 标记，0 表示不限，默认 8（根节点为第 1 层） |
| `-mlc, --max_location_count <count>` | `-r` 最多报告多少个事件位置，默认 256 |
| `-mrf, --max_record_frame <count>` | 默认模式记录的最长帧数量，默认 10 |
| `-metc, --max_exclude_time_count <count>` | 每个 Top 列表保留的最长 Exclude Time 事件数量，默认 30 |
| `-sr, --sampling_ratio <ratio>` | 默认模式采样比例，默认 0.8 |

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

UEBabyPramInsightFilter -f .\Capture.utrace -ed "[^UWorld::Tick^]" -mvc 5

# 每棵树最多打印 200 个节点、20 层，超出部分以 ... 标记
UEBabyPramInsightFilter -f .\Capture.utrace -ed "[^UWorld::Tick^]" -mvc 5 -mnc 200 -md 20

UEBabyPramInsightFilter -f .\Capture.utrace -ef "[^UWorld::Tick^]"

# 搜索 GameThread
UEBabyPramInsightFilter -f .\Capture.utrace -ts ".*GameThread.*"
```

#### 输出字段说明

- `-r`：文件路径是产生 utrace 的包体打包时的源文件路径，指向打包机器；需要用户自行转换为本地工作目录/源码树路径。
- `-es`：`Count`、`Total D/Mean D/Median D/Std D`、`Total E/Mean E`；Top occurrence 行的 `D`（耗时）、`E`（Exclude Time）、`ENCS`（去除离核时间后的 Exclude Time）、`CS`（Context Switch Time）、`TR`（时间范围）、`TName`（线程名）；Call Context 以 box-drawing 打印父链与子树。
- `-ed`：`View` 行的 `E` 为整棵 stack tree 总耗时；节点行的 `TR` 为时间范围、`D` 为耗时、`E` 为 Exclude Time、`CS` 为 Exclude Time 中线程不在 CPU 上的时间（为 0 时不打印）。打印的是**完整**调用树：查询的 `Time` 条件只决定保留哪些树，时间片外的事件照样打印；单棵树规模由 `-mnc`（节点数，默认 100）与 `-md`（层数，默认 8）限制，超出的节点不打印并以 `...` 标记，两个限制对每棵树分别生效（`0` 表示不限）。
- `-ef`：`D` 为调用路径的 inclusive 耗时，`Percent` 为占全部匹配时间的百分比。

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

生成目录可通过 `--output=<dir>` 指定，默认为仓库根目录下的 `Skills`。

## 许可证

[MIT](LICENSE)