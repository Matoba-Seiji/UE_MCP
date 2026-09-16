# UE 蓝图 MCP

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
| `ue_save_animation_asset` | 备份并保存 Sequence 或 Montage |
| `ue_read_animation_pose` | 读取 PIE 中 SkeletalMeshComponent 的求值姿势 |
| `ue_batch_animation_read` | 批量执行只读动画检查 |
| `ue_inspect_ta_asset` | 读取 BlendSpace、LevelSequence、Control Rig 等扩展资产 |
| `ue_create_ta_asset` | 在内存中创建扩展资产或复制已有资产 |
| `ue_evaluate_ta_asset` | 执行扩展资产的数据或曲线求值 |
| `ue_edit_ta_asset` | 编辑扩展资产的曲线、Notify、LOD、Physics、Control Rig 和 Sequencer |
| `ue_save_ta_asset` | 备份并保存扩展资产 |
| `ue_batch_ta_write` | 批量执行扩展资产的内存编辑 |

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

- BlendSpace：替换轴和采样点，在临时副本预检后重建采样网格。
- Montage：创建/替换命名 Slot 的组合段，防止截断现有 Section/Notify。
- 曲线与 Notify：float 曲线创建/替换/删除及切线参数，普通 Notify 和 Notify State 增删移动。
- Mesh：LOD 数据摘要和屏幕尺寸设置，Morph 稀疏差值分页读取、替换和缩放。
- Physics：刚体和约束检查、质量覆盖、角度限制设置。
- Control Rig：层级初始变换、节点增删、引脚默认值和连接编辑。
- Sequencer：现有绑定下动画 Section 创建、范围/速率编辑、删除。
- 批量写入：最多 20 项，遇错即停，不自动保存，不保证原子性。

这些接口增加了 UE4.24 ControlRig 插件依赖。BlendSpace 重建依赖本机引擎
Persona 私有源码，不能直接当作其他引擎版本的兼容实现。反射嵌套数组仍有
100 项限制，不是完整无损资产导出。

## 环境要求

- Unreal Engine 4.24.x，并准备一个 Unreal 工程。
- Visual Studio 2019、v142 工具链和兼容的 Windows SDK，用于编译插件。
- Python 3.9 或更高版本。桥接程序只使用 Python 标准库。

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

目标工程必须已经在 Unreal Editor 中打开，并且插件已经加载。桥接程序使用
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
