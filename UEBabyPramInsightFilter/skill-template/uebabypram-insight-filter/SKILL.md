---
name: uebabypram-insight-filter
description: 分析 Unreal Engine .utrace（Unreal Insights）文件中的 CPU 事件、调用堆栈与耗时。支持事件位置搜索、事件耗时统计、保存指定调用堆栈、生成火焰图以及线程搜索。仅在用户需要分析 UE .utrace 文件时使用。
---

# UEBabyPramInsightFilter 分析工具

UEBabyPramInsightFilter 是一个用于分析 Unreal Engine（UE4/UE5）`.utrace` 文件的命令行工具，支持：

- 默认模式：报告 GameThread 的帧耗时与统计；
- 事件位置搜索（`-r`）；
- 事件耗时统计（`-es`），含 Top N 最长 occurrence，可用 `-csv <path>` 直接导出 CSV 文件；
- 保存并打印指定调用堆栈（`-ed`），打印**完整**调用树（含时间片外的事件），含每个事件的耗时与 Exclude Time，并按节点数/层数上限截断；
- 生成火焰图（`-ef`），聚合所有匹配的调用路径，可用 `-fs <path>` 直接导出 folded stacks 文件；
- 线程搜索（`-ts`）。

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

`-r`、`-es`、`-ed`、`-ef`、`-ts` **不能同时使用**。

---

## 通用参数

| 选项 | 说明 |
|------|------|
| `-f, --file <path>` | 必填，`.utrace` 文件路径 |
| `-o, --output <path>` | 报告写入的文件（存在则清空重建），不给则写标准输出；写文件时每条消息的字节数与累计字节数会直接打印到终端 |
| `-a, --append <path>` | 报告**追加**到该文件末尾（不存在则创建、原有内容保留，仅支持纯文本）；不能与 `-o` 同时使用 |
| `-h, --help` | 打印帮助 |
| `-v, --version` | 输出工具的版本并退出，输出形如 `UEBabyPramInsightFilter <1.0>` |
| `-mlc, --max_location_count <count>` | `-r` 最多报告多少个事件位置，0 表示不限，默认 256 |
| `-tx, --top_count <count>` | `-es` 每个事件名保留的 Top N 最长 occurrence，默认 5 |
| `-dbc, --duration_bucket_count <count>` | `-es` 耗时分布直方图的分组数，默认 5 |
| `-csv, --csv <path>` | 把报告写成 CSV 文件（UTF-8，RFC 4180）而不是文本报告：`-es` 时每行一个事件统计；**默认模式**时每行一帧（`FrameIndex, Begin_s, End_s, Duration_ms, ENCS_ms, FPS`，覆盖采样区间内全部帧）。不能与 `-o`/`-a` 同时使用 |
| `-fs, --folded_stack <path>` | `-ef` 把火焰图写成 folded stacks 文件（`帧;帧 <微秒>`）而不是文本报告；不能与 `-o` 同时使用，只能与 `-ef` 一起用 |
| `-mvc, --max_view_count <count>` | `-ed` 保存、`-ef` 聚合的最长 stack tree 个数，默认 10 |
| `-mnc, --max_node_count <count>` | `-ed` 单棵 stack tree 最多打印多少个节点，超出不打印并以 `...` 标记，0 表示不限，默认 100 |
| `-md, --max_depth <depth>` | `-ed` 单棵 stack tree 最多打印多少层，超出不打印并以 `...` 标记，0 表示不限，默认 8（根节点为第 1 层） |
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
- 只有 `[^...^]` 与 `%[^...^]` 元素会被统计/保存，`(^...^)` 与 `%` 只用于补全调用链；前者按事件名各一个统计，后者把该元素匹配到的所有事件合并成一个统计（报告里以 `Regex:<...>` 呈现）；
- **时间片可以连写合并**：同一个条件里的多个时间片直接写在一起，中间没有分隔符，例如 `&Time=[0s,10s][20s,30s]` 表示两段时间片（`[]` `[]` 连写）；`&ITime=` 同理；
- `Time` 条件按**相交**过滤被统计的 occurrence：事件的 `[begin, end)` 与任一时间片有交集即通过，只贴边不算；
- `ITime` 条件按**包含或相等**过滤：事件的 `[begin, end)` 必须**整段**落在某个时间片内部（与时间片完全相等也算通过）；事件跨出时间片边界、或只与时间片有部分交集，都不通过；
- 一个查询只能带 `Time` 或 `ITime` 其中之一（`ITime` 通过的事件必然是 `Time` 通过的子集）；两者同时给出会解析失败；
- `-es` 的 `Time`/`ITime` 过滤的是**被统计的 occurrence**；`-ed`/`-ef` 报告的是整棵树，因此这两个模式里时间片选择的是**整棵树**：`Time` 选与时间片相交的树，`ITime` 只选整棵树都在时间片内的树；
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

在默认模式（不给 `-r`/`-es`/`-ed`/`-ef`/`-ts`）下给 `-csv <path>`，同样**不打印文本报告**，而是把**每一帧**写成一行：

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
- **总是打印完整调用树**：查询的 `Time` 条件只决定"保留哪些 stack tree"，不决定树里打印哪些事件。保留下来的树里，落在时间范围外（时间片外）的事件照样完整打印，其耗时字段也照常计算；
- 单棵树最多打印 `-mnc` 个节点（默认 100）与 `-md` 层（默认 8，根节点为第 1 层），两个上限对**每棵树分别生效**，超过上限的节点不打印，用 `...` 标记：
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
- **时间片只决定"聚合哪些树"，不裁剪事件时间**：只要一棵 stack tree 与查询 `Time` 条件的时间片相交，这棵树就**整棵**参与统计——树下的所有事件（包括完全落在时间片外的事件）都计入各自的调用路径，且任何事件的时间都不会被时间片裁剪；跨界的事件按**完整**耗时计入；
- **打印比统计更窄**：只有"至少有一个 occurrence 落在时间片内"的调用路径会被打印；完全落在时间片外的调用路径不打印，但它的时间仍然属于其父节点的 inclusive 耗时。因此打印出来的 `D` 是该调用路径的完整耗时（不裁剪），父子节点的时间之和不要求相等（inclusive 语义）。

