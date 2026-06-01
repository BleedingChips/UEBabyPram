---
name: uebabypram-log-filter
description: 使用 UEBabyPramLogFilter CLI 工具过滤和格式化 Unreal Engine .log 文件。用于按时间范围、日志级别、行号、分类和日志内容进行过滤。仅在用户需要处理 UE 日志文件过滤时使用。
---

# UEBabyPramLogFilter 日志过滤工具

UEBabyPramLogFilter 是一个用于过滤和格式化 Unreal Engine（UE4/UE5）`.log` 文件的命令行工具，支持按时间范围、日志级别、行号、分类（Category）和日志内容进行匹配过滤。

## 工具路径

```
uebabypram-log-filter\scripts\UEBabyPramLogFilter.exe
```

使用 PowerShell 调用时，必须使用 `&` 调用操作符包裹带空格的路径：

```powershell
& "uebabypram-log-filter\scripts\UEBabyPramLogFilter.exe" [参数...]
```

## 核心概念

- 工具读取 `.log` 文件，根据一个或多个条件过滤日志行，输出过滤后的结果。
- 默认情况下，输出文件保存在输入文件同目录下，扩展名为 `.filterout`。
- 多个 `-c` 条件之间为 **或（OR）** 关系；单个条件内部可使用 `&&` 和 `||` 构建复杂逻辑。

---

## 命令参考

### 输入

| 选项 | 说明 |
|------|------|
| `-f, --file <path>` | 添加单个 `.log` 文件作为输入。可多次使用以处理多个文件。 |
| `-p, --path <directory>` | 扫描目录下所有 `.log` 文件（非递归）并添加为输入。 |

### 过滤条件

| 选项 | 说明 |
|------|------|
| `-c, --condition <statement>` | 添加过滤条件（EBNF 语法）。多次使用表示 OR 关系。 |

### 输出控制

| 选项 | 说明 |
|------|------|
| `-oml, --output_mode_line` | 输出模式：带行号前缀（与 `-omtl`、`-omc` 互斥） |
| `-omtl, --output_mode_only_time_and_line` | 输出模式：仅显示时间戳和行号（与 `-oml`、`-omc` 互斥） |
| `-omc, --output_mode_custom <regex> <format>` | 自定义输出模式：用正则匹配并格式化（与 `-oml`、`-omtl` 互斥，可多次使用代表多个独立模式） |
| `-osf, --output_separate_frame` | 在帧之间插入帧计数分隔线 |
| `-e, --extension <ext>` | 自定义输出文件扩展名（默认 `.filterout`） |
| `-oc, --output_count <num>` | 限制最大输出行数 |
| `-op, --out_path <directory>` | 设置输出目录 |
| `-ostd, --output_std` | 输出到 stdout 而非文件，只在查询模式中配合-oc使用 |

---

## 过滤条件语法（EBNF 语法）

### 比较条件

| 类型 | 语法 | 说明 |
|------|------|------|
| 时间 | `Time <OP> <time>` | 按时间戳过滤 |
| 级别 | `Level <OP> <level>` | 按日志级别过滤 |
| 行号 | `Line <OP> <number>` | 按行号过滤 |
| 消息 | `Message.<FUNC>("<string>")` | 按日志消息内容过滤 |
| 分类 | `Category.<FUNC>("<string>")` | 按日志分类（Category）过滤 |
| 逻辑与 | `(<cond>) && (<cond>)` | 逻辑 AND |
| 逻辑与 | `(<cond>) & (<cond>)` | 逻辑 AND |
| 逻辑或 | `(<cond>) \|\| (<cond>)` | 逻辑 OR |
| 逻辑或 | `(<cond>) \| (<cond>)` | 逻辑 OR |
| 逻辑非 | `!<cond>` | 逻辑 NOT |

### 比较运算符 `<OP>`

`<` `<=` `==` `>=` `>`

### 字符串函数 `<FUNC>`

`StartWith` `EndWith` `Equal` `Contains` `Match`（RE2 正则）

### 日志级别（从低到高）

`VeryVerbose` `Verbose` `Log` `Display` `Warning` `Error` `Fatal`

### 时间格式

| 格式 | 示例 |
|------|------|
| `YYYY.MM.DD-HH.MM.SS:mmm` | `2021.10.11-11.53.12:082` |
| `MM.DD-HH.MM.SS:mmm` | `10.11-11.53.12:082`（省略年份） |
| `DD-HH.MM.SS:mmm` | `11-11.53.12:082`（省略年、月） |
| `HH.MM.SS` | `11.53.12`（仅时间） |

