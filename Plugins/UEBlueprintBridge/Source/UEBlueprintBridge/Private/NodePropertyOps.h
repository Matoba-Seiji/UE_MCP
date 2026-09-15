#pragma once

namespace NodePropertyOps
{
inline FObj Set(UBlueprint* BP, UEdGraph* Graph, UEdGraphNode* Node, const FObj& Request)
{
    if (!Node) return BlueprintAnimWrite::Error(TEXT("Node not found."));
    const FString Path = BlueprintAnimWrite::Str(Request, TEXT("property_path"));
    const FString Value = BlueprintAnimWrite::Str(Request, TEXT("value"));
    TArray<FString> Parts; Path.ParseIntoArray(Parts, TEXT("."), false);
    if (!Parts.Num() || Parts.Num() > 8) return BlueprintAnimWrite::Error(TEXT("Invalid property path."));
    UStruct* Scope = Node->GetClass(); void* Container = Node;
    UProperty* Property = nullptr; UProperty* Top = nullptr;
    for (int32 I = 0; I < Parts.Num(); ++I)
    {
        Property = FindField<UProperty>(Scope, *Parts[I]);
        if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst) || Property->ArrayDim != 1)
            return BlueprintAnimWrite::Error(TEXT("Only editable non-transient scalar properties are supported."));
        if (I == 0) Top = Property;
        if (Property->GetOwnerStruct() == UEdGraphNode::StaticClass()) return BlueprintAnimWrite::Error(TEXT("Use dedicated node editing operations for graph metadata."));
        if (I + 1 < Parts.Num())
        {
            auto* Struct = Cast<UStructProperty>(Property);
            if (!Struct) return BlueprintAnimWrite::Error(TEXT("Intermediate properties must be structs."));
            Container = Struct->ContainerPtrToValuePtr<void>(Container); Scope = Struct->Struct;
        }
    }
    auto* Number = Cast<UNumericProperty>(Property);
    auto* StructLeaf = Cast<UStructProperty>(Property);
    const bool MathStruct = StructLeaf && (StructLeaf->Struct == TBaseStructure<FVector>::Get() || StructLeaf->Struct == TBaseStructure<FRotator>::Get() || StructLeaf->Struct == TBaseStructure<FTransform>::Get());
    if (!Number && !Cast<UBoolProperty>(Property) && !Cast<UStrProperty>(Property) && !Cast<UNameProperty>(Property) && !Cast<UTextProperty>(Property) && !MathStruct)
        return BlueprintAnimWrite::Error(TEXT("Only numeric, bool, string, name and text leaves are supported; object references require a dedicated operation."));
    for (UEdGraphPin* Pin : Node->Pins) if (Pin && Pin->LinkedTo.Num())
        return BlueprintAnimWrite::Error(TEXT("Disconnect this node before property edits; editor reconstruction may change pins."));
    void* Temp = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
    Property->InitializeValue(Temp);
    const TCHAR* End = Property->ImportText(*Value, Temp, PPF_None, Node);
    bool Valid = End && FString(End).TrimStartAndEnd().IsEmpty();
    if (Valid && MathStruct)
    {
        if (StructLeaf->Struct == TBaseStructure<FVector>::Get()) Valid = !static_cast<FVector*>(Temp)->ContainsNaN();
        else if (StructLeaf->Struct == TBaseStructure<FRotator>::Get()) Valid = !static_cast<FRotator*>(Temp)->ContainsNaN();
        else Valid = !static_cast<FTransform*>(Temp)->ContainsNaN() && static_cast<FTransform*>(Temp)->GetRotation().IsNormalized();
    }
    if (Valid && Number && Number->IsFloatingPoint()) Valid = FMath::IsFinite(Number->GetFloatingPointPropertyValue(Temp));
    if (Valid && Number)
    {
        const double Numeric = Number->IsFloatingPoint() ? Number->GetFloatingPointPropertyValue(Temp) : static_cast<double>(Number->GetSignedIntPropertyValue(Temp));
        if (Property->HasMetaData(TEXT("ClampMin"))) Valid = Numeric >= FCString::Atod(*Property->GetMetaData(TEXT("ClampMin")));
        if (Property->HasMetaData(TEXT("ClampMax"))) Valid = Valid && Numeric <= FCString::Atod(*Property->GetMetaData(TEXT("ClampMax")));
    }
    if (!Valid)
    {
        Property->DestroyValue(Temp); FMemory::Free(Temp);
        return BlueprintAnimWrite::Error(TEXT("Invalid property value or value outside editor clamps."));
    }
    const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "Property", "MCP edit node property"));
    BP->Modify(); Graph->Modify(); Node->Modify(); Node->PreEditChange(Top);
    Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Container), Temp);
    Property->DestroyValue(Temp); FMemory::Free(Temp);
    FPropertyChangedEvent Event(Top, EPropertyChangeType::ValueSet);
    Node->PostEditChangeProperty(Event);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); Graph->NotifyGraphChanged();
    FObj R = MakeShared<FJsonObject>(); R->SetBoolField(TEXT("ok"), true); R->SetBoolField(TEXT("saved"), false);
    R->SetStringField(TEXT("property_path"), Path); return R;
}
}
