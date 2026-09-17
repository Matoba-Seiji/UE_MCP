#pragma once
#include "Animation/AnimMontage.h"
#include "Animation/MorphTarget.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"

namespace AnimationAssetOps
{
inline FObj Error(const FString& Message) { return BlueprintWrite::Error(Message); }
inline bool Number(const FObj& R, const TCHAR* Key, double& V, double Min, double Max)
{
    return R->TryGetNumberField(Key, V) && FMath::IsFinite(V) && V >= Min && V <= Max;
}
inline FString Revision(UObject* Asset)
{
    FString Text;
    for (TFieldIterator<UProperty> It(Asset->GetClass()); It; ++It)
    {
        if (It->HasAnyPropertyFlags(CPF_Transient)) continue;
        Text += It->GetName();
        for (int32 I = 0; I < It->ArrayDim; ++I)
        {
            FString Value; It->ExportText_InContainer(I, Value, Asset, Asset, Asset, PPF_None);
            Text += Value;
        }
    }
    // Raw tracks are not fully represented by reflected properties in UE4.24.
    if (auto* Sequence = Cast<UAnimSequence>(Asset))
        for (const auto& Track : Sequence->GetRawAnimationData())
        {
            Text += FString::FromInt(Track.PosKeys.Num()) + TEXT(":") + FMD5::HashBytes(reinterpret_cast<const uint8*>(Track.PosKeys.GetData()), Track.PosKeys.Num() * sizeof(FVector));
            Text += FString::FromInt(Track.RotKeys.Num()) + TEXT(":") + FMD5::HashBytes(reinterpret_cast<const uint8*>(Track.RotKeys.GetData()), Track.RotKeys.Num() * sizeof(FQuat));
            Text += FString::FromInt(Track.ScaleKeys.Num()) + TEXT(":") + FMD5::HashBytes(reinterpret_cast<const uint8*>(Track.ScaleKeys.GetData()), Track.ScaleKeys.Num() * sizeof(FVector));
        }
    FTCHARToUTF8 Bytes(*Text);
    return FMD5::HashBytes(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
}
inline UObject* Load(const FObj& R)
{
    const FString Path = BlueprintWrite::Str(R, TEXT("asset_path"));
    return Path.StartsWith(TEXT("/Game/")) ? LoadObject<UObject>(nullptr, *Path) : nullptr;
}
inline UAnimSequence* LoadSequence(const FObj& R, const TCHAR* Key)
{
    const FString Path = BlueprintWrite::Str(R, Key);
    return Path.StartsWith(TEXT("/Game/")) ? Cast<UAnimSequence>(LoadObject<UObject>(nullptr, *Path)) : nullptr;
}
inline bool Supported(UObject* A)
{
    return Cast<UAnimSequence>(A) || Cast<UAnimMontage>(A) || Cast<UBlendSpaceBase>(A) || Cast<USkeletalMesh>(A) || Cast<UPhysicsAsset>(A);
}
inline FObj CopyCurve(const FObj& Request)
{
    UAnimSequence* Source = LoadSequence(Request, TEXT("source_asset_path"));
    UAnimSequence* Target = LoadSequence(Request, TEXT("target_asset_path"));
    if (!Source || !Target) return Error(TEXT("source_asset_path and target_asset_path must reference AnimSequence assets."));
    if (!GEditor || GEditor->PlayWorld) return Error(TEXT("Stop PIE before copying animation curves."));
    double Expires = 0;
    if (!Request->TryGetNumberField(TEXT("expires_unix"), Expires) || Expires < FDateTime::UtcNow().ToUnixTimestamp()) return Error(TEXT("Request expired."));
    const FString SourceRevision = BlueprintWrite::Str(Request, TEXT("source_expected_revision"));
    const FString TargetRevision = BlueprintWrite::Str(Request, TEXT("target_expected_revision"));
    if (SourceRevision.IsEmpty() || SourceRevision != Revision(Source)) return Error(TEXT("Source animation revision mismatch."));
    if (TargetRevision.IsEmpty() || TargetRevision != Revision(Target)) return Error(TEXT("Target animation revision mismatch."));
    FString Type = BlueprintWrite::Str(Request, TEXT("curve_type"));
    if (Type != TEXT("float") && Type != TEXT("transform")) return Error(TEXT("curve_type must be float or transform."));
    const FString SourceName = BlueprintWrite::Str(Request, TEXT("source_curve_name"));
    const FString TargetName = BlueprintWrite::Str(Request, TEXT("target_curve_name")).IsEmpty() ? SourceName : BlueprintWrite::Str(Request, TEXT("target_curve_name"));
    if (SourceName.IsEmpty() || TargetName.IsEmpty()) return Error(TEXT("source_curve_name and target_curve_name must be nonempty."));
    const FName SourceCurveName(*SourceName), TargetCurveName(*TargetName);
    if (Type == TEXT("float") && !Source->RawCurveData.FloatCurves.ContainsByPredicate([&](const FFloatCurve& Curve) { return Curve.Name.DisplayName == SourceCurveName; })) return Error(TEXT("Source float curve not found."));
    if (Type == TEXT("transform") && !Source->RawCurveData.TransformCurves.ContainsByPredicate([&](const FTransformCurve& Curve) { return Curve.Name.DisplayName == SourceCurveName; })) return Error(TEXT("Source transform curve not found."));
    const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "CopyCurve", "MCP copy animation curve"));
    USkeleton* Skeleton = Target->GetSkeleton();
    if (!Skeleton) return Error(TEXT("Target animation has no Skeleton."));
    const FName Mapping = Type == TEXT("float") ? USkeleton::AnimCurveMappingName : USkeleton::AnimTrackCurveMappingName;
    FSmartName TargetSmart;
    bool Registered = Skeleton->GetSmartNameByName(Mapping, TargetCurveName, TargetSmart);
    if (!Registered)
    {
        if (BlueprintWrite::Str(Request, TEXT("expected_target_skeleton_revision")) != Revision(Skeleton)) return Error(TEXT("New target curve name changes Skeleton; supply its expected_target_skeleton_revision."));
        Skeleton->Modify();
        if (!Skeleton->AddSmartNameAndModify(Mapping, TargetCurveName, TargetSmart)) return Error(TEXT("Could not register target curve name on Skeleton."));
        Skeleton->MarkPackageDirty();
    }
    bool Changed = false;
    if (Type == TEXT("float"))
    {
        FFloatCurve* SourceCurve = Source->RawCurveData.FloatCurves.FindByPredicate([&](FFloatCurve& Curve) { return Curve.Name.DisplayName == SourceCurveName; });
        if (!SourceCurve) return Error(TEXT("Source float curve not found."));
        FFloatCurve* TargetCurve = Target->RawCurveData.FloatCurves.FindByPredicate([&](FFloatCurve& Curve) { return Curve.Name.DisplayName == TargetCurveName; });
        if (!TargetCurve) TargetCurve = &Target->RawCurveData.FloatCurves.Add_GetRef(FFloatCurve(TargetSmart, SourceCurve->GetCurveTypeFlags()));
        // FFloatCurve::CopyCurve is not exported by the UE4.24 engine DLL.
        TargetCurve->FloatCurve = SourceCurve->FloatCurve; Changed = true;
    }
    else
    {
        FTransformCurve* SourceCurve = Source->RawCurveData.TransformCurves.FindByPredicate([&](FTransformCurve& Curve) { return Curve.Name.DisplayName == SourceCurveName; });
        if (!SourceCurve) return Error(TEXT("Source transform curve not found."));
        FTransformCurve* TargetCurve = Target->RawCurveData.TransformCurves.FindByPredicate([&](FTransformCurve& Curve) { return Curve.Name.DisplayName == TargetCurveName; });
        if (!TargetCurve) TargetCurve = &Target->RawCurveData.TransformCurves.Add_GetRef(FTransformCurve(TargetSmart, SourceCurve->GetCurveTypeFlags()));
        // FTransformCurve::CopyCurve is not exported by the UE4.24 engine DLL.
        FMemory::Memcpy(&TargetCurve->TranslationCurve, &SourceCurve->TranslationCurve, sizeof(FVectorCurve));
        FMemory::Memcpy(&TargetCurve->RotationCurve, &SourceCurve->RotationCurve, sizeof(FVectorCurve));
        FMemory::Memcpy(&TargetCurve->ScaleCurve, &SourceCurve->ScaleCurve, sizeof(FVectorCurve));
        Changed = true;
    }
    if (!Changed) return Error(TEXT("Curve copy made no change."));
    Target->Modify(); Target->MarkRawDataAsModified(); Target->PostProcessSequence(); Target->MarkPackageDirty();
    FObj Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("ok"), true); Result->SetBoolField(TEXT("saved"), false);
    Result->SetStringField(TEXT("source_asset"), Source->GetPathName()); Result->SetStringField(TEXT("target_asset"), Target->GetPathName());
    Result->SetStringField(TEXT("curve_name"), TargetName); Result->SetStringField(TEXT("curve_type"), Type);
    Result->SetStringField(TEXT("source_revision"), SourceRevision); Result->SetStringField(TEXT("revision"), Revision(Target));
    Result->SetBoolField(TEXT("skeleton_modified"), !Registered);
    if (!Registered) Result->SetStringField(TEXT("also_modified"), Skeleton->GetPathName());
    return Result;
}
inline FObj Inspect(const FObj& Request)
{
    UObject* Asset = Load(Request);
    if (!Asset || !Supported(Asset)) return Error(TEXT("Expected a /Game/ AnimSequence, AnimMontage, BlendSpace, SkeletalMesh or PhysicsAsset."));
    FObj R = StructuredAssetOps::Inspect(Request);
    if (R->HasField(TEXT("error"))) return R;
    R->SetStringField(TEXT("revision"), Revision(Asset));
    R->SetStringField(TEXT("scope"), TEXT("editor_asset_not_runtime"));
    if (auto* Sequence = Cast<UAnimSequence>(Asset))
    {
        R->SetNumberField(TEXT("frames"), Sequence->GetNumberOfFrames());
        R->SetNumberField(TEXT("length_seconds"), Sequence->SequenceLength);
        R->SetStringField(TEXT("skeleton"), PathOf(Sequence->GetSkeleton()));
        TArray<TSharedPtr<FJsonValue>> Tracks;
        const auto& Names = Sequence->GetAnimationTrackNames();
        const auto& Data = Sequence->GetRawAnimationData();
        for (int32 I = 0; I < Names.Num(); ++I)
        {
            FObj T = MakeShared<FJsonObject>(); T->SetNumberField(TEXT("index"), I); T->SetStringField(TEXT("bone"), Names[I].ToString());
            if (Data.IsValidIndex(I))
            {
                T->SetNumberField(TEXT("position_keys"), Data[I].PosKeys.Num());
                T->SetNumberField(TEXT("rotation_keys"), Data[I].RotKeys.Num());
                T->SetNumberField(TEXT("scale_keys"), Data[I].ScaleKeys.Num());
            }
            Tracks.Add(JV(T));
        }
        R->SetArrayField(TEXT("tracks"), Tracks);
    }
    if (auto* Mesh = Cast<USkeletalMesh>(Asset))
    {
        R->SetNumberField(TEXT("lod_count"), Mesh->GetLODNum());
        R->SetNumberField(TEXT("bone_count"), Mesh->RefSkeleton.GetNum());
        R->SetStringField(TEXT("skeleton"), PathOf(Mesh->Skeleton));
        TArray<TSharedPtr<FJsonValue>> Morphs;
        for (UMorphTarget* Morph : Mesh->MorphTargets) if (Morph) Morphs.Add(MakeShared<FJsonValueString>(Morph->GetPathName()));
        R->SetArrayField(TEXT("morph_targets"), Morphs);
    }
    return R;
}
inline FObj Sample(const FObj& Request)
{
    auto* Sequence = Cast<UAnimSequence>(Load(Request));
    if (!Sequence || !Sequence->GetSkeleton()) return Error(TEXT("Expected an AnimSequence with a Skeleton."));
    if (Sequence->IsValidAdditive()) return Error(TEXT("Additive sequence pose composition is not supported by this sampler."));
    double Time = 0, End = 0;
    if (!Number(Request, TEXT("time"), Time, 0, Sequence->SequenceLength)) return Error(TEXT("time must be within the sequence, in seconds."));
    End = Time;
    if (Request->HasField(TEXT("end_time")) && !Number(Request, TEXT("end_time"), End, Time, Sequence->SequenceLength)) return Error(TEXT("end_time must be between time and sequence length."));
    const FReferenceSkeleton& Ref = Sequence->GetSkeleton()->GetReferenceSkeleton();
    const FString BoneName = BlueprintWrite::Str(Request, TEXT("bone_name"));
    const int32 Bone = Ref.FindBoneIndex(*BoneName);
    if (Bone == INDEX_NONE || Bone >= Ref.GetRawBoneNum()) return Error(TEXT("Choose a real Skeleton bone; virtual bone evaluation is not supported."));
    const auto& Map = Sequence->GetRawTrackToSkeletonMapTable();
    TArray<int32> Chain;
    for (int32 I = Bone; I != INDEX_NONE; I = Ref.GetParentIndex(I)) Chain.Add(I);
    FTransform Component = FTransform::Identity, Local = FTransform::Identity;
    bool HasTrack = false;
    for (int32 I = Chain.Num() - 1; I >= 0; --I)
    {
        const int32 Index = Chain[I];
        Local = Ref.GetRefBonePose()[Index];
        for (int32 T = 0; T < Map.Num(); ++T) if (Map[T].BoneTreeIndex == Index)
        {
            if (!Sequence->GetRawAnimationData().IsValidIndex(T)) return Error(TEXT("Raw track data is unavailable."));
            Sequence->GetBoneTransform(Local, T, Time, true);
            if (Index == Bone) HasTrack = true;
            break;
        }
        if (Local.ContainsNaN()) return Error(TEXT("Non-finite sampled transform."));
        Component = Local * Component;
    }
    FObj R = MakeShared<FJsonObject>();
    R->SetStringField(TEXT("asset"), Sequence->GetPathName()); R->SetStringField(TEXT("bone"), BoneName);
    R->SetNumberField(TEXT("time"), Time); R->SetBoolField(TEXT("has_raw_track"), HasTrack);
    R->SetObjectField(TEXT("local"), SkeletonRead::Transform(Local));
    R->SetObjectField(TEXT("component"), SkeletonRead::Transform(Component));
    R->SetObjectField(TEXT("root_motion_delta"), SkeletonRead::Transform(Sequence->ExtractRootMotionFromRange(Time, End)));
    R->SetStringField(TEXT("scope"), TEXT("raw_sequence_pose_no_mesh_retarget_no_AnimBP_no_world; missing tracks use Skeleton reference pose"));
    R->SetStringField(TEXT("translation_unit"), TEXT("centimeters"));
    return R;
}
inline FObj ReadTrack(const FObj& Request)
{
    auto* S = Cast<UAnimSequence>(Load(Request));
    const int32 Index = S ? S->GetAnimationTrackNames().IndexOfByKey(FName(*BlueprintWrite::Str(Request, TEXT("bone_name")))) : INDEX_NONE;
    if (!S || !S->GetRawAnimationData().IsValidIndex(Index)) return Error(TEXT("Raw bone track not found."));
    double Offset = 0, Limit = 100;
    if (Request->HasField(TEXT("offset")) && !Number(Request, TEXT("offset"), Offset, 0, MAX_int32)) return Error(TEXT("Invalid offset."));
    if (Request->HasField(TEXT("limit")) && !Number(Request, TEXT("limit"), Limit, 1, 500)) return Error(TEXT("Invalid limit."));
    if (Offset != FMath::FloorToDouble(Offset) || Limit != FMath::FloorToDouble(Limit)) return Error(TEXT("Pagination must be integral."));
    const auto& T = S->GetRawAnimationData()[Index];
    FObj R = MakeShared<FJsonObject>();
    R->SetStringField(TEXT("revision"), Revision(S));
    R->SetNumberField(TEXT("frames"), S->GetNumberOfFrames());
    R->SetNumberField(TEXT("length_seconds"), S->SequenceLength);
    R->SetStringField(TEXT("scope"), TEXT("raw local keys; single key means constant; quaternion xyzw, translation cm"));
    auto Vectors = [&](const TCHAR* Key, const TArray<FVector>& Values)
    {
        FObj O = MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> Items;
        for (int32 I = static_cast<int32>(Offset); I < Values.Num() && Items.Num() < Limit; ++I) Items.Add(JV(SkeletonRead::Vector(Values[I])));
        O->SetNumberField(TEXT("total"), Values.Num()); O->SetArrayField(TEXT("items"), Items); R->SetObjectField(Key, O);
    };
    Vectors(TEXT("positions"), T.PosKeys); Vectors(TEXT("scales"), T.ScaleKeys);
    TArray<TSharedPtr<FJsonValue>> Rotations;
    for (int32 I = static_cast<int32>(Offset); I < T.RotKeys.Num() && Rotations.Num() < Limit; ++I)
    {
        FObj Q = MakeShared<FJsonObject>(); const FQuat& V = T.RotKeys[I];
        Q->SetNumberField(TEXT("x"), V.X); Q->SetNumberField(TEXT("y"), V.Y); Q->SetNumberField(TEXT("z"), V.Z); Q->SetNumberField(TEXT("w"), V.W); Rotations.Add(JV(Q));
    }
    FObj Q = MakeShared<FJsonObject>(); Q->SetNumberField(TEXT("total"), T.RotKeys.Num()); Q->SetArrayField(TEXT("items"), Rotations); R->SetObjectField(TEXT("rotations"), Q);
    return R;
}
inline FObj Edit(const FObj& Request, bool Save)
{
    UObject* Asset = Load(Request);
    auto* Sequence = Cast<UAnimSequence>(Asset);
    auto* Montage = Cast<UAnimMontage>(Asset);
    if (!Sequence && !Montage) return Error(TEXT("Writes currently require AnimSequence or AnimMontage."));
    if (!GEditor || GEditor->PlayWorld) return Error(TEXT("Stop PIE before editing animation assets."));
    double Expires = 0;
    if (!Number(Request, TEXT("expires_unix"), Expires, FDateTime::UtcNow().ToUnixTimestamp(), 1e12)) return Error(TEXT("Write request expired."));
    if (BlueprintWrite::Str(Request, TEXT("expected_revision")) != Revision(Asset)) return Error(TEXT("Revision mismatch. Inspect the animation asset again."));
    if (Save)
    {
        const FString File = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        FString Backup;
        if (IFileManager::Get().FileExists(*File))
        {
            Backup = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UEBlueprintBridge/backups") / (FGuid::NewGuid().ToString() + TEXT("_") + FPaths::GetCleanFilename(File)));
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
            if (IFileManager::Get().Copy(*Backup, *File, false, false) != COPY_OK) return Error(TEXT("Backup failed; nothing saved."));
        }
        const bool Saved = UPackage::SavePackage(Asset->GetOutermost(), Asset, RF_Public | RF_Standalone, *File, GError, nullptr, false, true, SAVE_NoError);
        FObj R = MakeShared<FJsonObject>(); R->SetBoolField(TEXT("ok"), Saved); R->SetBoolField(TEXT("saved"), Saved);
        R->SetStringField(TEXT("backup"), Backup); R->SetStringField(TEXT("revision"), Revision(Asset)); return R;
    }
    const FString Op = BlueprintWrite::Str(Request, TEXT("operation"));
    const FString Name = BlueprintWrite::Str(Request, TEXT("name"));
    double Time = 0, Value = 0, Index = 0;
    UAnimSequenceBase* Base = Sequence ? static_cast<UAnimSequenceBase*>(Sequence) : Montage;
    if (Op == TEXT("replace_raw_track"))
    {
        const int32 TrackIndex = Sequence ? Sequence->GetAnimationTrackNames().IndexOfByKey(FName(*Name)) : INDEX_NONE;
        if (!Sequence || !Sequence->GetRawAnimationData().IsValidIndex(TrackIndex) || Sequence->HasSourceRawData()) return Error(TEXT("Select an existing raw track; sequences with source raw data require a dedicated modifier workflow."));
        FObj Config;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BlueprintWrite::Str(Request, TEXT("track_json"))), Config) || !Config.IsValid()) return Error(TEXT("Invalid track_json."));
        for (const auto& Pair : Config->Values) if (Pair.Key != TEXT("positions") && Pair.Key != TEXT("rotations") && Pair.Key != TEXT("scales")) return Error(TEXT("Unknown track field."));
        FRawAnimSequenceTrack Track;
        const TCHAR* Keys[] = { TEXT("positions"), TEXT("rotations"), TEXT("scales") };
        for (int32 K = 0; K < 3; ++K)
        {
            const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
            if (!Config->TryGetArrayField(Keys[K], Items) || (Items->Num() != 1 && Items->Num() != Sequence->GetNumberOfFrames()) || Items->Num() > 100000) return Error(TEXT("Each channel needs 1 or frame-count keys, at most 100000."));
            for (const auto& Item : *Items)
            {
                const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
                if (!Item->TryGetArray(Values) || Values->Num() != (K == 1 ? 4 : 3)) return Error(TEXT("Keys require xyz arrays or xyzw quaternion arrays."));
                double V[4] = {0, 0, 0, 1};
                for (int32 J = 0; J < Values->Num(); ++J) if (!(*Values)[J]->TryGetNumber(V[J]) || !FMath::IsFinite(V[J]) || FMath::Abs(V[J]) > 1e9) return Error(TEXT("Invalid key coordinate."));
                if (K == 1)
                {
                    FQuat Q(V[0], V[1], V[2], V[3]); if (!Q.IsNormalized()) return Error(TEXT("Rotation quaternion must be normalized.")); Track.RotKeys.Add(Q);
                }
                else if (K == 0) Track.PosKeys.Add(FVector(V[0], V[1], V[2]));
                else Track.ScaleKeys.Add(FVector(V[0], V[1], V[2]));
            }
        }
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "RawTrack", "MCP replace animation track"));
        Sequence->Modify(); Sequence->GetRawAnimationTrack(TrackIndex) = MoveTemp(Track);
        Sequence->MarkRawDataAsModified(); Sequence->PostProcessSequence();
    }
    else if (Op == TEXT("set_root_motion"))
    {
        bool Enabled = false, Lock = false;
        if (!Sequence || !Request->TryGetBoolField(TEXT("enabled"), Enabled) || !Request->TryGetBoolField(TEXT("force_root_lock"), Lock)) return Error(TEXT("AnimSequence, enabled and force_root_lock are required."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "RootMotion", "MCP configure root motion"));
        Sequence->Modify(); Sequence->bEnableRootMotion = Enabled; Sequence->bForceRootLock = Lock;
    }
    else if (Op == TEXT("set_float_curve_key"))
    {
        if (!Sequence || !Number(Request, TEXT("time"), Time, 0, Base->SequenceLength) || !Number(Request, TEXT("value"), Value, -1e9, 1e9)) return Error(TEXT("AnimSequence and bounded time/value required."));
        FFloatCurve* Found = nullptr;
        for (FFloatCurve& Curve : Sequence->RawCurveData.FloatCurves) if (Curve.Name.DisplayName == FName(*Name)) Found = &Curve;
        if (!Found) return Error(TEXT("Select an existing float curve by name."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "CurveKey", "MCP set float curve key"));
        Sequence->Modify(); Found->FloatCurve.UpdateOrAddKey(Time, Value); Sequence->MarkRawDataAsModified(); Sequence->PostProcessSequence();
    }
    else if (Op == TEXT("set_notify_time"))
    {
        if (!Number(Request, TEXT("index"), Index, 0, Base->Notifies.Num() - 1) || Index != FMath::FloorToDouble(Index) || !Number(Request, TEXT("time"), Time, 0, Base->SequenceLength)) return Error(TEXT("Valid notify index and time required."));
        auto& Notify = Base->Notifies[static_cast<int32>(Index)];
        if (Notify.NotifyStateClass) return Error(TEXT("Notify State movement requires updating both links and is not supported yet."));
        if (Time + Notify.GetDuration() > Base->SequenceLength) return Error(TEXT("Notify duration would extend beyond sequence."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "NotifyTime", "MCP move notify"));
        Base->Modify(); Notify.SetTime(Time); Base->SortNotifies();
    }
    else if (Op == TEXT("add_section"))
    {
        if (!Montage || !BlueprintAnimWrite::ValidName(Name) || Montage->GetSectionIndex(*Name) != INDEX_NONE || !Number(Request, TEXT("time"), Time, 0, Base->SequenceLength)) return Error(TEXT("Montage, unique section name and valid time required."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "Section", "MCP add montage section"));
        Montage->Modify();
        if (Montage->AddAnimCompositeSection(*Name, Time) == INDEX_NONE) return Error(TEXT("Section creation rejected."));
    }
    else if (Op == TEXT("set_section_next"))
    {
        const FString Next = BlueprintWrite::Str(Request, TEXT("next_section"));
        const int32 Section = Montage ? Montage->GetSectionIndex(*Name) : INDEX_NONE;
        if (Section == INDEX_NONE || (!Next.IsEmpty() && Montage->GetSectionIndex(*Next) == INDEX_NONE)) return Error(TEXT("Section or next section not found; empty next_section ends playback."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "SectionNext", "MCP link montage section"));
        Montage->Modify(); Montage->CompositeSections[Section].NextSectionName = *Next;
    }
    else return Error(TEXT("Unsupported animation asset operation."));
    Asset->PostEditChange(); Asset->MarkPackageDirty();
    FObj R = MakeShared<FJsonObject>(); R->SetBoolField(TEXT("ok"), true); R->SetBoolField(TEXT("saved"), false);
    R->SetStringField(TEXT("asset"), Asset->GetPathName()); R->SetStringField(TEXT("revision"), Revision(Asset)); return R;
}
inline FObj RuntimePose(const FObj& Request)
{
    auto* Mesh = FindObject<USkeletalMeshComponent>(nullptr, *BlueprintWrite::Str(Request, TEXT("object_path")));
    if (!GEditor || !GEditor->PlayWorld || !IsValid(Mesh) || Mesh->GetWorld() != GEditor->PlayWorld) return Error(TEXT("Select a SkeletalMeshComponent in the active PIE world."));
    const FString BoneName = BlueprintWrite::Str(Request, TEXT("bone_name"));
    const int32 Index = Mesh->GetBoneIndex(*BoneName);
    if (Index == INDEX_NONE) return Error(TEXT("Bone not found on component."));
    FObj R = MakeShared<FJsonObject>(); R->SetStringField(TEXT("component"), Mesh->GetPathName()); R->SetStringField(TEXT("bone"), BoneName);
    R->SetObjectField(TEXT("world"), SkeletonRead::Transform(Mesh->GetBoneTransform(Index)));
    R->SetObjectField(TEXT("component_space"), SkeletonRead::Transform(Mesh->GetBoneTransform(Index, FTransform::Identity)));
    R->SetStringField(TEXT("scope"), TEXT("last_evaluated_component_pose; may be stale when animation ticking is disabled"));
    if (UAnimInstance* Instance = Mesh->GetAnimInstance())
    {
        R->SetStringField(TEXT("anim_instance"), Instance->GetPathName());
        if (UAnimMontage* Active = Instance->GetCurrentActiveMontage())
        {
            R->SetStringField(TEXT("active_montage"), Active->GetPathName());
            R->SetNumberField(TEXT("montage_position"), Instance->Montage_GetPosition(Active));
            R->SetStringField(TEXT("montage_section"), Instance->Montage_GetCurrentSection(Active).ToString());
        }
        const FString Machine = BlueprintWrite::Str(Request, TEXT("machine_name"));
        if (!Machine.IsEmpty())
        {
            const int32 MachineIndex = Instance->GetStateMachineIndex(*Machine);
            if (MachineIndex == INDEX_NONE) return Error(TEXT("State machine not found."));
            R->SetStringField(TEXT("current_state"), Instance->GetCurrentStateName(MachineIndex).ToString());
        }
    }
    return R;
}
}
