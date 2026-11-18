---
name: uebabypram-insight-filter
description: 分析 Unreal Engine .utrace（Unreal Insights）文件中的 CPU 事件、调用堆栈、耗时与内存 spec。支持事件位置搜索、事件耗时统计、保存指定调用堆栈、生成火焰图、线程搜索以及内存 tag/tracker/tag set 统计。仅在用户需要分析 UE .utrace 文件时使用。
---

# UEBabyPramInsightFilter 分析工具

UEBabyPramInsightFilter 是一个用于分析 Unreal Engine（UE4/UE5）`.utrace` 文件的命令行工具，支持：

- 默认模式：报告 GameThread 的帧耗时与统计；
- 事件位置搜索（`-r`）；
- 事件耗时统计（`-es`），含 Top N 最长 occurrence，可用 `-csv <path>` 直接导出 CSV 文件；
- 保存并打印指定调用堆栈（`-ed`）：query 不含捕捉元素时打印**完整**调用树（含时间片外的事件）；含捕捉元素时每次匹配只打印被捕捉节点，并附带最后一个被捕捉节点的整棵子树。含每个事件的耗时与 Exclude Time，并按节点数/层数上限截断；
- 生成火焰图（`-ef`），聚合匹配的调用路径（query 含捕捉元素时只聚合被捕捉节点与最后一个被捕捉节点的子树），可用 `-fs <path>` 直接导出 folded stacks 文件；
- 线程搜索（`-ts`）；
- 内存统计（`-ms <query>`）：按 query 选择内存 tag，按 tracker × 内存 tag 聚合其全部快照，输出开始/结尾/最大/最小值及其时刻与平均值。

## 工具路径

```
.\scripts\UEBabyPramInsightFilter.exe
```

使用 PowerShell 调用时，用 `&` 包裹带空格的路径：

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "D:\Traces\Capture.utrace" [选项...]
```

---

## 运行模式（互斥）

| 模式 | 选项 | 说明 |
|------|------|------|
| 默认 | 无 | GameThread 帧耗时分析 |
| 事件位置 | `-r, --regex <regex>` | 按事件名正则搜索事件定义位置 |
| 事件统计 | `-es, --event_stat <query>` | 统计查询匹配事件的耗时 |
| 调用堆栈 | `-ed, --event_detail <query>` | 保存匹配的 stack tree 并打印详细堆栈 |
| 火焰图 | `-ef, --event_flame_graph <query>` | 聚合匹配的调用路径为火焰图 |
| 线程搜索 | `-ts, --thread_search <regex>` | 按线程名正则搜索线程 |
| 内存统计 | `-ms, --memory_stat <query>` | 按 EventQuery 选择内存 tag，按 tracker × tag 聚合快照的值统计（首/末/最大/最小/平均），值带 B/KB/MB/GB 单位 |
| 内存查询 | `-mt, --memory_tag <regex>` | 按正则搜索内存 tag / tracker / tag set 的 spec（只读 spec，不解析快照） |
| 内存时间线 | `-mv, --memory_value <query>` | 按 EventQuery 选择 tag，按 tracker × tag 打印曲线形状：首点、峰值（斜率转向处）、末点，每点带时间/值/斜率 || `--all_samples` | `-mv` 打印**全部采样点**（首点、全部中间点、末点），不再只打峰值；`-mvp`/`-mvs` 不生效，输出很大，建议配 `-csv` |
| `-mvp, --max_peak_count <count>` | `-mv` 每个 tag 最多打印多少个峰值（首点/末点除外），默认 20，0 表示不限 |
| `-mvs, --min_peak_sample_count <count>` | `-mv` 两个打印峰值之间至少隔多少个采样点，默认 5，0 表示不限 |
| `-msr, --memory_sort <name\|mean\|max>` | `-ms` 行排序：`name`（默认，query→tracker→tag）、`mean`（平均值从大到小）、`max`（最大值从大到小） |
| `-mst, --max_tag_count <count>` | `-ms` 最多打印多少个统计，0 表示不限，默认 0；超出时报告末尾提示 |

`-r`、`-es`、`-ed`、`-ef`、`-ts`、`-ms`、`-mv`、`-mt` **不能同时使用**。

---

## 通用参数

| 选项 | 说明 |
|------|------|
| `-f, --file <path>` | 必填，`.utrace` 文件路径；每累计读取超过 1 GB 在终端打印一次 `Input <本段字节数> byte(s), Total <累计字节数> byte(s)`，读取结束时打印 `Input Read Complete, Total <累计字节数> byte(s)` |
| `-o, --output <path>` | 报告写入的文件（存在则清空重建），不给则写标准输出 |
| `-a, --append <path>` | 报告**追加**到该文件末尾（不存在则创建、原有内容保留，仅支持纯文本）；不能与 `-o` 同时使用 |
| `-h, --help` | 打印帮助 |
| `-v, --version` | 输出工具的版本并退出，输出形如 `UEBabyPramInsightFilter <1.2>` |
| `-mlc, --max_location_count <count>` | `-r` 最多报告多少个事件位置，0 表示不限，默认 256 |
| `-tx, --top_count <count>` | `-es` 每个事件名保留的 Top N 最长 occurrence，默认 5 |
| `-dbc, --duration_bucket_count <count>` | `-es` 耗时分布直方图的分组数，默认 5 |
| `-csv, --csv <path>` | 把报告写成 CSV 文件（UTF-8，RFC 4180）而不是文本报告：`-es` 时每行一个事件统计；**默认模式**时每行一帧（`FrameIndex, Begin_s, End_s, Duration_ms, ENCS_ms, FPS`，覆盖采样区间内全部帧）。不能与 `-o`/`-a` 同时使用 |
| `-fs, --folded_stack <path>` | `-ef` 把火焰图写成 folded stacks 文件（`帧;帧 <微秒>`）而不是文本报告；不能与 `-o` 同时使用，只能与 `-ef` 一起用 |
| `-mvc, --max_view_count <count>` | `-ed` 保存、`-ef` 聚合的最长 stack tree 个数，默认 10 |
| `-mnc, --max_node_count <count>` | `-ed` 单棵报告树最多打印多少个节点，超出不打印并以 `...` 标记，0 表示不限，默认 100 |
| `-md, --max_depth <depth>` | `-ed` 单棵报告树最多打印多少层，超出不打印并以 `...` 标记，0 表示不限，默认 8（根节点为第 1 层） |
| `-mrf, --max_record_frame <count>` | 默认模式记录的最长帧数量，默认 10 |
| `-metc, --max_exclude_time_count <count>` | 每个 Top 列表保留的最长 Exclude Time 事件数量，默认 30 |
| `-sr, --sampling_ratio <ratio>` | 默认模式采样的时间比例，`[0.0, 1.0]`，默认 **0.9**（只分析中间 90% 的时间范围）；报告只统计与采样时间范围有交集的帧 |


---

## EventQuery 语法（-es / -ed / -ef 共用）

```
chain [ '&' ( 'Time' | 'ITime' ) '=' [TimePoint, TimePoint]+ ] [ '&' 'Thread' '=' (^Regex^) ]
chain     := elem ( '->' elem )*
elem      := (^Regex^)     只作为调用链的一部分，不统计
          |  [^Regex^]     匹配该事件并统计其耗时
          |  %[^Regex^]    匹配该事件并统计其耗时，且该元素匹配到的所有事件合并成同一个统计口径
          |  %            匹配任意事件，只作为调用链的一部分
