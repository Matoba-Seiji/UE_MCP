#pragma once
#include "AssetRegistryModule.h"
#include "Engine/DataTable.h"
#include "Engine/UserDefinedStruct.h"
#include "DataTableEditorUtils.h"
#include "JsonObjectConverter.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"

namespace DataTableOps
{
inline FObj Error(const FString& Message) { return BlueprintWrite::Error(Message); }

inline bool IsValidRowStruct(const UScriptStruct* Struct)
{
    const UScriptStruct* TableRowStruct = FTableRowBase::StaticStruct();
    const bool BasedOnTableRowBase = TableRowStruct && Struct && Struct->IsChildOf(TableRowStruct) && Struct != TableRowStruct;
    const bool IsUserDefined = Struct && Struct->IsA<UUserDefinedStruct>();
    return Struct && Struct->GetOutermost() != GetTransientPackage() && (BasedOnTableRowBase || IsUserDefined);
}

inline UDataTable* Load(const FObj& Request, const TCHAR* Key = TEXT("asset_path"))
{
    const FString Path = BlueprintWrite::Str(Request, Key);
    return Path.StartsWith(TEXT("/Game/")) ? LoadObject<UDataTable>(nullptr, *Path) : nullptr;
}

inline FString Revision(UDataTable* Table)
{
    FString Text = Table && Table->GetRowStruct() ? Table->GetRowStruct()->GetPathName() : FString(TEXT("<no_row_struct>"));
    TArray<FName> Names;
    if (Table) Table->GetRowMap().GenerateKeyArray(Names);
    Names.Sort([](const FName& A, const FName& B) { return A.ToString() < B.ToString(); });
    for (const FName& Name : Names)
    {
        const uint8* const* Row = Table->GetRowMap().Find(Name);
        if (!Row || !*Row || !Table->GetRowStruct()) continue;
        FString RowText;
        Table->GetRowStruct()->ExportText(RowText, *Row, nullptr, nullptr, PPF_None, nullptr);
        Text += Name.ToString() + TEXT("=") + RowText;
    }
    FTCHARToUTF8 Bytes(*Text);
    return FMD5::HashBytes(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
}

inline FObj RowValues(UDataTable* Table, const FName& RowName)
{
    FObj Values = MakeShared<FJsonObject>();
    if (!Table || !Table->GetRowStruct()) return Values;
    const uint8* const* Row = Table->GetRowMap().Find(RowName);
    if (!Row || !*Row) return Values;
    if (!FJsonObjectConverter::UStructToJsonObject(Table->GetRowStruct(), *Row, Values.ToSharedRef(), 0, CPF_Transient))
        Values->SetStringField(TEXT("_error"), TEXT("Row contains a property that cannot be represented as JSON."));
    return Values;
}

inline bool ParseValuesObject(const UScriptStruct* RowStruct, const FObj& Values, uint8* RowData, FString& OutError)
{
    if (!RowStruct || !Values.IsValid() || !RowData)
    {
        OutError = TEXT("DataTable row structure is unavailable.");
        return false;
    }
    for (const auto& Pair : Values->Values)
    {
        UProperty* Property = FindField<UProperty>(const_cast<UScriptStruct*>(RowStruct), *Pair.Key);
        if (!Property || Property->HasAnyPropertyFlags(CPF_Transient))
        {
            OutError = FString::Printf(TEXT("Unknown or transient row field: %s."), *Pair.Key);
            return false;
        }
    }
    if (!FJsonObjectConverter::JsonObjectToUStruct(Values.ToSharedRef(), RowStruct, RowData, 0, CPF_Transient))
    {
        OutError = TEXT("row_json could not be applied to the DataTable row structure.");
        return false;
    }
    return true;
}

inline bool ParseRowJson(UDataTable* Table, const FObj& Request, uint8* RowData, FString& OutError)
{
    FString Text = BlueprintWrite::Str(Request, TEXT("row_json"));
    TSharedPtr<FJsonObject> Values;
    if (Text.Len() > 4 * 1024 * 1024 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Values) || !Values.IsValid())
    {
        OutError = TEXT("row_json must be a JSON object of at most 4 MiB.");
        return false;
    }
    return ParseValuesObject(Table ? Table->GetRowStruct() : nullptr, Values, RowData, OutError);
}

inline FObj Create(const FObj& Request)
{
    if (!GEditor || GEditor->PlayWorld) return Error(TEXT("Stop PIE before creating DataTables."));
    double Expires = 0;
    if (!Request->TryGetNumberField(TEXT("expires_unix"), Expires) || Expires < FDateTime::UtcNow().ToUnixTimestamp()) return Error(TEXT("Request expired."));

    const FString Destination = BlueprintWrite::Str(Request, TEXT("destination"));
    const FString RowStructPath = BlueprintWrite::Str(Request, TEXT("row_struct"));
    if (!Destination.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Destination) || Destination.Contains(TEXT(".")) ||
        FPackageName::DoesPackageExist(Destination) || FindPackage(nullptr, *Destination))
        return Error(TEXT("Unused /Game/Folder/Name package path required."));

