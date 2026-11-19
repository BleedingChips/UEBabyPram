---
name: uebabypram-insight-filter
description: 用 UEBabyPramInsightFilter 分析 Unreal Insights .utrace：GameThread 帧耗时、事件统计/调用树/火焰图、线程搜索、内存 tag 统计与时间线。当需要定位卡顿、统计函数耗时、查事件定义位置、看内存增长、或写 CSV 报告时使用。
---

# UEBabyPramInsightFilter 分析工具

## 工具路径（全部用相对路径）

技能包内的布局固定为 `SKILL.md` + `scripts\`，所以在**技能根目录**（即 `SKILL.md` 所在目录）下执行：

```powershell
$env:PATH = ".\scripts;$env:PATH"                 # tbbmalloc.dll 等运行时库在 scripts 里
.\scripts\UEBabyPramInsightFilter.exe -v          # 打印版本
.\scripts\UEBabyPramInsightFilter.exe -f "<trace 路径>" ...
```

- 下文所有路径都是相对路径：输出也请用相对路径（如 `.\out\frames.txt`），换机器/换目录都不会失效。
- 仓库内构建产物在 `build\windows\x64\<debug|release>\`（相对仓库根；**release 比 debug 快一个数量级**），技能包里的 `scripts\` 就是它的 release 安装结果。

## 运行模式（同一时刻只能用一个）

| 模式 | 选项 | 输出 |
|------|------|------|
| 默认 | 无 | 帧率分布 + 最长帧 + 帧内排除时间 |
| 事件位置 | `-r <regex>` | 事件名 → 定义位置（`-mlc` 限数量） |
| 事件统计 | `-es <query>` | 每个 `[^regex^]` 元素：次数/总时长/均值/中位 + Top 出现 + 时长直方图 |
| 详细调用树 | `-ed <query>` | 命中 query 的栈树（`-mvc`/`-mnc`/`-md` 限制规模） |
| 火焰图 | `-ef <query>` | 折叠火焰图，`-fs` 导出折叠栈文件 |
| 线程搜索 | `-ts <regex>` | 线程名 + 数量 |
| 内存统计 | `-ms <query>` | 每个 tracker × tag：首末/最大最小/均值 + 趋势（区间、20/40/60/80% 点、斜率、斜率 σ） |
| 内存时间线 | `-mv <query>` | 每个 tag 的首点 + 峰值 + 末点（时间/值/斜率） |
| 内存 spec | `-mt <regex>` | 命中的 tag / tracker / tag set |
| 仅 tracker | `-mtt <regex>` | 只搜 memory tracker |

`-r -es -ed -ef -ts -ms -mv -mt -mtt` 互斥。

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
| `-mrf` / `-metc` / `-sr` | 默认模式：最长帧数（10）/ 排除时间条数（30）/ 采样比例（0.9） |
| `-msr <name\|mean\|max>` | `-ms` 行序，默认 `name`（先 tracker 后 tag） |
| `-mst <count>` | `-ms` 最多打印多少条统计，0=不限（默认 0） |
| `-mvp` / `-mvs` | `-mv` 每 tag 峰值数（20）/ 两峰最小采样间隔（5） |
| `--all_samples` | `-mv` 打印全部采样点（`-mvp`/`-mvs` 失效） |
| `-h` / `-v` | 帮助 / 版本 |

## EventQuery 语法（`-es` / `-ed` / `-ef` / `-ms` / `-mv`）

```
<query>   := <chain> [ '&' ( Time | ITime ) '=' <slices> ] [ '&' Thread=(^regex^) ] [ '&' Tracker=(^regex^) ]
<chain>   := <element> ( '->' <element> )*
<element> := % | (^regex^) | [^regex^] | %[^regex^]
<slices>  := '[' <time> ',' <time> ']' ...      <time> := 90s | 1m30s | 1m30.5s | 30.5s
```

- **正则全量匹配**：`-r`、`-ts`、`-mt`/`-mtt` 接收的 regex 与 query 里所有 `^...^` 都是**整名匹配**（`RE2::FullMatch`），`.*` 才是“任意”。默认模式不接收 regex。
- **元素**：`(^x^)` 只作为链的一环；`[^x^]` 额外统计耗时；`%[^x^]` 把命中的多个事件并成一个统计；`%` 匹配任意事件（链里用来跳层）。
- **`Time` / `ITime` 在 CPU 事件模式下不同**：`Time` = 事件时间范围与切片**相交**即选中（跨边界也计入，按整段计）；`ITime` = 事件时间范围必须**完全被某个切片包含**（或正好相等），跨边界的不算。**内存模式（`-ms`/`-mv`）下两者等价**——那里取的是快照的**时间点**，点落在切片内（含端点）即可，而「相交」与「包含」对一个点来说是同一件事。
- **`Thread`** 匹配调用栈线程名；**`Tracker`** 匹配内存 tracker 名。
- **不感兴趣的参数会被拒绝**（报错并退出，不会静默忽略）：
  `-es`/`-ed`/`-ef` 接受 chain + Time + Thread；`-ms`/`-mv` 接受 chain + Time + Tracker。
- 事件模式的链：元素按调用顺序匹配、允许中间夹其它事件；`[^.*^]` 只命中树根（一次元素在一个子树里只匹配一次）。
- **内存模式的链**：tag 名字就是父级路径，链必须**覆盖全名**：`(^A^)->(^B^)` 只选 `A/B`，不包含其子节点；`(^Mass^)->%` 选 Mass 下一层；要全部层级就用多条 query（`-mv "%" -mv "%->%" …`，同一 tag 只出现一次）。

## 各模式要点

### 默认模式

```
Total GameThread Time / Avg / Frame Rate: 5 档 FPS 分布 + 累计占比
Top <mrf> Longest GameThread: E / ENCS / TR
Top ExcludeTime In Longgest Frame / In Top <mrf> Frames
```

`E` 实际耗时，`ENCS` 去掉上下文切换后的耗时（ENCS 远小于 E ⇒ 在等，不是在算）。只统计与 `-sr` 采样区间**相交**的帧，帧本身不裁剪。

`-csv` 列：`FrameIndex, Begin_s, End_s, Duration_ms, ENCS_ms, FPS`。

### `-r` 事件位置

`-r "Tick"` 只命中名字**正好是** `Tick` 的事件（实测本 trace 有 3 个），想搜包含要写 `-r ".*Tick.*"`。`-mlc` 限制条数，0=不限。

### `-es` 事件统计

```
Name:<X>, Count:<?>, Total D:<?>, Mean D:<?>, Median D:<?>, Std D:<?>, Total E:<?>, Mean E:<?>
<0..N> TName:<线程>, D:<?>, E:<?>, ENCS:<?>, CS:<?>, TR: [b, e]
```

`D` 含子调用，`E` 不含（自耗时），`CS` 是自耗时里的非 CPU 时间。可多次给 `-es`，每个 query 独立统计。`-tx 1` 只留最长一次出现。时间条件过滤的是**发生**：`Time` 相交即计，`ITime` 要求整段落在切片内（同一时间片下 ITime 是 Time 的子集）。

`-csv` 列：`QueryIndex, Event, Kind, EventIdCount, OccurrenceCount, TotalD_ms, MeanD_ms, MedianD_ms, StdD_ms, TotalE_ms, MeanE_ms, Query`（`Kind` 为 `Name`/`Regex`，`Query` 是该行所属的 query 文本）。

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
    First: [t]=<v>, Slope:<x/s>
    Peak : [t]=<v>, Slope:<x/s>
    Sample: [t]=<v>, Slope:<x/s>      （--all_samples）
    Last : [t]=<v>, Slope:<x/s>
```

