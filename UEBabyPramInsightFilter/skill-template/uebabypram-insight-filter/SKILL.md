---
name: uebabypram-insight-filter
description: 用 UEBabyPramInsightFilter 分析 Unreal Insights .utrace：GameThread 帧耗时、事件统计/调用树/火焰图、线程搜索、ContextSwitch 每线程每 core 占用、内存 tag 统计与时间线。当需要定位卡顿、统计函数耗时、查事件定义位置、看 ContextSwitch 占用或内存增长、或写 CSV 报告时使用。
---

# UEBabyPramInsightFilter 分析工具

## 工具路径（全部用相对路径）

技能包内的布局固定为 `SKILL.md` + `scripts\`，所以在**技能根目录**（即 `SKILL.md` 所在目录）下执行：

```powershell
.\scripts\UEBabyPramInsightFilter.exe -v          # 打印版本
.\scripts\UEBabyPramInsightFilter.exe -f "<trace 路径>" ...
```

## 运行模式（同一时刻只能用一个）

| 模式 | 选项 | 输出 |
|------|------|------|
| 默认 | 无 | 帧率分布 + 最长帧 + 帧内排除时间 |
| 事件位置 | `-r <regex>` | 事件名 → 定义位置（`-mlc` 限数量） |
| 事件统计 | `-es <query>` | 每个统计元素（`[^regex^]` / `%%`）：次数/总时长/均值/中位 + Top 出现 + 时长直方图 |
| Exclude 排行 | `-ex <query>` | 按事件名取最高单次 ExcludeTime 排序（`-xt` 前 X，默认 50），输出最高值/范围/Duration 与该名次数/均值/中位/标准差 |
| 详细调用树 | `-ed <query>` | 命中 query 的栈树（`-mvc`/`-mnc`/`-md` 限制规模） |
| 火焰图 | `-ef <query>` | 折叠火焰图，`-fs` 导出折叠栈文件 |
| 线程搜索 | `-ts <regex>` | 线程名 + 数量 |
| ContextSwitch | `-cs <query>` | 每个 Thread × Core：窗口内 Active 时间、占比、事件次数 |
| 内存统计 | `-ms <query>` | 每个 tracker × tag：首末/最大最小/均值 + 趋势（区间、20/40/60/80% 点、斜率、斜率 σ） |
| 内存时间线 | `-mv <query>` | 极点模式（输出极值点与过滤前总数，`-mvs` 过滤小斜率、`-mvp` 限制打印数）或 `--all_samples` 分段模式（每段起/末/最大/最小点 + 均值/中位数/标准差） |
| 内存 spec | `-mt <regex>` | 命中的 tag / tracker / tag set |
| 仅 tracker | `-mtt <regex>` | 只搜 memory tracker |
| Bookmark | `-bm <query>` | 一层 Chain 匹配解码文本 + Time/ITime；只打印事件的时间与文本 |

`-r -es -ex -ed -ef -ts -cs -ms -mv -mt -mtt -bm` 互斥。

## 通用参数

| 选项 | 说明 |
|------|------|
| `-f, --file <path>` | `.utrace` 文件 |
| `-o, --output <path>` | 报告写入文件（覆盖） |
| `-a, --append <path>` | 报告追加到文件（与 `-o` 互斥） |
| `-csv, --csv <path>` | 报告写 CSV（`-es`、`-ms`、`-mv`、默认模式） |
| `-fs, --folded_stack <path>` | `-ef` 折叠栈输出 |
| `-mlc` / `-tx` / `-dbc` | `-r` 位置数 / `-es` top 数 / `-es` 直方图桶数（默认 256 / 5 / 5） |
| `-mvc` / `-mnc` / `-md` | `-ed`/`-ef` 视图数（10）/ 每视图节点数（100，0=不限）/ 最大深度（8，0=不限） |
| `-mrf` / `-metc` | 默认模式：保留并详细分析的最长帧数（10）/ 排除时间条数（30）；所有帧都计入总耗时/帧数/帧率 |
| `-msr <name\|mean\|max>` | `-ms` 行序，默认 `name`（先 tracker 后 tag） |
| `-mst <count>` | `-ms` 最多打印多少条统计，0=不限（默认 0） |
| `-mvp` | Peak 模式最多打印极值点数（20，0=不限）；All 模式分段数（0=1 段） |
| `-mvs` | Peak 模式过滤比例：绝对斜率 < 最大绝对斜率 × 该值的极值点不打印，默认 0.05 |
| `-xt` | `-ex` 最多打印的事件名数量（50，0=不限） |
| `--all_samples` | `-mv` 分段模式：按 `-mvp` 段数均分时间，每段输出起/末/最大/最小点与统计 |
| `-h` / `-v` | 帮助 / 版本 |

## EventQuery 语法（`-es` / `-ed` / `-ef` / `-cs` / `-ms` / `-mv`）

```
<query>   := <chain> [ '&' ( Time | ITime ) '=' <slices> ] [ '&' Thread=(^regex^) ] [ '&' Tracker=(^regex^) ]
<chain>   := <element> ( '->' <element> )*
<element> := % | %% | (^regex^) | [^regex^] | %[^regex^]
<slices>  := '[' <time> ',' <time> ']' ...      <time> := 90s | 1m30s | 1m30.5s | 30.5s
```

- **正则全量匹配**：`-r`、`-ts`、`-mt`/`-mtt` 接收的 regex 与 query 里所有 `^...^` 都是**整名匹配**（`RE2::FullMatch`），`.*` 才是“任意”。默认模式不接收 regex。
- **元素**：`(^x^)` 只作为链的一环；`[^x^]` 额外统计耗时；`%[^x^]` 把命中的多个事件并成一个统计；`%` 匹配任意事件（链里用来跳层）；`%%` 匹配任意事件并按事件名分别统计耗时。
- **`.*` 是任意事件的快捷写法**：`(^.*^)` 解析成 `%`，`[^.*^]` 解析成 `%%`，`%[^.*^]` 解析成“所有事件合并成一个统计”的快捷元素；三者都不再编译正则，选中的事件与原来相同，只是省掉了逐事件的正则匹配。
- **`Time` / `ITime` 在 CPU 事件模式下不同**：`Time` = 事件时间范围与切片**相交**即选中（跨边界也计入，按整段计）；`ITime` = 事件时间范围必须**完全被某个切片包含**（或正好相等），跨边界的不算。**内存模式（`-ms`/`-mv`）下两者等价**——那里取的是快照的**时间点**，点落在切片内（含端点）即可，而「相交」与「包含」对一个点来说是同一件事。
- **`Thread`** 匹配调用栈线程名；**`Tracker`** 匹配内存 tracker 名。
- **不感兴趣的参数会被拒绝**（报错并退出，不会静默忽略）：
  `-es`/`-ed`/`-ef` 接受 chain + Time + Thread；`-ms`/`-mv` 接受 chain + Time + Tracker。
- 事件模式的链：元素按调用顺序匹配、允许中间夹其它事件；`[^.*^]`/`%%` 只命中树根（一次元素在一个子树里只匹配一次）。
- **内存模式的链**：tag 名字就是父级路径，链必须**覆盖全名**：`(^A^)->(^B^)` 只选 `A/B`，不包含其子节点；`(^Mass^)->%` 选 Mass 下一层；要全部层级就用多条 query（`-mv "%" -mv "%->%" …`，同一 tag 只出现一次）。

## 各模式要点

### 默认模式

```
Total GameThread Time / Avg / Frame Rate: 5 档 FPS 分布 + 累计占比（全部帧）
Top <mrf> Longest GameThread: E / ENCS / TR（只保留最长 N 帧）
Top ExcludeTime In Longgest Frame / In Top <mrf> Frames
```

`E` 实际耗时，`ENCS` 去掉上下文切换后的耗时（ENCS 远小于 E ⇒ 在等，不是在算）。总耗时、帧数、帧率与 `-csv` 覆盖**全部帧**；Top 列表只保留最长 `-mrf` 帧（默认 10），`OnCPUStackTree` 阶段边分析边排序，不在 Top N 的帧直接丢弃、不保留事件数据。前 4 帧和后 4 帧受保存 trace 的开销影响；如果它们进入 Top 列表，会在该帧下面额外打印 `Frame <当前>/<总数>` 和 `Recording Begin/End Phase`，并提示可能有误差、仅供参考。

`-csv` 列：`FrameIndex, Begin_s, End_s, Duration_ms, ENCS_ms, FPS`，覆盖全部帧。

### `-r` 事件位置

`-r "Tick"` 只命中名字**正好是** `Tick` 的事件（实测本 trace 有 3 个），想搜包含要写 `-r ".*Tick.*"`。`-mlc` 限制条数，0=不限。

### `-es` 事件统计

```
Name:<X>, Count:<?>, Total D:<?>, Mean D:<?>, Median D:<?>, Std D:<?>, Total E:<?>, Mean E:<?>
<0..N> TName:<线程>, D:<?>, E:<?>, ENCS:<?>, CS:<?>, TR: [b, e]
```

`D` 含子调用，`E` 不含（自耗时），`CS` 是自耗时里的非 CPU 时间。可多次给 `-es`，每个 query 独立统计。`-tx 1` 只留最长一次出现。时间条件过滤的是**发生**：`Time` 相交即计，`ITime` 要求整段落在切片内（同一时间片下 ITime 是 Time 的子集）。

`-csv` 列：`QueryIndex, Event, Kind, EventIdCount, OccurrenceCount, TotalD_ms, MeanD_ms, MedianD_ms, StdD_ms, TotalE_ms, MeanE_ms, Query`（`Kind` 为 `Name`/`Regex`，`Query` 是该行所属的 query 文本）。

### `-ex` Exclude 排行

```
ExcludeStatisticParser Output:
    Event <总事件名数>, Top <打印数>
    [1] <事件名>
        Max Exclude:<... ms>, Range:<起, 末>, Duration:<... ms>
        Count:<...>, Mean Exclude:<... ms>, Median Exclude:<... ms>, StdDev Exclude:<... ms>
