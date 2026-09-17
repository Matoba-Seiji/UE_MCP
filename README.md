# UE 蓝图 MCP

当前版本：`0.5.1`

这是一个面向 Unreal Engine 4.24 的本地 MCP 插件，用于通过 MCP 客户端读取和
编辑蓝图、动画蓝图、骨架以及其他部分工程资产。

插件运行在 Unreal Editor 内，Python 桥接程序运行在本地 MCP 客户端一侧。两者
通过工程 `Saved/UEBlueprintBridge` 目录下的本地文件队列通信，不启动网络监听
服务，也不会把工程资产上传到网络。

## 功能

当前提供以下 MCP 工具：

| 工具 | 功能 |
| --- | --- |
| `ue_ping` | 查询工程、引擎和插件版本 |
| `ue_list_assets` | 分页列出 `/Game` 下的资产 |
| `ue_list_blueprints` | 列出 `/Game` 下的蓝图资产 |
| `ue_inspect_asset` | 读取资产的反射属性 |
| `ue_inspect_blueprint` | 读取图表、节点、引脚、连线和属性文本 |
| `ue_duplicate_blueprint` | 在编辑器内存中复制蓝图，拒绝覆盖已有目标 |
| `ue_create_blueprint` | 创建普通蓝图或动画蓝图 |
| `ue_edit_blueprint` | 修改引脚、连线、变量、节点、状态机和动画姿势 |
| `ue_copy_anim_nodes` | 将 AnimBP 姿势图、过渡图或 EventGraph 节点复制到当前 MCP 会话剪贴板 |
| `ue_paste_anim_nodes` | 将节点粘贴到另一个兼容 AnimBP 图表 |
| `ue_compile_blueprint` | 编译蓝图并返回错误和警告，不保存资产 |
| `ue_save_blueprint` | 编译通过后备份并保存蓝图 |
| `ue_list_skeletons` | 列出骨架资产 |
| `ue_inspect_skeleton` | 读取骨骼层级和参考姿势 |
| `ue_edit_skeleton` | 添加或修改部分骨架数据 |
| `ue_save_skeleton` | 备份并保存骨架资产 |
| `ue_pie_control` | 启动、停止或查询 PIE 状态 |
| `ue_list_actors` | 列出运行中 PIE 世界的 Actor |
| `ue_list_components` | 列出 Actor 的组件 |
| `ue_read_runtime_property` | 读取运行时公开属性 |
| `ue_inspect_animation_asset` | 读取序列、Montage、BlendSpace、SkeletalMesh 和 PhysicsAsset |
| `ue_read_animation_track` | 读取动画骨骼的原始位置、旋转和缩放关键帧 |
| `ue_sample_animation_bone` | 按时间采样动画骨骼和祖先链姿势 |
| `ue_edit_animation_asset` | 编辑动画轨迹、Root Motion、曲线、Notify 和 Montage 段 |
| `ue_copy_animation_curve` | 在两个 AnimSequence 之间复制 Float 或 Transform 曲线 |
| `ue_save_animation_asset` | 备份并保存 Sequence 或 Montage |
| `ue_read_animation_pose` | 读取 PIE 中 SkeletalMeshComponent 的求值姿势 |
| `ue_batch_animation_read` | 批量执行只读动画检查 |
| `ue_inspect_ta_asset` | 读取 BlendSpace、LevelSequence、网格、物理等扩展资产 |
| `ue_create_ta_asset` | 在内存中创建扩展资产或复制已有资产 |
| `ue_evaluate_ta_asset` | 执行扩展资产的数据或曲线求值 |
| `ue_edit_ta_asset` | 编辑扩展资产的曲线、Notify、LOD、Physics 和 Sequencer |
| `ue_save_ta_asset` | 备份并保存扩展资产 |
| `ue_batch_ta_write` | 批量执行扩展资产的内存编辑 |
| `ue_inspect_data_table` | 分页读取 DataTable 行结构和行值 |
| `ue_create_data_table` | 使用指定行结构创建 DataTable，并可写入初始行 |
| `ue_edit_data_table` | 添加/更新、删除、重命名或复制 DataTable 行 |
| `ue_save_data_table` | 备份并保存 DataTable |

