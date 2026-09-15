#pragma once

namespace StructuredAssetOps
{
inline TSharedPtr<FJsonValue> Value(UProperty* P, const void* Address, int32 Depth, int32& Budget)
{
    if (--Budget < 0 || Depth > 6) return MakeShared<FJsonValueString>(TEXT("<truncated>"));
    if (auto* B = Cast<UBoolProperty>(P)) return MakeShared<FJsonValueBoolean>(B->GetPropertyValue(Address));
    if (auto* N = Cast<UNumericProperty>(P))
    {
        // Preserve integer precision beyond the JSON/JavaScript exact integer range.
        if (N->IsInteger()) return MakeShared<FJsonValueString>(N->GetNumericPropertyValueToString(Address));
        const double Number = N->GetFloatingPointPropertyValue(Address);
        if (FMath::IsFinite(Number)) return MakeShared<FJsonValueNumber>(Number);
        return MakeShared<FJsonValueNull>();
    }
    if (auto* O = Cast<UObjectPropertyBase>(P)) return MakeShared<FJsonValueString>(PathOf(O->GetObjectPropertyValue(Address)));
    if (auto* S = Cast<UStructProperty>(P))
    {
        FObj Fields = MakeShared<FJsonObject>();
        for (TFieldIterator<UProperty> It(S->Struct); It && Budget > 0; ++It)
        {
            if (It->HasAnyPropertyFlags(CPF_Transient)) continue;
            Fields->SetField(It->GetName(), Value(*It, It->ContainerPtrToValuePtr<void>(Address), Depth + 1, Budget));
        }
        return JV(Fields);
    }
    if (auto* A = Cast<UArrayProperty>(P))
    {
        FScriptArrayHelper Helper(A, Address);
        TArray<TSharedPtr<FJsonValue>> Items;
        for (int32 I = 0; I < Helper.Num() && I < 100 && Budget > 0; ++I) Items.Add(Value(A->Inner, Helper.GetRawPtr(I), Depth + 1, Budget));
        FObj Result = MakeShared<FJsonObject>(); Result->SetNumberField(TEXT("total"), Helper.Num());
        Result->SetBoolField(TEXT("truncated"), Items.Num() != Helper.Num()); Result->SetArrayField(TEXT("items"), Items); return JV(Result);
    }
    FString Text; P->ExportTextItem(Text, Address, nullptr, nullptr, PPF_None);
    return MakeShared<FJsonValueString>(Text.Left(8192));
}
inline FObj Inspect(const FObj& Request)
{
    const FString Path = BlueprintWrite::Str(Request, TEXT("asset_path"));
    if (!Path.StartsWith(TEXT("/Game/"))) return BlueprintWrite::Error(TEXT("Asset path must start with /Game/."));
    UObject* Asset = LoadObject<UObject>(nullptr, *Path);
    if (!Asset || !Asset->IsAsset()) return BlueprintWrite::Error(TEXT("Asset not found or path references a subobject."));
    double Offset = 0, Limit = 50;
    Request->TryGetNumberField(TEXT("offset"), Offset); Request->TryGetNumberField(TEXT("limit"), Limit);
    if (!FMath::IsFinite(Offset) || !FMath::IsFinite(Limit) || Offset < 0 || Offset > MAX_int32 || Limit < 1 || Limit > 100 || Offset != FMath::FloorToDouble(Offset) || Limit != FMath::FloorToDouble(Limit)) return BlueprintWrite::Error(TEXT("Invalid pagination."));
    TArray<UProperty*> Properties;
    for (TFieldIterator<UProperty> It(Asset->GetClass()); It; ++It) if (!It->HasAnyPropertyFlags(CPF_Transient)) Properties.Add(*It);
    Properties.Sort([](const UProperty& A, const UProperty& B) { return A.GetName() < B.GetName(); });
    TArray<TSharedPtr<FJsonValue>> Items;
    int32 Budget = 5000;
    int32 Index = static_cast<int32>(Offset);
    for (; Index < Properties.Num() && Items.Num() < Limit && Budget > 0; ++Index)
    {
        UProperty* P = Properties[Index];
        FObj Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("name"), P->GetName());
        Item->SetStringField(TEXT("type"), P->GetCPPType()); Item->SetBoolField(TEXT("editable"), P->HasAnyPropertyFlags(CPF_Edit) && !P->HasAnyPropertyFlags(CPF_EditConst));
        Item->SetNumberField(TEXT("array_dim"), P->ArrayDim);
        Item->SetField(TEXT("value"), Value(P, P->ContainerPtrToValuePtr<void>(Asset), 0, Budget));
        Items.Add(JV(Item));
    }
    FObj R = MakeShared<FJsonObject>(); R->SetStringField(TEXT("asset"), Asset->GetPathName());
    R->SetStringField(TEXT("class"), Asset->GetClass()->GetPathName()); R->SetArrayField(TEXT("properties"), Items);
    R->SetNumberField(TEXT("total"), Properties.Num()); R->SetBoolField(TEXT("budget_exhausted"), Budget <= 0);
    if (Index < Properties.Num()) R->SetNumberField(TEXT("next_offset"), Index); else R->SetField(TEXT("next_offset"), MakeShared<FJsonValueNull>());
    return R;
}
}