```

筛选条件与 `-es` 相同；按事件名聚合，同名事件用单次最高 ExcludeTime 排序，`-xt` 限制打印名数（默认 50，0=不限）。`Max Exclude` 是该名最高单次 ExcludeTime，`Range`/`Duration` 是那次出现的时间范围与 Duration；`Count`/均值/中位/标准差覆盖筛选范围内该名的全部出现。ExcludeTime = Duration - 子事件时间。除 `Range` 外，时间值统一用 **ms**，最多 3 位小数。

### `-ed` 详细调用树

每个视图中：`D` 该节点耗时、`E` 自耗时、`CS` 自耗时中的非 CPU 时间、`TR` 时间范围。

- 时间片只决定**保留哪些树**：`Time` 保留与切片相交的树，`ITime` 只保留整棵树都落在某个切片内的树；被保留树的内容不裁剪。
- `-mnc` 超限时**整棵后续子树**都不打印，只留一个 `...`；`-md` 超深时**每层**各留一个 `...`。
- `-mnc 0` 是「不限」而不是「不打印」：本 trace 的 1.5 秒尖峰帧单视图输出 **55,582 行**。常规用 `-mnc 100~500 -md 8~20`，需要全量再显式给 0。
- `-mvc 0` 与它们不同：**0 表示一个视图都不保留**（报告为空），不是「不限」；要「尽量多」给大值（如 `-mvc 200`）。

### `-ef` 火焰图

`-fs <path>` 直接写 folded stacks：每行 `a;b;c <整数>`，整数是该路径的**自耗时（不含子路径），单位微秒**，可直接用 `flamegraph.pl` / speedscope 出图。需要「谁最贵」的排名时用 Python 按叶子聚合。

### `-ts` 线程搜索

`-ts ".*"` 列出全部线程；`-ts "GameThread"` 整名匹配。

### `-cs` ContextSwitch 统计

按 **Thread × Core** 统计 ContextSwitch 事件：

```
ContextSwitchStatisticParser Output:
    (Active is clipped to the Time Window; Share = Active / Window; Count = contributing events; only threads with a resolved name matching Thread are counted; threads are ordered by total Active)
    Query: <%&Time=[7m20s,7m30s]&Thread=(^GameThread$^)>
    Thread Regex: <GameThread$>
    Time Window Total: <0m10.000000s>
        [7m20.000000s, 7m30.000000s]
    Core Share:
        Core <0>: Active:<0m2.200000s>, Share:<22.00%>, Count:<...>
        Core <1>: Active:<0m1.600000s>, Share:<16.00%>, Count:<...>
    Thread <GameThread>, Active:<0m7.624820s>, Share:<76.25%>, Count:<6116>
        Core <0>: Active:<0m1.543752s>, Share:<15.44%>, Count:<1381>
        Core <1>: Active:<0m1.252845s>, Share:<12.53%>, Count:<1154>