---

## 自定义输出模式（-omc）占位符

| 占位符 | 说明 |
|--------|------|
| `{Time}` | 时间戳 |
| `{Level}` | 日志级别 |
| `{Line}` | 行号 |
| `{Message}` | 日志消息正文 |
| `{Category}` | 日志分类名称 |
| `{0}` - `{9}` | 正则捕获组引用（按左括号编号） |
| `{{` `}}` | 转义的花括号 |

---

## 典型示例

### 过滤警告及以上级别的日志
```powershell
& "...UEBabyPramLogFilter.exe" -f "D:\Logs\MyGame.log" -c "Level >= Warning"
```

### 过滤特定时间范围
```powershell
& "...\UEBabyPramLogFilter.exe" -f "MyGame.log" -c "Time >= 2021.10.11-10.00.00:000" -c "Time < 2021.10.11-12.00.00:000"
```

### 过滤包含 "Error" 的日志
```powershell
& "...\UEBabyPramLogFilter.exe" -f "MyGame.log" -c "Message.Contains(""Error"")"
```

### 组合条件：Error 级别且消息包含 "Fatal"
```powershell
& "...\UEBabyPramLogFilter.exe" -f "MyGame.log" -c "Level >= Error && Message.Contains(""Fatal"")"
```

### 按分类正则匹配
```powershell
& "...\UEBabyPramLogFilter.exe" -f "MyGame.log" -c "Category.Match(""Log.*"")"
```

### 处理整个目录的日志，输出到指定目录
```powershell
& "...\UEBabyPramLogFilter.exe" -p "D:\Logs" -c "Level >= Warning" -op "D:\Filtered"
```

### 仅输出时间戳和行号
```powershell
& "...\UEBabyPramLogFilter.exe" -f "MyGame.log" -c "Level >= Error" -omtl
```

### 使用自定义格式提取数据
```powershell
& "...\UEBabyPramLogFilter.exe" -f "MyGame.log" -omc "Loc:\[X=([-0-9\.]+) Y=([-0-9\.]+) Z=([-0-9\.]+)\]" "(x={1} y={2} z={3})" -ostd
```

### 限制输出行数并输出到 stdout
```powershell
& "...\UEBabyPramLogFilter.exe" -f "MyGame.log" -c "Level >= Error" -oc 50 -ostd
```

### 启用帧分隔
```powershell
& "...\UEBabyPramLogFilter.exe" -f "MyGame.log" -c "Level >= Warning" -osf
```

---

## 工作流程指南

当用户请求筛选或者查询，按以下步骤操作：

1. **确定输入**：确定用户的日志文件路径，或日志躲在目录路径
2. **理解过滤需求**：将用户的自然语言描述转换成对应的过滤条件语法
3. **选择输出方式**：
- 默认输出到文件(`.filterout` 扩展名)
- 若只需要查看符合条件的日志条数，添加 `-ostd` 参数让结果输出到stdio上，并且通过 `-oc 0` 屏蔽所有的具体日志输出
- 若需直接查看结果，添加 `-ostd` 参数，输出的结果数量可能过大，一般需要添加 `-oc 0` 或 `-oc 1` 限制输出个数
4. **选择输出格式**：确定用户的输出格式要求需求，若无特定需求可跳过
5. **构建命令**：组装完整的命令并执行，一般Windows系统下优先选用PowerShell
6. **展示结果**：想用户展示过滤结果或输出文件路径

---

## 注意事项

1. **路径中的空格**：文件路径或目录路径包含空格时，需用双引号包裹。
2. **条件嵌套**：条件字符串中的双引号在 PowerShell 中需转义为 `""`（两个双引号）。
3. **逻辑或与逻辑与**：逻辑或和逻辑与在 PowerShell 中应使用 `|` 与 `&` 。
4. **多条件关系**：多个 `-c` 条件为 **OR** 关系；单个 `-c` 内的 `&&` 为 AND 关系。
5. **互斥输出模式**：`-oml`、`-omtl`、`-omc` 三者互斥，同时只能选一个。
6. **输出目录**：使用 `-op` 时，目标目录必须已存在。
7. **正则引擎**：`Match` 函数使用 RE2 正则表达式语法。
8. **默认输出**：不指定 `-ostd` 时，每个输入 `.log` 文件会生成对应的 `.filterout` 文件。
9. **查询**：只有在查询下才会指定 `-ostd` 并且通常会跟谁指定 `-oc 0` 用以禁止输出太多日志到STDIO上。