示例含义：查询 `&Time=[10s,20s]` 命中一棵 `[9s,21s]` 的 stack tree 时，整棵树（含 `9s~10s`、`20s~21s` 里的事件）都计入各自调用路径的耗时；但一条只在时间片外出现过的调用路径不会出现在报告里，它的时间只体现在其父路径的 inclusive 耗时中。

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
- 只导出报告会打印的调用路径（与时间片相交的）；时间片外被略过的子路径，其时间计入持有它的父路径；
- 帧名中的 `;` 会被换成 `:`，换行/制表符换成空格，保证一行只表示一条调用路径；
- 自身耗时为 0 的路径不写行（其时间已在子节点的行里），文件更紧凑；
- 行顺序与文本报告一致（按耗时从大到小深度优先遍历），同一份 trace 输出稳定；
- `-fs` 只能与 `-ef` 一起用，且不能与 `-o` 同时使用（`-fs` 自己指定输出文件）。

### -ts 线程搜索

报告匹配线程的线程 id 与 system thread id；空正则 `-ts ""` 匹配所有线程。

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

### 7. 打印完整调用树但限制规模（每棵树最多 200 个节点、20 层）

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

---

## 注意事项

1. `-r`、`-es`、`-ed`、`-ef`、`-ts` 互斥，只能选一个。
2. 事件名与线程名正则必须匹配完整名称。
3. `-es` 中只有 `[^...^]`/`%[^...^]` 元素会被统计，`(^...^)` 与 `%` 仅补全调用链；`%[^...^]` 会把该元素命中的所有事件合并为一个统计口径。
4. `-es` 的 `Time` 条件按被统计事件**占用的时间范围**与查询时间范围是否相交过滤（只贴边不算相交），不影响补全调用链的事件。
5. `-ed` 保存、`-ef` 聚合的都是**最长的 N 个** stack tree，由 `-mvc` 限制（默认 10，两种模式同一选项同一含义）；满了以后更长的会顶掉最短的那个。
6. `-ed` 打印的树是**完整**的：查询 `Time` 条件只决定保留哪些树，不裁剪树里的事件；树规模由 `-mnc`（节点数，默认 100）与 `-md`（层数，默认 8）限制，超出的节点用 `...` 标记，且两个限制对每棵树分别生效（`0` 表示不限）。看到 `...` 说明该处被截断，可调大 `-mnc`/`-md` 重新运行。
7. `-ef` 只聚合 `-mvc` 个最长的匹配 stack tree（不再是"全部聚合"）。
8. **宽正则有性能风险**：`-r`、`-es`、`-ed`、`-ef`（以及 `-ts`）的每个正则都要对每个事件名（或线程名）逐一完整匹配，`.*`、`(^.*^)`、`[^.*Tick.*^]` 这类写法会让整次分析变慢、报告变大；优先写名字里的确定片段，query 里若"任意事件"即可，用不带正则的 `%` 元素（它完全不参与名字匹配）。
9. `.utrace` 的开头与结尾是触发保存 trace 的时刻，保存本身会进入这些帧的耗时，所以这两端的数据不准；`-sr`（默认 **0.9**）只分析中间部分以避开两端，**报告只覆盖与采样时间范围有交集的帧**（完全在范围外的帧不统计，跨边界的帧按整帧耗时统计），并且顶部会有一行明确说明这个口径；取 `-sr 1.0` 则分析完整范围。
10. 报告默认写到标准输出；用 `-o <path>` 可写入文件（文件会被清空重建），用 `-a <path>` 可**追加**到已有文件末尾（不存在则创建，仅纯文本），`-o` 与 `-a` 不能同时使用。写文件时，每写一条报告消息都会在终端打印该条消息的字节数与本次运行已写入的累计字节数（这些进度行不会进入报告文件），可用来观察长报告的输出进度。
11. `-es` 想给程序消费时用 `-csv <path>`：直接生成 CSV 文件、不打印文本报告；CSV 只含事件汇总（不含 Top occurrence / GameFrame / 直方图）。默认模式同样支持 `-csv <path>`（每行一帧，覆盖采样区间内全部帧，按时间排序），两种用法都不能与 `-o` 或 `-a` 同时使用。
12. `-ef` 想直接出图时用 `-fs <path>`：直接生成 folded stacks 文件（不打印文本报告），权重为该调用路径的自身耗时（微秒），各行之和等于报告的 `Total`；它只能与 `-ef` 一起用，且不能与 `-o` 或 `-a` 同时使用。
13. 若 trace 没有采集 **context switch** 通道（没有任何 context switch 事件），默认模式、`-es`、`-ed` 的报告会在标题下打印一行 `No Context Switch event is recorded in the trace ...`：此时 `CS` 恒为 0、`ENCS` 与 `E` 相同，报告里的耗时是线程"被调度"的时间而不是真正占用 CPU 的时间，不要据此刻画 CPU 占用。