## Skill 组织和工具元数据

工具仍使用原有的 MCP 名称和参数 schema，但领域清单已经按 Skill 拆分到
`server/skills`：

```text
server/skills/
  blueprint/SKILL.md + tools.json
  animation/SKILL.md + tools.json
  skeleton/SKILL.md + tools.json
  runtime/SKILL.md + tools.json
  extended-assets/SKILL.md + tools.json
```

`tools.json` 描述工具属于哪个领域，以及只读、破坏性、幂等、编辑器线程亲和性、
超时、验证策略和失败后的建议工具。`server/skills/catalog.py` 在启动时把这些
信息投影到 `tools/list` 的标准 `annotations` 和 `metadata` 字段中。当前的
schema 和统一入口仍保留在 `server/bridge.py`，以便客户端兼容；后续可以按领域
逐步迁移 schema 和执行函数，而不需要一次性改变工具名。

## 写入后的结果验证

蓝图、动画资产、骨架和扩展资产的创建或编辑会在真实 `Bridge` 上执行有限的
`before/after` 检查：

1. 读取写入前的资产状态和 revision。
2. 执行原有的编辑器请求。
3. 重新读取写入后的状态。
4. 返回两个状态的摘要、SHA-256 指纹、revision 和具体后置条件。

成功的写入结果会包含类似字段：

```json
{
  "ok": true,
  "verified": true,
  "verification": {
    "before": {"revision": "...", "counts": {"nodes": 4}},
    "after": {"revision": "...", "counts": {"nodes": 5}},
    "postconditions": [
      {"name": "revision_changed", "actual": true, "expected": true}
    ]
  }
}
```

蓝图连接、引脚默认值、节点移动、节点注释和节点删除会额外检查对应的结构；
其他资产编辑至少检查 revision 和状态指纹发生变化。若请求已经发送但结果不能
证明目标状态，响应会将 `ok` 和 `verified` 设为 `false`，并返回
`postcondition_not_met` 或 `postcondition_unobservable`。轻量测试替身可以不设置
`supports_postconditions`，因此不会被额外的编辑器读取调用影响。

动画蓝图支持状态机、状态、过渡、序列播放器、参考姿势、布尔混合、插槽、缓存
姿势、分层混合和部分 IK 节点。具体参数以 `server/bridge.py` 中注册的 MCP
工具定义为准。

## 动画和扩展资产接口

动画和其他扩展资产接口与蓝图、骨架及运行时接口属于同一套 MCP 工具体系。
具体参数、字段格式和边界见 [资产操作接口契约](docs/asset-operations.md)。

轨迹替换使用 `operation=replace_raw_track`，`name` 为已有骨骼轨迹名。
`track_json` 包含 `positions`、`rotations`、`scales` 三个数组，每个通道必须有
1 个键或序列帧数个键。位置单位厘米，旋转为归一化 xyzw 四元数，缩放无单位。
存在 SourceRawAnimationData 的序列会被拒绝，以免覆盖动画修改器工作流。
所有编辑先在内存执行；保存前应重新读取，建议先在资产副本上验证。
普通 Notify 移动后会排序，旧 index 不可继续复用；Notify State 暂不支持移动。
采样缺失轨迹使用 Skeleton 参考姿势，不等于最终渲染姿势。

