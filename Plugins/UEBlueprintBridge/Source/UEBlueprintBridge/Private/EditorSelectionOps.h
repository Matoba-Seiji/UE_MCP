#pragma once
#include "GraphEditor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "BlueprintEditorModule.h"

namespace EditorSelectionOps
{
inline FObj SelectBlueprintNode(const FObj& Request)
{
    const FString AssetPath = BlueprintWrite::Str(Request, TEXT("asset_path"));
    const FString GraphPath = BlueprintWrite::Str(Request, TEXT("graph_path"));
    const FString NodeId = BlueprintWrite::Str(Request, TEXT("node_id"));
    if (!AssetPath.StartsWith(TEXT("/Game/")) || !GraphPath.StartsWith(AssetPath + TEXT(":")) || NodeId.IsEmpty())
        return BlueprintWrite::Error(TEXT("asset_path, exact child graph_path and node_id are required."));
    UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *AssetPath);
    if (!BP) return BlueprintWrite::Error(TEXT("Blueprint not found."));
    TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
    UEdGraph* Graph = nullptr;
    for (UEdGraph* Candidate : Graphs) if (Candidate && Candidate->GetPathName() == GraphPath) { Graph = Candidate; break; }
    if (!Graph) return BlueprintWrite::Error(TEXT("Graph not found; use an exact graph_path from inspection."));
    UEdGraphNode* Node = nullptr;
    for (UEdGraphNode* Candidate : Graph->Nodes) if (Candidate && Candidate->NodeGuid.ToString() == NodeId) { Node = Candidate; break; }
    if (!Node) return BlueprintWrite::Error(TEXT("Node not found in graph."));
    TSharedPtr<IBlueprintEditor> Editor = FKismetEditorUtilities::GetIBlueprintEditorForObject(BP, true);
    if (!Editor.IsValid()) return BlueprintWrite::Error(TEXT("Could not open a Blueprint editor for this asset."));
    TSharedPtr<SGraphEditor> GraphEditor = Editor->OpenGraphAndBringToFront(Graph, true);
    if (!GraphEditor.IsValid()) return BlueprintWrite::Error(TEXT("Could not open the requested graph tab."));
    GraphEditor->ClearSelectionSet();
    GraphEditor->SetNodeSelection(Node, true);
    GraphEditor->JumpToNode(Node, false, true);
    FObj R = MakeShared<FJsonObject>();
    R->SetBoolField(TEXT("ok"), true); R->SetBoolField(TEXT("selected"), true);
    R->SetStringField(TEXT("asset"), BP->GetPathName()); R->SetStringField(TEXT("graph_path"), Graph->GetPathName());
    R->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString()); R->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
    R->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
    R->SetNumberField(TEXT("x"), Node->NodePosX); R->SetNumberField(TEXT("y"), Node->NodePosY);
    R->SetNumberField(TEXT("selected_node_count"), GraphEditor->GetSelectedNodes().Num());
    R->SetStringField(TEXT("action"), TEXT("selected_and_focused"));
    return R;
}
}