TimePoint := Integer 'm' Decimal 's' | Decimal 's' | Integer 's' | Integer 'm' Integer 's'
```

规则：

- 所有正则使用 RE2 语法，必须匹配**完整**事件名；
- 调用链按调用顺序匹配，后一个元素必须是前一个元素的后代（中间可以隔着其他事件）；
- **单个查询节点在一棵子树里只会匹配一次**：某节点匹配成功后，同一节点的下一个候选从它**整棵子树之后**继续查找，所以同一节点的候选之间不会互相嵌套。由此可得：用 `[^.*^]` / `(^.*^)` 这类"匹配全部名字"的写法时，**只会统计到调用树的根节点**（根是第一个候选，其余同名匹配都在根的子树里、已被消费），不能用来枚举所有事件；要统计具体事件请写明确的名字，只用于补全调用链的节点用 `%`；
- 只有 `[^...^]` 与 `%[^...^]` 元素会被统计/捕捉，`(^...^)` 与 `%` 只用于补全调用链；前者按事件名各一个统计，后者把该元素匹配到的所有事件合并成一个统计（报告里以 `Regex:<...>` 呈现）；
- 对 `-ed`/`-ef`，query 不含 `[^...^]`/`%[^...^]` 时只作为筛选条件，打印/聚合并打印匹配的整棵调用树；含捕捉元素时每次匹配只报告被捕捉的节点，并附带最后一个被捕捉节点的整棵子树；
- **时间片可以连写合并**：同一个条件里的多个时间片直接写在一起，中间没有分隔符，例如 `&Time=[0s,10s][20s,30s]` 表示两段时间片（`[]` `[]` 连写）；`&ITime=` 同理；
- `Time` 条件按**相交**过滤被统计的 occurrence：事件的 `[begin, end)` 与任一时间片有交集即通过，只贴边不算；
- `ITime` 条件按**包含或相等**过滤：事件的 `[begin, end)` 必须**整段**落在某个时间片内部（与时间片完全相等也算通过）；事件跨出时间片边界、或只与时间片有部分交集，都不通过；
- 一个查询只能带 `Time` 或 `ITime` 其中之一（`ITime` 通过的事件必然是 `Time` 通过的子集）；两者同时给出会解析失败；
- `-es` 的 `Time`/`ITime` 过滤的是**被统计的 occurrence**；`-ed`/`-ef` 里时间片只决定保留哪些 stack tree：`Time` 选与时间片相交的树，`ITime` 只选整棵树都在时间片内的树，时间片不裁剪被保留树的报告内容；
- `Thread` 条件匹配完整线程名，如 `&Thread=(^.*GameThread.*^)`；
- 正则由一对 `^` 界定：`[`、`]`、`(`、`)`、`%`、`->` 等语法字符可以直接出现在正则里，不需要转义；正则内部若要表示 `^`，需要写成 `\^`；
- `-es` 可以重复给多个 query，每个 query 独立统计。

示例：

```text
(^FEngineLoop::Tick^)->[^.*TickActor.*^]
[^UWorld::Tick^]&Time=[0s,10s]&Thread=(^.*GameThread.*^)
[^UWorld::Tick^]&ITime=[0s,10s][20s,30s]
%[^UWorld::Tick^]
```

---

## 各模式输出说明

### 默认模式 GameThread 帧耗时

```text
GameThreadParser Output:
    (Every frame, statistic and list below only covers the frames whose time range intersects the Sampled Range of -sr: a frame entirely out of the Sampled Range is not counted, and a frame which only crosses its boundary is counted with its whole time)
    Total GameThread Time: <18.663s>, Total GameFrame: <691>, Avg GameThread Time: <27.009ms>
    Frame Rate: Total <691> Frame(s), Mean Frame Time <27.009ms>, Mean <37.02> FPS
        FPS Level       Frame Time              Frames     Share
        > 120 FPS       < 8.33 ms                    0     0.00%
        60 ~ 120 FPS    8.33 ~ 16.67 ms              2     0.29%
        30 ~ 60 FPS     16.67 ~ 33.33 ms           652    94.36%
        15 ~ 30 FPS     33.33 ~ 66.67 ms            28     4.05%
        <= 15 FPS       >= 66.67 ms                  9     1.30%
        Frames Above 60 FPS: <2> (<0.29%>), Above 30 FPS: <654> (<94.65%>), Above 15 FPS: <682> (<98.70%>)
    Time Range: [1m36.925707s, 1m57.655903s] Sampled Range: [1m37.962217s, 1m56.619394s] Sampled Ratio: <0.90>