```

- chain 只接受 `%`（可以是由 `%` 组成的链）且被忽略；`Time`/`ITime` 都只用来标记时间窗；没有时间条件时用全部 ContextSwitch 事件的范围。
- `Thread` 匹配完整线程名；没有 `Thread` 条件时统计所有线程。**只有 `GetThreadInfo` 能取到名字、且名字匹配 `Thread` 条件的线程才会被统计**；取不到名字或名字不匹配的线程事件直接跳过。
- `Core Share` 是整体数据：把**所有被统计线程**在同一个 core 上的 Active 相加，再除以时间窗总时长；core 列表在 Thread 过滤前从所有事件收集，因此即使某个 core 上没有命中线程也会列出（Active 为 0）。
- 线程内的每 core 行给出该线程在该 core 的 `Active` 与 `Share = Active / Time Window`；`Count` 是贡献到窗口的事件数（裁剪后仍算一次）。
- 时间窗会先与 ContextSwitch 事件范围求交，再合并重叠/相邻的时间片，所以多个时间片不会重复计算。
- 线程按 **总 Active 时间从大到小** 排序；不输出 SystemId；输出是文本，`-csv` 目前不支持 `-cs`。

### `-ms` 内存统计

```
Memory Tag <tag 数>, Memory Tracker <tracker 数>, Memory Tag Statistic <统计条数>
Tracker:<名>:
    Tracker:<名>, MemoryTag:<全名>
        Count:<n>, First:[t]=<v>, Last:[t]=<v>, Max:[t]=<v>, Min:[t]=<v>, Mean:<v>
        Trend: Interval:[t0, t1], Sample <n>, Slope StdDev:<x/s>
            20%: [t]=<v>, Slope:<x/s>    （40% / 60% / 80% 同理）
