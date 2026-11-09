---
name: uebabypram-insight-filter
description: 分析 Unreal Engine .utrace（Unreal Insights）文件中的 CPU 事件、调用堆栈与耗时。支持事件位置搜索、事件耗时统计、保存指定调用堆栈、生成火焰图以及线程搜索。仅在用户需要分析 UE .utrace 文件时使用。
---

# UEBabyPramInsightFilter 分析工具

UEBabyPramInsightFilter 是一个用于分析 Unreal Engine（UE4/UE5）`.utrace` 文件的命令行工具，支持：

- 默认模式：报告 GameThread 的帧耗时与统计；
- 事件位置搜索（`-r`）；
- 事件耗时统计（`-es`），含 Top N 最长 occurrence 与其调用上下文；
- 保存并打印指定调用堆栈（`-ed`），含每个事件的耗时与 Exclude Time；
- 生成火焰图（`-ef`），聚合所有匹配的调用路径；
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
| `-o, --output <path>` | 报告写入的文件（存在则清空重建），不给则写标准输出 |
| `-h, --help` | 打印帮助 |
| `-mlc, --max_location_count <count>` | `-r` 最多报告多少个事件位置，0 表示不限，默认 256 |
| `-tx, --top_count <count>` | `-es` 每个事件名保留的 Top N 最长 occurrence，默认 5 |
| `-mc, --max_context_depth <depth>` | `-es` 调用上下文相对 occurrence 的最大深度差，默认 6 |
| `-mvc, --max_view_count <count>` | `-ed` 保留最长的多少个 stack tree，默认 10 |
| `-mrf, --max_record_frame <count>` | 默认模式记录的最长帧数量，默认 10 |
| `-metc, --max_exclude_time_count <count>` | 每个 Top 列表保留的最长 Exclude Time 事件数量，默认 30 |
| `-sr, --sampling_ratio <ratio>` | 默认模式采样的时间比例，`[0.0, 1.0]`，默认 1.0（分析完整时间范围） |

---

## EventQuery 语法（-es / -ed / -ef 共用）

```
chain [ '&' 'Time' '=' [TimePoint, TimePoint]+ ] [ '&' 'Thread' '=' (^Regex^) ]
chain     := elem ( '->' elem )*
elem      := (^Regex^)   只作为调用链的一部分，不统计
          |  [^Regex^]   匹配该事件并统计其耗时
          |  (%)         匹配任意事件，只作为调用链的一部分
TimePoint := Integer 'm' Decimal 's' | Decimal 's' | Integer 's' | Integer 'm' Integer 's'
```

规则：

- 所有正则使用 RE2 语法，必须匹配**完整**事件名；
- 调用链按调用顺序匹配，后一个元素必须是前一个元素的后代（中间可以隔着其他事件）；
- 只有 `[...]` 包裹的元素会被统计/保存，`(...)` 与 `(%)` 只用于补全调用链；
- `Time` 条件只过滤被统计的 occurrence，时间范围写成 `[begin, end]`，如 `&Time=[0s,10s]`；
- `Thread` 条件匹配完整线程名，如 `&Thread=(^.*GameThread.*^)`；
- `-es` 可以重复给多个 query，每个 query 独立统计。

示例：

```text
(^FEngineLoop::Tick^)->[^.*TickActor.*^]
[^UWorld::Tick^]&Time=[0s,10s]&Thread=(^.*GameThread.*^)
(%)->[^UWorld::Tick^]
```

---

## 各模式输出说明

### -r 事件位置搜索

报告匹配事件名对应的定义位置（文件与行号）；CPU scope logger 事件没有位置，报告为 unknown file、line 0。

输出的文件路径是**产生 utrace 的包体打包时的源文件路径**，指向打包机器；需要用户自行将其转换为本地工作目录/源码树路径（例如替换构建机路径前缀）。

### -es 事件耗时统计

每个查询、每个事件名输出：

- `Count`、`Total D`、`Mean D`、`Median D`、`Std D`、`Total E`、`Mean E`；
- `Top N Longest Occurrence` 列表：`D`（耗时）、`E`（Exclude Time）、`ENCS`（去除离核时间后的 Exclude Time）、`CS`（Context Switch Time）、`TR`（时间范围）、`TName`（线程名）；
- 每个 Top occurrence 的 `Call Context`，用 box-drawing 打印：
  - `Parents`：祖先链，最后一行是 occurrence 自身（`***[事件名]***`），其他事件为 `<事件名>`；
  - `Children`：以 occurrence 为根的子树，第一行是 occurrence 自身；
  - 每行包含 `TR`、`D`、`E`、`CS`；
  - 被深度限制截断时用 `...` 占位。

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
- `CS` 为该事件 Exclude Time 中线程不在 CPU 上的时间，仅在不为 0 时打印。

### -ef 火焰图

聚合所有匹配的 stack tree 的调用路径：

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
- 子节点按耗时从大到小排列。

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
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -es "(^FEngineLoop::Tick^)->[^.*TickActor.*^]" -tx 10 -mc 8
```

### 4. 统计指定时间与线程内的 UWorld::Tick

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -es "[^UWorld::Tick^]&Time=[0s,10s]&Thread=(^.*GameThread.*^)"
```

### 5. 保存并打印 UWorld::Tick 的详细堆栈（最多 5 个）

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -ed "[^UWorld::Tick^]" -mvc 5
```

### 6. 生成 UWorld::Tick 的火焰图

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -ef "[^UWorld::Tick^]"
```

### 7. 搜索 GameThread

```powershell
& ".\scripts\UEBabyPramInsightFilter.exe" -f "Capture.utrace" -ts ".*GameThread.*"
```

---

## 注意事项

1. `-r`、`-es`、`-ed`、`-ef`、`-ts` 互斥，只能选一个。
2. 事件名与线程名正则必须匹配完整名称。
3. `-es` 中只有 `[...]` 元素会被统计，`(...)` 与 `(%)` 仅补全调用链。
4. `-es` 的 `Time` 条件按被统计事件**占用的时间范围**与查询时间范围是否相交过滤（只贴边不算相交），不影响补全调用链的事件。
5. `-ed` 保存的是**最长的 N 个** stack tree，由 `-mvc` 限制（默认 10）；满了以后更长的会顶掉最短的那个。
6. `-ef` 会聚合所有匹配的 stack tree，没有数量限制。
7. 路径含空格时使用双引号包裹。
8. `.utrace` 的开头与结尾是触发保存 trace 的时刻，保存本身会进入这些帧的耗时，所以这两端的数据不准；`-sr` 取小于 1.0 时只分析中间部分以避开两端（默认 1.0 分析完整范围）。
9. `-r`、`-es`、`-ed`、`-ef` 的筛选范围要尽量缩小：范围太大时会输出大量信息，既慢又难以阅读。
10. 报告默认写到标准输出；用 `-o <path>` 可写入文件（文件会被清空重建），文件内容是 UTF-8 纯文本。
