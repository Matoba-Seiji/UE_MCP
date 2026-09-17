# UE4.24 资产操作接口契约

## Contract

先使用 `ue_inspect_ta_asset` 获取扩展资产的 `revision`，再将其作为
`ue_edit_ta_asset` 的 `expected_revision`。每次编辑都会返回新的 revision。
`config_json` 是 JSON 编码的对象。修改会先保留在内存中，直到调用
`ue_save_ta_asset`；保存前会创建磁盘备份。这类资产 revision 与动画、蓝图和骨架
revision 相互独立，包含所属对象序列化内容，是当前会话的校验值，不是可跨会话
复用的哈希。

Core profile 只使用目标编辑器中可用的通用动画、骨架、物理和 Sequencer 接口。
BlendSpace 保留读取、求值和 AnimBP 播放节点支持，但不创建或替换 BlendSpace；
Control Rig 和依赖 Persona 私有源码的操作不在此配置中。不会修改引擎文件。

DataTable 使用独立的 `ue_inspect_data_table`、`ue_edit_data_table` 和
`ue_save_data_table` 工具；具体行操作见 [DataTable 操作接口](data-table-operations.md)。

`ue_copy_animation_curve` 可以在两个 AnimSequence 之间复制 Float 或 Transform
曲线。它要求源动画和目标动画的最新 revision；目标 Skeleton 尚未注册新曲线名时，
还必须提供目标 Skeleton revision。复制只修改内存中的目标动画，之后调用
`ue_save_animation_asset` 保存。

## 操作配置

| operation | config_json fields |
| --- | --- |
| replace_montage_slot | `slot`, `segments: [{animation,start,in,out,rate,loops}]`; seconds, nonoverlapping increasing starts, existing Skeleton slot. Creates/replaces named slot track. Empty segments clear that track if no Section/Notify is truncated. |
| replace_float_curve | `name`, `keys: [{time,value,interpolation,arrive_tangent,leave_tangent}]`; increasing times in seconds, interpolation `constant`, `linear`, or `cubic`. Replaces entire float curve including keys. |
| remove_float_curve | `name`; does not remove shared Skeleton name. |
| add_notify | `time`, `duration`, `track` (index), optional `class_path`, `name`; class must be a concrete AnimNotify/AnimNotifyState, or omit class for named notify. States require positive duration; ordinary notifies require zero. |
| edit_notify | `index,time,duration,track`; moves an existing Notify or State and updates both links/offsets. |
| remove_notify | `index`; re-read indices after every edit. |
| set_lod_screen_size | `lod,value`; value 0..1. |
| scale_morph_deltas | `lod,name,value`; factor 0..100, position and tangent deltas. |
| replace_morph_deltas | `lod,name,deltas:[{source_index,position_delta:[x,y,z],tangent_delta:[x,y,z]}]`; replaces one LOD's sparse deltas, creates target if absent, rebuilds render data. Uses imported-LOD vertex indices, not DCC vertex indices. |
| set_body_mass | `index,mass_kg`; changes default body mass override. |
| set_constraint_limits | `index,swing1,swing2,twist`; degrees 0..180; zero locks an axis, positive uses Limited mode. |
| replace_body_primitives | `index,shapes`; replaces the body's sphere/box/capsule arrays, preserving convex geometry. Shapes use local cm: sphere `{kind:"sphere",center:[x,y,z],radius}`, box `{kind:"box",center,rotation_degrees:[pitch,yaw,roll],size:[x,y,z]}` (full dimensions), capsule `{kind:"capsule",center,rotation_degrees,radius,length}` (cylinder length). Maximum 256 shapes; empty clears these three primitive arrays. |
| sequencer_replace_float_keys | `section_path,channel,keys:[{frame,value,interpolation}]`; replaces keys on an existing float channel. Integer increasing tick-resolution frames, linear/constant interpolation. Empty keys clears key data, preserves channel default. Does not extend section range. |
| sequencer_add_animation | `binding,animation,start_frame,end_frame`; existing binding GUID, frames in MovieScene tick resolution, end exclusive. |
| sequencer_edit_section | `section_path,start_frame,end_frame,play_rate` |
| sequencer_remove_section | `section_path` |

