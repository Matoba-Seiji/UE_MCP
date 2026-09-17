#pragma once
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"

namespace SkeletonRead
{
inline FObj Vector(const FVector& V)
{
    FObj R = MakeShared<FJsonObject>();
    R->SetNumberField(TEXT("x"), V.X); R->SetNumberField(TEXT("y"), V.Y); R->SetNumberField(TEXT("z"), V.Z);
    return R;
}
inline FObj Transform(const FTransform& T)
{
    FObj R = MakeShared<FJsonObject>();
    R->SetObjectField(TEXT("translation"), Vector(T.GetTranslation()));
    R->SetObjectField(TEXT("scale"), Vector(T.GetScale3D()));
    const FQuat Q = T.GetRotation();
    FObj Rotation = MakeShared<FJsonObject>();
    Rotation->SetNumberField(TEXT("x"), Q.X); Rotation->SetNumberField(TEXT("y"), Q.Y);
    Rotation->SetNumberField(TEXT("z"), Q.Z); Rotation->SetNumberField(TEXT("w"), Q.W);
    R->SetObjectField(TEXT("rotation_quaternion"), Rotation);
    return R;
}
inline FObj Inspect(const FObj& Request)
{
    FString Path;
    Request->TryGetStringField(TEXT("asset_path"), Path);
    if (!Path.StartsWith(TEXT("/Game/"))) return BlueprintWrite::Error(TEXT("Use a /Game/ Skeleton asset path."));
    USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *Path);
    if (!Skeleton) return BlueprintWrite::Error(TEXT("Skeleton not found or asset has the wrong type. Use ue_list_skeletons."));
    bool IncludeVirtual = true;
    if (Request->HasField(TEXT("include_virtual_bones")) && !Request->TryGetBoolField(TEXT("include_virtual_bones"), IncludeVirtual))
        return BlueprintWrite::Error(TEXT("include_virtual_bones must be a boolean."));
    const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
    const TArray<FTransform>& Local = Ref.GetRefBonePose();
    const int32 Count = IncludeVirtual ? Ref.GetNum() : Ref.GetRawBoneNum();
    if (Local.Num() != Ref.GetNum() || Count > Ref.GetNum()) return BlueprintWrite::Error(TEXT("Skeleton reference pose data is inconsistent."));
    TArray<int32> Depth;
    TArray<FTransform> Component;
    Depth.SetNum(Count); Component.SetNum(Count);
    TArray<TSharedPtr<FJsonValue>> Bones;
    TArray<TSharedPtr<FJsonValue>> Roots;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const int32 Parent = Ref.GetParentIndex(Index);
        if (Parent < INDEX_NONE || Parent >= Index || Local[Index].ContainsNaN())
            return BlueprintWrite::Error(TEXT("Invalid reference hierarchy or non-finite reference transform."));
        Depth[Index] = Parent == INDEX_NONE ? 0 : Depth[Parent] + 1;
        // UE FTransform composition: child local first, then parent component.
        Component[Index] = Parent == INDEX_NONE ? Local[Index] : Local[Index] * Component[Parent];
        if (Component[Index].ContainsNaN()) return BlueprintWrite::Error(TEXT("Non-finite component reference transform."));
        FObj Bone = MakeShared<FJsonObject>();
        Bone->SetNumberField(TEXT("index"), Index);
        Bone->SetStringField(TEXT("name"), Ref.GetBoneName(Index).ToString());
        Bone->SetNumberField(TEXT("parent_index"), Parent);
        if (Parent == INDEX_NONE)
        {
            Bone->SetField(TEXT("parent_name"), MakeShared<FJsonValueNull>());
            Roots.Add(MakeShared<FJsonValueNumber>(Index));
        }
        else Bone->SetStringField(TEXT("parent_name"), Ref.GetBoneName(Parent).ToString());
        Bone->SetNumberField(TEXT("depth"), Depth[Index]);
        Bone->SetBoolField(TEXT("is_virtual"), Index >= Ref.GetRawBoneNum());
        Bone->SetObjectField(TEXT("reference_local"), Transform(Local[Index]));
        Bone->SetObjectField(TEXT("reference_component"), Transform(Component[Index]));
        Bones.Add(JV(Bone));
    }
    FObj Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset"), Skeleton->GetPathName());
    Result->SetStringField(TEXT("class"), Skeleton->GetClass()->GetPathName());
    Result->SetStringField(TEXT("pose_source"), TEXT("USkeleton.GetReferenceSkeleton.GetRefBonePose"));
    Result->SetStringField(TEXT("translation_unit"), TEXT("centimeters"));
    Result->SetStringField(TEXT("rotation_format"), TEXT("quaternion_xyzw"));
    Result->SetStringField(TEXT("coordinate_system"), TEXT("Unreal left-handed, X forward, Y right, Z up"));
    Result->SetStringField(TEXT("component_composition"), TEXT("local * parent_component using UE FTransform; no actor/world transform"));
    Result->SetBoolField(TEXT("include_virtual_bones"), IncludeVirtual);
    Result->SetNumberField(TEXT("bone_count"), Count);
    Result->SetNumberField(TEXT("raw_bone_count"), Ref.GetRawBoneNum());
    Result->SetNumberField(TEXT("virtual_bone_count"), Ref.GetNum() - Ref.GetRawBoneNum());
    Result->SetArrayField(TEXT("root_indices"), Roots);
    Result->SetArrayField(TEXT("bones"), Bones);
    return Result;
}
}