    UScriptStruct* RowStruct = LoadObject<UScriptStruct>(nullptr, *RowStructPath);
    if (!IsValidRowStruct(RowStruct)) return Error(TEXT("row_struct must be a valid native or user-defined DataTable row structure."));

    TArray<TPair<FName, FObj>> PendingRows;
    const FString RowsText = BlueprintWrite::Str(Request, TEXT("rows_json"));
    if (!RowsText.IsEmpty())
    {
        if (RowsText.Len() > 4 * 1024 * 1024) return Error(TEXT("rows_json must be at most 4 MiB."));
        TSharedPtr<FJsonObject> Rows;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RowsText), Rows) || !Rows.IsValid())
            return Error(TEXT("rows_json must be a JSON object mapping row names to row values."));
        for (const auto& Pair : Rows->Values)
        {
            if (Pair.Value->Type != EJson::Object) return Error(TEXT("Each rows_json value must be a JSON object."));
            const FName RowName(*Pair.Key);
            if (RowName.IsNone()) return Error(TEXT("rows_json contains an invalid row name."));
            const FObj Values = Pair.Value->AsObject();
            uint8* Temp = static_cast<uint8*>(FMemory::Malloc(RowStruct->GetStructureSize()));
            RowStruct->InitializeStruct(Temp);
            FString ParseError;
            const bool Parsed = ParseValuesObject(RowStruct, Values, Temp, ParseError);
            RowStruct->DestroyStruct(Temp);
            FMemory::Free(Temp);
            if (!Parsed) return Error(ParseError);
            PendingRows.Add(TPair<FName, FObj>(RowName, Values));
        }
    }

    const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "CreateDataTable", "MCP create DataTable"));
    UPackage* Package = CreatePackage(nullptr, *Destination);
    const FString AssetName = FPackageName::GetLongPackageAssetName(Destination);
    UDataTable* Table = NewObject<UDataTable>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
    if (!Table) return Error(TEXT("DataTable creation failed."));
    Table->RowStruct = RowStruct;
    for (const TPair<FName, FObj>& Pending : PendingRows)
    {
        uint8* RowData = FDataTableEditorUtils::AddRow(Table, Pending.Key);
        if (!RowData) return Error(TEXT("Could not create a DataTable row."));
        FString ParseError;
        if (!ParseValuesObject(RowStruct, Pending.Value, RowData, ParseError)) return Error(ParseError);
        FDataTableEditorUtils::BroadcastPostChange(Table, FDataTableEditorUtils::EDataTableChangeInfo::RowData);
    }
    FAssetRegistryModule::AssetCreated(Table);
    Table->PostEditChange();
    Table->MarkPackageDirty();

    FObj Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), true);
    Result->SetBoolField(TEXT("saved"), false);
    Result->SetStringField(TEXT("asset"), Table->GetPathName());
    Result->SetStringField(TEXT("row_struct"), RowStruct->GetPathName());
    Result->SetNumberField(TEXT("rows_created"), PendingRows.Num());
    Result->SetNumberField(TEXT("total"), Table->GetRowMap().Num());
    Result->SetStringField(TEXT("revision"), Revision(Table));
    return Result;
}

