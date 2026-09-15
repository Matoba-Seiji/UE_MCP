#pragma once
#include "Engine/SkeletalMeshSocket.h"

namespace SkeletonWrite
{
inline FObj Snapshot(USkeleton* S)
{
    FObj R = MakeShared<FJsonObject>();
    R->SetStringField(TEXT("asset"), S->GetPathName());
    FObj Properties = MakeShared<FJsonObject>();
    for (TFieldIterator<UProperty> It(S->GetClass()); It; ++It)
    {
        if (It->HasAnyPropertyFlags(CPF_Transient)) continue;
        FString V; It->ExportText_InContainer(0, V, S, S, S, PPF_None);
        Properties->SetStringField(It->GetName(), V);
    }
    R->SetObjectField(TEXT("properties_text"), Properties);
    TArray<TSharedPtr<FJsonValue>> Sockets;
    for (USkeletalMeshSocket* Socket : S->Sockets) if (Socket)
    {
        FObj Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"), Socket->SocketName.ToString());
        Item->SetStringField(TEXT("bone"), Socket->BoneName.ToString());
        Item->SetObjectField(TEXT("transform"), SkeletonRead::Transform(Socket->GetSocketLocalTransform()));
        Sockets.Add(JV(Item));
    }
    R->SetArrayField(TEXT("sockets"), Sockets);
    TArray<FName> PoseNames; S->AnimRetargetSources.GetKeys(PoseNames);
    PoseNames.Sort([](const FName& A, const FName& B) { return A.ToString() < B.ToString(); });
    TArray<TSharedPtr<FJsonValue>> Poses;
    for (const FName& PoseName : PoseNames)
    {
        const FReferencePose& Pose = S->AnimRetargetSources.FindChecked(PoseName);
        FObj Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("name"), PoseName.ToString());
        Item->SetStringField(TEXT("source_mesh"), Pose.SourceReferenceMesh.ToSoftObjectPath().ToString());
        TArray<TSharedPtr<FJsonValue>> Transforms;
        for (const FTransform& Transform : Pose.ReferencePose) Transforms.Add(JV(SkeletonRead::Transform(Transform)));
        Item->SetArrayField(TEXT("local_transforms"), Transforms); Poses.Add(JV(Item));
    }
    R->SetArrayField(TEXT("retarget_poses"), Poses);
    FString Text; FJsonSerializer::Serialize(R.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
    FTCHARToUTF8 Bytes(*Text);
    R->SetStringField(TEXT("revision"), FMD5::HashBytes(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length()));
    return R;
}
inline FObj Run(const FString& Action, const FObj& Request)
{
    const FString Path = BlueprintWrite::Str(Request, TEXT("asset_path"));
    if (!Path.StartsWith(TEXT("/Game/"))) return BlueprintWrite::Error(TEXT("Skeleton must be under /Game/."));
    USkeleton* S = LoadObject<USkeleton>(nullptr, *Path);
    if (!S) return BlueprintWrite::Error(TEXT("Skeleton not found."));
    FObj R = Snapshot(S);
    if (Action == TEXT("inspect_skeleton_edit")) return R;
    if (GEditor && GEditor->PlayWorld) return BlueprintWrite::Error(TEXT("Stop PIE before editing Skeletons."));
    double Expires = 0;
    if (!Request->TryGetNumberField(TEXT("expires_unix"), Expires) || Expires < FDateTime::UtcNow().ToUnixTimestamp()) return BlueprintWrite::Error(TEXT("Skeleton request expired."));
    if (BlueprintWrite::Str(Request, TEXT("expected_revision")) != R->GetStringField(TEXT("revision"))) return BlueprintWrite::Error(TEXT("Skeleton revision mismatch. Use inspect_skeleton_edit first."));
    if (Action == TEXT("save_skeleton"))
    {
        const FString File = FPackageName::LongPackageNameToFilename(S->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        FString Backup;
        if (IFileManager::Get().FileExists(*File))
        {
            Backup = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UEBlueprintBridge/backups") / (FGuid::NewGuid().ToString() + TEXT("_") + FPaths::GetCleanFilename(File)));
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
            if (IFileManager::Get().Copy(*Backup, *File, false, false) != COPY_OK) return BlueprintWrite::Error(TEXT("Backup failed."));
        }
        const bool Saved = UPackage::SavePackage(S->GetOutermost(), S, RF_Public | RF_Standalone, *File, GError, nullptr, false, true, SAVE_NoError);
        R->SetBoolField(TEXT("ok"), Saved); R->SetBoolField(TEXT("saved"), Saved); R->SetStringField(TEXT("backup"), Backup); return R;
    }
    const FString Op = BlueprintWrite::Str(Request, TEXT("operation"));
    const FString Name = BlueprintWrite::Str(Request, TEXT("name"));
    const FString Bone = BlueprintWrite::Str(Request, TEXT("bone_name"));
    const FString Target = BlueprintWrite::Str(Request, TEXT("target_bone"));
    const FString NewName = BlueprintWrite::Str(Request, TEXT("new_name"));
    USkeletalMeshSocket* ExistingSocket = nullptr;
    for (USkeletalMeshSocket* Socket : S->Sockets) if (Socket && Socket->SocketName == FName(*Name)) ExistingSocket = Socket;
    if (Op == TEXT("remove_virtual_bone") || Op == TEXT("rename_virtual_bone") || Op == TEXT("remove_retarget_pose"))
    {
        bool Confirm = false;
        if (!Request->TryGetBoolField(TEXT("acknowledge_reference_changes"), Confirm) || !Confirm) return BlueprintWrite::Error(TEXT("External references are not repaired. acknowledge_reference_changes must be true."));
        if (Op == TEXT("remove_retarget_pose"))
        {
            if (!S->AnimRetargetSources.Contains(*Name)) return BlueprintWrite::Error(TEXT("Retarget pose not found."));
        }
        else
        {
            bool Found = false;
            for (const FVirtualBone& VB : S->GetVirtualBones())
            {
                if (VB.VirtualBoneName == FName(*Name)) Found = true;
                if ((VB.SourceBoneName == FName(*Name) || VB.TargetBoneName == FName(*Name)) && VB.VirtualBoneName != FName(*Name)) return BlueprintWrite::Error(TEXT("Another virtual bone depends on this bone; edit dependents first."));
            }
            if (!Found) return BlueprintWrite::Error(TEXT("Virtual bone not found; use its exact VB-prefixed name."));
            for (USkeletalMeshSocket* Socket : S->Sockets) if (Socket && Socket->BoneName == FName(*Name)) return BlueprintWrite::Error(TEXT("A Skeleton socket depends on this virtual bone."));
            if (Op == TEXT("rename_virtual_bone") && (!NewName.StartsWith(TEXT("VB ")) || !BlueprintAnimWrite::ValidName(NewName.Mid(3)) || S->GetReferenceSkeleton().FindBoneIndex(*NewName) != INDEX_NONE)) return BlueprintWrite::Error(TEXT("Use an unused name prefixed with 'VB '."));
        }
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "VirtualBoneEdit", "MCP edit virtual bone or retarget pose"));
        S->Modify();
        if (Op == TEXT("remove_retarget_pose")) { S->AnimRetargetSources.Remove(*Name); S->CallbackRetargetSourceChanged(); }
        else if (Op == TEXT("rename_virtual_bone")) S->RenameVirtualBone(*Name, *NewName);
        else { TArray<FName> Names; Names.Add(*Name); S->RemoveVirtualBones(Names); }
        S->MarkPackageDirty(); R = Snapshot(S); R->SetBoolField(TEXT("ok"), true); R->SetBoolField(TEXT("saved"), false); return R;
    }
    if (Op == TEXT("remove_slot") || Op == TEXT("rename_slot") || Op == TEXT("remove_socket") || Op == TEXT("rename_socket"))
    {
        bool Confirm = false;
        if (!Request->TryGetBoolField(TEXT("acknowledge_reference_changes"), Confirm) || !Confirm) return BlueprintWrite::Error(TEXT("These operations do not repair external references; acknowledge_reference_changes must be true."));
        if ((Op.EndsWith(TEXT("slot")) && !S->ContainsSlotName(*Name)) || (Op.EndsWith(TEXT("socket")) && !ExistingSocket)) return BlueprintWrite::Error(TEXT("Named slot/socket not found."));
        if (Op.StartsWith(TEXT("rename")))
        {
            if (!BlueprintAnimWrite::ValidName(NewName)) return BlueprintWrite::Error(TEXT("Invalid new_name."));
            if (Op.EndsWith(TEXT("slot")) && S->ContainsSlotName(*NewName)) return BlueprintWrite::Error(TEXT("Slot already exists."));
            for (USkeletalMeshSocket* Socket : S->Sockets) if (Socket && Socket->SocketName == FName(*NewName)) return BlueprintWrite::Error(TEXT("Socket name already exists."));
        }
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "SkeletonRemoveRename", "MCP rename/remove Skeleton item"));
        S->Modify();
        if (Op == TEXT("remove_slot")) S->RemoveSlotName(*Name);
        else if (Op == TEXT("rename_slot")) S->RenameSlotName(*Name, *NewName);
        else { ExistingSocket->Modify(); if (Op == TEXT("remove_socket")) S->Sockets.Remove(ExistingSocket); else ExistingSocket->SocketName = *NewName; }
        S->MarkPackageDirty(); R = Snapshot(S); R->SetBoolField(TEXT("ok"), true); R->SetBoolField(TEXT("saved"), false); return R;
    }
    if (Op == TEXT("set_socket_transform") || Op == TEXT("set_retarget_bone") || Op == TEXT("add_retarget_pose"))
    {
        if (Op == TEXT("set_socket_transform") && !ExistingSocket) return BlueprintWrite::Error(TEXT("Socket not found."));
        const int32 BoneIndex = S->GetReferenceSkeleton().FindBoneIndex(*Bone);
        FReferencePose* Pose = S->AnimRetargetSources.Find(*Name);
        if (Op == TEXT("add_retarget_pose") && (!BlueprintAnimWrite::ValidName(Name) || Pose)) return BlueprintWrite::Error(TEXT("Invalid or duplicate pose name."));
        if (Op == TEXT("set_retarget_bone") && (!Pose || BoneIndex == INDEX_NONE || !Pose->ReferencePose.IsValidIndex(BoneIndex))) return BlueprintWrite::Error(TEXT("Retarget pose or bone not found."));
        double TX = 0, TY = 0, TZ = 0, Pitch = 0, Yaw = 0, Roll = 0, SX = 1, SY = 1, SZ = 1;
        const TCHAR* Keys[] = { TEXT("tx"), TEXT("ty"), TEXT("tz"), TEXT("pitch"), TEXT("yaw"), TEXT("roll"), TEXT("sx"), TEXT("sy"), TEXT("sz") };
        double* Values[] = { &TX, &TY, &TZ, &Pitch, &Yaw, &Roll, &SX, &SY, &SZ };
        if (Op != TEXT("add_retarget_pose")) for (int32 I = 0; I < 9; ++I)
            if (!Request->TryGetNumberField(Keys[I], *Values[I]) || !FMath::IsFinite(*Values[I]) || FMath::Abs(*Values[I]) > 1000000) return BlueprintWrite::Error(TEXT("All nine transform components are required, finite, and bounded by +/-1000000."));
        if (SX <= 0 || SY <= 0 || SZ <= 0) return BlueprintWrite::Error(TEXT("Scale components must be positive."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "SkeletonTransform", "MCP edit Skeleton transform"));
        S->Modify();
        if (Op == TEXT("set_socket_transform"))
        {
            ExistingSocket->Modify(); ExistingSocket->RelativeLocation = FVector(TX, TY, TZ);
            ExistingSocket->RelativeRotation = FRotator(Pitch, Yaw, Roll); ExistingSocket->RelativeScale = FVector(SX, SY, SZ);
        }
        else
        {
            if (Op == TEXT("add_retarget_pose")) { FReferencePose NewPose; NewPose.PoseName = *Name; NewPose.ReferencePose = S->GetReferenceSkeleton().GetRefBonePose(); S->AnimRetargetSources.Add(*Name, NewPose); }
            else Pose->ReferencePose[BoneIndex] = FTransform(FRotator(Pitch, Yaw, Roll), FVector(TX, TY, TZ), FVector(SX, SY, SZ));
            S->CallbackRetargetSourceChanged();
        }
        S->MarkPackageDirty(); R = Snapshot(S); R->SetBoolField(TEXT("ok"), true); R->SetBoolField(TEXT("saved"), false); return R;
    }
    if (Op == TEXT("add_slot"))
    {
        if (!BlueprintAnimWrite::ValidName(Name) || S->ContainsSlotName(*Name)) return BlueprintWrite::Error(TEXT("Slot name invalid or already exists."));
    }
    else if (Op == TEXT("add_socket"))
    {
        if (!BlueprintAnimWrite::ValidName(Name) || S->GetReferenceSkeleton().FindBoneIndex(*Bone) == INDEX_NONE) return BlueprintWrite::Error(TEXT("Invalid socket name or bone."));
        for (USkeletalMeshSocket* Socket : S->Sockets) if (Socket && Socket->SocketName == FName(*Name)) return BlueprintWrite::Error(TEXT("Socket already exists."));
    }
    else if (Op == TEXT("add_virtual_bone"))
    {
        if (Bone == Target || S->GetReferenceSkeleton().FindBoneIndex(*Bone) == INDEX_NONE || S->GetReferenceSkeleton().FindBoneIndex(*Target) == INDEX_NONE) return BlueprintWrite::Error(TEXT("Choose distinct existing source and target bones."));
        for (const FVirtualBone& VB : S->GetVirtualBones()) if (VB.SourceBoneName == FName(*Bone) && VB.TargetBoneName == FName(*Target)) return BlueprintWrite::Error(TEXT("Virtual bone already exists."));
    }
    else return BlueprintWrite::Error(TEXT("Unsupported skeleton operation."));
    const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "SkeletonEdit", "MCP edit Skeleton"));
    S->Modify();
    if (Op == TEXT("add_slot")) S->RegisterSlotNode(*Name);
    else if (Op == TEXT("add_socket"))
    {
        auto* Socket = NewObject<USkeletalMeshSocket>(S, NAME_None, RF_Transactional);
        Socket->SocketName = *Name; Socket->BoneName = *Bone; S->Sockets.Add(Socket);
    }
    else
    {
        FName Created;
        if (!S->AddNewVirtualBone(*Bone, *Target, Created)) return BlueprintWrite::Error(TEXT("Virtual bone creation failed."));
    }
    S->MarkPackageDirty();
    R = Snapshot(S); R->SetBoolField(TEXT("ok"), true); R->SetBoolField(TEXT("saved"), false);
    return R;
}
}
