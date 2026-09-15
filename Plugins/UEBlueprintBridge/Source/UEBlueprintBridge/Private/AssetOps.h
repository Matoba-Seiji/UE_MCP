#pragma once
#include "Factories/BlueprintFactory.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Animation/AnimInstance.h"

namespace AssetOps
{
inline FObj List(const FObj& Request)
{
    FString Root = BlueprintWrite::Str(Request, TEXT("root_path"));
    if (Root.IsEmpty()) Root = TEXT("/Game");
    if (!(Root == TEXT("/Game") || Root.StartsWith(TEXT("/Game/"))))
        return BlueprintWrite::Error(TEXT("root_path must be /Game or a subfolder."));
    const FString Kind = BlueprintWrite::Str(Request, TEXT("asset_class"));
    const FString Search = BlueprintWrite::Str(Request, TEXT("name_contains"));
    double OffsetValue = 0, LimitValue = 100;
    Request->TryGetNumberField(TEXT("offset"), OffsetValue);
    Request->TryGetNumberField(TEXT("limit"), LimitValue);
    if (!FMath::IsFinite(OffsetValue) || !FMath::IsFinite(LimitValue) || OffsetValue < 0 || OffsetValue > MAX_int32 || LimitValue < 1 || LimitValue > 500 || OffsetValue != FMath::FloorToDouble(OffsetValue) || LimitValue != FMath::FloorToDouble(LimitValue))
        return BlueprintWrite::Error(TEXT("offset must be a nonnegative integer; limit must be 1..500."));
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    FARFilter Filter;
    Filter.PackagePaths.Add(*Root); Filter.bRecursivePaths = true;
    if (!Kind.IsEmpty()) Filter.ClassNames.Add(*Kind);
    bool Recursive = true; Request->TryGetBoolField(TEXT("recursive_classes"), Recursive);
    Filter.bRecursiveClasses = Recursive;
    TArray<FAssetData> Assets;
    Registry.GetAssets(Filter, Assets);
    Assets.RemoveAll([&](const FAssetData& A) { return !Search.IsEmpty() && !A.AssetName.ToString().Contains(Search); });
    Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.ObjectPath.ToString() < B.ObjectPath.ToString(); });
    const int32 Offset = static_cast<int32>(OffsetValue);
    const int32 End = Offset + FMath::Min(static_cast<int32>(LimitValue), FMath::Max(0, Assets.Num() - Offset));
    TArray<TSharedPtr<FJsonValue>> Items;
    for (int32 I = Offset; I < End; ++I)
    {
        FObj Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("path"), Assets[I].ObjectPath.ToString());
        Item->SetStringField(TEXT("name"), Assets[I].AssetName.ToString());
        Item->SetStringField(TEXT("class"), Assets[I].AssetClass.ToString());
        Item->SetStringField(TEXT("package"), Assets[I].PackageName.ToString());
        Items.Add(JV(Item));
    }
    FObj R = MakeShared<FJsonObject>();
    R->SetArrayField(TEXT("assets"), Items); R->SetNumberField(TEXT("total"), Assets.Num());
    R->SetNumberField(TEXT("offset"), Offset);
    if (End < Assets.Num()) R->SetNumberField(TEXT("next_offset"), End);
    else R->SetField(TEXT("next_offset"), MakeShared<FJsonValueNull>());
    return R;
}

inline FObj Create(const FObj& Request)
{
    if (GEditor && GEditor->PlayWorld) return BlueprintWrite::Error(TEXT("Stop PIE before creating assets."));
    const FString Destination = BlueprintWrite::Str(Request, TEXT("destination"));
    if (!Destination.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Destination) || Destination.Contains(TEXT(".")))
        return BlueprintWrite::Error(TEXT("destination must be an unused /Game/Folder/Name package path."));
    if (FPackageName::DoesPackageExist(Destination) || FindPackage(nullptr, *Destination))
        return BlueprintWrite::Error(TEXT("Destination already exists."));
    FString Kind = BlueprintWrite::Str(Request, TEXT("blueprint_type"));
    if (Kind.IsEmpty()) Kind = TEXT("normal");
    if (Kind != TEXT("normal") && Kind != TEXT("animation")) return BlueprintWrite::Error(TEXT("Unknown blueprint_type."));
    FString ParentPath = BlueprintWrite::Str(Request, TEXT("parent_class"));
    if (ParentPath.IsEmpty()) ParentPath = Kind == TEXT("animation") ? TEXT("/Script/Engine.AnimInstance") : TEXT("/Script/Engine.Actor");
    if (!ParentPath.StartsWith(TEXT("/Script/"))) return BlueprintWrite::Error(TEXT("parent_class must be a native /Script/ class."));
    UClass* Parent = LoadObject<UClass>(nullptr, *ParentPath);
    if (!Parent || !FKismetEditorUtilities::CanCreateBlueprintOfClass(Parent)) return BlueprintWrite::Error(TEXT("Parent class cannot be used for a Blueprint."));
    if (Parent->IsChildOf(UAnimInstance::StaticClass()) != (Kind == TEXT("animation"))) return BlueprintWrite::Error(TEXT("AnimInstance parents require animation blueprint_type."));
    UFactory* Factory = nullptr;
    if (Kind == TEXT("animation"))
    {
        const FString SkeletonPath = BlueprintWrite::Str(Request, TEXT("skeleton_path"));
        if (!SkeletonPath.StartsWith(TEXT("/Game/"))) return BlueprintWrite::Error(TEXT("skeleton_path must be a /Game/ Skeleton."));
        USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
        if (!Skeleton) return BlueprintWrite::Error(TEXT("Skeleton not found."));
        auto* AnimFactory = NewObject<UAnimBlueprintFactory>();
        AnimFactory->ParentClass = Parent; AnimFactory->TargetSkeleton = Skeleton;
        Factory = AnimFactory;
    }
    else
    {
        if (Request->HasField(TEXT("skeleton_path"))) return BlueprintWrite::Error(TEXT("skeleton_path only applies to animation blueprints."));
        auto* BPFactory = NewObject<UBlueprintFactory>(); BPFactory->ParentClass = Parent;
        Factory = BPFactory;
    }
    const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "CreateAsset", "MCP create Blueprint"));
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    UObject* Asset = Tools.CreateAsset(FPackageName::GetLongPackageAssetName(Destination), FPackageName::GetLongPackagePath(Destination), Kind == TEXT("animation") ? UAnimBlueprint::StaticClass() : UBlueprint::StaticClass(), Factory);
    if (!Asset) return BlueprintWrite::Error(TEXT("Asset creation failed."));
    FObj R = MakeShared<FJsonObject>(); R->SetBoolField(TEXT("ok"), true);
    R->SetBoolField(TEXT("saved"), false); R->SetStringField(TEXT("asset"), Asset->GetPathName());
    return R;
}
}
