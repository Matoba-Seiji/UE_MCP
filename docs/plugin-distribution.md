# 插件分发清单

本项目分成两种上传内容：源码仓库内容，以及带预编译插件的发布包。两者都只描述
通用 UE 插件和 MCP bridge，不包含任何具体工程的资产、配置或构建缓存。

## 源码仓库

应保留以下内容：

- `Plugins/UEBlueprintBridge/UEBlueprintBridge.uplugin`
- `Plugins/UEBlueprintBridge/Config/`（插件配置存在时）
- `Plugins/UEBlueprintBridge/Source/`（需要源码构建时）
- `Plugins/UEBlueprintBridge/Content/`（插件自带 `.uasset`、Python 或本地化内容时）
- `Plugins/UEBlueprintBridge/Resources/`（插件图标等资源存在时）
- `server/`、`scripts/`、`docs/` 和根目录 README 等 MCP 工程文件

`Content/` 和 `Resources/` 是按实际需要加入的目录，不为了模仿其他插件而创建空目录。
KawaiiPhysics 使用这两个目录保存本地化、Python 工具和图标；本插件当前不依赖它们。

## 带 DLL 的发布包

`scripts/package-release.ps1` 会在 `releases/` 生成：

- 插件清单、配置、源码，以及实际存在的 Content/Resources；
- 对应 Unreal Engine 和平台的 `Binaries/Win64/UE4Editor-UEBlueprintBridge.dll`；
- 与 DLL 匹配的 `Binaries/Win64/UE4Editor.modules`；
- 记录版本、引擎、平台和 DLL SHA-256 的 `package-manifest.json`。

发布包不应包含：

- `*.pdb`、`Intermediate/`、`Saved/`、`DerivedDataCache/` 或临时构建目录；
- 任意 `.uproject`、示例工程、游戏资产或项目专属路径；
- 只有本机可用的旧 DLL、调试产物或未验证的二进制文件。

如果插件将来增加 Content 或 Resources，打包和两种安装脚本会自动复制这些目录；
当前 `0.5.1` 包仍保持最小结构。
