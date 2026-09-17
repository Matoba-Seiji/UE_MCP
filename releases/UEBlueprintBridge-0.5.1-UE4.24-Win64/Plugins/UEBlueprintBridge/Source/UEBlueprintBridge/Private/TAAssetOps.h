#pragma once
#include "Animation/BlendSpace1D.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "Rendering/SkeletalMeshModel.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/UObjectHash.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneChannelProxy.h"

namespace TAAssetOps
{
using namespace AnimationAssetOps;
inline FObj Error(const FString& Message) { return BlueprintWrite::Error(Message); }
inline bool SupportedAsset(UObject* A)
{
    return Supported(A) || Cast<USkeleton>(A) || Cast<ULevelSequence>(A);
}
inline FString FullRevision(UObject* Asset)
{
    TArray<UObject*> Objects; GetObjectsWithOuter(Asset, Objects, true); Objects.Add(Asset);
    Objects.Sort([](const UObject& A, const UObject& B) { return A.GetPathName() < B.GetPathName(); });
    FString Hashes = Revision(Asset);
    for (UObject* Object : Objects)
    {
        if (Object->HasAnyFlags(RF_Transient)) continue;
        TArray<uint8> Bytes; FObjectWriter Writer(Object, Bytes);
        Hashes += Object->GetPathName() + FMD5::HashBytes(Bytes.GetData(), Bytes.Num());
    }
    FTCHARToUTF8 Bytes(*Hashes);
    return FMD5::HashBytes(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
}
inline bool Config(const FObj& R, FObj& C)
{
    FString Text = BlueprintWrite::Str(R, TEXT("config_json"));
    return Text.Len() <= 4 * 1024 * 1024 && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), C) && C.IsValid();
}
inline bool Fields(const FObj& C, std::initializer_list<const TCHAR*> Allowed)
{
    for (const auto& Pair : C->Values)
    {
        bool Found = false; for (const TCHAR* Key : Allowed) if (Pair.Key == Key) Found = true;
        if (!Found) return false;
    }
    return true;
}
inline bool Index(const FObj& C, const TCHAR* Key, int32 Count, int32& Out)
{
    double V = 0; if (!Number(C, Key, V, 0, Count - 1) || V != FMath::FloorToDouble(V)) return false;
    Out = static_cast<int32>(V); return true;
}
inline bool VectorValue(const FObj& C, const TCHAR* Key, FVector& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
    if (!C->TryGetArrayField(Key, A) || A->Num() != 3) return false;
    for (int32 I = 0; I < 3; ++I)
    {
        double V = 0; if (!(*A)[I]->TryGetNumber(V) || !FMath::IsFinite(V) || FMath::Abs(V) > 1e6) return false;
        Out[I] = V;
    }
    return true;
}
inline FObj Struct(UScriptStruct* Type, const void* Data)
{
    FObj R = MakeShared<FJsonObject>(); int32 Budget = 20000;
    for (TFieldIterator<UProperty> It(Type); It && Budget > 0; ++It)
        if (!It->HasAnyPropertyFlags(CPF_Transient)) R->SetField(It->GetName(), StructuredAssetOps::Value(*It, It->ContainerPtrToValuePtr<void>(Data), 0, Budget));
    return R;
}
inline FObj Inspect(const FObj& Request)
{
    UObject* A = Load(Request);
    if (!A || !SupportedAsset(A)) return Error(TEXT("Unsupported TA asset."));
    double Offset = 0, Limit = 100;
    if (Request->HasField(TEXT("offset")) && !Number(Request, TEXT("offset"), Offset, 0, MAX_int32)) return Error(TEXT("Invalid offset."));
    if (Request->HasField(TEXT("limit")) && !Number(Request, TEXT("limit"), Limit, 1, 500)) return Error(TEXT("Invalid limit."));
    if (Offset != FMath::FloorToDouble(Offset) || Limit != FMath::FloorToDouble(Limit)) return Error(TEXT("Integral pagination required."));
    FObj R = MakeShared<FJsonObject>();
    R->SetStringField(TEXT("asset"), A->GetPathName()); R->SetStringField(TEXT("class"), A->GetClass()->GetPathName());
    R->SetStringField(TEXT("revision"), FullRevision(A));
    const FString View = BlueprintWrite::Str(Request, TEXT("view"));
    TArray<TSharedPtr<FJsonValue>> Items; int32 Total = 0;
    auto Page = [&](int32 I) { return I >= Offset && I - Offset < Limit; };
    if (auto* B = Cast<UBlendSpaceBase>(A))
    {
        TArray<TSharedPtr<FJsonValue>> Axes;
        for (int32 I = 0; I < (Cast<UBlendSpace1D>(B) ? 1 : 2); ++I) Axes.Add(JV(Struct(FBlendParameter::StaticStruct(), &B->GetBlendParameter(I))));
        R->SetArrayField(TEXT("axes"), Axes); Total = B->GetBlendSamples().Num();
        for (int32 I = 0; I < Total; ++I) if (Page(I)) Items.Add(JV(Struct(FBlendSample::StaticStruct(), &B->GetBlendSample(I))));
        R->SetNumberField(TEXT("grid_elements"), B->GetGridSamples().Num());
    }
    else if (auto* S = Cast<UAnimSequenceBase>(A))
    {
        if (View == TEXT("notifies"))
        {
            Total = S->Notifies.Num();
            for (int32 I = 0; I < Total; ++I) if (Page(I)) Items.Add(JV(Struct(FAnimNotifyEvent::StaticStruct(), &S->Notifies[I])));
        }
        else if (View == TEXT("transform_curves"))
        {
            Total = S->RawCurveData.TransformCurves.Num();
            for (int32 I = 0; I < Total; ++I) if (Page(I)) Items.Add(JV(Struct(FTransformCurve::StaticStruct(), &S->RawCurveData.TransformCurves[I])));
        }
        else if (View == TEXT("notify_properties"))
        {
            int32 NotifyIndex = 0; if (!Index(Request, TEXT("index"), S->Notifies.Num(), NotifyIndex)) return Error(TEXT("Valid notify index required."));
            UObject* Notify = S->Notifies[NotifyIndex].Notify ? static_cast<UObject*>(S->Notifies[NotifyIndex].Notify) : S->Notifies[NotifyIndex].NotifyStateClass;
            if (!Notify) return Error(TEXT("Named Notify has no object properties."));
            TArray<UProperty*> Properties;
            for (TFieldIterator<UProperty> It(Notify->GetClass()); It; ++It) if (It->HasAnyPropertyFlags(CPF_Edit) && !It->HasAnyPropertyFlags(CPF_Transient)) Properties.Add(*It);
            Total = Properties.Num(); int32 Budget = 20000;
            for (int32 I = 0; I < Total; ++I) if (Page(I))
            {
                FObj P = MakeShared<FJsonObject>(); P->SetStringField(TEXT("name"), Properties[I]->GetName());
                P->SetField(TEXT("value"), StructuredAssetOps::Value(Properties[I], Properties[I]->ContainerPtrToValuePtr<void>(Notify), 0, Budget)); Items.Add(JV(P));
            }
        }
        else if (View == TEXT("curves"))
        {
            Total = S->RawCurveData.FloatCurves.Num();
            for (int32 I = 0; I < Total; ++I) if (Page(I)) Items.Add(JV(Struct(FFloatCurve::StaticStruct(), &S->RawCurveData.FloatCurves[I])));
        }
        else if (auto* M = Cast<UAnimMontage>(S))
        {
            if (View == TEXT("sections"))
            {
                Total = M->CompositeSections.Num();
                for (int32 I = 0; I < Total; ++I) if (Page(I)) Items.Add(JV(Struct(FCompositeSection::StaticStruct(), &M->CompositeSections[I])));
            }
            else if (View == TEXT("slots"))
            {
                Total = M->SlotAnimTracks.Num();
                for (int32 I = 0; I < Total; ++I) if (Page(I)) Items.Add(JV(Struct(FSlotAnimationTrack::StaticStruct(), &M->SlotAnimTracks[I])));
            }
            else return Error(TEXT("Montage views: notifies, curves, sections, slots."));
        }
        else return Error(TEXT("Sequence views: notifies, curves."));
    }
    else if (auto* Mesh = Cast<USkeletalMesh>(A))
    {
        if (View == TEXT("skin_vertices"))
        {
            int32 LOD = 0; if (!Index(Request, TEXT("lod"), Mesh->GetLODNum(), LOD) || !Mesh->GetImportedModel() || !Mesh->GetImportedModel()->LODModels.IsValidIndex(LOD)) return Error(TEXT("Imported LOD required."));
            const auto& Model = Mesh->GetImportedModel()->LODModels[LOD]; Total = Model.NumVertices;
            for (const auto& Section : Model.Sections) for (int32 V = 0; V < Section.SoftVertices.Num(); ++V)
            {
                const int32 VertexIndex = Section.BaseVertexIndex + V; if (!Page(VertexIndex)) continue;
                const auto& Vertex = Section.SoftVertices[V]; FObj Out = MakeShared<FJsonObject>(); Out->SetNumberField(TEXT("index"), VertexIndex); Out->SetObjectField(TEXT("position"), SkeletonRead::Vector(Vertex.Position));
                TArray<TSharedPtr<FJsonValue>> Influences;
                for (int32 I = 0; I < MAX_TOTAL_INFLUENCES; ++I) if (Vertex.InfluenceWeights[I])
                {
                    if (!Section.BoneMap.IsValidIndex(Vertex.InfluenceBones[I])) return Error(TEXT("Invalid imported skin bone map."));
                    FObj W = MakeShared<FJsonObject>(); W->SetNumberField(TEXT("bone_index"), Section.BoneMap[Vertex.InfluenceBones[I]]); W->SetNumberField(TEXT("weight_byte"), Vertex.InfluenceWeights[I]); Influences.Add(JV(W));
                }
                Out->SetArrayField(TEXT("influences"), Influences); Items.Add(JV(Out));
            }
        }
        else if (View == TEXT("lods"))
        {
            Total = Mesh->GetLODNum();
            for (int32 I = 0; I < Total; ++I) if (Page(I))
            {
                FObj L = Struct(FSkeletalMeshLODInfo::StaticStruct(), Mesh->GetLODInfo(I));
                if (Mesh->GetImportedModel() && Mesh->GetImportedModel()->LODModels.IsValidIndex(I))
                {
                    const auto& Model = Mesh->GetImportedModel()->LODModels[I];
                    L->SetNumberField(TEXT("vertices"), Model.NumVertices); L->SetNumberField(TEXT("indices"), Model.IndexBuffer.Num());
                    L->SetNumberField(TEXT("sections"), Model.Sections.Num());
                }
                Items.Add(JV(L));
            }
        }
        else if (View == TEXT("morph_deltas"))
        {
            int32 LOD = 0; if (!Index(Request, TEXT("lod"), Mesh->GetLODNum(), LOD)) return Error(TEXT("Valid lod required."));
            UMorphTarget* Morph = Mesh->FindMorphTarget(FName(*BlueprintWrite::Str(Request, TEXT("name"))));
            if (!Morph) return Error(TEXT("Morph target not found."));
            FMorphTargetDelta* Data = Morph->GetMorphTargetDelta(LOD, Total);
            for (int32 I = 0; I < Total; ++I) if (Page(I))
            {
                FObj D = MakeShared<FJsonObject>(); D->SetNumberField(TEXT("source_index"), Data[I].SourceIdx);
                D->SetObjectField(TEXT("position_delta"), SkeletonRead::Vector(Data[I].PositionDelta));
                D->SetObjectField(TEXT("tangent_delta"), SkeletonRead::Vector(Data[I].TangentZDelta)); Items.Add(JV(D));
            }
        }
        else return Error(TEXT("Mesh views: lods, morph_deltas (name/lod required)."));
    }
    else if (auto* P = Cast<UPhysicsAsset>(A))
    {
        TArray<UObject*> Objects;
        if (View == TEXT("bodies")) for (auto* Body : P->SkeletalBodySetups) Objects.Add(Body);
        else if (View == TEXT("constraints")) for (auto* C : P->ConstraintSetup) Objects.Add(C);
        else return Error(TEXT("Physics views: bodies, constraints."));
        Total = Objects.Num();
        for (int32 I = 0; I < Total; ++I) if (Page(I) && Objects[I])
        {
            FObj O = MakeShared<FJsonObject>(); int32 Budget = 20000;
            O->SetStringField(TEXT("path"), Objects[I]->GetPathName());
            for (TFieldIterator<UProperty> It(Objects[I]->GetClass()); It && Budget > 0; ++It)
                if (!It->HasAnyPropertyFlags(CPF_Transient)) O->SetField(It->GetName(), StructuredAssetOps::Value(*It, It->ContainerPtrToValuePtr<void>(Objects[I]), 0, Budget));
            Items.Add(JV(O));
        }
    }
    else if (auto* Sequence = Cast<ULevelSequence>(A))
    {
        UMovieScene* Scene = Sequence->GetMovieScene(); if (!Scene) return Error(TEXT("MovieScene missing."));
        if (View == TEXT("float_keys"))
        {
            auto* Section = FindObject<UMovieSceneSection>(nullptr, *BlueprintWrite::Str(Request, TEXT("section_path")));
            if (!Section || !Section->IsIn(Sequence)) return Error(TEXT("Select a section in this LevelSequence."));
            auto Channels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>(); int32 ChannelIndex = 0;
            if (!Index(Request, TEXT("channel"), Channels.Num(), ChannelIndex)) return Error(TEXT("Valid float channel index required."));
            auto Data = Channels[ChannelIndex]->GetData(); auto Times = Data.GetTimes(); auto Values = Data.GetValues();
            Total = Times.Num();
            for (int32 I = 0; I < Total; ++I) if (Page(I))
            {
                FObj Key = Struct(FMovieSceneFloatValue::StaticStruct(), &Values[I]); Key->SetNumberField(TEXT("frame"), Times[I].Value); Items.Add(JV(Key));
            }
            R->SetArrayField(TEXT("items"), Items); R->SetNumberField(TEXT("total"), Total); R->SetBoolField(TEXT("has_more"), Offset + Limit < Total);
            R->SetNumberField(TEXT("float_channel_count"), Channels.Num()); return R;
        }
        const auto& Bindings = Scene->GetBindings(); Total = Bindings.Num();
        R->SetNumberField(TEXT("tick_resolution_numerator"), Scene->GetTickResolution().Numerator);
        R->SetNumberField(TEXT("tick_resolution_denominator"), Scene->GetTickResolution().Denominator);
        for (int32 I = 0; I < Total; ++I) if (Page(I))
        {
            FObj BindingObject = MakeShared<FJsonObject>(); BindingObject->SetStringField(TEXT("guid"), Bindings[I].GetObjectGuid().ToString()); BindingObject->SetStringField(TEXT("name"), Bindings[I].GetName());
            TArray<TSharedPtr<FJsonValue>> Sections;
            for (auto* Track : Bindings[I].GetTracks()) for (auto* Section : Track->GetAllSections())
            {
                FObj SectionObject = MakeShared<FJsonObject>(); SectionObject->SetStringField(TEXT("path"), Section->GetPathName());
                SectionObject->SetNumberField(TEXT("float_channel_count"), Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>().Num());
                if (Section->HasStartFrame()) SectionObject->SetNumberField(TEXT("start_frame"), Section->GetInclusiveStartFrame().Value);
                if (Section->HasEndFrame()) SectionObject->SetNumberField(TEXT("end_frame_exclusive"), Section->GetExclusiveEndFrame().Value);
                if (auto* Anim = Cast<UMovieSceneSkeletalAnimationSection>(Section)) SectionObject->SetStringField(TEXT("animation"), PathOf(Anim->Params.Animation));
                Sections.Add(JV(SectionObject));
            }
            BindingObject->SetArrayField(TEXT("sections"), Sections); Items.Add(JV(BindingObject));
        }
    }
    R->SetArrayField(TEXT("items"), Items); R->SetNumberField(TEXT("total"), Total); R->SetNumberField(TEXT("offset"), Offset);
    R->SetBoolField(TEXT("has_more"), Offset + Limit < Total);
    R->SetStringField(TEXT("nested_limits"), TEXT("Reflected nested arrays capped at 100; morph_deltas has independent pagination.")); return R;
}
}
#include "TAProductionOps.h"
namespace TAAssetOps
{
inline FObj Edit(const FObj& Request, bool Save)
{
    UObject* A = Load(Request);
    if (!A || !SupportedAsset(A)) return Error(TEXT("Unsupported TA asset."));
    if (!GEditor || GEditor->PlayWorld) return Error(TEXT("Stop PIE before TA writes."));
    double Expires = 0;
    if (!Number(Request, TEXT("expires_unix"), Expires, FDateTime::UtcNow().ToUnixTimestamp(), 1e12)) return Error(TEXT("Request expired."));
    if (BlueprintWrite::Str(Request, TEXT("expected_revision")) != FullRevision(A)) return Error(TEXT("Revision mismatch; use ue_inspect_ta_asset."));
    if (Save)
    {
        if (auto* BP = Cast<UBlueprint>(A))
        {
            FObj Result = BlueprintWrite::Compile(BP); if (!Result->GetBoolField(TEXT("ok"))) return Result;
        }
        const FString File = FPackageName::LongPackageNameToFilename(A->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        FString Backup;
        if (IFileManager::Get().FileExists(*File))
        {
            Backup = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UEBlueprintBridge/backups") / (FGuid::NewGuid().ToString() + TEXT("_") + FPaths::GetCleanFilename(File)));
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
            if (IFileManager::Get().Copy(*Backup, *File, false, false) != COPY_OK) return Error(TEXT("Backup failed."));
        }
        const bool Saved = UPackage::SavePackage(A->GetOutermost(), A, RF_Public | RF_Standalone, *File, GError, nullptr, false, true, SAVE_NoError);
        FObj R = MakeShared<FJsonObject>(); R->SetBoolField(TEXT("ok"), Saved); R->SetBoolField(TEXT("saved"), Saved);
        R->SetStringField(TEXT("backup"), Backup); R->SetStringField(TEXT("revision"), FullRevision(A)); return R;
    }
    FObj C; if (!Config(Request, C)) return Error(TEXT("config_json must be an object of at most 4 MiB."));
    const FString Op = BlueprintWrite::Str(Request, TEXT("operation"));
    if (FObj Extra = TAProductionOps::Edit(A, Op, C)) return Extra;
    UObject* OtherModified = nullptr;
    if (Op == TEXT("replace_montage_slot"))
    {
        auto* M = Cast<UAnimMontage>(A); const FString Slot = BlueprintWrite::Str(C, TEXT("slot"));
        const TArray<TSharedPtr<FJsonValue>>* Segments = nullptr;
        if (!M || !M->GetSkeleton() || !Fields(C, {TEXT("slot"), TEXT("segments")}) || !M->GetSkeleton()->ContainsSlotName(*Slot) || !C->TryGetArrayField(TEXT("segments"), Segments) || Segments->Num() > 500) return Error(TEXT("Existing Skeleton slot and at most 500 segments required."));
        FSlotAnimationTrack NewTrack; NewTrack.SlotName = *Slot;
        double PreviousEnd = 0;
        for (const auto& Item : *Segments)
        {
            if (Item->Type != EJson::Object) return Error(TEXT("Segment must be an object."));
            FObj O = Item->AsObject(); double Start, In, Out, Rate, Loops;
            FString Path = BlueprintWrite::Str(O, TEXT("animation"));
            auto* S = Path.StartsWith(TEXT("/Game/")) ? LoadObject<UAnimSequence>(nullptr, *Path) : nullptr;
            if (!Fields(O, {TEXT("animation"), TEXT("start"), TEXT("in"), TEXT("out"), TEXT("rate"), TEXT("loops")}) || !S || !M->GetSkeleton()->IsCompatible(S->GetSkeleton()) ||
                !Number(O, TEXT("start"), Start, PreviousEnd, 1e6) || !Number(O, TEXT("in"), In, 0, S->SequenceLength) || !Number(O, TEXT("out"), Out, In, S->SequenceLength) || Out <= In ||
                !Number(O, TEXT("rate"), Rate, -100, 100) || FMath::Abs(Rate * S->RateScale) < 1e-4 || !Number(O, TEXT("loops"), Loops, 1, 1000) || Loops != FMath::FloorToDouble(Loops)) return Error(TEXT("Invalid or overlapping montage segment."));
            FAnimSegment Segment; Segment.AnimReference = S; Segment.StartPos = Start; Segment.AnimStartTime = In; Segment.AnimEndTime = Out; Segment.AnimPlayRate = Rate; Segment.LoopingCount = Loops;
            PreviousEnd = Segment.GetEndPos(); if (PreviousEnd > 1e6) return Error(TEXT("Montage length exceeds limit."));
            NewTrack.AnimTrack.AnimSegments.Add(Segment);
        }
        int32 SlotIndex = INDEX_NONE;
        float Length = PreviousEnd;
        for (int32 I = 0; I < M->SlotAnimTracks.Num(); ++I)
            if (M->SlotAnimTracks[I].SlotName == *Slot) SlotIndex = I;
            else Length = FMath::Max(Length, M->SlotAnimTracks[I].AnimTrack.GetLength());
        for (const auto& N : M->Notifies) if (N.GetTime() + N.GetDuration() > Length) return Error(TEXT("Replacement would truncate a Notify."));
        for (const auto& S : M->CompositeSections) if (S.GetTime() > Length) return Error(TEXT("Replacement would truncate a Section."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "MontageSlot", "MCP replace montage slot")); M->Modify();
        if (SlotIndex == INDEX_NONE) M->SlotAnimTracks.Add(NewTrack); else M->SlotAnimTracks[SlotIndex] = NewTrack;
        M->SetSequenceLength(M->CalculateSequenceLength()); M->UpdateLinkableElements();
    }
    else if (Op == TEXT("replace_float_curve") || Op == TEXT("remove_float_curve"))
    {
        auto* S = Cast<UAnimSequence>(A); const FString Name = BlueprintWrite::Str(C, TEXT("name"));
        if (!S || !S->GetSkeleton() || Name.IsEmpty() || !Fields(C, {TEXT("name"), TEXT("keys"), TEXT("expected_skeleton_revision")})) return Error(TEXT("AnimSequence, curve name and keys required."));
        int32 Found = S->RawCurveData.FloatCurves.IndexOfByPredicate([&](const FFloatCurve& F) { return F.Name.DisplayName == *Name; });
        FRichCurve Curve;
        if (Op == TEXT("replace_float_curve"))
        {
            const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
            if (!C->TryGetArrayField(TEXT("keys"), Keys) || Keys->Num() > 100000) return Error(TEXT("keys array required; maximum 100000."));
            double Last = -1;
            for (const auto& Item : *Keys)
            {
                if (Item->Type != EJson::Object) return Error(TEXT("Curve key must be an object."));
                FObj K = Item->AsObject(); double Time, Value, Arrive, Leave;
                FString Interp = BlueprintWrite::Str(K, TEXT("interpolation"));
                if (!Fields(K, {TEXT("time"), TEXT("value"), TEXT("interpolation"), TEXT("arrive_tangent"), TEXT("leave_tangent"), TEXT("weight_mode"), TEXT("arrive_weight"), TEXT("leave_weight")}) ||
                    !Number(K, TEXT("time"), Time, 0, S->SequenceLength) || Time <= Last || !Number(K, TEXT("value"), Value, -1e9, 1e9) ||
                    !Number(K, TEXT("arrive_tangent"), Arrive, -1e9, 1e9) || !Number(K, TEXT("leave_tangent"), Leave, -1e9, 1e9) ||
                    (Interp != TEXT("linear") && Interp != TEXT("constant") && Interp != TEXT("cubic"))) return Error(TEXT("Keys require increasing times, finite values/tangents and interpolation linear/constant/cubic."));
                FKeyHandle Handle = Curve.AddKey(Time, Value); auto& Key = Curve.GetKey(Handle);
                Key.InterpMode = Interp == TEXT("linear") ? RCIM_Linear : Interp == TEXT("constant") ? RCIM_Constant : RCIM_Cubic;
                Key.TangentMode = RCTM_User; Key.ArriveTangent = Arrive; Key.LeaveTangent = Leave; Last = Time;
                FString WeightMode = BlueprintWrite::Str(K, TEXT("weight_mode"));
                if (!WeightMode.IsEmpty())
                {
                    double ArriveWeight = 0, LeaveWeight = 0;
                    if ((WeightMode != TEXT("none") && WeightMode != TEXT("arrive") && WeightMode != TEXT("leave") && WeightMode != TEXT("both")) ||
                        !Number(K, TEXT("arrive_weight"), ArriveWeight, 0, 1e9) || !Number(K, TEXT("leave_weight"), LeaveWeight, 0, 1e9)) return Error(TEXT("weight_mode requires none/arrive/leave/both and nonnegative arrive_weight/leave_weight."));
                    Key.TangentWeightMode = WeightMode == TEXT("both") ? RCTWM_WeightedBoth : WeightMode == TEXT("arrive") ? RCTWM_WeightedArrive : WeightMode == TEXT("leave") ? RCTWM_WeightedLeave : RCTWM_WeightedNone;
                    Key.ArriveTangentWeight = ArriveWeight; Key.LeaveTangentWeight = LeaveWeight;
                }
                else if (K->HasField(TEXT("arrive_weight")) || K->HasField(TEXT("leave_weight"))) return Error(TEXT("Weights require explicit weight_mode."));
            }
        }
        else if (Found == INDEX_NONE) return Error(TEXT("Curve not found."));
        FSmartName Smart;
        const bool Registered = S->GetSkeleton()->GetSmartNameByName(USkeleton::AnimCurveMappingName, *Name, Smart);
        if (!Registered && BlueprintWrite::Str(C, TEXT("expected_skeleton_revision")) != FullRevision(S->GetSkeleton())) return Error(TEXT("New curve name changes Skeleton; supply its ue_inspect_ta_asset revision."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "FloatCurve", "MCP replace float curve")); S->Modify();
        if (Op == TEXT("remove_float_curve")) S->RawCurveData.FloatCurves.RemoveAt(Found);
        else
        {
            if (!Registered)
            {
                S->GetSkeleton()->Modify(); S->GetSkeleton()->AddSmartNameAndModify(USkeleton::AnimCurveMappingName, *Name, Smart);
                S->GetSkeleton()->MarkPackageDirty(); OtherModified = S->GetSkeleton();
            }
            if (Found == INDEX_NONE) Found = S->RawCurveData.FloatCurves.Add(FFloatCurve(Smart, AACF_DefaultCurve));
            S->RawCurveData.FloatCurves[Found].FloatCurve = Curve;
        }
        S->MarkRawDataAsModified(); S->PostProcessSequence();
    }
    else if (Op == TEXT("add_notify") || Op == TEXT("edit_notify") || Op == TEXT("remove_notify"))
    {
        auto* S = Cast<UAnimSequenceBase>(A); int32 I = INDEX_NONE;
        if (!S || !Fields(C, {TEXT("index"), TEXT("name"), TEXT("class_path"), TEXT("time"), TEXT("duration"), TEXT("track")})) return Error(TEXT("Sequence or Montage required."));
        if (Op != TEXT("add_notify") && !Index(C, TEXT("index"), S->Notifies.Num(), I)) return Error(TEXT("Valid notify index required."));
        double Time = 0, Duration = 0; int32 Track = 0; UClass* Class = nullptr; bool State = false;
        if (Op != TEXT("remove_notify"))
        {
            if (!Number(C, TEXT("time"), Time, 0, S->SequenceLength) || !Number(C, TEXT("duration"), Duration, 0, S->SequenceLength - Time) || !Index(C, TEXT("track"), S->AnimNotifyTracks.Num(), Track)) return Error(TEXT("Valid time/duration/track required."));
            if (Op == TEXT("add_notify"))
            {
                FString Path = BlueprintWrite::Str(C, TEXT("class_path"));
                if (!Path.IsEmpty())
                {
                    if (!Path.StartsWith(TEXT("/Script/")) && !Path.StartsWith(TEXT("/Game/"))) return Error(TEXT("Invalid Notify class path."));
                    Class = LoadObject<UClass>(nullptr, *Path);
                    if (!Class || Class->HasAnyClassFlags(CLASS_Abstract) || (!Class->IsChildOf(UAnimNotify::StaticClass()) && !Class->IsChildOf(UAnimNotifyState::StaticClass()))) return Error(TEXT("Concrete AnimNotify or AnimNotifyState class required."));
                    State = Class->IsChildOf(UAnimNotifyState::StaticClass());
                }
                else if (BlueprintWrite::Str(C, TEXT("name")).IsEmpty()) return Error(TEXT("Named Notify requires name."));
            }
            else State = S->Notifies[I].NotifyStateClass != nullptr;
            if ((State && Duration <= 0) || (!State && Duration != 0)) return Error(TEXT("Notify State requires positive duration; ordinary Notify requires zero."));
        }
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "Notify", "MCP edit notify")); S->Modify();
        if (Op == TEXT("remove_notify")) S->Notifies.RemoveAt(I);
        else
        {
            if (Op == TEXT("add_notify"))
            {
                I = S->Notifies.AddDefaulted(); auto& N = S->Notifies[I];
                N.NotifyName = *BlueprintWrite::Str(C, TEXT("name"));
                if (Class && State) N.NotifyStateClass = NewObject<UAnimNotifyState>(S, Class, NAME_None, RF_Transactional);
                else if (Class) N.Notify = NewObject<UAnimNotify>(S, Class, NAME_None, RF_Transactional);
            }
            auto& N = S->Notifies[I]; N.TrackIndex = Track; N.Link(S, Time); N.SetDuration(Duration);
            if (State) N.EndLink.Link(S, Time + Duration);
            N.TriggerTimeOffset = GetTriggerTimeOffsetForType(S->CalculateOffsetForNotify(Time));
            N.EndTriggerTimeOffset = State ? GetTriggerTimeOffsetForType(S->CalculateOffsetForNotify(Time + Duration)) : 0;
        }
        S->SortNotifies(); S->RefreshCacheData();
    }
    else if (Op == TEXT("set_lod_screen_size") || Op == TEXT("scale_morph_deltas"))
    {
        auto* Mesh = Cast<USkeletalMesh>(A); int32 LOD; double Value;
        if (!Mesh || !Fields(C, {TEXT("lod"), TEXT("value"), TEXT("name")}) || !Index(C, TEXT("lod"), Mesh->GetLODNum(), LOD) || !Number(C, TEXT("value"), Value, 0, Op == TEXT("scale_morph_deltas") ? 100 : 1)) return Error(TEXT("Mesh, lod and bounded value required."));
        UMorphTarget* Morph = nullptr; int32 Count = 0; FMorphTargetDelta* Deltas = nullptr;
        if (Op == TEXT("scale_morph_deltas"))
        {
            Morph = Mesh->FindMorphTarget(*BlueprintWrite::Str(C, TEXT("name")));
            if (Morph) Deltas = Morph->GetMorphTargetDelta(LOD, Count);
            if (!Morph || !Deltas || !Count) return Error(TEXT("Morph has no deltas at this LOD."));
        }
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "MeshData", "MCP edit mesh data")); Mesh->Modify();
        if (Morph)
        {
            Morph->Modify(); for (int32 I = 0; I < Count; ++I) { Deltas[I].PositionDelta *= Value; Deltas[I].TangentZDelta *= Value; }
            Mesh->InitMorphTargetsAndRebuildRenderData();
        }
        else Mesh->GetLODInfo(LOD)->ScreenSize.Default = Value;
    }
    else if (Op == TEXT("replace_morph_deltas"))
    {
        auto* Mesh = Cast<USkeletalMesh>(A); int32 LOD = 0;
        const FString Name = BlueprintWrite::Str(C, TEXT("name"));
        const TArray<TSharedPtr<FJsonValue>>* Input = nullptr;
        if (!Mesh || !BlueprintAnimWrite::ValidName(Name) || !Fields(C, {TEXT("name"), TEXT("lod"), TEXT("deltas")}) ||
            !Index(C, TEXT("lod"), Mesh->GetLODNum(), LOD) || !Mesh->GetImportedModel() || !Mesh->GetImportedModel()->LODModels.IsValidIndex(LOD) ||
            !C->TryGetArrayField(TEXT("deltas"), Input) || Input->Num() > 100000) return Error(TEXT("Mesh, imported lod, valid name and deltas (max 100000) required."));
        const auto& Model = Mesh->GetImportedModel()->LODModels[LOD]; TArray<FMorphTargetDelta> Deltas; TSet<int32> Seen;
        for (const auto& Item : *Input)
        {
            if (Item->Type != EJson::Object) return Error(TEXT("Delta must be an object."));
            FObj D = Item->AsObject(); int32 Source; FVector Position, Tangent;
            if (!Fields(D, {TEXT("source_index"), TEXT("position_delta"), TEXT("tangent_delta")}) || !Index(D, TEXT("source_index"), Model.NumVertices, Source) || Seen.Contains(Source) ||
                !VectorValue(D, TEXT("position_delta"), Position) || !VectorValue(D, TEXT("tangent_delta"), Tangent)) return Error(TEXT("Invalid or duplicate morph source index/delta."));
            FMorphTargetDelta Delta; Delta.SourceIdx = Source; Delta.PositionDelta = Position; Delta.TangentZDelta = Tangent; Deltas.Add(Delta); Seen.Add(Source);
        }
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "MorphReplace", "MCP replace morph deltas")); Mesh->Modify();
        UMorphTarget* Morph = Mesh->FindMorphTarget(*Name);
        if (!Morph) { Morph = NewObject<UMorphTarget>(Mesh, *Name, RF_Public | RF_Transactional); Morph->BaseSkelMesh = Mesh; }
        Morph->Modify(); Morph->PopulateDeltas(Deltas, LOD, Model.Sections, true);
        Mesh->RegisterMorphTarget(Morph, false); Mesh->InitMorphTargetsAndRebuildRenderData();
    }
    else if (Op == TEXT("set_constraint_limits"))
    {
        auto* P = Cast<UPhysicsAsset>(A); int32 I; double Swing1 = 0, Swing2 = 0, Twist = 0;
        if (!P || !Fields(C, {TEXT("index"), TEXT("swing1"), TEXT("swing2"), TEXT("twist")}) || !Index(C, TEXT("index"), P->ConstraintSetup.Num(), I) || !P->ConstraintSetup[I] ||
            !Number(C, TEXT("swing1"), Swing1, 0, 180) || !Number(C, TEXT("swing2"), Swing2, 0, 180) || !Number(C, TEXT("twist"), Twist, 0, 180)) return Error(TEXT("Valid constraint index and angles 0..180 required."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "Constraint", "MCP set angular limits")); P->Modify(); auto* Constraint = P->ConstraintSetup[I]; Constraint->Modify();
        Constraint->DefaultInstance.SetAngularSwing1Limit(Swing1 == 0 ? ACM_Locked : ACM_Limited, Swing1);
        Constraint->DefaultInstance.SetAngularSwing2Limit(Swing2 == 0 ? ACM_Locked : ACM_Limited, Swing2);
        Constraint->DefaultInstance.SetAngularTwistLimit(Twist == 0 ? ACM_Locked : ACM_Limited, Twist); Constraint->PostEditChange();
    }
    else if (Op == TEXT("set_body_mass"))
    {
        auto* P = Cast<UPhysicsAsset>(A); int32 I; double Mass;
        if (!P || !Fields(C, {TEXT("index"), TEXT("mass_kg")}) || !Index(C, TEXT("index"), P->SkeletalBodySetups.Num(), I) || !P->SkeletalBodySetups[I] || !Number(C, TEXT("mass_kg"), Mass, 0.001, 1e6)) return Error(TEXT("Valid body index and positive mass_kg required."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "BodyMass", "MCP set body mass")); P->Modify(); auto* Body = P->SkeletalBodySetups[I]; Body->Modify();
        Body->DefaultInstance.bOverrideMass = true; Body->DefaultInstance.SetMassOverride(Mass, true); Body->PostEditChange();
    }
    else if (Op == TEXT("replace_body_primitives"))
    {
        auto* Physics = Cast<UPhysicsAsset>(A); int32 BodyIndex = 0;
        const TArray<TSharedPtr<FJsonValue>>* Shapes = nullptr;
        if (!Physics || !Fields(C, {TEXT("index"), TEXT("shapes")}) || !Index(C, TEXT("index"), Physics->SkeletalBodySetups.Num(), BodyIndex) ||
            !Physics->SkeletalBodySetups[BodyIndex] || !C->TryGetArrayField(TEXT("shapes"), Shapes) || Shapes->Num() > 256) return Error(TEXT("Valid body index and shapes array (max 256) required."));
        TArray<FKSphereElem> Spheres; TArray<FKBoxElem> Boxes; TArray<FKSphylElem> Capsules;
        for (const auto& Item : *Shapes)
        {
            if (Item->Type != EJson::Object) return Error(TEXT("Shape must be an object."));
            FObj Shape = Item->AsObject(); FVector Center, Rotation, Size; double Radius = 0, Length = 0;
            const FString Kind = BlueprintWrite::Str(Shape, TEXT("kind"));
            if (!VectorValue(Shape, TEXT("center"), Center)) return Error(TEXT("Shape requires finite local center in cm."));
            if (Kind == TEXT("sphere"))
            {
                if (!Fields(Shape, {TEXT("kind"), TEXT("center"), TEXT("radius")}) || !Number(Shape, TEXT("radius"), Radius, 0.001, 1e6)) return Error(TEXT("Sphere requires positive radius."));
                FKSphereElem Sphere; Sphere.Center = Center; Sphere.Radius = Radius; Spheres.Add(Sphere);
            }
            else if (Kind == TEXT("box"))
            {
                if (!Fields(Shape, {TEXT("kind"), TEXT("center"), TEXT("rotation_degrees"), TEXT("size")}) || !VectorValue(Shape, TEXT("rotation_degrees"), Rotation) || !VectorValue(Shape, TEXT("size"), Size) ||
                    Size.X <= 0 || Size.Y <= 0 || Size.Z <= 0) return Error(TEXT("Box requires rotation and positive full size xyz."));
                FKBoxElem Box; Box.Center = Center; Box.Rotation = FRotator(Rotation.X, Rotation.Y, Rotation.Z); Box.X = Size.X; Box.Y = Size.Y; Box.Z = Size.Z; Boxes.Add(Box);
            }
            else if (Kind == TEXT("capsule"))
            {
                if (!Fields(Shape, {TEXT("kind"), TEXT("center"), TEXT("rotation_degrees"), TEXT("radius"), TEXT("length")}) || !VectorValue(Shape, TEXT("rotation_degrees"), Rotation) ||
                    !Number(Shape, TEXT("radius"), Radius, 0.001, 1e6) || !Number(Shape, TEXT("length"), Length, 0, 1e6)) return Error(TEXT("Capsule requires rotation, radius and cylinder length."));
                FKSphylElem Capsule; Capsule.Center = Center; Capsule.Rotation = FRotator(Rotation.X, Rotation.Y, Rotation.Z); Capsule.Radius = Radius; Capsule.Length = Length; Capsules.Add(Capsule);
            }
            else return Error(TEXT("Shape kind must be sphere, box or capsule."));
        }
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "BodyShapes", "MCP replace body primitives"));
        auto* Body = Physics->SkeletalBodySetups[BodyIndex]; Physics->Modify(); Body->Modify();
        Body->AggGeom.SphereElems = MoveTemp(Spheres); Body->AggGeom.BoxElems = MoveTemp(Boxes); Body->AggGeom.SphylElems = MoveTemp(Capsules);
        Body->InvalidatePhysicsData(); Body->CreatePhysicsMeshes(); Body->PostEditChange();
    }
    else if (Op == TEXT("sequencer_replace_float_keys"))
    {
        auto* Sequence = Cast<ULevelSequence>(A);
        auto* Section = FindObject<UMovieSceneSection>(nullptr, *BlueprintWrite::Str(C, TEXT("section_path")));
        if (!Sequence || !Section || !Section->IsIn(Sequence) || !Fields(C, {TEXT("section_path"), TEXT("channel"), TEXT("keys")})) return Error(TEXT("Select a section owned by this LevelSequence."));
        auto Channels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>(); int32 Channel = 0;
        const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
        if (!Index(C, TEXT("channel"), Channels.Num(), Channel) || !C->TryGetArrayField(TEXT("keys"), Keys) || Keys->Num() > 100000) return Error(TEXT("Valid float channel index and keys array required."));
        TArray<FFrameNumber> Times; TArray<FMovieSceneFloatValue> Values; double Previous = -1e8 - 1;
        for (const auto& Item : *Keys)
        {
            if (Item->Type != EJson::Object) return Error(TEXT("Key must be an object."));
            FObj Key = Item->AsObject(); double Frame = 0, Value = 0;
            const FString Mode = BlueprintWrite::Str(Key, TEXT("interpolation"));
            if (!Fields(Key, {TEXT("frame"), TEXT("value"), TEXT("interpolation"), TEXT("arrive_tangent"), TEXT("leave_tangent")}) || !Number(Key, TEXT("frame"), Frame, -1e8, 1e8) || Frame != FMath::FloorToDouble(Frame) || Frame <= Previous ||
                !Number(Key, TEXT("value"), Value, -1e9, 1e9) || (Mode != TEXT("linear") && Mode != TEXT("constant") && Mode != TEXT("cubic"))) return Error(TEXT("Increasing integer frames, finite values and linear/constant/cubic interpolation required."));
            Times.Add(FFrameNumber(static_cast<int32>(Frame))); FMovieSceneFloatValue V(Value); V.InterpMode = Mode == TEXT("linear") ? RCIM_Linear : Mode == TEXT("constant") ? RCIM_Constant : RCIM_Cubic;
            if (Mode == TEXT("cubic"))
            {
                double Arrive = 0, Leave = 0;
                if (!Number(Key, TEXT("arrive_tangent"), Arrive, -1e9, 1e9) || !Number(Key, TEXT("leave_tangent"), Leave, -1e9, 1e9)) return Error(TEXT("Cubic keys require arrive_tangent/leave_tangent in value per tick-resolution frame."));
                V.TangentMode = RCTM_User; V.Tangent.ArriveTangent = Arrive; V.Tangent.LeaveTangent = Leave;
            }
            Values.Add(V); Previous = Frame;
        }
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "FloatChannel", "MCP replace float channel keys")); Sequence->Modify(); Section->Modify();
        Channels[Channel]->Set(MoveTemp(Times), MoveTemp(Values)); Section->MarkAsChanged();
    }
    else if (Op == TEXT("sequencer_edit_section") || Op == TEXT("sequencer_remove_section"))
    {
        auto* Sequence = Cast<ULevelSequence>(A);
        const FString Path = BlueprintWrite::Str(C, TEXT("section_path"));
        auto* Section = FindObject<UMovieSceneSkeletalAnimationSection>(nullptr, *Path);
        if (!Sequence || !Section || !Section->IsIn(Sequence) || !Fields(C, {TEXT("section_path"), TEXT("start_frame"), TEXT("end_frame"), TEXT("play_rate")})) return Error(TEXT("Select an animation section belonging to this LevelSequence."));
        auto* Track = Cast<UMovieSceneSkeletalAnimationTrack>(Section->GetOuter()); if (!Track) return Error(TEXT("Animation track not found."));
        double Start = 0, End = 0, Rate = 1;
        if (Op == TEXT("sequencer_edit_section") && (!Number(C, TEXT("start_frame"), Start, -1e8, 1e8) || !Number(C, TEXT("end_frame"), End, -1e8, 1e8) || End <= Start ||
            Start != FMath::FloorToDouble(Start) || End != FMath::FloorToDouble(End) || !Number(C, TEXT("play_rate"), Rate, 0.001, 100))) return Error(TEXT("Valid frame range and positive play_rate required."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "SequenceSection", "MCP edit animation section")); Sequence->Modify(); Track->Modify(); Section->Modify();
        if (Op == TEXT("sequencer_remove_section")) Track->RemoveSection(*Section);
        else { Section->SetRange(TRange<FFrameNumber>(FFrameNumber(static_cast<int32>(Start)), FFrameNumber(static_cast<int32>(End)))); Section->Params.PlayRate = Rate; }
    }
    else if (Op == TEXT("sequencer_add_animation"))
    {
        auto* Sequence = Cast<ULevelSequence>(A); FGuid Binding; double Start = 0, End = 0;
        FString Path = BlueprintWrite::Str(C, TEXT("animation"));
        auto* Animation = Path.StartsWith(TEXT("/Game/")) ? LoadObject<UAnimSequence>(nullptr, *Path) : nullptr;
        if (!Sequence || !Sequence->GetMovieScene() || !Animation || !Fields(C, {TEXT("binding"), TEXT("animation"), TEXT("start_frame"), TEXT("end_frame")}) ||
            !FGuid::Parse(BlueprintWrite::Str(C, TEXT("binding")), Binding) || !Sequence->GetMovieScene()->FindBinding(Binding) ||
            !Number(C, TEXT("start_frame"), Start, -1e8, 1e8) || !Number(C, TEXT("end_frame"), End, -1e8, 1e8) || End <= Start || Start != FMath::FloorToDouble(Start) || End != FMath::FloorToDouble(End)) return Error(TEXT("Existing binding, animation, integer start/end tick-resolution frames required."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "Sequencer", "MCP add animation section"));
        Sequence->Modify(); UMovieScene* Scene = Sequence->GetMovieScene(); Scene->Modify();
        auto* Track = Scene->FindTrack<UMovieSceneSkeletalAnimationTrack>(Binding);
        if (!Track) Track = Scene->AddTrack<UMovieSceneSkeletalAnimationTrack>(Binding);
        if (!Track) return Error(TEXT("Unable to create animation track.")); Track->Modify();
        auto* Section = Cast<UMovieSceneSkeletalAnimationSection>(Track->CreateNewSection());
        if (!Section) return Error(TEXT("Unable to create animation section.")); Section->Modify(); Section->Params.Animation = Animation;
        Section->SetRange(TRange<FFrameNumber>(FFrameNumber(static_cast<int32>(Start)), FFrameNumber(static_cast<int32>(End)))); Track->AddSection(*Section);
    }
    else return Error(TEXT("Unsupported TA operation."));
    A->PostEditChange(); A->MarkPackageDirty();
    FObj R = MakeShared<FJsonObject>(); R->SetBoolField(TEXT("ok"), true); R->SetBoolField(TEXT("saved"), false);
    R->SetStringField(TEXT("revision"), FullRevision(A)); R->SetStringField(TEXT("asset"), A->GetPathName());
    if (OtherModified) { R->SetStringField(TEXT("also_modified"), OtherModified->GetPathName()); R->SetStringField(TEXT("also_modified_revision"), FullRevision(OtherModified)); }
    return R;
}
}
