# DataTable 操作接口

## 读取

先调用 `ue_inspect_data_table`，传入准确的 `/Game/...` DataTable 路径。返回值包含
`row_struct`、分页后的 `rows` 和 revision。每一行形如：

```json
{
  "name": "Row_01",
  "values": {
    "DisplayName": "Example"
  }
}
```

传入 `row_name` 可以只读取一行。`values` 使用 UE4.24 JsonUtilities 按行结构反射；
不能表示的字段会让读取结果带有错误标记，而不是静默丢失。

## 编辑

所有编辑都要求最新的 `expected_revision`，只在内存中执行：

| operation | 字段 | 行为 |
| --- | --- | --- |
| `upsert_row` | `row_name`, `row_json` | 新行从默认结构创建；已有行只更新 row_json 中提供的字段。 |
| `remove_row` | `row_name` | 删除一行。 |
| `rename_row` | `row_name`, `new_row_name` | 重命名，目标名称必须未使用。 |
| `copy_row` | `row_name`, `new_row_name` | 复制一行，目标名称必须未使用。 |

编辑后调用 `ue_save_data_table` 才会写入 `.uasset`。保存前会在
`Saved/UEBlueprintBridge/backups` 中创建备份。DataTable 行写入不在 PIE 期间执行。
