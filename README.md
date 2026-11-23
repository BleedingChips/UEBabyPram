# UEBabyPram

一组 Unreal Engine（UE4/UE5）外部辅助工具：过滤 UE `.log` 日志、分析 Unreal Insights `.utrace` 文件。

## UEBabyPramLogFilter —— 日志过滤

读取单个 UE `.log`，按 **时间、级别、行号、Category、消息内容** 组合过滤（`&` 与、`|` 或、`!` 非），支持大文件流式读取。默认只统计匹配结果；给 `-o` 时把匹配行写成新日志，并可选择输出行号、插入帧分隔线、限制输出行范围。

## UEBabyPramInsightFilter —— trace 分析

读取 `.utrace`，用于定位卡顿、耗时与内存问题：

- GameThread 帧耗时、帧率分布与最长帧；
- 事件耗时统计、Exclude Time 排行、调用树、火焰图（可导出 folded stacks）；
- 线程搜索、ContextSwitch 每线程 × core 占用；
- 内存 tag 统计与时间线（可导出 CSV）、tag/tracker 搜索；
- Bookmark 事件。

## 详细命令与参数

- 直接运行 `-h` / `--help` 查看全部参数；LogFilter 的条件语法用 `UEBabyPramLogFilter -h condition`。
- 完整说明（示例、EventQuery 语法、CSV 列、输出字段）见 skill 文档：
  - [UEBabyPramLogFilter SKILL.md](UEBabyPramLogFilter/skill-template/uebabypram-log-filter/SKILL.md)
  - [UEBabyPramInsightFilter SKILL.md](UEBabyPramInsightFilter/skill-template/uebabypram-insight-filter/SKILL.md)
  - 打包后的分发版本位于 `Skills\uebabypram-log-filter\`、`Skills\uebabypram-insight-filter\`（含 `SKILL.md` 与 `scripts\`）。

## 构建

要求 Windows x64、Visual Studio 2022 与 [xmake](https://xmake.io/)；依赖的 [Potato](https://github.com/BleedingChips/Potato) 仓库需 Clone 到本仓库同级目录（`../Potato/`）。也可通过 `includes("../UEBabyPram/")` 作为子项目引入。

```powershell
xmake build UEBabyPramLogFilter
xmake build UEBabyPramInsightFilter
```

打包为 skill（release 构建，并把 exe 与运行时 DLL 部署到 `scripts\`）：

```powershell
xmake build-logfilter-skill
xmake build-insight-filter-skill
```

## 许可证

[MIT](LICENSE)