```

- **报告顶部那行括号就是采样口径声明**：下面所有帧数、帧率、耗时与 Top 列表，统计的都是**与采样时间范围（Sampled Range）有交集**的帧；完全落在采样范围外的帧不统计、不列出；跨采样范围边界的帧按**整帧耗时**统计（时间不裁剪）。
- 帧率按 5 档（120 / 60 / 30 / 15 FPS）统计：每行给出**档位**（`> 120 FPS`、`60 ~ 120 FPS` … `<= 15 FPS`）、对应的**帧耗时区间**、**帧数**与**占比**；5 档占比之和为 100%。
- `Frames Above X FPS` 是**累计**统计：帧率高于 X FPS 的帧数与占比（即该档位及以上各档之和），用来回答"有多少帧跑到了 X FPS 以上"。
- `Mean FPS` 由平均帧耗时换算（`1 / 平均帧耗时`），不是各帧帧率的算术平均。
- `-sr`（默认 **0.9**，即只取 trace 中间 90% 的时间范围）决定 `Sampled Range`；`Time Range` 是 trace 里记录的完整帧区间，`Sampled Ratio` 是实际生效的采样比例。
- 若 trace 里没有记录到 GameFrame，报告只有一行 `No GameFrame is recorded, there is nothing to report.`。

### -r 事件位置搜索

报告匹配事件名对应的定义位置（文件与行号）；CPU scope logger 事件没有位置，报告为 unknown file、line 0。

输出的文件路径是**产生 utrace 的包体打包时的源文件路径**，指向打包机器；需要用户自行将其转换为本地工作目录/源码树路径（例如替换构建机路径前缀）。

> **性能提示**：`-r` 的正则要对 trace 里的**每一个事件名**（真实 trace 常有数千个）做一次完整匹配，`.*`、`.*Tick.*` 这类宽正则会显著拖慢整次扫描并让报告膨胀；请尽量写确定的部分，例如 `FEngineLoop::Tick` 优于 `.*Tick.*`。

### -es 事件耗时统计

每个查询、每个事件（按事件 id 分开统计）输出：

```text
EventStatisticParser Output:
    Query <0>: [^A^] & Thread=(^.*GameThread.*^)
        <2> Event(s):
            Name:<A>, Count:<2834>, Total D:<1718.769ms>, Mean D:<0.606ms>, Median D:<0.134ms>, Std D:<1.738ms>, Total E:<26.759ms>, Mean E:<0.009ms>
                Top <1> Longest Occurrence:
                    <0   > TName:<RHIThread>, D:<26.401ms>, E:<0.016ms>, ENCS:<0.000ms>, CS:<24.295ms>, TR: [1m39.721219s, 1m39.747619s]
                GameFrame Statistics:
                    <11 Time(s) In One Frame>: Frames <2>, Mean Total D:<3.013ms>, Longest TR:<[1m39.336328s, 1m39.349485s]>, Longest D:<3.282ms>
                Duration Distribution <5> Bucket(s):
                    <[0.007ms, 2.105ms]>, Count:<141>, Percent:<92.16%>
```

- `Name` 行：`Count`、`Total D`、`Mean D`、`Median D`、`Std D`、`Total E`、`Mean E`（按单个 occurrence 统计）；
- `Top N Longest Occurrence`：该事件耗时最长的 N 个 occurrence，含 `D`、`E`、`ENCS`（去除离核时间的 Exclude Time）、`CS`（离核时间）、`TR`（时间范围）、`TName`（线程名）；
- `GameFrame Statistics`：把 occurrence 归到它起点所在的 GameFrame，再按"每帧出现次数"合并：
  - `N Time(s) In One Frame`：该事件这一帧里出现 N 次；
  - `Frames`：出现 N 次的帧数；
  - `Mean Total D`：这些帧里该事件总耗时的平均值；
  - `Longest TR` / `Longest D`：出现 N 次的帧中总耗时最长的帧的时间范围与耗时。
- `Duration Distribution <N> Bucket(s)`：当该事件的出现次数大于 `-tx`（Top N）时，把单个 occurrence 的耗时从最短到最长均分为 `-dbc` 个区间（默认 5，与 `-tx` 无关），每行给出区间范围、落入该区间的个数与占总个数的百分比。

#### `-es` 导出 CSV（`-csv <path>`）

加上 `-csv <path>` 后**不再打印文本报告**，而是直接把统计结果写进 `<path>`（文件存在则清空重建）：

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -es "[^UWorld::Tick^]" -csv "result.csv"
```

