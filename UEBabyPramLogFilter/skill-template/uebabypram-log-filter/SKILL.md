---
name: uebabypram-log-filter
description: 使用 UEBabyPramLogFilter 过滤 Unreal Engine .log 文件。用于按时间、级别、行号、Category 和消息内容查找日志，并把匹配行写入指定输出文件。仅在用户需要处理 UE 日志时使用。
---

# UEBabyPramLogFilter

UEBabyPramLogFilter 读取**一个** `.log` 文件，按条件筛选日志行。

## 核心行为

- `-f` 只能给一次，且文件扩展名必须是 `.log`。
- **不带 `-o`**：只查找/计数，不写任何日志文件。
- **带 `-o <file>`**：把匹配行写入该文件。
- stdout 只输出摘要，不输出日志正文：
  ```text
  Target: <输入日志>
  Output: <输出日志>        # 仅带 -o 时
  Matched: <匹配条数>
  Start: <第一条匹配的时间>
  End: <最后一条匹配的时间>
  StartLine: <第一条匹配的行号>
  EndLine: <最后一条匹配的行号>
  ```
- 没有匹配时 `Start` / `End` / `StartLine` / `EndLine` 为 `---`。
- `EndLine` 始终是最后一条匹配日志的行号，不受 `-or` 限制。

## 命令

| 选项 | 说明 |
|------|------|
| `-h, --help [condition]` | 总帮助；`-h condition` 查看条件语法 |
| `-v, --version` | 版本，当前 `UEBabyPramLogFilter <1.2>` |
| `-f, --file <path>` | 唯一输入 `.log` 文件 |
| `-c, --condition <statement>` | 过滤条件；可重复，多个 `-c` 之间为 **OR** |
| `-o, --output <path>` | 把匹配行写入该文件；省略则只统计 |
| `-oml, --output_mode_line` | 写入时给每行加 `line-<行号>:`；需要 `-o` |
| `-osf, --output_separate_frame` | 写入时在帧号变化处插入帧分隔行；需要 `-o` |
| `-or, --output_range <min> <max>` | 只写入输出序号 `[min, max)` 的行，0-based；需要 `-o` |

## 过滤条件

```text
Time <OP> <time>
Level <OP> <level>
Line <OP> <number>
Message.<FUNC>(^字符串^)
Category.<FUNC>(^字符串^)

(cond) & (cond)     AND
(cond) | (cond)     OR
!cond               NOT
```

比较符：

```text
<  <=  ==  >=  >
```

字符串函数：

```text
StartWith  EndWith  Equal  Contains  Match
```

- `Match` 使用 RE2，非锚定匹配；
- 其他函数为大小写敏感的普通字符串比较；
- 字符串由 `^...^` 包裹，字面量 `^` 写成 `^^`。

日志级别从低到高：

```text
VeryVerbose  Verbose  Log  Display  Warning  Error  Fatal
```

时间格式：

```text
YYYY.MM.DD-HH.MM.SS:mmm
YYYY.MM.DD-HH.MM.SS
HH.MM.SS:mmm
HH.MM.SS
```

多个 `-c` 是 OR。时间范围必须写在同一个 `-c` 内：

```powershell
-c "Time >= 2021.10.11-10.00.00:000 & Time < 2021.10.11-12.00.00:000"
```

## 示例

```powershell
# 只统计 Warning 及以上数量，不写日志
& ".\scripts\UEBabyPramLogFilter.exe" -f "D:\Logs\MyGame.log" -c "Level >= Warning"

# 把 Error 及以上写入文件
& ".\scripts\UEBabyPramLogFilter.exe" -f "MyGame.log" -c "Level >= Error" -o "D:\Logs\errors.log"

# 输出带行号
& ".\scripts\UEBabyPramLogFilter.exe" -f "MyGame.log" -c "Message.Contains(^Error^)" -o "out.log" -oml

# 限制写入前 50 条（Matched 仍是全部匹配数）
& ".\scripts\UEBabyPramLogFilter.exe" -f "MyGame.log" -c "Level >= Error" -o "out.log" -or 0 50

# 时间范围 + 级别
& ".\scripts\UEBabyPramLogFilter.exe" -f "MyGame.log" -c "Time >= 2021.10.11-10.00.00:000 & Time < 2021.10.11-12.00.00:000 & Level >= Warning"
```

## 注意

- `-o` 的父目录必须存在；
- `-oml`、`-osf`、`-or` 只能与 `-o` 一起使用；
- `-or` 只限制写入文件的行，摘要中的 `Matched` 是全部匹配数；
- 不带 `-o` 时不会生成 `.filterout` 文件。
