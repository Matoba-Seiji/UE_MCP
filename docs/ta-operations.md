# UE4.24 TA 操作契约

## Contract

先使用 `ue_inspect_ta_asset` 获取 `revision`，再将其作为
`ue_edit_ta_asset` 的 `expected_revision`。每次编辑都会返回新的 revision。
`config_json` 是 JSON 编码的对象。修改会先保留在内存中，直到调用
`ue_save_ta_asset`；保存前会创建磁盘备份。TA revision 与动画、蓝图和骨架
revision 相互独立，包含所属对象序列化内容，是当前会话的校验值，不是可跨会话
复用的哈希。

此版本需要 UE4.24 ControlRig 插件。BlendSpace 重建会调用当前引擎的 Persona
辅助实现，因此必须存在完整的引擎头文件和 Persona 私有源码；不会修改引擎文件。

## 操作配置

| operation | config_json fields |
| --- | --- |
| replace_blendspace | `axes: [{name,min,max,divisions}]`, `samples: [{animation,position:[x,y,z]}]`; one axis for 1D, two for 2D, inactive coordinates zero. Replaces all axes/samples and rebuilds grid. |
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
| rig_add_element | `name,kind,parent,translation,rotation_degrees,scale`; kind `bone`, `space`, or `control`. Parent must be same kind, empty means root. Bones use UE's initial global transform; spaces and controls use their native local transform/value. Transform controls only, positive scale. Recompile before evaluating. |
| sequencer_replace_float_keys | `section_path,channel,keys:[{frame,value,interpolation}]`; replaces keys on an existing float channel. Integer increasing tick-resolution frames, linear/constant interpolation. Empty keys clears key data, preserves channel default. Does not extend section range. |
| rig_add_node | `node,function,x,y`; function is the UE4.24 controller's rig function name. |
| rig_remove_node | `node` |
| rig_set_pin | `node,pin,value`; value is UE property text. |
| rig_connect / rig_disconnect | `node,pin,target_node,target_pin` |
| rig_set_initial_transform | `name,type,translation:[x,y,z],rotation_degrees:[pitch,yaw,roll],scale:[x,y,z]`; type from hierarchy inspection, initial global transform; recompile before evaluating instances. |
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

`ue_inspect_ta_asset` accepts `view`, `offset`, `limit` (1..500).
BlendSpace returns axes/sample data/grid count; sequences offer `notifies` and
`curves`; Montages additionally offer `sections` and `slots`; Mesh offers `lods`
and `morph_deltas` (requires `name,lod`); Physics offers `bodies` and `constraints`.
Control Rig returns hierarchy transforms; use Blueprint inspection for its graph.
LevelSequence returns bindings and animation section ranges. Skeleton returns a
revision. Reflected nested arrays still have a 100-item cap; Morph deltas are
independently paginated. These are not lossless whole-asset exports.

## 明确边界

### 生产工作流扩展

`ue_create_ta_asset` creates `blendspace`, `blendspace1d`, `level_sequence`, or
`control_rig` at an unused destination package. BlendSpaces require skeleton_path.
Alternatively source_asset plus expected_revision duplicates a supported asset.
Creation never saves. `ue_evaluate_ta_asset` offers editor_actors, blend_weights,
float_curve and sequencer_float; these are data/curve evaluations, not rendering.

Additional edit operations:

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
| rig_reparent_element | kind,name,parent,acknowledge_references:true; same-kind parent, cycles rejected |
| rig_remove_element | kind,name,acknowledge_references:true; hierarchy dependents rejected, graph references not repaired |
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