- 每行一个事件统计（只有 occurrence 数 > 0 的事件才写），列顺序固定为：

  `QueryIndex, Event, Kind, EventIdCount, OccurrenceCount, TotalD_ms, MeanD_ms, MedianD_ms, StdD_ms, TotalE_ms, MeanE_ms, Query`

  - `Kind`：`Name`（按事件名的统计口径）或 `Regex`（`%[^...^]` 合并口径，此时 `Event` 列是正则文本）；
  - `EventIdCount`：该统计口径覆盖的事件 id 数量（`Name` 通常为 1，`Regex` 是被合并的事件数）；
  - 耗时列单位均为毫秒，保留 6 位小数；
  - `Query` 列重复该行所属查询的查询文本，便于筛选/透视。
- 行顺序：先按 `QueryIndex`，同一查询内按 `TotalD_ms` 从大到小（`Name` 与 `Regex` 混排）。
- 编码 UTF-8（**无 BOM**），字段符合 RFC 4180：含 `,`、`"` 或换行的字段用双引号包裹，内部 `"` 写成 `""`。
- CSV 只包含事件的汇总统计；Top N occurrence、GameFrame Statistics、Duration Distribution 仍然只在文本报告里输出。
- `-csv` 只能与 `-es` 一起用，或与**默认模式**一起用（见下节），且不能与 `-o`/`-a` 同时使用（`-csv` 自己指定输出文件）。

#### 默认模式导出帧 CSV（`-csv <path>`）

在默认模式（不给 `-r`/`-es`/`-ed`/`-ef`/`-ts`/`-ms`/`-mv`/`-mt`）下给 `-csv <path>`，同样**不打印文本报告**，而是把**每一帧**写成一行：

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -csv "frames.csv"
```

- 列顺序固定：`FrameIndex, Begin_s, End_s, Duration_ms, ENCS_ms, FPS`
  - `FrameIndex`：按时间顺序从 **1** 开始编号，与文本报告 `Top N Longest GameThread` 里的编号一致；
  - `Begin_s` / `End_s`：帧的起止时间，单位秒，保留 6 位小数（trace 绝对时间）；
  - `Duration_ms`：帧耗时（毫秒）；
  - `ENCS_ms`：帧内**真正在 CPU 上**的时间（Exclude Time 去掉离核时间后的累计），即 `Duration_ms - 离核时间`；
  - `FPS`：`1000 / Duration_ms`，保留 2 位小数。
- 与文本报告的关键区别：文本报告只列最长的 `-mrf` 帧，而 CSV 覆盖**采样区间内的全部帧**并按时间排序，所以可以直接做时间序列分析（找 hitch、看 FPS 变化）。
- 采样（`-sr`，默认 0.9）依然生效：CSV 里只有采样区间内的帧。

### -ed 详细调用堆栈

对每个保存的 stack tree 输出：

```text
    View <0>: TName:<GameThread>, TR:<[0m0.000000s, 0m10.000000s]>, E:<10000.000ms>
        <A>, TR:<[0m0.000000s, 0m10.000000s]>, D:<10000.000ms>, E:<6000.000ms>
        ├─ <B>, TR:<[0m2.000000s, 0m4.000000s]>, D:<2000.000ms>, E:<2000.000ms>, CS:<500.000ms>
        └─ <C>, TR:<[0m6.000000s, 0m8.000000s]>, D:<2000.000ms>, E:<2000.000ms>
```

- `View` 行的 `E` 是整棵 stack tree 的总耗时；
- 节点行：`TR` 时间范围、`D` 耗时、`E` Exclude Time；
- `CS` 为该事件 Exclude Time 中线程不在 CPU 上的时间，仅在不为 0 时打印；
- **打印内容由 query 决定**：
  - query **不含** `[^...^]`/`%[^...^]`（没有需要捕捉的事件）时，query 只作为筛选条件，打印**完整**调用树；`Time`/`ITime` 只决定保留哪些树，时间片外的事件照样打印；
  - query **含**捕捉元素时，每次匹配输出一组，以 `Match <n>: <A -> B>` 开头；只打印被 `[^...^]`/`%[^...^]` 命中的节点（按调用顺序），并在最后一个被捕捉节点下打印它的整棵子树；被捕捉节点之上的补全链节点与祖先省略。

含捕捉元素时的示例（query `(^FEngineLoop::Tick^)->[^UWorld::Tick^]`）：

```text
    View <0>: TName:<GameThread>, TR:<[0m0.000000s, 0m10.000000s]>, E:<10000.000ms>
        Match <0>: <UWorld::Tick>
        <UWorld::Tick>, TR:<[0m1.000000s, 0m9.000000s]>, D:<8000.000ms>, E:<3000.000ms>
        ├─ <B>, TR:<[0m2.000000s, 0m4.000000s]>, D:<2000.000ms>, E:<2000.000ms>
        └─ <C>, TR:<[0m6.000000s, 0m8.000000s]>, D:<2000.000ms>, E:<2000.000ms>
