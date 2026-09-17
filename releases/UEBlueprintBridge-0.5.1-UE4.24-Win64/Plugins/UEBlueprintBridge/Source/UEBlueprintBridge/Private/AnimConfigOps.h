#pragma once

namespace AnimConfigOps
{
inline FObj Run(UBlueprint* BP, UEdGraph* Graph, UEdGraphNode* Node, const FObj& Request)
{
    auto Error = [](const FString& S) { return BlueprintAnimWrite::Error(S); };
    auto* AnimBP = Cast<UAnimBlueprint>(BP);
    if (!AnimBP || !AnimBP->TargetSkeleton || !Node) return Error(TEXT("Select an animation node with a target skeleton."));
    for (UEdGraphPin* Pin : Node->Pins) if (Pin && Pin->LinkedTo.Num()) return Error(TEXT("Disconnect the node before reconfiguration; pins may be reconstructed."));
    FObj Config;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BlueprintAnimWrite::Str(Request, TEXT("config_json"))), Config) || !Config.IsValid()) return Error(TEXT("config_json must encode a JSON object."));
    auto* Layered = Cast<UAnimGraphNode_LayeredBoneBlend>(Node);
    auto* IK = Cast<UAnimGraphNode_TwoBoneIK>(Node);
    const FString Op = BlueprintAnimWrite::Str(Request, TEXT("operation"));
    if (Op == TEXT("configure_layered_blend") && Layered)
    {
        for (const auto& Pair : Config->Values) if (Pair.Key != TEXT("layers")) return Error(TEXT("Unknown Layered Blend field."));
        const TArray<TSharedPtr<FJsonValue>>* Layers = nullptr;
        if (!Config->TryGetArrayField(TEXT("layers"), Layers) || Layers->Num() < 1 || Layers->Num() > 32) return Error(TEXT("layers must contain 1..32 layers."));
        TArray<FInputBlendPose> Setup; TArray<float> Weights;
        for (const auto& Item : *Layers)
        {
            if (Item->Type != EJson::Object) return Error(TEXT("Each layer must be an object."));
            FObj Layer = Item->AsObject();
            for (const auto& Pair : Layer->Values) if (Pair.Key != TEXT("weight") && Pair.Key != TEXT("filters")) return Error(TEXT("Unknown layer field."));
            double Weight = 0; const TArray<TSharedPtr<FJsonValue>>* Filters = nullptr;
            if (!Layer->TryGetNumberField(TEXT("weight"), Weight) || !FMath::IsFinite(Weight) || Weight < 0 || Weight > 1 || !Layer->TryGetArrayField(TEXT("filters"), Filters) || !Filters->Num() || Filters->Num() > 128) return Error(TEXT("Each layer requires weight 0..1 and 1..128 filters."));
            FInputBlendPose Pose;
            for (const auto& FilterItem : *Filters)
            {
                if (FilterItem->Type != EJson::Object) return Error(TEXT("Filter must be an object."));
                FObj Filter = FilterItem->AsObject(); FString Bone; double Depth = 0;
                for (const auto& Pair : Filter->Values) if (Pair.Key != TEXT("bone") && Pair.Key != TEXT("depth")) return Error(TEXT("Unknown filter field."));
                if (!Filter->TryGetStringField(TEXT("bone"), Bone) || AnimBP->TargetSkeleton->GetReferenceSkeleton().FindBoneIndex(*Bone) == INDEX_NONE || !Filter->TryGetNumberField(TEXT("depth"), Depth) || !FMath::IsFinite(Depth) || Depth != FMath::FloorToDouble(Depth) || FMath::Abs(Depth) > 1024) return Error(TEXT("Filter requires an existing bone and integer depth in -1024..1024."));
                FBranchFilter Branch; Branch.BoneName = *Bone; Branch.BlendDepth = static_cast<int32>(Depth); Pose.BranchFilters.Add(Branch);
            }
            Setup.Add(Pose); Weights.Add(Weight);
        }
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "LayerConfig", "MCP configure Layered Blend"));
        BP->Modify(); Graph->Modify(); Node->Modify();
        Layered->Node.BlendPoses.SetNum(Setup.Num()); Layered->Node.LayerSetup = Setup; Layered->Node.BlendWeights = Weights;
        Layered->ReconstructNode();
    }
    else if (Op == TEXT("configure_two_bone_ik") && IK)
    {
        for (const auto& Pair : Config->Values) if (Pair.Key != TEXT("effector") && Pair.Key != TEXT("joint_target") && Pair.Key != TEXT("space")) return Error(TEXT("Unknown IK config field."));
        FString Space;
        if (!Config->TryGetStringField(TEXT("space"), Space) || (Space != TEXT("component") && Space != TEXT("world"))) return Error(TEXT("space must be component or world."));
        FVector Positions[2]; const TCHAR* Names[] = { TEXT("effector"), TEXT("joint_target") };
        for (int32 I = 0; I < 2; ++I)
        {
            const TArray<TSharedPtr<FJsonValue>>* Coordinates = nullptr;
            if (!Config->TryGetArrayField(Names[I], Coordinates) || Coordinates->Num() != 3) return Error(TEXT("Targets must be three-number arrays in centimeters."));
            for (int32 J = 0; J < 3; ++J)
            {
                double V;
                if (!(*Coordinates)[J]->TryGetNumber(V) || !FMath::IsFinite(V) || FMath::Abs(V) > 1000000) return Error(TEXT("Invalid target coordinate."));
                Positions[I][J] = V;
            }
        }
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "IKConfig", "MCP configure Two Bone IK"));
        BP->Modify(); Graph->Modify(); Node->Modify();
        IK->Node.EffectorLocation = Positions[0]; IK->Node.JointTargetLocation = Positions[1];
        IK->Node.EffectorLocationSpace = Space == TEXT("world") ? BCS_WorldSpace : BCS_ComponentSpace;
        IK->Node.JointTargetLocationSpace = IK->Node.EffectorLocationSpace;
        IK->ReconstructNode();
        for (int32 I = 0; I < 2; ++I)
        {
            UEdGraphPin* Pin = IK->FindPin(I == 0 ? TEXT("EffectorLocation") : TEXT("JointTargetLocation"));
            if (Pin) Graph->GetSchema()->TrySetDefaultValue(*Pin, Positions[I].ToString());
        }
    }
    else return Error(TEXT("Operation does not match node type."));
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); Graph->NotifyGraphChanged();
    FObj R = MakeShared<FJsonObject>(); R->SetBoolField(TEXT("ok"), true); R->SetBoolField(TEXT("saved"), false); return R;
}
}
