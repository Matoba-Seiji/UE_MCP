#pragma once
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Logging/TokenizedMessage.h"
#include "ScopedTransaction.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "AnimBlueprintWriteOps.h"
#include "GraphEditOps.h"
#include "NodePropertyOps.h"
#include "AnimConfigOps.h"

namespace BlueprintWrite
{
inline FObj Error(const FString& Message)
{
    FObj R = MakeShared<FJsonObject>();
    R->SetStringField(TEXT("error"), Message);
    return R;
}
inline FString Str(const FObj& R, const TCHAR* Key)
{
    FString V; R->TryGetStringField(Key, V); return V;
}
inline FObj Compile(UBlueprint* BP)
{
    FCompilerResultsLog Log;
    FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipSave, &Log);
    FObj R = MakeShared<FJsonObject>();
    R->SetBoolField(TEXT("ok"), Log.NumErrors == 0 && BP->Status != BS_Error);
    R->SetNumberField(TEXT("errors"), Log.NumErrors);
    R->SetNumberField(TEXT("warnings"), Log.NumWarnings);
    TArray<TSharedPtr<FJsonValue>> Messages;
    TSet<FString> Seen;
    for (const auto& M : Log.Messages)
    {
        const FString Text = M->ToText().ToString();
        if (!Seen.Contains(Text)) { Seen.Add(Text); Messages.Add(MakeShared<FJsonValueString>(Text)); }
    }
    R->SetArrayField(TEXT("messages"), Messages);
    R->SetBoolField(TEXT("saved"), false);
    return R;
}
inline UEdGraphNode* Node(UEdGraph* G, const FString& Id)
{
    for (UEdGraphNode* N : G->Nodes)
        if (N && N->NodeGuid.ToString() == Id) return N;
    return nullptr;
}
inline UEdGraphPin* Pin(UEdGraphNode* N, const FString& Id)
{
    if (N) for (UEdGraphPin* P : N->Pins)
        if (P && P->PinId.ToString() == Id) return P;
    return nullptr;
}
inline FObj Run(UBlueprint* BP, const FString& Action, const FObj& Request)
{
    if (GEditor && GEditor->PlayWorld) return Error(TEXT("Stop PIE before editing Blueprint assets."));
    if (Action == TEXT("compile_blueprint")) return Compile(BP);
    if (Action == TEXT("save_blueprint"))
    {
        FObj R = Compile(BP);
        if (!R->GetBoolField(TEXT("ok"))) return R;
        UPackage* Package = BP->GetOutermost();
        const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
        FString Backup;
        if (IFileManager::Get().FileExists(*Filename))
        {
            Backup = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UEBlueprintBridge/backups") /
                (FGuid::NewGuid().ToString() + TEXT("_") + FPaths::GetCleanFilename(Filename)));
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
            if (IFileManager::Get().Copy(*Backup, *Filename, false, false) != COPY_OK)
                return Error(TEXT("Backup failed; asset was not saved."));
        }
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        const bool Saved = UPackage::SavePackage(Package, BP, RF_Public | RF_Standalone, *Filename, GError, nullptr, false, true, SAVE_NoError);
        R->SetBoolField(TEXT("saved"), Saved);
        R->SetBoolField(TEXT("ok"), Saved);
        R->SetStringField(TEXT("backup"), Backup);
        R->SetStringField(TEXT("file"), Filename);
        return R;
    }
    if (Action == TEXT("duplicate_blueprint"))
    {
        const FString Dest = Str(Request, TEXT("destination"));
        if (!Dest.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Dest) || Dest.Contains(TEXT(".")))
            return Error(TEXT("destination must be an unused /Game/Folder/Asset package path, without .Asset."));
        if (FPackageName::DoesPackageExist(Dest) || FindPackage(nullptr, *Dest))
            return Error(TEXT("Destination already exists."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "Duplicate", "MCP duplicate Blueprint"));
        auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
        UObject* Copy = Tools.DuplicateAsset(FPackageName::GetLongPackageAssetName(Dest), FPackageName::GetLongPackagePath(Dest), BP);
        if (!Copy) return Error(TEXT("Duplicate failed."));
        FObj R = MakeShared<FJsonObject>();
        R->SetBoolField(TEXT("ok"), true);
        R->SetBoolField(TEXT("saved"), false);
        R->SetStringField(TEXT("asset"), Copy->GetPathName());
        return R;
    }
    const FString Op = Str(Request, TEXT("operation"));
    if (Op == TEXT("rename_node")) return GraphEdit::Run(BP, Op, Request);
    if (Op == TEXT("set_variable_metadata"))
    {
        const FName Name(*Str(Request, TEXT("name")));
        if (FBlueprintEditorUtils::FindNewVariableIndex(BP, Name) == INDEX_NONE) return Error(TEXT("Select a locally declared member variable."));
        const FString Key = Str(Request, TEXT("metadata_key"));
        const FString Value = Str(Request, TEXT("value"));
        if (Key != TEXT("Category") && Key != TEXT("ToolTip") && Key != TEXT("InstanceEditable") && Key != TEXT("BlueprintReadOnly") && Key != TEXT("SaveGame") && Key != TEXT("Transient")) return Error(TEXT("Unsupported metadata key."));
        if (Key != TEXT("Category") && Key != TEXT("ToolTip") && Value != TEXT("true") && Value != TEXT("false")) return Error(TEXT("Boolean metadata requires true or false."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "Metadata", "MCP edit variable metadata"));
        BP->Modify(); const bool Enabled = Value == TEXT("true");
        if (Key == TEXT("Category")) FBlueprintEditorUtils::SetBlueprintVariableCategory(BP, Name, nullptr, FText::FromString(Value));
        else if (Key == TEXT("ToolTip")) FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, Name, nullptr, *Key, Value);
        else if (Key == TEXT("InstanceEditable")) FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(BP, Name, !Enabled);
        else if (Key == TEXT("BlueprintReadOnly")) FBlueprintEditorUtils::SetBlueprintPropertyReadOnlyFlag(BP, Name, Enabled);
        else if (Key == TEXT("SaveGame")) FBlueprintEditorUtils::SetVariableSaveGameFlag(BP, Name, Enabled);
        else FBlueprintEditorUtils::SetVariableTransientFlag(BP, Name, Enabled);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
        FObj Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("ok"), true); Result->SetBoolField(TEXT("saved"), false); return Result;
    }
    if (Op == TEXT("add_signature_pin") || Op == TEXT("add_event") || Op == TEXT("rename_graph") || Op == TEXT("delete_node") || Op == TEXT("move_node") || Op == TEXT("set_node_comment") || Op == TEXT("add_function_graph") || Op == TEXT("add_macro_graph") || Op == TEXT("add_custom_event"))
        return GraphEdit::Run(BP, Op, Request);
    if (Op == TEXT("rename_state_machine") || Op == TEXT("delete_state_machine") || Op == TEXT("add_state_machine") || Op == TEXT("add_state") || Op == TEXT("set_entry_state") || Op == TEXT("add_transition") || Op == TEXT("add_pose_node") || Op == TEXT("delete_state") || Op == TEXT("delete_transition") || Op == TEXT("rename_state") || Op == TEXT("set_transition_duration"))
        return BlueprintAnimWrite::Run(BP, Op, Request);
    if (Op == TEXT("add_variable"))
    {
        const FString Name = Str(Request, TEXT("name"));
        if (Name.IsEmpty() || !(FChar::IsAlpha(Name[0]) || Name[0] == '_')) return Error(TEXT("Use a variable identifier starting with a letter or underscore."));
        for (TCHAR C : Name) if (!FChar::IsAlnum(C) && C != '_') return Error(TEXT("Invalid variable identifier."));
        if (FindField<UProperty>(BP->SkeletonGeneratedClass, *Name) || FBlueprintEditorUtils::FindNewVariableIndex(BP, *Name) != INDEX_NONE)
            return Error(TEXT("Variable already exists."));
        FEdGraphPinType Type;
        const FString Kind = Str(Request, TEXT("variable_type"));
        if (Kind == TEXT("bool")) Type.PinCategory = UEdGraphSchema_K2::PC_Boolean;
        else if (Kind == TEXT("float")) Type.PinCategory = UEdGraphSchema_K2::PC_Float;
        else if (Kind == TEXT("int")) Type.PinCategory = UEdGraphSchema_K2::PC_Int;
        else if (Kind == TEXT("byte")) Type.PinCategory = UEdGraphSchema_K2::PC_Byte;
        else if (Kind == TEXT("int64")) Type.PinCategory = UEdGraphSchema_K2::PC_Int64;
        else if (Kind == TEXT("string")) Type.PinCategory = UEdGraphSchema_K2::PC_String;
        else if (Kind == TEXT("name")) Type.PinCategory = UEdGraphSchema_K2::PC_Name;
        else if (Kind == TEXT("text")) Type.PinCategory = UEdGraphSchema_K2::PC_Text;
        else if (Kind == TEXT("struct") || Kind == TEXT("object") || Kind == TEXT("class") || Kind == TEXT("softobject") || Kind == TEXT("softclass") || Kind == TEXT("enum"))
        {
            const FString TypePath = Str(Request, TEXT("type_path"));
            if (!TypePath.StartsWith(TEXT("/Script/")) && !TypePath.StartsWith(TEXT("/Game/"))) return Error(TEXT("type_path must be a /Script/ or /Game/ object path."));
            UObject* TypeObject = LoadObject<UObject>(nullptr, *TypePath);
            if ((Kind == TEXT("struct") && !Cast<UScriptStruct>(TypeObject)) || (Kind == TEXT("enum") && !Cast<UEnum>(TypeObject)) ||
                (Kind != TEXT("struct") && Kind != TEXT("enum") && !Cast<UClass>(TypeObject))) return Error(TEXT("type_path has the wrong reflected type."));
            Type.PinCategory = Kind == TEXT("enum") ? UEdGraphSchema_K2::PC_Byte : FName(*Kind);
            Type.PinSubCategoryObject = TypeObject;
        }
        else return Error(TEXT("Unsupported variable type."));
        const FString Container = Str(Request, TEXT("container"));
        if (Container == TEXT("array")) Type.ContainerType = EPinContainerType::Array;
        else if (Container == TEXT("set") || Container == TEXT("map"))
        {
            if (Kind != TEXT("bool") && Kind != TEXT("byte") && Kind != TEXT("int") && Kind != TEXT("int64") && Kind != TEXT("string") && Kind != TEXT("name") && Kind != TEXT("enum") && Kind != TEXT("object") && Kind != TEXT("class")) return Error(TEXT("Unsupported hashable key/element type."));
            Type.ContainerType = Container == TEXT("set") ? EPinContainerType::Set : EPinContainerType::Map;
            if (Container == TEXT("map"))
            {
                const FString ValueType = Str(Request, TEXT("value_type"));
                if (ValueType == TEXT("bool")) Type.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_Boolean;
                else if (ValueType == TEXT("int")) Type.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_Int;
                else if (ValueType == TEXT("float")) Type.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_Float;
                else if (ValueType == TEXT("string")) Type.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_String;
                else if (ValueType == TEXT("name")) Type.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_Name;
                else return Error(TEXT("Map value_type supports bool/int/float/string/name."));
            }
        }
        else if (!Container.IsEmpty() && Container != TEXT("none")) return Error(TEXT("Only scalar and array containers are currently supported."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "Variable", "MCP add Blueprint variable"));
        BP->Modify();
        if (!FBlueprintEditorUtils::AddMemberVariable(BP, *Name, Type)) return Error(TEXT("Could not add variable."));
        FObj R = MakeShared<FJsonObject>(); R->SetBoolField(TEXT("ok"), true); R->SetBoolField(TEXT("saved"), false); return R;
    }
    TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
    UEdGraph* Graph = nullptr;
    for (UEdGraph* G : Graphs) if (G && G->GetPathName() == Str(Request, TEXT("graph_path"))) Graph = G;
    if (!Graph || !Graph->GetSchema()) return Error(TEXT("Graph not found; use an exact graph_path from inspection."));
    const UEdGraphSchema* Schema = Graph->GetSchema();
    UEdGraphNode* N = Node(Graph, Str(Request, TEXT("node_id")));
    UEdGraphPin* P = Pin(N, Str(Request, TEXT("pin_id")));
    if (Op == TEXT("set_node_property")) return NodePropertyOps::Set(BP, Graph, N, Request);
    if (Op == TEXT("configure_layered_blend") || Op == TEXT("configure_two_bone_ik")) return AnimConfigOps::Run(BP, Graph, N, Request);
    FObj R = MakeShared<FJsonObject>();
    if (Op == TEXT("set_pin_object") || Op == TEXT("set_pin_text"))
    {
        if (!P || P->Direction != EGPD_Input || P->LinkedTo.Num() || P->PinType.ContainerType != EPinContainerType::None)
            return Error(TEXT("Choose an unconnected scalar input pin."));
        FString Value; if (!Request->TryGetStringField(TEXT("value"), Value)) return Error(TEXT("value must be a string."));
        UObject* Object = nullptr;
        if (Op == TEXT("set_pin_object"))
        {
            if (P->PinType.PinCategory != UEdGraphSchema_K2::PC_Object && P->PinType.PinCategory != UEdGraphSchema_K2::PC_Class) return Error(TEXT("Choose an object or class pin."));
            if (!Value.IsEmpty())
            {
                if (!Value.StartsWith(TEXT("/Game/")) && !Value.StartsWith(TEXT("/Script/"))) return Error(TEXT("Object path must start with /Game/ or /Script/. Empty value clears the reference."));
                Object = LoadObject<UObject>(nullptr, *Value);
                if (!Object) return Error(TEXT("Default object not found."));
            }
        }
        else if (P->PinType.PinCategory != UEdGraphSchema_K2::PC_Text) return Error(TEXT("Choose a text pin."));
        const FText Text = Op == TEXT("set_pin_text") ? FText::FromString(Value) : FText::GetEmpty();
        const FString Invalid = Schema->IsPinDefaultValid(P, FString(), Object, Text);
        if (!Invalid.IsEmpty()) return Error(Invalid);
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "TypedPin", "MCP set typed pin default"));
        BP->Modify(); Graph->Modify(); N->Modify();
        if (Op == TEXT("set_pin_object")) Schema->TrySetDefaultObject(*P, Object);
        else Schema->TrySetDefaultText(*P, Text);
        R->SetStringField(TEXT("actual_value"), Op == TEXT("set_pin_object") ? PathOf(P->DefaultObject) : P->DefaultTextValue.ToString());
    }
    else if (Op == TEXT("set_pin_default"))
    {
        if (!P || P->Direction != EGPD_Input || P->LinkedTo.Num() || P->PinType.PinCategory == TEXT("exec"))
            return Error(TEXT("Select an unconnected input data pin."));
        if (P->PinType.PinCategory == TEXT("object") || P->PinType.PinCategory == TEXT("class") || P->PinType.PinCategory == TEXT("text"))
            return Error(TEXT("Object, class and localized text defaults are not supported in this version."));
        FString Value;
        if (!Request->TryGetStringField(TEXT("value"), Value)) return Error(TEXT("value must be a string."));
        const FString Invalid = Schema->IsPinDefaultValid(P, Value, nullptr, FText::GetEmpty());
        if (!Invalid.IsEmpty()) return Error(Invalid);
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "Default", "MCP set pin default"));
        BP->Modify(); Graph->Modify(); N->Modify();
        Schema->TrySetDefaultValue(*P, Value);
        R->SetStringField(TEXT("actual_value"), P->DefaultValue);
    }
    else if (Op == TEXT("connect_pins") || Op == TEXT("disconnect_pins"))
    {
        UEdGraphNode* Target = Node(Graph, Str(Request, TEXT("target_node_id")));
        UEdGraphPin* Q = Pin(Target, Str(Request, TEXT("target_pin_id")));
        if (!P || !Q || P == Q) return Error(TEXT("Pin not found or identical pins selected."));
        if (Op == TEXT("connect_pins"))
        {
            if (P->LinkedTo.Contains(Q)) return Error(TEXT("Pins are already connected."));
            const FPinConnectionResponse Response = Schema->CanCreateConnection(P, Q);
            const bool OnlyEmptyPinsWouldBeCleared =
                (Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_A && P->LinkedTo.Num() == 0) ||
                (Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_B && Q->LinkedTo.Num() == 0) ||
                (Response.Response == CONNECT_RESPONSE_BREAK_OTHERS_AB && P->LinkedTo.Num() == 0 && Q->LinkedTo.Num() == 0);
            if (Response.Response != CONNECT_RESPONSE_MAKE && !OnlyEmptyPinsWouldBeCleared)
                return Error(TEXT("Connection rejected. Existing links must be disconnected explicitly; auto-conversions are not supported. ") + Response.Message.ToString());
        }
        else if (!P->LinkedTo.Contains(Q)) return Error(TEXT("Pins are not connected."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "Links", "MCP edit Blueprint connection"));
        BP->Modify(); Graph->Modify(); N->Modify(); Target->Modify();
        if (Op == TEXT("connect_pins"))
        {
            if (!Schema->TryCreateConnection(P, Q)) return Error(TEXT("Schema could not create connection."));
        }
        else Schema->BreakSinglePinLink(P, Q);
    }
    else if (Op == TEXT("add_branch") || Op == TEXT("add_function") || Op == TEXT("add_instance_function") || Op == TEXT("add_variable_get") || Op == TEXT("add_variable_set"))
    {
        const bool EventGraph = Schema->GetClass() == UEdGraphSchema_K2::StaticClass() && BP->UbergraphPages.Contains(Graph);
        const bool AnimDataGraph = Cast<UAnimBlueprint>(BP) &&
            (Schema->IsA(UAnimationGraphSchema::StaticClass()) || Schema->IsA(UAnimationTransitionSchema::StaticClass()));
        if (!EventGraph && !AnimDataGraph) return Error(TEXT("Select a K2 EventGraph, animation pose graph or transition rule graph."));
        UFunction* Function = nullptr;
        UProperty* Variable = nullptr;
        if (Op == TEXT("add_function") || Op == TEXT("add_instance_function"))
        {
            const FString FunctionPath = Str(Request, TEXT("function_path"));
            if (!FunctionPath.StartsWith(TEXT("/Script/"))) return Error(TEXT("Use a reflected native /Script/Module.Class:Function path."));
            Function = FindObject<UFunction>(nullptr, *FunctionPath);
            if (!Function || !Function->HasAnyFunctionFlags(FUNC_BlueprintCallable) ||
                (Function->HasAnyFunctionFlags(FUNC_Static) != (Op == TEXT("add_function"))) || Function->HasMetaData(TEXT("Latent")) ||
                Function->HasAnyFunctionFlags(FUNC_Delegate))
                return Error(TEXT("Function must be loaded, native BlueprintCallable, non-latent, and match the static/instance operation."));
        }
        if (Op == TEXT("add_variable_get") || Op == TEXT("add_variable_set"))
        {
            Variable = FindField<UProperty>(BP->SkeletonGeneratedClass, *Str(Request, TEXT("name")));
            if (!Variable || !Variable->HasAnyPropertyFlags(CPF_BlueprintVisible) ||
                (Op == TEXT("add_variable_set") && Variable->HasAnyPropertyFlags(CPF_BlueprintReadOnly)))
                return Error(TEXT("Variable not found or not writable/Blueprint-visible; compile after adding variables."));
        }
        if (!EventGraph && Op != TEXT("add_variable_get") && !((Op == TEXT("add_function") || Op == TEXT("add_instance_function")) && Function->HasAnyFunctionFlags(FUNC_BlueprintPure)))
            return Error(TEXT("Animation pose/rule graphs support variable getters and pure functions only."));
        const FScopedTransaction Transaction(NSLOCTEXT("UEBlueprintBridge", "Node", "MCP add Blueprint node"));
        BP->Modify(); Graph->Modify();
        UEdGraphNode* NewNode = nullptr;
        if (Op == TEXT("add_branch")) NewNode = NewObject<UK2Node_IfThenElse>(Graph, NAME_None, RF_Transactional);
        else if (Op == TEXT("add_function") || Op == TEXT("add_instance_function"))
        {
            auto* Call = NewObject<UK2Node_CallFunction>(Graph, NAME_None, RF_Transactional);
            Call->SetFromFunction(Function); NewNode = Call;
        }
        else
        {
            UK2Node_Variable* V = Op == TEXT("add_variable_get") ? static_cast<UK2Node_Variable*>(NewObject<UK2Node_VariableGet>(Graph, NAME_None, RF_Transactional)) : static_cast<UK2Node_Variable*>(NewObject<UK2Node_VariableSet>(Graph, NAME_None, RF_Transactional));
            V->VariableReference.SetSelfMember(Variable->GetFName()); NewNode = V;
        }
        Graph->AddNode(NewNode, false, false);
        NewNode->CreateNewGuid(); NewNode->PostPlacedNewNode(); NewNode->AllocateDefaultPins();
        double X = 0, Y = 0; Request->TryGetNumberField(TEXT("x"), X); Request->TryGetNumberField(TEXT("y"), Y);
        NewNode->NodePosX = FMath::Clamp(X, -1000000.0, 1000000.0); NewNode->NodePosY = FMath::Clamp(Y, -1000000.0, 1000000.0);
        R->SetStringField(TEXT("node_id"), NewNode->NodeGuid.ToString());
    }
    else return Error(TEXT("Unknown edit operation."));
    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
    Graph->NotifyGraphChanged();
    R->SetBoolField(TEXT("ok"), true);
    R->SetBoolField(TEXT("saved"), false);
    return R;
}
}
