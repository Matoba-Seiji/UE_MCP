#pragma once
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AnimationGraphSchema.h"
#include "AnimationStateMachineSchema.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimationTransitionSchema.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimStateNode.h"
#include "AnimStateEntryNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_LocalRefPose.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_Slot.h"
#include "Kismet/KismetMathLibrary.h"
#include "Animation/BlendSpaceBase.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_TwoBoneIK.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"

namespace BlueprintAnimWrite
{
inline FString Str(const FObj& R, const TCHAR* Key) { FString S; R->TryGetStringField(Key, S); return S; }
inline FObj Error(const FString& S) { FObj R = MakeShared<FJsonObject>(); R->SetStringField(TEXT("error"), S); return R; }
inline UEdGraphNode* FindNode(UEdGraph* G, const FString& Id)
{
    for (UEdGraphNode* N : G->Nodes) if (N && N->NodeGuid.ToString() == Id) return N;
    return nullptr;
}
inline bool ValidName(const FString& S)
{
    if (S.IsEmpty() || !(FChar::IsAlpha(S[0]) || S[0] == '_')) return false;
    for (TCHAR C : S) if (!FChar::IsAlnum(C) && C != '_') return false;
    return true;
}
inline void FinishNode(UEdGraph* G, UEdGraphNode* N, const FObj& Request)
{
    G->AddNode(N, false, false);
    N->CreateNewGuid(); N->PostPlacedNewNode(); N->AllocateDefaultPins();
    double X = 0, Y = 0; Request->TryGetNumberField(TEXT("x"), X); Request->TryGetNumberField(TEXT("y"), Y);
    N->NodePosX = FMath::Clamp(X, -1000000.0, 1000000.0);
    N->NodePosY = FMath::Clamp(Y, -1000000.0, 1000000.0);
}
inline FObj Run(UBlueprint* BP, const FString& Op, const FObj& Request)
{
    UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(BP);
    if (!AnimBP || !AnimBP->TargetSkeleton) return Error(TEXT("An Animation Blueprint with a target skeleton is required."));
    if (AnimBP->ParentClass && AnimBP->ParentClass->ClassGeneratedBy)
        return Error(TEXT("Editing inherited child AnimBlueprint graphs is not supported."));
    TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
    UEdGraph* Graph = nullptr;
    for (UEdGraph* G : Graphs) if (G && G->GetPathName() == Str(Request, TEXT("graph_path"))) Graph = G;
    if (!Graph || !Graph->GetSchema()) return Error(TEXT("Graph not found; inspect the asset for exact paths."));
    const bool PoseGraph = Graph->GetSchema()->IsA(UAnimationGraphSchema::StaticClass());
    UAnimationStateMachineGraph* Machine = Cast<UAnimationStateMachineGraph>(Graph);
    FObj R = MakeShared<FJsonObject>();
    UEdGraphNode* Created = nullptr;
    if (Op == TEXT("rename_state_machine") || Op == TEXT("delete_state_machine"))
    {
        auto* N = Cast<UAnimGraphNode_StateMachine>(FindNode(Graph, Str(Request, TEXT("node_id"))));
        if (!N || !N->EditorStateMachineGraph) return Error(TEXT("Select a state machine node in its parent pose graph."));
        const FString Name = Str(Request, TEXT("name"));
        if (Op == TEXT("rename_state_machine"))
        {
            if (!ValidName(Name)) return Error(TEXT("Invalid state machine name."));
            auto Validator = N->MakeNameValidator();
            if (!Validator.IsValid()) return Error(TEXT("Name validator unavailable."));
            const EValidatorResult Result = Validator->IsValid(Name);
            if (Result != EValidatorResult::Ok && Result != EValidatorResult::ExistingName) return Error(INameValidatorInterface::GetErrorString(Name, Result));
        }
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "EditMachine", "MCP edit state machine"));
        BP->Modify(); Graph->Modify(); N->Modify(); N->EditorStateMachineGraph->Modify();
        if (Op == TEXT("rename_state_machine")) { N->OnRenameNode(Name); R->SetStringField(TEXT("child_graph_path"), N->EditorStateMachineGraph->GetPathName()); }
        else FBlueprintEditorUtils::RemoveNode(BP, N, true);
    }
    else if (Op == TEXT("delete_state") || Op == TEXT("delete_transition") || Op == TEXT("rename_state") || Op == TEXT("set_transition_duration"))
    {
        if (!Machine) return Error(TEXT("Select a state machine graph."));
        UEdGraphNode* N = FindNode(Graph, Str(Request, TEXT("node_id")));
        UAnimStateNode* State = Cast<UAnimStateNode>(N);
        UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(N);
        const bool StateOp = Op == TEXT("delete_state") || Op == TEXT("rename_state");
        if ((StateOp && !State) || (!StateOp && !Transition)) return Error(TEXT("Wrong node type."));
        const FString Name = Str(Request, TEXT("name"));
        double Duration = 0;
        if (Op == TEXT("rename_state"))
        {
            if (!ValidName(Name)) return Error(TEXT("Invalid state name."));
            for (UEdGraph* G : Graph->SubGraphs) if (G && G != State->BoundGraph && G->GetName().Equals(Name, ESearchCase::IgnoreCase)) return Error(TEXT("State name already exists."));
        }
        if (Op == TEXT("set_transition_duration") && (!Request->TryGetNumberField(TEXT("blend_duration"), Duration) || !FMath::IsFinite(Duration) || Duration < 0 || Duration > 60)) return Error(TEXT("blend_duration must be 0..60."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "EditState", "MCP edit animation state"));
        BP->Modify(); Graph->Modify(); N->Modify();
        if (Op == TEXT("rename_state")) { State->BoundGraph->Modify(); FBlueprintEditorUtils::RenameGraph(State->BoundGraph, Name); R->SetStringField(TEXT("child_graph_path"), State->BoundGraph->GetPathName()); }
        else if (Op == TEXT("set_transition_duration")) Transition->CrossfadeDuration = Duration;
        else
        {
            if (State)
            {
                TArray<UAnimStateTransitionNode*> Connected;
                for (UEdGraphNode* Candidate : Graph->Nodes) if (auto* T = Cast<UAnimStateTransitionNode>(Candidate))
                    if (T->GetPreviousState() == State || T->GetNextState() == State) Connected.Add(T);
                for (UAnimStateTransitionNode* T : Connected) { T->Modify(); FBlueprintEditorUtils::RemoveNode(BP, T, true); }
            }
            FBlueprintEditorUtils::RemoveNode(BP, N, true);
        }
    }
    else if (Op == TEXT("add_state_machine") || Op == TEXT("add_state"))
    {
        if ((Op == TEXT("add_state_machine") && !PoseGraph) || (Op == TEXT("add_state") && !Machine))
            return Error(TEXT("State machines require a pose graph; states require a state-machine graph."));
        const FString Name = Str(Request, TEXT("name"));
        if (!ValidName(Name)) return Error(TEXT("Use a nonempty name containing letters, digits and underscores."));
        for (UEdGraph* Sub : Graph->SubGraphs)
            if (Sub && Sub->GetName().Equals(Name, ESearchCase::IgnoreCase)) return Error(TEXT("A child graph with that name already exists."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "AnimGraph", "MCP add animation graph"));
        BP->Modify(); Graph->Modify();
        UEdGraph* Child = nullptr;
        if (Op == TEXT("add_state_machine"))
        {
            auto* N = NewObject<UAnimGraphNode_StateMachine>(Graph, NAME_None, RF_Transactional);
            FinishNode(Graph, N, Request);
            Child = N->EditorStateMachineGraph; Created = N;
        }
        else
        {
            auto* N = NewObject<UAnimStateNode>(Graph, NAME_None, RF_Transactional);
            FinishNode(Graph, N, Request);
            Child = N->BoundGraph; Created = N;
        }
        Child->Modify();
        FBlueprintEditorUtils::RenameGraph(Child, Name);
        R->SetStringField(TEXT("child_graph_path"), Child->GetPathName());
    }
    else if (Op == TEXT("set_entry_state"))
    {
        if (!Machine || !Machine->EntryNode || Machine->EntryNode->Pins.Num() == 0) return Error(TEXT("State-machine entry node not found."));
        UAnimStateNode* State = Cast<UAnimStateNode>(FindNode(Graph, Str(Request, TEXT("node_id"))));
        if (!State) return Error(TEXT("node_id must identify a state in this state machine."));
        UEdGraphPin* EntryPin = Machine->EntryNode->Pins[0];
        if (EntryPin->LinkedTo.Num()) return Error(TEXT("Entry already connected. Disconnect it explicitly before changing the entry state."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "Entry", "MCP connect animation entry state"));
        BP->Modify(); Graph->Modify(); State->Modify(); Machine->EntryNode->Modify();
        if (!Graph->GetSchema()->TryCreateConnection(EntryPin, State->GetInputPin())) return Error(TEXT("Entry connection rejected by schema."));
    }
    else if (Op == TEXT("add_transition"))
    {
        if (!Machine) return Error(TEXT("Transitions require a state-machine graph."));
        UAnimStateNode* From = Cast<UAnimStateNode>(FindNode(Graph, Str(Request, TEXT("node_id"))));
        UAnimStateNode* To = Cast<UAnimStateNode>(FindNode(Graph, Str(Request, TEXT("target_node_id"))));
        if (!From || !To || From == To) return Error(TEXT("Choose two distinct states in this state machine."));
        for (UEdGraphNode* N : Graph->Nodes)
            if (auto* T = Cast<UAnimStateTransitionNode>(N))
                if (T->GetPreviousState() == From && T->GetNextState() == To) return Error(TEXT("That transition already exists."));
        double Duration = 0.2; Request->TryGetNumberField(TEXT("blend_duration"), Duration);
        if (!FMath::IsFinite(Duration) || Duration < 0 || Duration > 60) return Error(TEXT("blend_duration must be between 0 and 60 seconds."));
        const FString VariableName = Str(Request, TEXT("condition_variable"));
        bool Invert = false, Condition = false;
        Request->TryGetBoolField(TEXT("invert_condition"), Invert);
        Request->TryGetBoolField(TEXT("condition_value"), Condition);
        UBoolProperty* Variable = VariableName.IsEmpty() ? nullptr : FindField<UBoolProperty>(BP->SkeletonGeneratedClass, *VariableName);
        if (!VariableName.IsEmpty() && (!Variable || !Variable->HasAnyPropertyFlags(CPF_BlueprintVisible)))
            return Error(TEXT("condition_variable must be a Blueprint-visible bool; compile newly added variables first."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "Transition", "MCP add animation transition"));
        BP->Modify(); Graph->Modify(); From->Modify(); To->Modify();
        auto* T = NewObject<UAnimStateTransitionNode>(Graph, NAME_None, RF_Transactional);
        FinishNode(Graph, T, Request);
        // This is the same endpoint creation path used by the UE4 state-machine schema.
        T->CreateConnections(From, To);
        T->CrossfadeDuration = Duration;
        Created = T;
        UEdGraph* Rule = T->BoundGraph;
        auto* Result = CastChecked<UAnimationTransitionGraph>(Rule)->GetResultNode();
        UEdGraphPin* ResultPin = Result->FindPin(TEXT("bCanEnterTransition"));
        bool Wired = ResultPin != nullptr;
        if (Variable)
        {
            auto* Getter = NewObject<UK2Node_VariableGet>(Rule, NAME_None, RF_Transactional);
            Getter->VariableReference.SetSelfMember(Variable->GetFName());
            FinishNode(Rule, Getter, MakeShared<FJsonObject>());
            Getter->NodePosX = -400;
            UEdGraphPin* ValuePin = Getter->FindPin(Variable->GetFName());
            if (Invert)
            {
                auto* Not = NewObject<UK2Node_CallFunction>(Rule, NAME_None, RF_Transactional);
                Not->SetFromFunction(UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("Not_PreBool")));
                FinishNode(Rule, Not, MakeShared<FJsonObject>());
                Not->NodePosX = -200;
                Wired = ValuePin && Not->FindPin(TEXT("A")) && Rule->GetSchema()->TryCreateConnection(ValuePin, Not->FindPin(TEXT("A")));
                ValuePin = Not->FindPin(TEXT("ReturnValue"));
            }
            Wired = Wired && ValuePin && ResultPin && Rule->GetSchema()->TryCreateConnection(ValuePin, ResultPin);
        }
        else if (ResultPin) Rule->GetSchema()->TrySetDefaultValue(*ResultPin, Condition ? TEXT("true") : TEXT("false"));
        if (!Wired)
        {
            T->DestroyNode();
            return Error(TEXT("Could not initialize transition condition; transition removed."));
        }
        R->SetStringField(TEXT("child_graph_path"), Rule->GetPathName());
    }
    else if (Op == TEXT("add_pose_node"))
    {
        if (!PoseGraph) return Error(TEXT("Pose nodes require AnimGraph or a state's pose graph, not a transition rule."));
        const FString Kind = Str(Request, TEXT("pose_type"));
        UAnimSequence* Sequence = nullptr;
        UBlendSpaceBase* BlendSpace = nullptr;
        UAnimGraphNode_SaveCachedPose* Cache = nullptr;
        const FString CacheName = Str(Request, TEXT("name"));
        const FString Bone = Str(Request, TEXT("bone_name"));
        FString SlotName;
        double PlayRate = 1; bool Loop = true;
        if (Kind == TEXT("sequence_player"))
        {
            const FString Asset = Str(Request, TEXT("animation_asset"));
            if (!Asset.StartsWith(TEXT("/Game/"))) return Error(TEXT("animation_asset must reference a /Game/ AnimSequence."));
            Sequence = LoadObject<UAnimSequence>(nullptr, *Asset);
            if (!Sequence || !AnimBP->TargetSkeleton->IsCompatible(Sequence->GetSkeleton()))
                return Error(TEXT("Sequence missing, wrong asset type, or incompatible skeleton."));
            Request->TryGetNumberField(TEXT("play_rate"), PlayRate);
            Request->TryGetBoolField(TEXT("loop"), Loop);
            if (!FMath::IsFinite(PlayRate) || FMath::Abs(PlayRate) > 100) return Error(TEXT("play_rate must be finite and between -100 and 100."));
        }
        else if (Kind == TEXT("slot"))
        {
            SlotName = Str(Request, TEXT("slot_name"));
            if (SlotName.IsEmpty() || !AnimBP->TargetSkeleton->ContainsSlotName(*SlotName))
                return Error(TEXT("slot_name must already exist on the target skeleton; this tool does not modify skeleton assets."));
        }
        else if (Kind == TEXT("blendspace_player"))
        {
            const FString Asset = Str(Request, TEXT("animation_asset"));
            if (!Asset.StartsWith(TEXT("/Game/"))) return Error(TEXT("BlendSpace must be under /Game/."));
            BlendSpace = LoadObject<UBlendSpaceBase>(nullptr, *Asset);
            if (!BlendSpace || !AnimBP->TargetSkeleton->IsCompatible(BlendSpace->GetSkeleton())) return Error(TEXT("Missing or incompatible BlendSpace."));
        }
        else if (Kind == TEXT("save_cached_pose") || Kind == TEXT("use_cached_pose"))
        {
            if (!ValidName(CacheName)) return Error(TEXT("A valid cache name is required."));
            for (UEdGraph* G : Graphs) if (G) for (UEdGraphNode* N : G->Nodes)
                if (auto* Saved = Cast<UAnimGraphNode_SaveCachedPose>(N)) if (Saved->CacheName == CacheName) Cache = Saved;
            if (Kind == TEXT("save_cached_pose") && (Cache || Graph->GetOuter() != BP)) return Error(TEXT("Save Cached Pose requires a unique name and the top-level AnimGraph."));
            if (Kind == TEXT("use_cached_pose") && !Cache) return Error(TEXT("Named cache not found in this Blueprint."));
        }
        else if (Kind == TEXT("two_bone_ik"))
        {
            const FReferenceSkeleton& Ref = AnimBP->TargetSkeleton->GetReferenceSkeleton();
            const int32 Index = Ref.FindBoneIndex(*Bone);
            if (Index == INDEX_NONE || Ref.GetParentIndex(Index) == INDEX_NONE || Ref.GetParentIndex(Ref.GetParentIndex(Index)) == INDEX_NONE) return Error(TEXT("IK requires an end bone with two ancestors."));
        }
        else if (Kind != TEXT("reference_pose") && Kind != TEXT("blend_by_bool") && Kind != TEXT("layered_blend") && Kind != TEXT("local_to_component") && Kind != TEXT("component_to_local")) return Error(TEXT("Unknown pose_type."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "Pose", "MCP add animation pose node"));
        BP->Modify(); Graph->Modify();
        if (Sequence)
        {
            auto* N = NewObject<UAnimGraphNode_SequencePlayer>(Graph, NAME_None, RF_Transactional);
            N->Node.Sequence = Sequence; N->Node.PlayRate = PlayRate; N->Node.bLoopAnimation = Loop;
            Created = N;
        }
        else if (BlendSpace)
        {
            auto* N = NewObject<UAnimGraphNode_BlendSpacePlayer>(Graph, NAME_None, RF_Transactional);
            N->Node.BlendSpace = BlendSpace; Created = N;
        }
        else if (Kind == TEXT("save_cached_pose"))
        {
            auto* N = NewObject<UAnimGraphNode_SaveCachedPose>(Graph, NAME_None, RF_Transactional);
            N->CacheName = CacheName; Created = N;
        }
        else if (Kind == TEXT("use_cached_pose"))
        {
            auto* N = NewObject<UAnimGraphNode_UseCachedPose>(Graph, NAME_None, RF_Transactional);
            N->SaveCachedPoseNode = Cache; Created = N;
        }
        else if (Kind == TEXT("two_bone_ik"))
        {
            auto* N = NewObject<UAnimGraphNode_TwoBoneIK>(Graph, NAME_None, RF_Transactional);
            N->Node.IKBone.BoneName = *Bone; Created = N;
        }
        else if (Kind == TEXT("layered_blend")) Created = NewObject<UAnimGraphNode_LayeredBoneBlend>(Graph, NAME_None, RF_Transactional);
        else if (Kind == TEXT("local_to_component")) Created = NewObject<UAnimGraphNode_LocalToComponentSpace>(Graph, NAME_None, RF_Transactional);
        else if (Kind == TEXT("component_to_local")) Created = NewObject<UAnimGraphNode_ComponentToLocalSpace>(Graph, NAME_None, RF_Transactional);
        else if (Kind == TEXT("reference_pose")) Created = NewObject<UAnimGraphNode_LocalRefPose>(Graph, NAME_None, RF_Transactional);
        else if (Kind == TEXT("blend_by_bool")) Created = NewObject<UAnimGraphNode_BlendListByBool>(Graph, NAME_None, RF_Transactional);
        else
        {
            auto* N = NewObject<UAnimGraphNode_Slot>(Graph, NAME_None, RF_Transactional);
            N->Node.SlotName = *SlotName; Created = N;
        }
        FinishNode(Graph, Created, Request);
    }
    else return Error(TEXT("Unknown animation operation."));
    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
    Graph->NotifyGraphChanged();
    R->SetBoolField(TEXT("ok"), true); R->SetBoolField(TEXT("saved"), false);
    if (Created) R->SetStringField(TEXT("node_id"), Created->NodeGuid.ToString());
    return R;
}
}