inline FObj Inspect(const FObj& Request)
{
    UDataTable* Table = Load(Request);
    if (!Table || !Table->GetRowStruct()) return Error(TEXT("Expected a /Game/ DataTable with a row structure."));
    double OffsetValue = 0, LimitValue = 100;
    Request->TryGetNumberField(TEXT("offset"), OffsetValue);
    Request->TryGetNumberField(TEXT("limit"), LimitValue);
    if (!FMath::IsFinite(OffsetValue) || !FMath::IsFinite(LimitValue) || OffsetValue < 0 || OffsetValue > MAX_int32 ||
        LimitValue < 1 || LimitValue > 500 || OffsetValue != FMath::FloorToDouble(OffsetValue) || LimitValue != FMath::FloorToDouble(LimitValue))
        return Error(TEXT("offset must be a nonnegative integer and limit must be 1..500."));
    const FString RequestedRow = BlueprintWrite::Str(Request, TEXT("row_name"));
    TArray<FName> Names;
    Table->GetRowMap().GenerateKeyArray(Names);
    Names.Sort([](const FName& A, const FName& B) { return A.ToString() < B.ToString(); });
    if (!RequestedRow.IsEmpty())
    {
        const FName RowName(*RequestedRow);
        if (!Table->GetRowMap().Contains(RowName)) return Error(TEXT("DataTable row not found."));
        Names.Reset();
        Names.Add(RowName);
        OffsetValue = 0;
    }
    const int32 Offset = static_cast<int32>(OffsetValue);
    const int32 Limit = static_cast<int32>(LimitValue);
    FObj Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset"), Table->GetPathName());
    Result->SetStringField(TEXT("row_struct"), Table->GetRowStruct()->GetPathName());
    Result->SetStringField(TEXT("revision"), Revision(Table));
    Result->SetNumberField(TEXT("total"), Names.Num());
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (int32 I = Offset; I < Names.Num() && Rows.Num() < Limit; ++I)
    {
        FObj Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("name"), Names[I].ToString());
        Row->SetObjectField(TEXT("values"), RowValues(Table, Names[I]));
        Rows.Add(JV(Row));
    }
    Result->SetArrayField(TEXT("rows"), Rows);
    Result->SetNumberField(TEXT("offset"), Offset);
    Result->SetBoolField(TEXT("has_more"), Offset + Rows.Num() < Names.Num());
    if (Offset + Rows.Num() < Names.Num()) Result->SetNumberField(TEXT("next_offset"), Offset + Rows.Num());
    return Result;
}

