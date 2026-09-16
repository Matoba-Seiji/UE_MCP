#pragma once
#include "Factories/BlendSpaceFactoryNew.h"
#include "Factories/BlendSpaceFactory1D.h"
#include "LODUtilities.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Sections/MovieScene3DTransformSection.h"

namespace TAProductionOps
{
using namespace TAAssetOps;
inline FObj Error(const FString& Text) { return BlueprintWrite::Error(Text); }
inline FObj Done(UObject* A)
{
    A->PostEditChange(); A->MarkPackageDirty();
    FObj R = MakeShared<FJsonObject>(); R->SetBoolField(TEXT("ok"), true); R->SetBoolField(TEXT("saved"), false);
    R->SetStringField(TEXT("asset"), A->GetPathName()); R->SetStringField(TEXT("revision"), FullRevision(A)); return R;
}
inline FObj Evaluate(const FObj& Request)
{
    const FString Mode = BlueprintWrite::Str(Request, TEXT("mode"));
    FObj R = MakeShared<FJsonObject>();
    if (Mode == TEXT("editor_actors"))
    {
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (!World) return Error(TEXT("Editor world unavailable."));
        TArray<TSharedPtr<FJsonValue>> Items;
        for (TActorIterator<AActor> It(World); It && Items.Num() < 500; ++It)
        {
            FObj Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("path"), It->GetPathName()); Item->SetStringField(TEXT("label"), It->GetActorLabel()); Items.Add(JV(Item));
        }
        R->SetArrayField(TEXT("actors"), Items); return R;
    }
    UObject* A = Load(Request); if (!A) return Error(TEXT("Asset not found."));
    if (Mode == TEXT("blend_weights"))
    {
        auto* B = Cast<UBlendSpaceBase>(A); FVector Position;
        FObj C; if (!B || !Config(Request, C) || !VectorValue(C, TEXT("position"), Position)) return Error(TEXT("BlendSpace and config_json position required."));
        TArray<FBlendSampleData> Samples; B->GetSamplesFromBlendInput(Position, Samples);
        TArray<TSharedPtr<FJsonValue>> Items;
        for (const auto& Sample : Samples)
        {
            FObj Item = MakeShared<FJsonObject>(); Item->SetNumberField(TEXT("index"), Sample.SampleDataIndex); Item->SetNumberField(TEXT("weight"), Sample.TotalWeight); Items.Add(JV(Item));
        }
        R->SetArrayField(TEXT("samples"), Items); R->SetStringField(TEXT("scope"), TEXT("grid interpolation weights, not evaluated pose")); return R;
    }
    double Time = 0;
    if (Mode == TEXT("float_curve"))
    {
        auto* S = Cast<UAnimSequence>(A); if (!S || !Number(Request, TEXT("time"), Time, 0, S->SequenceLength)) return Error(TEXT("Sequence and time in seconds required."));
        for (const auto& Curve : S->RawCurveData.FloatCurves) if (Curve.Name.DisplayName == *BlueprintWrite::Str(Request, TEXT("name")))
        { R->SetNumberField(TEXT("value"), Curve.FloatCurve.Eval(Time)); return R; }
        return Error(TEXT("Curve not found."));
    }
    if (Mode == TEXT("sequencer_float"))
    {
        auto* Sequence = Cast<ULevelSequence>(A); auto* Section = FindObject<UMovieSceneSection>(nullptr, *BlueprintWrite::Str(Request, TEXT("section_path")));
        if (!Sequence || !Section || !Section->IsIn(Sequence) || !Number(Request, TEXT("time"), Time, -1e8, 1e8)) return Error(TEXT("Owned section and tick-resolution frame time required."));
        auto Channels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>(); int32 I = 0;
        if (!Index(Request, TEXT("channel"), Channels.Num(), I)) return Error(TEXT("Float channel not found."));
        float Value = 0; if (!Channels[I]->Evaluate(FFrameTime::FromDecimal(Time), Value)) return Error(TEXT("Channel has no evaluable value."));
        R->SetNumberField(TEXT("value"), Value); return R;
    }
    return Error(TEXT("Unknown evaluation mode."));
}
inline FObj Create(const FObj& Request)
{
    if (!GEditor || GEditor->PlayWorld) return Error(TEXT("Stop PIE before creating TA assets."));
    double Expires = 0;
    if (!Number(Request, TEXT("expires_unix"), Expires, FDateTime::UtcNow().ToUnixTimestamp(), 1e12)) return Error(TEXT("Request expired."));
    const FString Dest = BlueprintWrite::Str(Request, TEXT("destination"));
    if (!Dest.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Dest) || Dest.Contains(TEXT(".")) || FPackageName::DoesPackageExist(Dest) || FindPackage(nullptr, *Dest)) return Error(TEXT("Unused /Game/Folder/Name package path required."));
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    const FString SourcePath = BlueprintWrite::Str(Request, TEXT("source_asset"));
    UObject* Created = nullptr;
    if (!SourcePath.IsEmpty())
    {
        UObject* Source = SourcePath.StartsWith(TEXT("/Game/")) ? LoadObject<UObject>(nullptr, *SourcePath) : nullptr;
        if (!Source || !SupportedAsset(Source) || !Source->IsAsset()) return Error(TEXT("Unsupported source asset."));
        if (BlueprintWrite::Str(Request, TEXT("expected_revision")) != FullRevision(Source)) return Error(TEXT("Source revision mismatch."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "DuplicateTA", "MCP duplicate TA asset"));
        Created = Tools.DuplicateAsset(FPackageName::GetLongPackageAssetName(Dest), FPackageName::GetLongPackagePath(Dest), Source);
    }
    else
    {
        const FString Kind = BlueprintWrite::Str(Request, TEXT("kind")); UFactory* Factory = nullptr; UClass* Class = nullptr;
        if (Kind == TEXT("blendspace") || Kind == TEXT("blendspace1d"))
        {
            const FString SkeletonPath = BlueprintWrite::Str(Request, TEXT("skeleton_path"));
            auto* Skeleton = SkeletonPath.StartsWith(TEXT("/Game/")) ? LoadObject<USkeleton>(nullptr, *SkeletonPath) : nullptr;
            if (!Skeleton) return Error(TEXT("Existing /Game/ Skeleton required."));
            if (Kind == TEXT("blendspace1d")) { auto* F = NewObject<UBlendSpaceFactory1D>(); F->TargetSkeleton = Skeleton; Factory = F; }
            else { auto* F = NewObject<UBlendSpaceFactoryNew>(); F->TargetSkeleton = Skeleton; Factory = F; }
            Class = Kind == TEXT("blendspace1d") ? UBlendSpace1D::StaticClass() : UBlendSpace::StaticClass();
        }
        else if (Kind == TEXT("level_sequence"))
        {
            const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "NewSequence", "MCP create LevelSequence"));
            UPackage* Package = CreatePackage(nullptr, *Dest);
            auto* Sequence = NewObject<ULevelSequence>(Package, *FPackageName::GetLongPackageAssetName(Dest), RF_Public | RF_Standalone | RF_Transactional);
            Sequence->Initialize(); FAssetRegistryModule::AssetCreated(Sequence); Created = Sequence;
        }
        else if (Kind == TEXT("control_rig"))
        {
            if (!FModuleManager::Get().LoadModule(TEXT("ControlRigEditor"))) return Error(TEXT("ControlRigEditor unavailable."));
            UClass* FactoryClass = FindObject<UClass>(nullptr, TEXT("/Script/ControlRigEditor.ControlRigBlueprintFactory"));
            if (!FactoryClass || !FactoryClass->IsChildOf(UFactory::StaticClass())) return Error(TEXT("ControlRig factory unavailable."));
            Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass); Class = UControlRigBlueprint::StaticClass();
        }
        else return Error(TEXT("kind must be blendspace, blendspace1d, level_sequence or control_rig."));
        if (Factory)
        {
            const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "NewTA", "MCP create TA asset"));
            Created = Tools.CreateAsset(FPackageName::GetLongPackageAssetName(Dest), FPackageName::GetLongPackagePath(Dest), Class, Factory);
        }
    }
    if (!Created) return Error(TEXT("Asset creation failed."));
    return Done(Created);
}
inline FObj Edit(UObject* A, const FString& Op, const FObj& C)
{
    if (Op == TEXT("remove_morph_target"))
    {
        auto* Mesh = Cast<USkeletalMesh>(A); bool Ack = false;
        if (!Mesh || !Fields(C, {TEXT("name"), TEXT("acknowledge_references")}) || !C->TryGetBoolField(TEXT("acknowledge_references"), Ack) || !Ack) return Error(TEXT("Mesh and reference-change acknowledgement required."));
        auto* Morph = Mesh->FindMorphTarget(*BlueprintWrite::Str(C, TEXT("name"))); if (!Morph) return Error(TEXT("Morph not found."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "RemoveMorph", "MCP remove morph target")); Mesh->Modify(); Morph->Modify();
        Mesh->UnregisterMorphTarget(Morph); Mesh->InitMorphTargetsAndRebuildRenderData(); return Done(A);
    }
    if (Op == TEXT("replace_montage_sections"))
    {
        auto* Montage = Cast<UAnimMontage>(A); const TArray<TSharedPtr<FJsonValue>>* Input = nullptr;
        if (!Montage || !Fields(C, {TEXT("sections")}) || !C->TryGetArrayField(TEXT("sections"), Input) || Input->Num() < 1 || Input->Num() > 500) return Error(TEXT("Montage requires 1..500 sections."));
        TArray<FName> Names, NextNames; TArray<float> Times; TSet<FName> Unique; float Previous = -1;
        for (const auto& Item : *Input)
        {
            if (Item->Type != EJson::Object) return Error(TEXT("Section must be an object."));
            const FObj Section = Item->AsObject(); FString Name, Next; double Time = 0;
            if (!Fields(Section, {TEXT("name"), TEXT("time"), TEXT("next")}) || !Section->TryGetStringField(TEXT("name"), Name) || !BlueprintAnimWrite::ValidName(Name) ||
                !Section->TryGetStringField(TEXT("next"), Next) || !Number(Section, TEXT("time"), Time, 0, Montage->SequenceLength) || static_cast<float>(Time) <= Previous || Unique.Contains(*Name)) return Error(TEXT("Unique names and strictly increasing representable section times required."));
            Names.Add(*Name); NextNames.Add(*Next); Times.Add(Time); Unique.Add(*Name); Previous = Time;
        }
        if (Times[0] != 0) return Error(TEXT("First section must start at zero."));
        for (FName Next : NextNames) if (!Next.IsNone() && !Unique.Contains(Next)) return Error(TEXT("Next section not in replacement set."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "Sections", "MCP replace montage sections")); Montage->Modify(); Montage->CompositeSections.Empty();
        for (int32 I = 0; I < Names.Num(); ++I)
        {
            const int32 SectionIndex = Montage->AddAnimCompositeSection(Names[I], Times[I]);
            if (SectionIndex == INDEX_NONE) return Error(TEXT("Engine rejected section; inspect partial state before retrying."));
            Montage->CompositeSections[SectionIndex].NextSectionName = NextNames[I];
        }
        Montage->UpdateLinkableElements(); return Done(A);
    }
    if (Op == TEXT("set_notify_object"))
    {
        auto* Sequence = Cast<UAnimSequenceBase>(A); int32 NotifyIndex = 0;
        if (!Sequence || !Fields(C, {TEXT("index"), TEXT("property"), TEXT("object_path")}) || !Index(C, TEXT("index"), Sequence->Notifies.Num(), NotifyIndex)) return Error(TEXT("Valid notify index required."));
        UObject* Notify = Sequence->Notifies[NotifyIndex].Notify ? static_cast<UObject*>(Sequence->Notifies[NotifyIndex].Notify) : Sequence->Notifies[NotifyIndex].NotifyStateClass;
        if (!Notify || Notify->GetOuter() != Sequence) return Error(TEXT("Notify has no owned object."));
        auto* Property = FindField<UObjectProperty>(Notify->GetClass(), *BlueprintWrite::Str(C, TEXT("property")));
        if (!Property || Cast<UClassProperty>(Property) || !Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst | CPF_InstancedReference) || Property->ArrayDim != 1) return Error(TEXT("Editable non-instanced asset reference required."));
        FString Path; if (!C->TryGetStringField(TEXT("object_path"), Path)) return Error(TEXT("object_path string required; empty clears."));
        UObject* Value = nullptr;
        if (!Path.IsEmpty())
        {
            if (!Path.StartsWith(TEXT("/Game/"))) return Error(TEXT("Only /Game/ asset references accepted."));
            Value = LoadObject<UObject>(nullptr, *Path);
            if (!Value || !Value->IsAsset() || !Value->IsA(Property->PropertyClass)) return Error(TEXT("Missing asset or wrong reference type."));
        }
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "NotifyAsset", "MCP set Notify asset")); Sequence->Modify(); Notify->Modify(); Notify->PreEditChange(Property);
        Property->SetObjectPropertyValue_InContainer(Notify, Value); FPropertyChangedEvent Event(Property); Notify->PostEditChangeProperty(Event); return Done(A);
    }
    if (Op == TEXT("rig_reparent_element") || Op == TEXT("rig_remove_element"))
    {
        auto* Rig = Cast<UControlRigBlueprint>(A); const FString Kind = BlueprintWrite::Str(C, TEXT("kind"));
        const FName Name(*BlueprintWrite::Str(C, TEXT("name"))), Parent(*BlueprintWrite::Str(C, TEXT("parent")));
        bool Ack = false;
        if (!Rig || !Fields(C, {TEXT("kind"), TEXT("name"), TEXT("parent"), TEXT("acknowledge_references")}) || !C->TryGetBoolField(TEXT("acknowledge_references"), Ack) || !Ack) return Error(TEXT("Explicit reference-change acknowledgement required; graph references are not repaired."));
        ERigElementType Type;
        if (Kind == TEXT("bone")) Type = ERigElementType::Bone;
        else if (Kind == TEXT("space")) Type = ERigElementType::Space;
        else if (Kind == TEXT("control")) Type = ERigElementType::Control;
        else return Error(TEXT("kind must be bone/space/control."));
        auto& H = Rig->HierarchyContainer; const FRigElementKey Key(Name, Type); const int32 I = H.GetIndex(Key);
        if (I == INDEX_NONE) return Error(TEXT("Element not found."));
        const int32 ParentIndex = Parent.IsNone() ? INDEX_NONE : H.GetIndex(FRigElementKey(Parent, Type));
        if (Op == TEXT("rig_reparent_element") && (!Parent.IsNone() && (ParentIndex == INDEX_NONE || Parent == Name || H.IsParentedTo(Type, ParentIndex, Type, I)))) return Error(TEXT("Parent missing or would create a cycle."));
        if (Op == TEXT("rig_remove_element"))
            for (const FRigElementKey& Other : H.GetAllItems())
                if (Other != Key && H.IsParentedTo(Other.Type, H.GetIndex(Other), Type, I)) return Error(TEXT("Remove or reparent dependent hierarchy elements first."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "RigHierarchy", "MCP edit rig hierarchy")); Rig->Modify(); bool Ok = true;
        if (Op == TEXT("rig_remove_element"))
        {
            if (Type == ERigElementType::Bone) H.BoneHierarchy.Remove(Name);
            else if (Type == ERigElementType::Space) H.SpaceHierarchy.Remove(Name);
            else H.ControlHierarchy.Remove(Name);
        }
        else
        {
            if (Type == ERigElementType::Bone) Ok = H.BoneHierarchy.Reparent(Name, Parent);
            else if (Type == ERigElementType::Space) Ok = H.SpaceHierarchy.Reparent(Name, Parent.IsNone() ? ERigSpaceType::Global : ERigSpaceType::Space, Parent);
            else Ok = H.ControlHierarchy.Reparent(Name, Parent);
        }
        if (!Ok) return Error(TEXT("Engine rejected hierarchy edit; inspect before retrying."));
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Rig); return Done(A);
    }
    if (Op == TEXT("set_notify_scalar"))
    {
        auto* Sequence = Cast<UAnimSequenceBase>(A); int32 IndexValue = 0;
        if (!Sequence || !Fields(C, {TEXT("index"), TEXT("property"), TEXT("value")}) || !Index(C, TEXT("index"), Sequence->Notifies.Num(), IndexValue)) return Error(TEXT("Valid Notify index/property/value required."));
        UObject* Notify = Sequence->Notifies[IndexValue].Notify ? static_cast<UObject*>(Sequence->Notifies[IndexValue].Notify) : Sequence->Notifies[IndexValue].NotifyStateClass;
        if (!Notify || Notify->GetOuter() != Sequence) return Error(TEXT("Notify has no owned object."));
        UProperty* Property = FindField<UProperty>(Notify->GetClass(), *BlueprintWrite::Str(C, TEXT("property")));
        FString Value; if (!C->TryGetStringField(TEXT("value"), Value)) return Error(TEXT("value must be UE property text string."));
        if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient) || Property->ArrayDim != 1 ||
            (!Cast<UNumericProperty>(Property) && !Cast<UBoolProperty>(Property) && !Cast<UStrProperty>(Property) && !Cast<UNameProperty>(Property))) return Error(TEXT("Only editable scalar number/bool/string/name properties are supported."));
        void* Temp = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment()); Property->InitializeValue(Temp);
        const TCHAR* End = Property->ImportText(*Value, Temp, PPF_None, Notify); bool Valid = End && FString(End).TrimStartAndEnd().IsEmpty();
        if (auto* Numeric = Cast<UNumericProperty>(Property))
        {
            const double NumberValue = Numeric->IsFloatingPoint() ? Numeric->GetFloatingPointPropertyValue(Temp) : static_cast<double>(Numeric->GetSignedIntPropertyValue(Temp));
            Valid = Valid && FMath::IsFinite(NumberValue);
            if (Property->HasMetaData(TEXT("ClampMin"))) Valid = Valid && NumberValue >= FCString::Atod(*Property->GetMetaData(TEXT("ClampMin")));
            if (Property->HasMetaData(TEXT("ClampMax"))) Valid = Valid && NumberValue <= FCString::Atod(*Property->GetMetaData(TEXT("ClampMax")));
        }
        if (!Valid) { Property->DestroyValue(Temp); FMemory::Free(Temp); return Error(TEXT("Invalid scalar value.")); }
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "NotifyScalar", "MCP edit Notify scalar")); Sequence->Modify(); Notify->Modify(); Notify->PreEditChange(Property);
        Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Notify), Temp); Property->DestroyValue(Temp); FMemory::Free(Temp);
        FPropertyChangedEvent Event(Property); Notify->PostEditChangeProperty(Event); return Done(A);
    }
    if (Op == TEXT("replace_transform_curve") || Op == TEXT("remove_transform_curve") || Op == TEXT("bake_transform_curves"))
    {
        auto* S = Cast<UAnimSequence>(A); if (!S || !S->GetSkeleton()) return Error(TEXT("AnimSequence with Skeleton required."));
        if (Op == TEXT("bake_transform_curves"))
        {
            bool Ack = false; if (!Fields(C, {TEXT("acknowledge_raw_track_changes")}) || !C->TryGetBoolField(TEXT("acknowledge_raw_track_changes"), Ack) || !Ack) return Error(TEXT("Explicit raw-track change acknowledgement required."));
            const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "BakeCurve", "MCP bake transform curves")); S->Modify(); S->BakeTrackCurvesToRawAnimation(); return Done(A);
        }
        const FString Name = BlueprintWrite::Str(C, TEXT("name"));
        if (!Fields(C, {TEXT("name"), TEXT("keys"), TEXT("expected_skeleton_revision")}) || S->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(*Name) == INDEX_NONE) return Error(TEXT("Transform curve must name an existing bone."));
        const int32 Found = S->RawCurveData.TransformCurves.IndexOfByPredicate([&](const FTransformCurve& Curve) { return Curve.Name.DisplayName == *Name; });
        if (Op == TEXT("remove_transform_curve"))
        {
            if (Found == INDEX_NONE) return Error(TEXT("Transform curve not found."));
            const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "RemoveTransformCurve", "MCP remove transform curve")); S->Modify(); S->RawCurveData.TransformCurves.RemoveAt(Found); return Done(A);
        }
        const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
        if (!C->TryGetArrayField(TEXT("keys"), Keys) || Keys->Num() > 100000) return Error(TEXT("keys required, max 100000."));
        FTransformCurve Curve; double Previous = -1;
        for (const auto& Item : *Keys)
        {
            if (Item->Type != EJson::Object) return Error(TEXT("Key must be an object."));
            FObj Key = Item->AsObject(); FVector Translation, Rotation, Scale; double Time = 0;
            if (!Fields(Key, {TEXT("time"), TEXT("translation"), TEXT("rotation_degrees"), TEXT("scale")}) || !Number(Key, TEXT("time"), Time, 0, S->SequenceLength) || Time <= Previous ||
                !VectorValue(Key, TEXT("translation"), Translation) || !VectorValue(Key, TEXT("rotation_degrees"), Rotation) || !VectorValue(Key, TEXT("scale"), Scale) || Scale.X <= 0 || Scale.Y <= 0 || Scale.Z <= 0) return Error(TEXT("Increasing times and valid transform keys required."));
            Curve.UpdateOrAddKey(FTransform(FRotator(Rotation.X, Rotation.Y, Rotation.Z), Translation, Scale), Time); Previous = Time;
        }
        FSmartName Smart; auto* Skeleton = S->GetSkeleton();
        const bool Registered = Skeleton->GetSmartNameByName(USkeleton::AnimTrackCurveMappingName, *Name, Smart);
        if (!Registered && BlueprintWrite::Str(C, TEXT("expected_skeleton_revision")) != FullRevision(Skeleton)) return Error(TEXT("New name requires current Skeleton TA revision."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "TransformCurve", "MCP replace transform curve")); S->Modify();
        if (!Registered) { Skeleton->Modify(); Skeleton->AddSmartNameAndModify(USkeleton::AnimTrackCurveMappingName, *Name, Smart); Skeleton->MarkPackageDirty(); }
        Curve.Name = Smart; Curve.SetCurveTypeFlags(AACF_DefaultCurve);
        if (Found == INDEX_NONE) S->RawCurveData.TransformCurves.Add(Curve); else S->RawCurveData.TransformCurves[Found] = Curve;
        FObj R = Done(A); R->SetStringField(TEXT("bake_required"), TEXT("Call bake_transform_curves explicitly to update raw animation."));
        if (!Registered) { R->SetStringField(TEXT("also_modified"), Skeleton->GetPathName()); R->SetStringField(TEXT("also_modified_revision"), FullRevision(Skeleton)); }
        return R;
    }
    if (Op == TEXT("regenerate_lods") || Op == TEXT("remove_lod"))
    {
        auto* Mesh = Cast<USkeletalMesh>(A); if (!Mesh) return Error(TEXT("SkeletalMesh required."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "LODWorkflow", "MCP change LODs"));
        if (Op == TEXT("remove_lod"))
        {
            int32 LOD = 0; if (!Fields(C, {TEXT("lod")}) || !Index(C, TEXT("lod"), Mesh->GetLODNum(), LOD) || LOD == 0) return Error(TEXT("Existing non-base LOD required."));
            Mesh->Modify(); FSkeletalMeshUpdateContext Context; Context.SkeletalMesh = Mesh; FLODUtilities::RemoveLOD(Context, LOD);
        }
        else
        {
            double Count = 0; bool Imported = false;
            if (!Fields(C, {TEXT("count"), TEXT("regenerate_imported")}) || !Number(C, TEXT("count"), Count, Mesh->GetLODNum(), 8) || Count != FMath::FloorToDouble(Count) || !C->TryGetBoolField(TEXT("regenerate_imported"), Imported)) return Error(TEXT("LOD count must be current count..8; regenerate_imported required."));
            Mesh->Modify(); if (!FLODUtilities::RegenerateLOD(Mesh, Count, Imported, false)) return Error(TEXT("LOD regeneration failed or reduction backend unavailable; inspect asset before retrying."));
        }
        return Done(A);
    }
    if (Op == TEXT("sequencer_bind_actor"))
    {
        auto* Sequence = Cast<ULevelSequence>(A); UWorld* World = GEditor->GetEditorWorldContext().World();
        auto* Actor = FindObject<AActor>(nullptr, *BlueprintWrite::Str(C, TEXT("object_path")));
        if (!Sequence || !Sequence->GetMovieScene() || !World || !IsValid(Actor) || Actor->GetWorld() != World || !Fields(C, {TEXT("object_path")})) return Error(TEXT("Existing Actor in the editor world required."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "BindActor", "MCP bind Sequencer actor")); Sequence->Modify(); Sequence->GetMovieScene()->Modify();
        FGuid Id = Sequence->GetMovieScene()->AddPossessable(Actor->GetActorLabel(), Actor->GetClass()); Sequence->BindPossessableObject(Id, *Actor, World);
        FObj R = Done(A); R->SetStringField(TEXT("binding"), Id.ToString()); return R;
    }
    if (Op == TEXT("sequencer_add_transform"))
    {
        auto* Sequence = Cast<ULevelSequence>(A); FGuid Binding; double Start = 0, End = 0;
        if (!Sequence || !Sequence->GetMovieScene() || !Fields(C, {TEXT("binding"), TEXT("start_frame"), TEXT("end_frame")}) || !FGuid::Parse(BlueprintWrite::Str(C, TEXT("binding")), Binding) || !Sequence->GetMovieScene()->FindBinding(Binding) ||
            !Number(C, TEXT("start_frame"), Start, -1e8, 1e8) || !Number(C, TEXT("end_frame"), End, -1e8, 1e8) || End <= Start || Start != FMath::FloorToDouble(Start) || End != FMath::FloorToDouble(End)) return Error(TEXT("Binding and integer frame range required."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "TransformSection", "MCP add transform section")); Sequence->Modify(); Sequence->GetMovieScene()->Modify();
        auto* Track = Sequence->GetMovieScene()->FindTrack<UMovieScene3DTransformTrack>(Binding);
        if (!Track) Track = Sequence->GetMovieScene()->AddTrack<UMovieScene3DTransformTrack>(Binding);
        if (!Track) return Error(TEXT("Transform track creation failed.")); Track->Modify();
        auto* Section = Track->CreateNewSection(); Section->SetRange(TRange<FFrameNumber>(FFrameNumber(static_cast<int32>(Start)), FFrameNumber(static_cast<int32>(End)))); Track->AddSection(*Section);
        FObj R = Done(A); R->SetStringField(TEXT("section_path"), Section->GetPathName()); return R;
    }
    return nullptr;
}
}