```

- 每组/每棵报告树最多打印 `-mnc` 个节点（默认 100）与 `-md` 层（默认 8，根节点为第 1 层），两个上限对**每组分别生效**，超过上限的节点不打印，用 `...` 标记：
  - 节点数超限：按先序（父节点 → 子树）取前 `-mnc` 个节点，到上限的位置只打印一个 `...`，其后的节点（含整棵子树）全部省略；
  - 层数超限：被截断的最深一层，每个父节点下各打印一个 `...` 代表被省略的子树；
  - `-mnc 0` 或 `-md 0` 表示该维度不限。

截断示例（`-mnc 5 -md 2`）：

```text
    View <0>: TName:<GameThread>, TR:<[0m0.000000s, 0m10.000000s]>, E:<10000.000ms>
        <A>, TR:<[0m0.000000s, 0m10.000000s]>, D:<10000.000ms>, E:<6000.000ms>
        ├─ <B>, TR:<[0m2.000000s, 0m4.000000s]>, D:<2000.000ms>, E:<2000.000ms>
        │  └─ ...
        └─ <C>, TR:<[0m6.000000s, 0m8.000000s]>, D:<2000.000ms>, E:<2000.000ms>
           └─ ...
```

节点数上限触发时的示例：

```text
        <A>, TR:<[0m0.000000s, 0m10.000000s]>, D:<10000.000ms>, E:<6000.000ms>
        ├─ <B>, TR:<[0m2.000000s, 0m4.000000s]>, D:<2000.000ms>, E:<2000.000ms>
        └─ ...
```

### -ef 火焰图

聚合 `-mvc` 个最长的匹配 stack tree 的调用路径：

```text
EventFlameGraphParser Output:
    Query: <[^A^]>
    Total: <20000.000ms>
        <A>, D:<20000.000ms>, Percent:<100.00%>
        ├─ <B>, D:<10000.000ms>, Percent:<50.00%>
        ├─ <C>, D:<5000.000ms>, Percent:<25.00%>
        └─ <D>, D:<5000.000ms>, Percent:<25.00%>
```

- `D` 是调用路径的 inclusive 耗时（该路径所有 occurrence 的耗时之和）；
- `Percent` 是占全部匹配时间的百分比；
- 子节点按耗时从大到小排列；
- **聚合范围由 query 决定**：query 不含 `[^...^]`/`%[^...^]` 时整棵树参与聚合并打印全部调用路径；含捕捉元素时，每次匹配只聚合被捕捉的节点与最后一个被捕捉节点的整棵子树（被捕捉节点之上的补全链节点与祖先省略），同一节点在多次匹配中只计一次；
- **时间片只决定"聚合哪些树"，不裁剪事件时间**：只要一棵 stack tree 与查询 `Time` 条件的时间片相交，该树就按上面的聚合范围参与统计（不含捕捉元素时整棵，含捕捉元素时只含被捕捉节点与最后捕捉节点的子树）；事件时间不会被时间片裁剪，跨界的事件按**完整**耗时计入；
- 聚合出的所有调用路径都会打印，不再按时间片丢弃路径；因此打印出来的 `D` 是该调用路径的完整耗时（不裁剪），父子节点的时间之和不要求相等（inclusive 语义）。

示例含义（query 不含捕捉元素时）：查询 `&Time=[10s,20s]` 命中一棵 `[9s,21s]` 的 stack tree 时，整棵树（含 `9s~10s`、`20s~21s` 里的事件）都计入各自调用路径的耗时并全部打印。

#### `-ef` 导出 folded stacks（`-fs <path>`）

加上 `-fs <path>` 后**不再打印文本报告**，而是把火焰图写成 folded stacks（collapsed stacks）文件，可直接交给 flamegraph.pl / speedscope 等工具出图：

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -ef "[^UWorld::Tick^]" -fs "result.folded"
flamegraph.pl result.folded > result.svg
```

每行格式为 `帧1;帧2;...;帧N <微秒>`（权重是**微秒**整数），例如：

```text
FEngineLoop::Tick;Frame;FEngineLoop_UpdateTimeAndHandleMaxTickRate;FThreadIdleStats::FScopeIdle 315000
FEngineLoop::Tick;Frame;Tick_Engine 4364
```

- 权重是该调用路径的**自身耗时**（inclusive 减去已导出子节点的 inclusive），因此各行权重之和 = 报告里的 `Total`，不会重复计算；
- 导出图中的全部调用路径（与文本报告一致）；
- 帧名中的 `;` 会被换成 `:`，换行/制表符换成空格，保证一行只表示一条调用路径；
- 自身耗时为 0 的路径不写行（其时间已在子节点的行里），文件更紧凑；
- 行顺序与文本报告一致（按耗时从大到小深度优先遍历），同一份 trace 输出稳定；
- `-fs` 只能与 `-ef` 一起用，且不能与 `-o` 同时使用（`-fs` 自己指定输出文件）。

### -ts 线程搜索