- 峰值 = 斜率由正变负、由负变正或变 0 的采样；与上一保留峰值差 < 两者最大值 **5%** 视为小波丢弃（首末点总是打印）。
- `-mvs` 峰值间最小采样间隔（默认 5）；`-mvp` 峰值上限（默认 20，超出时等距抽取）。
- `-csv` 列：`Tracker, MemoryTag, PointIndex, Kind, Time_s, Value_B, Slope_Bps`（`Kind` = `First`/`Peak`/`Sample`/`Last`）。

### `-mt` / `-mtt` 内存 spec 搜索

`-mt "Mass"` 同时搜 tag / tracker / tag set；`-mtt "Default"` 只搜 tracker。两者互斥。`.*` 或空串匹配全部。

tag 行形如 `Tag <id>: <全名>, Name:<最后一段>, Parent:<id:名>, Tag Set:<id:名>`。

## 典型命令

```powershell
$exe = ".\scripts\UEBabyPramInsightFilter.exe"   # 技能根目录下
$t   = "<trace 路径>"

& $exe -f $t -mrf 30 -metc 15 -o ".\out\frames.txt"            # 1 帧率总览
& $exe -f $t -sr 1.0 -mrf 200 -o ".\out\frames_all.txt"        # 2 全范围帧表
& $exe -f $t -r ".*Tick.*" -mlc 20                             # 3 找事件定义位置
& $exe -f $t -es "[^UWorld::Tick^]" -tx 5                      # 4 统计耗时
& $exe -f $t -es "[^UWorld::Tick^]&Time=[0s,10s]&Thread=(^.*GameThread.*^)"
& $exe -f $t -es "[^UWorld::Tick^]" -csv ".\out\stat.csv"      # 5 导出 CSV
& $exe -f $t -ed "[^UWorld::Tick^]" -mvc 5 -mnc 200 -md 20     # 6 调用树
& $exe -f $t -ef "[^UWorld::Tick^]"                            # 7 火焰图
& $exe -f $t -ef "[^UWorld::Tick^]" -fs ".\out\tick.folded"    # 8 折叠栈
& $exe -f $t -ts ".*"                                          # 9 线程清单
& $exe -f $t -ms "%" -msr mean -mst 20                         # 10 内存最大的 tag
& $exe -f $t -ms "(^Mass^)&Tracker=(^Default^)"                # 11 指定 tracker
& $exe -f $t -mv "(^PagefileUsed^)" -mvp 10                    # 12 内存曲线
& $exe -f $t -mv "%" --all_samples -csv ".\out\all.csv"        # 13 全量采样
& $exe -f $t -mt "Mass"                                        # 14 tag 树
& $exe -f $t -mtt ".*"                                         # 15 tracker 列表
```