New float curve names also require `expected_skeleton_revision` from
`ue_inspect_ta_asset` on the Skeleton. Such edits modify **two assets**; response
returns `also_modified` and `also_modified_revision`. Save the Skeleton separately
with `ue_save_ta_asset`. Existing shared curve names do not modify the Skeleton.

## 批量写入

Float curve keys optionally accept `weight_mode` (`none`, `arrive`, `leave`,
`both`) together with nonnegative `arrive_weight` and `leave_weight`. Omitting
these retains the previous unweighted behavior. Weights without a mode are rejected.

LevelSequence section summaries include `float_channel_count`. To read keys, use
`ue_inspect_ta_asset` with `view:"float_keys"`, `section_path`, `channel`,
`offset`, and `limit`. Key values and tangent data are returned with integer frames.

`ue_batch_ta_write` accepts `requests_json` containing 1..20 edit argument objects,
and `acknowledge_partial_completion: true`. It checks all outer schemas before
submission, runs sequentially, stops at the first engine error, and never saves.
It is **not atomic**; earlier successful edits remain in memory. A timeout may
mean the failing item completed: re-inspect before retrying. Repeated asset paths
are rejected because later edits need fresh revisions. Shared Skeleton changes
can also invalidate later revisions and cause a safe stop.

## 读取视图和限制

`ue_inspect_ta_asset` 接受 `view`、`offset`、`limit`（1..500）。BlendSpace 返回
坐标轴、采样点和网格数量；Sequence 支持 `notifies` 和 `curves`；Montage 额外
支持 `sections` 和 `slots`；Mesh 支持 `lods` 和 `morph_deltas`（需要 `name,lod`）；
Physics 支持 `bodies` 和 `constraints`。LevelSequence 返回绑定和动画段范围。
Skeleton 返回 revision。Control Rig 不属于 Core profile 支持范围。
反射嵌套数组仍有 100 项限制，Morph 差值可以单独分页。这些结果不是完整无损的
整资产导出。

## 明确边界

### 生产工作流扩展

在 Core profile 中，`ue_create_ta_asset` 只能在未使用的目标包路径创建
`level_sequence`，也可以通过 `source_asset` 和 `expected_revision` 复制已有支持资产。
创建不会自动保存。BlendSpace 只能读取或作为已有资产复制，不能通过 MCP 创建或替换。
`ue_evaluate_ta_asset` 提供 editor_actors、blend_weights、float_curve 和
sequencer_float 求值，这些是数据或曲线求值，不是渲染结果。

其他编辑操作：

| operation | config_json |
| --- | --- |
| replace_transform_curve | name (existing bone), keys of time/translation/rotation_degrees/scale; expected_skeleton_revision when registering a new track name |
| remove_transform_curve | name; remove curve data, not baked raw keys |
| bake_transform_curves | acknowledge_raw_track_changes:true; explicitly bake editor track curves to raw animation |
| set_notify_scalar | index, property, value (UE scalar text); editable numeric/bool/string/name leaves |
| set_notify_object | index, property, object_path; editable non-instanced /Game asset reference, empty clears |
| regenerate_lods | count (current count..8), regenerate_imported:boolean; requires installed reduction backend, base LOD unchanged |
| remove_lod | lod, non-base existing LOD |
| remove_morph_target | name, acknowledge_references:true; external references are not repaired |
| replace_montage_sections | sections:[{name,time,next}]; first starts at zero, unique names, increasing times, next references within replacement set |
| sequencer_bind_actor | object_path of an existing editor-world actor; returns binding GUID |
| sequencer_add_transform | binding,start_frame,end_frame; returns transform section_path |

`sequencer_replace_float_keys` now also accepts cubic interpolation with explicit
arrive_tangent/leave_tangent in value per tick-resolution frame. Inspection adds
transform_curves, notify_properties (index), and skin_vertices (lod) views.

VectorCurves in UE4.24 are transient editor data, not persistent runtime curves.
They are deliberately not advertised as a save/reopen-capable curve workflow.

These APIs are not complete replacements for every editor feature. Arbitrary
nested Notify object properties, mesh topology/skin-weight painting, external LOD
import, physics shape creation beyond sphere/box/capsule replacement on existing
bodies, all constraint profiles, and non-float Sequencer channels are not implemented.
Binding-to-Skeleton compatibility must be checked in the target scene. A successful
compile does not establish animation playback, rendering, physics or save/reopen
correctness. Verify on copies before using production assets.