报告匹配线程的线程 id 与 system thread id；空正则 `-ts ""` 匹配所有线程。

### -ms 内存统计

按 **EventQuery** 选择内存 tag，并按“每个 tracker × 每个内存 tag”为单位聚合该 tag 的全部快照，输出值统计：

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -ms "(^Mass^)"
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -ms "%&Thread=(^Platform^)"
```

输出形如：

```text
MemoryStatisticParser Output:
    (Memory Tag Statistic: the values which the snapshots of one tracker sampled for one memory tag which the query selects)
    (First/Last: ...; Max/Min: ...; Mean: ...)
    Sorted by: <name>
    Memory Tag <265>, Memory Tracker <2>, Memory Tag Set <1>, Memory Tag Statistic <1>

Query <0>:
        Tracker:<Default>, MemoryTag:<Mass>
            Count:<777>, First:[7m17.076858s]=<49.82 MB>, Last:[8m2.351220s]=<51.09 MB>, Max:[7m31.278041s]=<52.86 MB>, Min:[7m27.461665s]=<48.01 MB>, Mean:<50.63 MB>
            Trend: Interval:[7m27.461665s, 7m31.278041s], Sample <7>, Slope StdDev:<4.99 MB/s>
                20%: [7m28.302194s]=<52.55 MB>, Slope:<5.40 MB/s>
                40%: [7m28.568873s]=<50.28 MB>, Slope:<-8.53 MB/s>
                60%: [7m30.114857s]=<50.53 MB>, Slope:<166.41 KB/s>
                80%: [7m30.114857s]=<50.53 MB>, Slope:<0.00 B/s>
```

query 的读法与 `-es`/`-ed`/`-ef` 相同，但内存数据的形状不同，因此有三处语义不同：

- **`Thread` → tracker**：`&Thread=(^...^)` 匹配的是产生快照的 **memory tracker 名字**（如 `Default`、`Platform`），不带 `Thread` 条件则选择所有 tracker；
- **`Chain` → tag 全名**：内存 tag 的名字就是它的父级路径（`A/B/C`），链的每个元素从根开始依次匹配**一段**名字，并且**必须覆盖整个名字**才选中：`(^A^)->(^B^)` 只选 `A/B`（不再默认包含其子节点）；用 `%`（不带括号的元素）多走一层，例如 `(^A^)->(^B^)->%` 选 `A/B` 下一层的 tag，`(^Mass^)->%` 选 Mass 的 8 个子 tag；名字段数与链长不一致的 tag 不会被选中；
- **时间条件是“时间点”而不是“时间段”**：快照的 `time` 是单个时间点，
  - `Time=[a,b]`：选中 `a <= t <= b` 的快照（含边界）；
  - `ITime=[a,b]`：选中 `a < t < b` 的快照（严格在内，可能为空）。

其余规则：

- 统计单位为 **per tracker × per memory tag**：**不再区分 QueryIndex**，同一个 tag 无论被多少个 query 命中都只出现一次（重复给相同 query 也不会翻倍）；不同 tracker 采样同一 tag 时仍分别统计；
- 每行含义：`Count`（携带该 tag 的快照数）、`First/Last`（时间+该时刻的值）、`Max/Min`（最大值/最小值及其所在时刻）、`Mean`（全部值的算术平均）；
- **数值带单位**：按 1024 进制自动选 `B` / `KB` / `MB` / `GB`（≥1 GB 用 GB），保留 2 位小数，例如 `49.82 MB`、`9.42 GB`；
- 行的顺序：**先按 Tracker 名，再按 MemoryTag 名**；`-msr` 可把排序改成 `mean`（平均值从大到小）或 `max`（最大值从大到小），同值时仍按 tracker → tag 顺序，报告头会打印 `Sorted by: <...>`；文本报告按 tracker 分块（`Tracker:<名字>:`）；
- **趋势统计**：每个 tag 还输出 `Trend`，区间取「第一次达到最大值的快照」到「第一次达到最小值的快照」，在该区间上按时间等分取 **20%/40%/60%/80%** 四个点（取最近的实际采样点），给出每点的时间、值，以及**每段斜率**——即「上一个节点 → 当前节点」的插值 `(v_i − v_{i−1}) / (t_i − t_{i−1})`，**带单位**（`B/s`、`KB/s`、`MB/s`、`GB/s`，可负）；最后给出四个斜率的**标准差** `Slope StdDev`（同样带单位）；
- **打印数量上限**：`-mst <count>` 限制打印的统计条数（按 `-msr` 排序后取前 N 条，0 = 不限，默认 0）；被截断时报告末尾提示 `The report carries the first <N> statistic(s) of <M>`；
- 只累加快照里实际带 value 的 tag（快照两个数组长度不一致时，多出的 tag 跳过）；
- 加 `-csv <path>` 时改为写 CSV（不打印文本报告）：**CSV 固定按 tracker → MemoryTag 排序**（与文本报告的 `-msr` 排序无关），每行一个统计，列为
  `Tracker, MemoryTag, Count, FirstTime_s, FirstValue_B, LastTime_s, LastValue_B, MaxTime_s, MaxValue_B, MinTime_s, MinValue_B, Mean_B, TrendStart_s, Trend20_s, Trend20_B, Trend40_s, Trend40_B, Trend60_s, Trend60_B, Trend80_s, Trend80_B, TrendEnd_s, Slope20_Bps, Slope40_Bps, Slope60_Bps, Slope80_Bps, SlopeStdDev_Bps`；
  CSV 里的值是**字节数**（不带单位），单位由列名 `_B` 表示，便于直接计算；
- trace 未采集 memory 通道、或 query 未选中任何 tag 时，输出 `No memory tag is selected by the query, or the trace carries no memory tag snapshot.`。

### -mv 内存 tag 时间线

`-ms` 只给聚合值，`-mv` 则输出每个 tag 的**曲线形状**：**第一个采样点**、**最后一个采样点**，以及中间的**峰值**——即斜率由正变负、由负变正、或变为 0 的采样点，每个点打印时间、值和该点斜率（字节/秒，带单位）。

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -mv "(^Mass^)"
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -mv "(^Mass^)" -mvp 5 -mvs 50
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -mv "(^Mass^)" -csv ".\line.csv"
```