```

- 统计单位 **per tracker × per tag**，**不区分 QueryIndex**（多 query 命中同一 tag 只出一次）。
- 趋势区间 = 首次最大值与首次最小值之间的区间（按时间先后，先出现的作起点）；四点取**最近的实际采样**，区间内采样稀疏时 60% 与 80% 可能落在同一个点（该段斜率为 0）；斜率是「上一节点 → 当前节点」的插值，带单位（`B/s`…`GB/s`，可负）；`Slope StdDev` 是四个斜率的标准差。
- 文本按 `-msr` 排序；`-csv` **固定按 tracker → tag 排序**。列：`Tracker, MemoryTag, Count, FirstTime_s, FirstValue_B, LastTime_s, LastValue_B, MaxTime_s, MaxValue_B, MinTime_s, MinValue_B, Mean_B, TrendStart_s, Trend20_s, Trend20_B, Trend40_s, Trend40_B, Trend60_s, Trend60_B, Trend80_s, Trend80_B, TrendEnd_s, Slope20_Bps, Slope40_Bps, Slope60_Bps, Slope80_Bps, SlopeStdDev_Bps`。

### `-mv` 内存时间线

```
Tracker:<名>, MemoryTag:<全名>, Point <打印数> of <总采样数>
    First     : Time:<t>, Value:<v>, Slope:<x/s>
    Peak      : Time:<t>, Value:<v>, Slope:<x/s>
    Min/Peak  : Time:<t>, Value:<v>, Slope:<x/s>
    LastPeak  : Time:<t>, Value:<v>, Slope:<x/s>
    Max       : Time:<t>, Value:<v>, Slope:<x/s>
    Last      : Time:<t>, Value:<v>, Slope:<x/s>
