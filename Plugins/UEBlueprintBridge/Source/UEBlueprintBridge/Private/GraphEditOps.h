#pragma once
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_EditablePinBase.h"

namespace GraphEdit
{
inline FObj Run(UBlueprint* BP, const FString& Op, const FObj& Request)
{
    const FString Name = BlueprintAnimWrite::Str(Request, TEXT("name"));
    const FString GraphPath = BlueprintAnimWrite::Str(Request, TEXT("graph_path"));
    auto Error = [](const FString& S) { return BlueprintAnimWrite::Error(S); };
    TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
    UEdGraph* Graph = nullptr;
    for (UEdGraph* G : Graphs) if (G && G->GetPathName() == GraphPath) Graph = G;
    FObj R = MakeShared<FJsonObject>();
    if (Op == TEXT("rename_graph"))
    {
        if (!Graph || (!BP->FunctionGraphs.Contains(Graph) && !BP->MacroGraphs.Contains(Graph))) return Error(TEXT("Select a user function or macro graph."));
        if (Graph->GetFName() == UEdGraphSchema_K2::FN_UserConstructionScript || (BP->ParentClass && BP->ParentClass->FindFunctionByName(Graph->GetFName())) || !BlueprintAnimWrite::ValidName(Name)) return Error(TEXT("Parent overrides/construction scripts cannot be renamed, or name is invalid."));
        FKismetNameValidator Validator(BP, Graph->GetFName());
        const EValidatorResult Valid = Validator.IsValid(Name);
        if (Valid != EValidatorResult::Ok && Valid != EValidatorResult::ExistingName) return Error(INameValidatorInterface::GetErrorString(Name, Valid));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "RenameGraph", "MCP rename graph"));
        BP->Modify(); Graph->Modify();
        FBlueprintEditorUtils::RenameGraph(Graph, Name);
        R->SetStringField(TEXT("child_graph_path"), Graph->GetPathName());
    }
    else if (Op == TEXT("add_function_graph") || Op == TEXT("add_macro_graph"))
    {
        if (BP->GetClass() != UBlueprint::StaticClass() || BP->BlueprintType != BPTYPE_Normal)
            return Error(TEXT("Function/macro creation currently requires a normal Blueprint."));
        if (!BlueprintAnimWrite::ValidName(Name)) return Error(TEXT("Invalid graph name."));
        if (FindObject<UObject>(BP, *Name) || (BP->SkeletonGeneratedClass && BP->SkeletonGeneratedClass->FindFunctionByName(*Name))) return Error(TEXT("Name already exists."));
        for (UEdGraph* G : Graphs) if (G && G->GetName().Equals(Name, ESearchCase::IgnoreCase)) return Error(TEXT("Graph name already exists."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "NewGraph", "MCP create graph"));
        BP->Modify();
        Graph = FBlueprintEditorUtils::CreateNewGraph(BP, *Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        if (Op == TEXT("add_function_graph")) FBlueprintEditorUtils::AddFunctionGraph<UClass>(BP, Graph, true, nullptr);
        else FBlueprintEditorUtils::AddMacroGraph(BP, Graph, true, nullptr);
        R->SetStringField(TEXT("child_graph_path"), Graph->GetPathName());
    }
    else
    {
        if (!Graph || !Graph->GetSchema()) return Error(TEXT("Graph not found."));
        UEdGraphNode* Node = BlueprintAnimWrite::FindNode(Graph, BlueprintAnimWrite::Str(Request, TEXT("node_id")));
        if (Op == TEXT("add_signature_pin"))
        {
            auto* Editable = Cast<UK2Node_EditablePinBase>(Node);
            if (!Editable || !BlueprintAnimWrite::ValidName(Name) || Node->FindPin(*Name)) return Error(TEXT("Select a signature node and an unused valid pin name."));
            const FString Kind = BlueprintAnimWrite::Str(Request, TEXT("variable_type"));
            FEdGraphPinType Type;
            if (Kind == TEXT("bool")) Type.PinCategory = UEdGraphSchema_K2::PC_Boolean;
            else if (Kind == TEXT("int")) Type.PinCategory = UEdGraphSchema_K2::PC_Int;
            else if (Kind == TEXT("float")) Type.PinCategory = UEdGraphSchema_K2::PC_Float;
            else if (Kind == TEXT("string")) Type.PinCategory = UEdGraphSchema_K2::PC_String;
            else return Error(TEXT("Signature pin supports bool/int/float/string."));
            const FString Direction = BlueprintAnimWrite::Str(Request, TEXT("direction"));
            if (Direction != TEXT("input") && Direction != TEXT("output")) return Error(TEXT("direction must be input or output from the signature node perspective."));
            const EEdGraphPinDirection Dir = Direction == TEXT("input") ? EGPD_Input : EGPD_Output;
            FText Reason;
            if (!Editable->CanCreateUserDefinedPin(Type, Dir, Reason)) return Error(Reason.ToString());
            const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "SignaturePin", "MCP add signature pin"));
            BP->Modify(); Graph->Modify(); Node->Modify();
            UEdGraphPin* Created = Editable->CreateUserDefinedPin(*Name, Type, Dir, false);
            if (!Created) return Error(TEXT("Signature pin creation failed."));
            R->SetStringField(TEXT("pin_id"), Created->PinId.ToString());
        }
        else if (Op == TEXT("add_event"))
        {
            if (!BP->UbergraphPages.Contains(Graph) || Graph->GetSchema()->GetClass() != UEdGraphSchema_K2::StaticClass()) return Error(TEXT("Events require a K2 EventGraph."));
            UFunction* Function = BP->ParentClass ? BP->ParentClass->FindFunctionByName(*Name) : nullptr;
            if (!Function || !UEdGraphSchema_K2::FunctionCanBePlacedAsEvent(Function)) return Error(TEXT("Parent function is not an overridable event."));
            for (UEdGraph* G : Graphs) if (G) for (UEdGraphNode* N : G->Nodes)
                if (auto* Event = Cast<UK2Node_Event>(N)) if (Event->EventReference.GetMemberName() == FName(*Name)) return Error(TEXT("Event already exists."));
            const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "OverrideEvent", "MCP add event override"));
            BP->Modify(); Graph->Modify();
            auto* Event = NewObject<UK2Node_Event>(Graph, NAME_None, RF_Transactional);
            Event->EventReference.SetExternalMember(Function->GetFName(), Function->GetOuterUClass());
            Event->bOverrideFunction = true;
            BlueprintAnimWrite::FinishNode(Graph, Event, Request);
            R->SetStringField(TEXT("node_id"), Event->NodeGuid.ToString());
        }
        else if (Op == TEXT("add_custom_event"))
        {
            if (!BP->UbergraphPages.Contains(Graph) || Graph->GetSchema()->GetClass() != UEdGraphSchema_K2::StaticClass()) return Error(TEXT("Custom events require a K2 EventGraph."));
            if (!BlueprintAnimWrite::ValidName(Name)) return Error(TEXT("Invalid event name."));
            if (BP->SkeletonGeneratedClass && BP->SkeletonGeneratedClass->FindFunctionByName(*Name)) return Error(TEXT("Function/event already exists."));
            for (UEdGraph* G : Graphs) if (G) for (UEdGraphNode* N : G->Nodes)
                if (auto* E = Cast<UK2Node_CustomEvent>(N)) if (E->CustomFunctionName == FName(*Name)) return Error(TEXT("Event already exists."));
            const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "NewEvent", "MCP create custom event"));
            BP->Modify(); Graph->Modify();
            auto* Event = NewObject<UK2Node_CustomEvent>(Graph, NAME_None, RF_Transactional);
            Event->CustomFunctionName = *Name;
            BlueprintAnimWrite::FinishNode(Graph, Event, Request);
            R->SetStringField(TEXT("node_id"), Event->NodeGuid.ToString());
        }
        else
        {
            if (!Node) return Error(TEXT("Node not found."));
            if (Op == TEXT("delete_node") && !Node->CanUserDeleteNode()) return Error(TEXT("This node is protected by the editor."));
            if (Op == TEXT("rename_node"))
            {
                if (!BlueprintAnimWrite::ValidName(Name)) return Error(TEXT("Invalid event name."));
                if (!Cast<UAnimGraphNode_SaveCachedPose>(Node) && !Cast<UAnimGraphNode_StateMachine>(Node) && !Cast<UAnimStateNode>(Node)) return Error(TEXT("This node has a derived title or externally referenced symbol. Use rename_graph for functions, rename_state for states, or set_node_comment for display annotations."));
                auto Validator = Node->MakeNameValidator();
                if (!Validator.IsValid()) return Error(TEXT("Node has no editor rename validator."));
                const EValidatorResult Result = Validator->IsValid(Name);
                if (Result != EValidatorResult::Ok && Result != EValidatorResult::ExistingName) return Error(INameValidatorInterface::GetErrorString(Name, Result));
            }
            if (Op == TEXT("delete_node") && !Cast<UK2Node>(Node)) return Error(TEXT("Animation/state deletion requires graph ownership cleanup and is not supported by this operation yet."));
            if (Op == TEXT("delete_node"))
                for (TFieldIterator<UObjectProperty> It(Node->GetClass()); It; ++It)
                    if (Cast<UEdGraph>(It->GetObjectPropertyValue_InContainer(Node))) return Error(TEXT("Graph-owning nodes cannot be deleted by this operation."));
            double X = 0, Y = 0;
            if (Op == TEXT("move_node") && (!Request->TryGetNumberField(TEXT("x"), X) || !Request->TryGetNumberField(TEXT("y"), Y) || !FMath::IsFinite(X) || !FMath::IsFinite(Y) || FMath::Abs(X) > 1000000 || FMath::Abs(Y) > 1000000)) return Error(TEXT("x and y must be finite coordinates within +/-1000000."));
            const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "EditNode", "MCP edit node"));
            BP->Modify(); Graph->Modify(); Node->Modify();
            if (Op == TEXT("delete_node")) FBlueprintEditorUtils::RemoveNode(BP, Node, true);
            else if (Op == TEXT("move_node")) { Node->NodePosX = X; Node->NodePosY = Y; }
            else if (Op == TEXT("set_node_comment")) Node->NodeComment = BlueprintAnimWrite::Str(Request, TEXT("comment"));
            else if (Op == TEXT("rename_node")) Node->OnRenameNode(Name);
            else return Error(TEXT("Unknown graph operation."));
        }
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    Graph->NotifyGraphChanged();
    R->SetBoolField(TEXT("ok"), true); R->SetBoolField(TEXT("saved"), false);
    return R;
}
}