- BlendSpace：读取轴、采样点和网格；Core profile 不创建、不替换 BlendSpace。
- Montage：创建/替换命名 Slot 的组合段，防止截断现有 Section/Notify。
- 曲线与 Notify：float 曲线创建/替换/删除及切线参数，普通 Notify 和 Notify State 增删移动。
- Mesh：LOD 数据摘要和屏幕尺寸设置，Morph 稀疏差值分页读取、替换和缩放。
- Physics：刚体和约束检查、质量覆盖、角度限制设置。
- Control Rig：层级初始变换、节点增删、引脚默认值和连接编辑。
- Sequencer：现有绑定下动画 Section 创建、范围/速率编辑、删除。
- 批量写入：最多 20 项，遇错即停，不自动保存，不保证原子性。

Core profile 移除了 ControlRig、ControlRigDeveloper 和 Persona 私有源码依赖，
以保持插件对常见 Unreal Engine 4.24 编辑器环境的独立性。Control Rig、BlendSpace
创建和整表替换不在此配置中。反射嵌套数组仍有 100 项限制，不是完整无损资产导出。

AnimBP 的 `ue_edit_blueprint` 增加了 `set_class_settings` 操作，可以修改
`parent_class`、`target_skeleton`、`use_multithreaded_animation_update`、
`warn_about_blueprint_usage`、`generate_const_class`、`generate_abstract_class`
和 `deprecate`。父类必须是原生 `/Script/` 下的 `AnimInstance` 子类；修改目标骨架
后应立即编译并检查图表兼容性。

AnimBP 节点复制使用当前 MCP bridge 会话内的 clipboard id，不使用系统剪贴板。
复制的节点可以来自姿势图、过渡图或 EventGraph，源和目标必须使用同一个 Target
Skeleton；内部节点连线会保留，指向源图表外部变量或对象的引用需要粘贴后重新检查。

## 上传构建产物

每次向 GitHub 上传前，应先将本次构建的通用插件打包到 `releases/`。打包脚本会验证
插件版本必须是 `0.5.1`，并记录 DLL 的 SHA-256；旧的 `0.1.0` 只读 DLL 会被拒绝：

```powershell
./scripts/package-release.ps1 `
  -BuiltPlugin ./build/Verify/Plugins/UEBlueprintBridge
```

构建、打包、提交并推送可以一次完成：

```powershell
./scripts/publish.ps1 `
  -BuiltPlugin ./build/Verify/Plugins/UEBlueprintBridge `
  -Message "release: publish core plugin build"
```

最终提交会包含 `releases/UEBlueprintBridge-...` 下的基础插件文件、Editor DLL、
modules 清单和 `package-manifest.json`。插件目录按 Unreal 的常见分发结构保留
`UEBlueprintBridge.uplugin`、`Config/`、`Source/`，以及插件实际存在时的
`Content/`、`Resources/`；当前 Core 插件没有后两个目录。明确不包含 PDB 调试符号、
中间文件、缓存或任何项目工程文件。`build/` 仍然只是临时构建目录，不会把旧缓存
一并上传。`install-release.ps1` 安装时也会校验包清单中的 DLL 哈希，并复制插件拥有的
可选内容目录。

安装只复制 `UEBlueprintBridge` 编辑器插件；MCP bridge 仍从本仓库的
`server/bridge.py` 启动。当前安装脚本会在目标工程的 `Saved/UEBlueprintBridge`
下保留安装备份。

## 环境要求

- Unreal Engine 4.24.x，并准备一个 Unreal 工程。
- Visual Studio 2019、v142 工具链和兼容的 Windows SDK，用于编译插件。
- Python 3.9 或更高版本。桥接程序只使用 Python 标准库。

预编译发布包只免除插件编译所需的 Visual Studio、Windows SDK 和
UnrealBuildTool；运行 MCP bridge 仍然需要 Python 3.9 或更高版本。

## 构建插件

关闭目标编辑器后，在 PowerShell 中执行：

```powershell
./scripts/build.ps1 -Engine C:\path\to\UE_4.24
```

如果本机工具链版本不同，可以额外指定：

```powershell
./scripts/build.ps1 `
  -Engine C:\path\to\UE_4.24 `
  -CompilerVersion 14.29.30154 `
  -WindowsSdkVersion 10.0.19041.0
```