```

`--all_samples` 分段模式：

```
    Segment <i>/<N> [起, 末], Samples <n>:
        Start/Max : Time:<t>, Value:<v>, Slope:<x/s>
        End/Min   : Time:<t>, Value:<v>, Slope:<x/s>
        Mean:<...>, Median:<...>, StdDev:<...>
```

- 默认**极点模式**：输出斜率由正转负或由负转正的采样点；**斜率为 0 不算极点**；报告头和每个 tag 行给出**过滤前**的全部极值点个数（`Extremum Points` / `Extremum`）。
- `-mvs`（默认 0.05）过滤掉绝对斜率小于「该 tag 最大绝对斜率 × ratio」的极值点；`-mvp`（默认 20，0=不限）限制实际打印的极值点数。
- 最后一个实际打印的 Peak 带 `LastPeak` 属性；额外输出 `First`/`Last`/`Max`/`Min` 四个特殊点。
- 极点模式额外输出 `First`/`Last`/`Max`/`Min` 四个特殊点；一个点可带多个属性，用 `/` 组合。
- `--all_samples`：把 tag 的时间范围均分为 `-mvp` 段（0=1 段）；每段输出 `Start`/`End`/`Max`/`Min` 四个点（带时间/数值/斜率，重合时属性合并），最后输出该段所有采样的 `Mean`、`Median`、`StdDev`（标准差，单位与值一致）。
- 所有点按时间排序；`-csv` 只写点，分段统计只在文本报告中输出。

### `-mt` / `-mtt` 内存 spec 搜索

`-mt "Mass"` 同时搜 tag / tracker / tag set；`-mtt "Default"` 只搜 tracker。两者互斥。`.*` 或空串匹配全部。

tag 行形如 `Tag <id>: <全名>, Name:<最后一段>, Parent:<id:名>, Tag Set:<id:名>`。

### `-bm` Bookmark 事件

Query 只接受 **恰好一层 Chain** 与 `Time`/`ITime`；Chain 正则对**解码后的 bookmark 文本**完整匹配，`%` 匹配全部；`Time` 与 `ITime` 等价，事件时间点落在任一时间片内（含端点）才保留，无时间条件时全部保留。只打印事件，不打印 spec、`Id` 和 `CallStack`。

```
BookMarksParser Output:
    Bookmark Event <10>
    [0] Time:<7m26.368661s>, Text:<LoadMap - /ShooterCore/Maps/L_ShooterPerf>
    [1] Time:<7m26.368686s>, Text:<LoadingScreen/Show>
```

示例：`-bm "%"`、`-bm "(^LoadMap.*^)"`、`-bm "%&Time=[7m26s,7m27s]"`。Chain 多于一层、或带 Thread/Tracker 条件会被拒绝。

## 典型命令

```powershell
$exe = ".\scripts\UEBabyPramInsightFilter.exe"   # 技能根目录下
$t   = "<trace 路径>"