```text
    Memory Tag <265>, Memory Tracker <2>, Memory Tag Set <1>, Memory Value Line <1>
    Limit of the peaks of one tag: <20>, Sample between the peaks: <5>, Point <3> of <777>

Tracker:<Default>:
        Tracker:<Default>, MemoryTag:<Mass>, Point <3> of <777>
            First: [7m17.076858s]=<49.82 MB>, Slope:<0.00 B/s>
            Peak : [7m28.302194s]=<52.55 MB>, Slope:<-10.66 MB/s>
            Last : [8m2.351220s]=<51.09 MB>, Slope:<0.00 B/s>
```

规则：

- query 语义与 `-ms` 完全相同（`Thread` → tracker 名，链 → tag 全名，`Time`/`ITime` → 快照时间点），同样**不区分 QueryIndex**、不再包含子节点；
- **首点与末点必然打印**；首点斜率取其向后的段，末点取其向前的段；
- **小波过滤**：某峰值与上一个被保留峰值的差 < 两者最大值的 **5%** 时丢弃（首末点不受此限），避免分配器噪声淹没形状；
- **峰值间距**：`-mvs`（默认 **5**）——距上一个保留峰值不足该采样点数的峰值丢弃；
- **峰值数量上限**：`-mvp`（默认 **20**）——超过时在保留的峰值中**等距抽取**，保证覆盖整段时间线；
- **导出全部采样点**：`--all_samples` 时跳过上述峰值流程，逐点打印每个采样（`Kind` 为 `First`/`Sample`/`Last`），用于画完整曲线或交给外部工具聚合；此时 `-mvp`/`-mvs` 不生效，报告头会显示 `All samples, Point <N> of <N>`；
- **选中所有层级的 tag**：链必须覆盖全名，所以「全部 tag」用多条 query 组合——`-mv "%" -mv "%->%" -mv "%->%->%" -mv "%->%->%->%"`（同一 tag 只保留一条，实测 264 个 tag × 777 快照 = 205128 行），`-ms` 同理；
- 报告头给出 `Point <打印点数> of <总采样点数>`，打印量超过 1000 点时提示输出较大；
- `-csv <path>` 时每行一个打印点，列为 `Tracker, MemoryTag, PointIndex, Kind, Time_s, Value_B, Slope_Bps`（`Kind` 为 `First`/`Peak`/`Last`，`--all_samples` 时为 `First`/`Sample`/`Last`；值单位为字节、斜率为字节/秒）。
### -mt 内存 spec 搜索

用一条正则同时搜索 trace 里的 memory tag、memory tracker 与 memory tag set，命中的 spec 输出 id 与名字：

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -mv "(^Mass^)" -mvn 20
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -mt "^Mass"
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -mt ".*"     # 导出全部 spec
```

```text
MemoryTagSearchParser Output:
    Memory Tag <265>, Memory Tracker <2>, Memory Tag Set <1>
    Tracker <1>: <Default>
    Tracker <0>: <Platform>
    Tag <1513665333504>: <Mass>, Name:<Mass>, Parent:<1513393817856:CustomName>, Tag Set:<0:System>
    Matched <0> Tag Set(s), <2> Tracker(s), <1> Tag(s)
```

- 正则匹配**完整名字**（tag 用 FullName），`.*` 匹配全部 spec（空正则也匹配全部，但某些 shell 会把空参数丢掉）；
- tag 行给出 `FullName`、`Name`（最后一段）、父 tag 与所属 tag set 的 `id:名字`；
- 只读取 spec（`ParserRequireFlag::MEMORY_TAG`），快照事件不解析，但 trace 文件仍需从头读到尾，所以耗时主要由读文件决定；
- 用途：把 `-ms` 报告里的 tag 名字反查成 id，或用 `-mt ".*"` 导出整棵内存 tag 树。

---

## 典型示例

### 1. 默认 GameThread 分析

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "D:\Traces\Capture.utrace"
```

### 2. 搜索事件定义位置

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -r "FEngineLoop::.*"
```

### 3. 统计 TickActor 的耗时

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -es "(^FEngineLoop::Tick^)->[^.*TickActor.*^]" -tx 10
```

### 4. 统计指定时间与线程内的 UWorld::Tick

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -es "[^UWorld::Tick^]&Time=[0s,10s]&Thread=(^.*GameThread.*^)"
```

### 5. 把事件耗时统计导出为 CSV

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -es "[^UWorld::Tick^]" -csv "result.csv"
```

