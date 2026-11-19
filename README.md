# 边境战役 · Border Battle

使用 Unreal Engine 5.8 和 C++ 开发的即时战略游戏。本仓库公开玩法源码、项目配置和平衡数据，供阅读实现。

## 玩法与系统

- 单位选择、框选、移动与编队站位。
- 建造、资源采集、运输与单位生产。
- 战斗、目标选择、敌方 AI 与战役回合推进。
- 战役资源管理、存档和 UMG 界面。

## 源码导航

核心代码位于 [`Source/rts/RTS`](Source/rts/RTS)：

| 目录 | 内容 |
| --- | --- |
| `Orders`、`Units` | 移动指令、单位状态与编队移动 |
| `Combat`、`AI` | 战斗与 AI 决策 |
| `Buildings`、`Economy` | 建造、生产和采集 |
| `Campaign` | 战役回合、敌方发展与持久化 |
| `Selection`、`Camera`、`UI` | 选择、镜头与界面 |
| `Tests` | 自动化测试与场景检查代码 |

平衡配置位于 [`Content/RTS/Data/Balance`](Content/RTS/Data/Balance)。

## 内容范围与打开方式

这是源码展示仓库，未包含演示场景的完整 Content。使用 UE 5.8、Visual Studio 2022 和相应 C++ 工具链可以生成项目文件、阅读和编译代码；本次发布未重新执行完整 UE 构建与运行回归。

当前游戏引用 `Toony_Tiny_RTS_Set` 的单位模型、动画、材质以及项目地图等资源。第三方素材与派生头像没有在本仓库重新分发，默认地图配置仍指向原项目地图，因此克隆后不能直接复现作品集对局。运行完整演示需要自行合法取得并接入对应素材及场景。仓库也不包含引擎、缓存、打包产物或本地编辑器工具配置。

## 来源说明

- 项目使用 Unreal Engine；现有 Epic Games 版权声明保留。引擎及第三方资源遵循各自条款。
- RTS 实现学习参考包括 [CodeLikeMe UE5 RTS with C++](https://www.youtube.com/playlist?list=PLNTm9yU0zou5_PYxEdjNbAgbVRn-daOga)。
- 移动方案调研参考 [OpenRA](https://github.com/OpenRA/OpenRA)、[0 A.D.](https://github.com/0ad/0ad) 和 [RecoilEngine](https://github.com/beyond-all-reason/RecoilEngine)；参考来源不表示本仓库取得这些项目以外的资产授权。

[作品集](https://wcy1213812138.github.io/)