& $exe -f $t -mrf 30 -metc 15 -o ".\out\frames.txt"            # 1 帧率总览
& $exe -f $t -mrf 200 -o ".\out\frames_top200.txt"              # 2 保留最长 200 帧的详细数据
& $exe -f $t -r ".*Tick.*" -mlc 20                             # 3 找事件定义位置
& $exe -f $t -es "[^UWorld::Tick^]" -tx 5                      # 4 统计耗时
& $exe -f $t -ex "(^FEngineLoop::Tick^)->[^.*^]" -xt 20          # 4b Exclude 排行
& $exe -f $t -es "[^UWorld::Tick^]&Time=[0s,10s]&Thread=(^.*GameThread.*^)"
& $exe -f $t -es "[^UWorld::Tick^]" -csv ".\out\stat.csv"      # 5 导出 CSV
& $exe -f $t -ed "[^UWorld::Tick^]" -mvc 5 -mnc 200 -md 20     # 6 调用树
& $exe -f $t -ef "[^UWorld::Tick^]"                            # 7 火焰图
& $exe -f $t -ef "[^UWorld::Tick^]" -fs ".\out\tick.folded"    # 8 折叠栈
& $exe -f $t -ts ".*"                                          # 9 线程清单
& $exe -f $t -ms "%" -msr mean -mst 20                         # 10 内存最大的 tag
& $exe -f $t -ms "(^Mass^)&Tracker=(^Default^)"                # 11 指定 tracker
& $exe -f $t -mv "(^PagefileUsed^)" -mvs 0.05 -mvp 20          # 12 内存曲线极值点
& $exe -f $t -mv "(^PagefileUsed^)" --all_samples -mvp 10      # 12b 内存曲线分段
& $exe -f $t -bm "%" -o ".\out\bookmarks.txt"                  # 13 bookmark 事件
& $exe -f $t -mv "%" --all_samples -csv ".\out\segments.csv"    # 13 分段采样（点导出 CSV）
& $exe -f $t -mt "Mass"                                        # 14 tag 树
& $exe -f $t -mtt ".*"                                         # 15 tracker 列表
& $exe -f $t -cs "%" -o ".\out\context_switch.txt"              # 16 每线程每 core 的 ContextSwitch 占用
& $exe -f $t -cs "%&Time=[7m20s,7m30s]&Thread=(^.*GameThread.*^)"  # 17 指定时间窗与线程
```

## 选项限制（违反会被拒绝并退出，不会静默忽略）

| 组合 | 规则 |
|------|------|
| 模式 | `-r -es -ed -ef -ts -cs -ms -mv -mt -mtt` 只能给一个 |
| `-csv` | 只能配 `-es`、`-ms`、`-mv` 或默认模式；不能与 `-o`/`-a` 同时用 |
| `-fs` | 只能配 `-ef`；不能与 `-o`/`-a` 同时用 |
| `-mt` / `-mtt` | 二者互斥 |
| `-a` / `-o` | 二者互斥 |
| query 条件 | `-es`/`-ed`/`-ef` 拒绝 `Tracker`，`-cs` 只接受 `%` chain + Time/ITime + Thread，`-ms`/`-mv` 拒绝 `Thread`；条件顺序固定 Time → Thread → Tracker |

## 注意事项

- **先便宜的再贵的**（本 trace，622 MB / 778 帧）：`-ms`/`-mv`/`-mt`/`-mtt` **7–10 s**（只读 LLM channel），`-r`/`-ts` **~8 s**，`-es`/`-ed`/`-ef` **8–16 s**，默认模式 **30–90 s**（耗时主要来自完整读取 trace；`-mrf` 只影响保留多少帧的详细数据，不影响总帧数/帧率统计）。
- 多个 `-es` 合并到一次运行，比跑多次快得多。
- 慢帧定位：`-mrf` 拿到 `TR:[b,e]`，再用 `-ed "[^FEngineLoop::Tick^]&ITime=[b,e]"` 取单帧（时间戳建议加 0.1ms 余量）。
- 事件名必须猜对：`-r ".*"` 可列出所有事件；query 匹配为空多半是名字不对。
- **录制首尾帧**：保存 trace 本身会影响前 4 帧和后 4 帧的耗时；它们若出现在 Top 列表里，会在帧下面标注 `Frame <当前>/<总数>` 和 `Recording Begin/End Phase`，数据可能有误差、仅供参考；总帧数/帧率仍覆盖全部帧。