## 选项限制（违反会被拒绝并退出，不会静默忽略）

| 组合 | 规则 |
|------|------|
| 模式 | `-r -es -ed -ef -ts -ms -mv -mt -mtt` 只能给一个 |
| `-csv` | 只能配 `-es`、`-ms`、`-mv` 或默认模式；不能与 `-o`/`-a` 同时用 |
| `-fs` | 只能配 `-ef`；不能与 `-o`/`-a` 同时用 |
| `-mt` / `-mtt` | 二者互斥 |
| `-a` / `-o` | 二者互斥 |
| query 条件 | `-es`/`-ed`/`-ef` 拒绝 `Tracker`，`-ms`/`-mv` 拒绝 `Thread`；条件顺序固定 Time → Thread → Tracker |

## 注意事项

- **先便宜的再贵的**（本 trace，622 MB / 778 帧，release 实测）：`-ms`/`-mv`/`-mt`/`-mtt` **7–10 s**（只读 LLM channel），`-r`/`-ts` **~8 s**，`-es`/`-ed`/`-ef` **8–16 s**，默认模式 **30–90 s**（`-sr` 越小越快）。
- 多个 `-es` 合并到一次运行，比跑多次快得多。
- 慢帧定位：`-mrf` 拿到 `TR:[b,e]`，再用 `-ed "[^FEngineLoop::Tick^]&ITime=[b,e]"` 取单帧（时间戳建议加 0.1ms 余量）。
- 事件名必须猜对：`-r ".*"` 可列出所有事件；query 匹配为空多半是名字不对。
- `-sr` 只统计与采样区间相交的帧；`-sr 1.0` 含采集收尾帧（保存 trace 的开销）。
- 运行前把 `scripts` 目录加进 `PATH`，否则会报 `System DLL not found`（缺 `tbbmalloc.dll`）。
- 版本在各自 CLI 内维护（`-v`），两个工具（LogFilter / InsightFilter）独立版本。
- 技能包里的 `scripts\` 是 `xmake build-insight-filter-skill` 的产物；若 `-v` 与预期版本不一致，先在仓库根重新打包（`xmake build-insight-filter-skill`）再分发。