### 6. 保存并打印 UWorld::Tick 的详细堆栈（最多 5 个）

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -ed "[^UWorld::Tick^]" -mvc 5
```

### 7. 打印详细调用树并限制规模（每组最多 200 个节点、20 层）

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -ed "[^UWorld::Tick^]" -mnc 200 -md 20
```

### 8. 生成 UWorld::Tick 的火焰图

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -ef "[^UWorld::Tick^]"
```

### 9. 把火焰图导出为 folded stacks（可直接出图）

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -ef "[^UWorld::Tick^]" -fs "result.folded"
flamegraph.pl result.folded > result.svg
```

### 10. 搜索 GameThread

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -ts ".*GameThread.*"
```

### 11. 内存 tag 的值统计 / spec 搜索

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -ms "(^Mass^)"
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -ms "%&Thread=(^Platform^)" -csv ".\memory.csv"
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -mv "(^Mass^)" -mvn 20
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -mt "^Mass"
```

---

## 注意事项

1. `-r`、`-es`、`-ed`、`-ef`、`-ts`、`-ms`、`-mv`、`-mt` 互斥，只能选一个。
2. 事件名与线程名正则必须匹配完整名称。
3. `-es` 中只有 `[^...^]`/`%[^...^]` 元素会被统计，`(^...^)` 与 `%` 仅补全调用链；`%[^...^]` 会把该元素命中的所有事件合并为一个统计口径。
4. `-es` 的 `Time` 条件按被统计事件**占用的时间范围**与查询时间范围是否相交过滤（只贴边不算相交），不影响补全调用链的事件。
5. `-ed` 保存、`-ef` 聚合的都是**最长的 N 个** stack tree，由 `-mvc` 限制（默认 10，两种模式同一选项同一含义）；满了以后更长的会顶掉最短的那个。
6. `-ed` 的打印范围由 query 决定：不含 `[^...^]`/`%[^...^]` 时打印**完整**调用树（`Time` 条件只决定保留哪些树，不裁剪树里的事件）；含捕捉元素时每次匹配只打印被捕捉节点与最后一个被捕捉节点的整棵子树。规模由 `-mnc`（节点数，默认 100）与 `-md`（层数，默认 8）限制，超出的节点用 `...` 标记，且两个限制对每组分别生效（`0` 表示不限）。看到 `...` 说明该处被截断，可调大 `-mnc`/`-md` 重新运行。
7. `-ed`/`-ef` 只处理 `-mvc` 个最长的匹配 stack tree（不再是"全部聚合"）；同一棵树内的多次匹配，`-ed` 每次输出一组，`-ef` 合并进同一张图。
8. **宽正则有性能风险**：`-r`、`-es`、`-ed`、`-ef`（以及 `-ts`）的每个正则都要对每个事件名（或线程名）逐一完整匹配，`.*`、`(^.*^)`、`[^.*Tick.*^]` 这类写法会让整次分析变慢、报告变大；优先写名字里的确定片段，query 里若"任意事件"即可，用不带正则的 `%` 元素（它完全不参与名字匹配）。
9. `.utrace` 的开头与结尾是触发保存 trace 的时刻，保存本身会进入这些帧的耗时，所以这两端的数据不准；`-sr`（默认 **0.9**）只分析中间部分以避开两端，**报告只覆盖与采样时间范围有交集的帧**（完全在范围外的帧不统计，跨边界的帧按整帧耗时统计），并且顶部会有一行明确说明这个口径；取 `-sr 1.0` 则分析完整范围。
10. 报告默认写到标准输出；用 `-o <path>` 可写入文件（文件会被清空重建），用 `-a <path>` 可**追加**到已有文件末尾（不存在则创建，仅纯文本），`-o` 与 `-a` 不能同时使用。读取输入时，每累计超过 1 GB 会在终端打印一次 `Input <本段字节数> byte(s), Total <累计字节数> byte(s)`，读取结束时打印 `Input Read Complete, Total <累计字节数> byte(s)`（这些进度行不会进入报告文件）；如果报告同时写标准输出，进度行也会出现在标准输出里，长分析建议配 `-o`。
11. `-es` 想给程序消费时用 `-csv <path>`：直接生成 CSV 文件、不打印文本报告；CSV 只含事件汇总（不含 Top occurrence / GameFrame / 直方图）。默认模式同样支持 `-csv <path>`（每行一帧，覆盖采样区间内全部帧，按时间排序），两种用法都不能与 `-o` 或 `-a` 同时使用。
12. `-ef` 想直接出图时用 `-fs <path>`：直接生成 folded stacks 文件（不打印文本报告），权重为该调用路径的自身耗时（微秒），各行之和等于报告的 `Total`；它只能与 `-ef` 一起用，且不能与 `-o` 或 `-a` 同时使用。
13. 若 trace 没有采集 **context switch** 通道（没有任何 context switch 事件），默认模式、`-es`、`-ed` 的报告会在标题下打印一行 `No Context Switch event is recorded in the trace ...`：此时 `CS` 恒为 0、`ENCS` 与 `E` 相同，报告里的耗时是线程"被调度"的时间而不是真正占用 CPU 的时间，不要据此刻画 CPU 占用。
