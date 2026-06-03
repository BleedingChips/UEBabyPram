# UEBabyPram

一些UE外部辅助工具

## 安装

从 Github 上将以下库Clone到本地，并使其保持在同一个路径下：

```
https://github.com/BleedingChips/UEBabyPram.git
https://github.com/BleedingChips/Potato.git
```

在包含该项目的 xmake.lua 上，添加如下代码即可：

```lua
includes("../Potato/")
includes("../UEBabyPram/")

target(xxx)
	...
	add_deps("UEBabyPram")
target_end()
```

运行 `xmake_install.ps1` 安装 `xmake`，运行`xmake_generate_vs_project.ps1`将在`vsxmake2022`下产生vs的项目文件。或直接使用 `xmake build-xxx-skill` 来直接在 `.\skills\` 下生成对应的SKILL

## 功能

### LogFilter 

根据时间，类别，行数，日志内容，来对UE日志进行筛选，查找，以及结构化输出

#### 特点

1. 流式加载，支持大文件
1. 支持多条件组合查询
1. 支持自定义格式化输出
1. AI友好，一键生成对应的SKILL

#### 构建方法：

```
xmake build-logfilter-skill
```