inline FObj Edit(const FObj& Request)
{
    UDataTable* Table = Load(Request);
    if (!Table || !Table->GetRowStruct()) return Error(TEXT("Expected a /Game/ DataTable with a row structure."));
    if (!GEditor || GEditor->PlayWorld) return Error(TEXT("Stop PIE before editing DataTables."));
    double Expires = 0;
    if (!Request->TryGetNumberField(TEXT("expires_unix"), Expires) || Expires < FDateTime::UtcNow().ToUnixTimestamp()) return Error(TEXT("Request expired."));
    if (BlueprintWrite::Str(Request, TEXT("expected_revision")) != Revision(Table)) return Error(TEXT("Revision mismatch; inspect the DataTable again."));
    const FString Operation = BlueprintWrite::Str(Request, TEXT("operation"));
    const FName RowName(*BlueprintWrite::Str(Request, TEXT("row_name")));
    if (RowName.IsNone()) return Error(TEXT("row_name is required."));
    bool Changed = false;
    if (Operation == TEXT("remove_row"))
    {
        Changed = FDataTableEditorUtils::RemoveRow(Table, RowName);
        if (!Changed) return Error(TEXT("Row not found."));
    }
    else if (Operation == TEXT("rename_row"))
    {
        const FName NewName(*BlueprintWrite::Str(Request, TEXT("new_row_name")));
        if (NewName.IsNone() || !FDataTableEditorUtils::RenameRow(Table, RowName, NewName)) return Error(TEXT("Row rename rejected; source must exist and destination must be unused."));
        Changed = true;
    }
    else if (Operation == TEXT("copy_row"))
    {
        const FName NewName(*BlueprintWrite::Str(Request, TEXT("new_row_name")));
        if (NewName.IsNone() || !FDataTableEditorUtils::DuplicateRow(Table, RowName, NewName)) return Error(TEXT("Row copy rejected; source must exist and destination must be unused."));
        Changed = true;
    }
    else if (Operation == TEXT("upsert_row"))
    {
        bool NewRow = !Table->GetRowMap().Contains(RowName);
        uint8* RowData = NewRow ? FDataTableEditorUtils::AddRow(Table, RowName) : Table->GetRowMap()[RowName];
        if (!RowData) return Error(TEXT("Could not create or find the requested row."));
        TArray<uint8> Backup;
        if (!NewRow)
        {
            Backup.SetNumUninitialized(Table->GetRowStruct()->GetStructureSize());
            Table->GetRowStruct()->InitializeStruct(Backup.GetData());
            Table->GetRowStruct()->CopyScriptStruct(Backup.GetData(), RowData);
        }
        FString ParseError;
        if (!ParseRowJson(Table, Request, RowData, ParseError))
        {
            if (NewRow) FDataTableEditorUtils::RemoveRow(Table, RowName);
            else { Table->GetRowStruct()->CopyScriptStruct(RowData, Backup.GetData()); Table->GetRowStruct()->DestroyStruct(Backup.GetData()); }
            return Error(ParseError);
        }
        if (!NewRow) Table->GetRowStruct()->DestroyStruct(Backup.GetData());
        FDataTableEditorUtils::BroadcastPostChange(Table, FDataTableEditorUtils::EDataTableChangeInfo::RowData);
        Changed = true;
    }
    else return Error(TEXT("operation must be upsert_row, remove_row, rename_row or copy_row."));
    if (!Changed) return Error(TEXT("DataTable operation made no change."));
    Table->MarkPackageDirty();
    FObj Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("ok"), true); Result->SetBoolField(TEXT("saved"), false);
    Result->SetStringField(TEXT("asset"), Table->GetPathName()); Result->SetStringField(TEXT("revision"), Revision(Table));
    return Result;
}

inline FObj Save(const FObj& Request)
{
    UDataTable* Table = Load(Request);
    if (!Table || !Table->GetRowStruct()) return Error(TEXT("Expected a /Game/ DataTable with a row structure."));
    if (GEditor && GEditor->PlayWorld) return Error(TEXT("Stop PIE before saving DataTables."));
    double Expires = 0;
    if (!Request->TryGetNumberField(TEXT("expires_unix"), Expires) || Expires < FDateTime::UtcNow().ToUnixTimestamp()) return Error(TEXT("Request expired."));
    if (BlueprintWrite::Str(Request, TEXT("expected_revision")) != Revision(Table)) return Error(TEXT("Revision mismatch; inspect the DataTable again."));
    const FString File = FPackageName::LongPackageNameToFilename(Table->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    FString Backup;
    if (IFileManager::Get().FileExists(*File))
    {
        Backup = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UEBlueprintBridge/backups") / (FGuid::NewGuid().ToString() + TEXT("_") + FPaths::GetCleanFilename(File)));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
        if (IFileManager::Get().Copy(*Backup, *File, false, false) != COPY_OK) return Error(TEXT("Backup failed; DataTable was not saved."));
    }
    const bool Saved = UPackage::SavePackage(Table->GetOutermost(), Table, RF_Public | RF_Standalone, *File, GError, nullptr, false, true, SAVE_NoError);
    FObj Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("ok"), Saved); Result->SetBoolField(TEXT("saved"), Saved);
    Result->SetStringField(TEXT("asset"), Table->GetPathName()); Result->SetStringField(TEXT("backup"), Backup); Result->SetStringField(TEXT("revision"), Revision(Table));
    return Result;
}
}