编译产物位于 `build/Verify/Plugins/UEBlueprintBridge`。`build/` 已被 Git 忽略。

## 安装插件

关闭目标 Unreal Editor，然后执行：

```powershell
./scripts/install.ps1 -Project C:\path\to\YourProject.uproject
```

安装脚本会把插件源代码和编译产物复制到目标工程的
`Plugins/UEBlueprintBridge` 目录，并在目标工程的
`Saved/UEBlueprintBridge/install-backups` 目录中创建安装备份。安装完成后，
在工程中启用 `UEBlueprintBridge`，再重新启动编辑器。

### 使用预编译发布包

Windows 用户可以从 [GitHub v0.5.0 Release](https://github.com/Matoba-Seiji/UE_MCP/releases/tag/v0.5.0)
下载与 UE4.24 匹配的
`UEBlueprintBridge-0.5.0-UE4.24-Win64.zip`。解压后，在 PowerShell 中执行：

```powershell
./Install-UEBlueprintBridge.ps1 -Project C:\path\to\YourProject.uproject
```

发布包已经包含编译好的 Editor DLL，不需要安装 Python、Visual Studio 或
UnrealBuildTool。安装脚本会为目标工程创建安装备份，并为 DLL 生成带 SHA-256
后缀的唯一文件名；目标 Unreal Editor 必须关闭，安装完成后重新启动编辑器。

预编译包只适用于对应的 Unreal Engine、平台和编辑器 ABI。当前发布包只针对
UE4.24 Win64；其他引擎版本或平台请继续使用源码构建流程。

当前包的 SHA-256：

```text
6518EA9B68E1830E012C31D17AF9C32848D066DABF28BEA3A1015A07B994593E
```

## 配置 MCP 客户端

复制 `mcp-client.example.json`，将其中的两个占位路径替换为本机实际路径：

```json
{
  "mcpServers": {
    "ue-blueprint-bridge": {
      "command": "python",
      "args": [
        "-u",
        "C:/path/to/this-repository/server/bridge.py",
        "--project",
        "C:/path/to/your-project/YourProject.uproject"
      ]
    }
  }
}
```

也可以直接启动桥接程序：

```powershell
python -u server/bridge.py --project C:\path\to\YourProject.uproject
```

目标工程必须已经在 Unreal Editor 中打开，并且插件已经加载。预编译插件安装完成后，
仍需使用本仓库中的 `server/bridge.py` 启动 MCP bridge。桥接程序使用
UTF-8 MCP stdio，通过本地文件队列串行处理编辑器请求。

## 推荐使用流程

1. 先读取目标资产，取得最新的 `revision`。
2. 需要修改时，优先复制到新的 `/Game` 路径，避免直接修改原资产。
3. 根据最新读取结果中的 `graph_path`、`node_id` 和 `pin_id` 执行编辑。
4. 编辑后重新读取并检查结果；涉及变量或节点结构变化时，重新获取最新 ID。
5. 调用编译工具确认没有错误，再调用保存工具写入资产。

所有写操作都需要提供 `asset_path` 和 `expected_revision`。过期 revision 会被
拒绝，以避免在资产状态变化后误覆盖编辑。编译失败时，修改会保留在编辑器
内存中供继续修正，但保存工具不会写入未通过编译的蓝图。

## 注意事项

- 这是面向 UE4.24 的编辑器插件，未承诺与其他 Unreal Engine 版本完全兼容。
- 保存蓝图或骨架前会创建旧资产备份；保存内容包含该资产当前的全部内存修改。
- PIE 运行期间不允许写入资产。
- 读取结果主要是静态结构，不等同于实时运行状态；运行时读取需要先启动 PIE。
- 不要同时用多个编辑器打开同一个 Unreal 工程。
- 插件源代码位于 `Plugins/UEBlueprintBridge`，MCP 桥接程序位于 `server`。
