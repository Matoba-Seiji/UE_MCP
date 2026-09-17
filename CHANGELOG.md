# Changelog

## 0.5.0-dfm-lite - 2026-09-16

- 移除 ControlRig、ControlRigDeveloper 和 Persona 私有源码依赖。
- 保留蓝图、动画、骨架、运行时、Sequencer、网格和物理相关接口。
- 将 BlendSpace 限制为读取、求值和 AnimBP 播放；禁用创建和整表替换。
- MCP schema 不再广告 Control Rig、Rig 编辑和 Persona 专用操作。
- 增加 DataTable 行级读取、更新、删除、重命名、复制和保存。
- 增加 AnimSequence Float/Transform 曲线跨资产复制。
- 增加兼容 AnimBP 图表之间的节点会话剪贴板复制粘贴。
- 增加 AnimBP 类设置修改和检查。

## 0.5.0 - 2026-09-16

正式发布 UE4.24 本地 MCP。

- 增加 Blueprint、Animation、Skeleton、Runtime 和 Extended Assets Skill 清单。
- 为全部 MCP 工具补充只读、破坏性、幂等、线程亲和性、超时和后续工具元数据。
- 为蓝图、动画、骨架和扩展资产写入增加 before/after/postcondition 验证。
- 支持蓝图节点、引脚、动画曲线、Notify、Montage、BlendSpace、Control Rig、Physics 和 Sequencer 工作流。
- 增加 revision 检查、保存前备份、编译门禁、请求取消和 Windows response 文件锁重试。
- 已通过 UE4.24.3 真实编辑器读写、保存恢复和蓝图编译集成测试。
