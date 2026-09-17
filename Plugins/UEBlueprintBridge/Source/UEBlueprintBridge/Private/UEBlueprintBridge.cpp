#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/EngineVersion.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "UObject/UnrealType.h"
#include "AssetRegistryModule.h"
#include "Misc/SecureHash.h"

using FObj = TSharedPtr<FJsonObject>;
static TSharedPtr<FJsonValue> JV(const FObj& O) { return MakeShared<FJsonValueObject>(O); }
static FString PathOf(const UObject* O) { return O ? O->GetPathName() : FString(); }
#include "BlueprintWriteOps.h"
#include "SkeletonReadOps.h"
#include "AssetOps.h"
#include "RuntimeOps.h"
#include "SkeletonWriteOps.h"
#include "StructuredAssetOps.h"
#include "DataTableOps.h"
#include "AnimationAssetOps.h"
#include "TAAssetOps.h"
#include "EditorSelectionOps.h"

class FUEBlueprintBridge : public IModuleInterface
{
    FDelegateHandle Handle;
    FString Root;

    FObj Inspect(const FString& Path)
    {
        FObj Result = MakeShared<FJsonObject>();
        if (!Path.StartsWith(TEXT("/Game/")))
        {
            Result->SetStringField(TEXT("error"), TEXT("Only /Game/ assets are supported."));
            return Result;
        }
        UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Path);
        if (!BP)
        {
            Result->SetStringField(TEXT("error"), TEXT("Blueprint not found."));
            return Result;
        }
        Result->SetStringField(TEXT("asset"), BP->GetPathName());
        Result->SetStringField(TEXT("class"), BP->GetClass()->GetPathName());
        Result->SetStringField(TEXT("parent_class"), PathOf(BP->ParentClass));
        if (UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(BP))
        {
            TArray<TSharedPtr<FJsonValue>> Slots;
            if (AnimBP->TargetSkeleton)
                for (const FAnimSlotGroup& Group : AnimBP->TargetSkeleton->GetSlotGroups())
                    for (const FName Slot : Group.SlotNames)
                    {
                        FObj Item = MakeShared<FJsonObject>();
                        Item->SetStringField(TEXT("group"), Group.GroupName.ToString());
                        Item->SetStringField(TEXT("name"), Slot.ToString());
                        Slots.Add(JV(Item));
                    }
            Result->SetArrayField(TEXT("skeleton_slots"), Slots);
        }
        TArray<UEdGraph*> Graphs;
        BP->GetAllGraphs(Graphs);
        // Follow editor graph references, not compiler-generated duplicate objects.
        for (int32 Index = 0; Index < Graphs.Num(); ++Index)
        {
            UEdGraph* G = Graphs[Index];
            if (!G) continue;
            for (UEdGraph* Sub : G->SubGraphs) if (Sub) Graphs.AddUnique(Sub);
            for (UEdGraphNode* N : G->Nodes)
            {
                if (!N) continue;
                for (TFieldIterator<UObjectProperty> It(N->GetClass()); It; ++It)
                {
                    if (It->HasAnyPropertyFlags(CPF_Transient)) continue;
                    if (UEdGraph* Sub = Cast<UEdGraph>(It->GetObjectPropertyValue_InContainer(N)))
                        if (Sub->IsIn(BP)) Graphs.AddUnique(Sub);
                }
            }
        }
        FObj BPProperties = MakeShared<FJsonObject>();
        for (const FName Key : { FName(TEXT("TargetSkeleton")), FName(TEXT("NewVariables")) })
        {
            if (UProperty* P = FindField<UProperty>(BP->GetClass(), Key))
            {
                FString Value;
                P->ExportText_InContainer(0, Value, BP, BP, BP, PPF_None);
                BPProperties->SetStringField(Key.ToString(), Value);
            }
        }
        Result->SetObjectField(TEXT("properties_text"), BPProperties);
        if (UAnimBlueprint* SettingsBP = Cast<UAnimBlueprint>(BP))
        {
            FObj ClassSettings = MakeShared<FJsonObject>();
            ClassSettings->SetStringField(TEXT("parent_class"), PathOf(SettingsBP->ParentClass));
            ClassSettings->SetStringField(TEXT("target_skeleton"), PathOf(SettingsBP->TargetSkeleton));
            ClassSettings->SetBoolField(TEXT("use_multithreaded_animation_update"), SettingsBP->bUseMultiThreadedAnimationUpdate);
            ClassSettings->SetBoolField(TEXT("warn_about_blueprint_usage"), SettingsBP->bWarnAboutBlueprintUsage);
            ClassSettings->SetBoolField(TEXT("generate_const_class"), SettingsBP->bGenerateConstClass);
            ClassSettings->SetBoolField(TEXT("generate_abstract_class"), SettingsBP->bGenerateAbstractClass);
            ClassSettings->SetBoolField(TEXT("deprecate"), SettingsBP->bDeprecate);
            Result->SetObjectField(TEXT("class_settings"), ClassSettings);
        }
        TArray<TSharedPtr<FJsonValue>> OutGraphs;
        for (UEdGraph* G : Graphs)
        {
            if (!G) continue;
            FObj GO = MakeShared<FJsonObject>();
            GO->SetStringField(TEXT("path"), G->GetPathName());
            GO->SetStringField(TEXT("name"), G->GetName());
            GO->SetStringField(TEXT("class"), G->GetClass()->GetPathName());
            GO->SetStringField(TEXT("outer"), PathOf(G->GetOuter()));
            TArray<TSharedPtr<FJsonValue>> Nodes;
            for (UEdGraphNode* N : G->Nodes)
            {
                if (!N) continue;
                FObj NO = MakeShared<FJsonObject>();
                NO->SetStringField(TEXT("id"), N->NodeGuid.ToString());
                NO->SetStringField(TEXT("path"), N->GetPathName());
                NO->SetStringField(TEXT("class"), N->GetClass()->GetPathName());
                NO->SetStringField(TEXT("title"), N->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                NO->SetStringField(TEXT("comment"), N->NodeComment);
                NO->SetNumberField(TEXT("x"), N->NodePosX);
                NO->SetNumberField(TEXT("y"), N->NodePosY);
                FObj Properties = MakeShared<FJsonObject>();
                for (TFieldIterator<UProperty> It(N->GetClass()); It; ++It)
                {
                    UProperty* P = *It;
                    if (P->HasAnyPropertyFlags(CPF_Transient) ||
                        P->GetOwnerStruct() == UEdGraphNode::StaticClass() ||
                        P->GetFName() == TEXT("ShowPinForProperties")) continue;
                    // Includes animation node structs, function references and bound graphs.
                    FString Value;
                    P->ExportText_InContainer(0, Value, N, N, N, PPF_None);
                    Properties->SetStringField(P->GetName(), Value);
                }
                NO->SetObjectField(TEXT("properties_text"), Properties);
                TArray<TSharedPtr<FJsonValue>> Pins;
                for (UEdGraphPin* P : N->Pins)
                {
                    if (!P) continue;
                    FObj PO = MakeShared<FJsonObject>();
                    PO->SetStringField(TEXT("id"), P->PinId.ToString());
                    PO->SetStringField(TEXT("name"), P->PinName.ToString());
                    PO->SetStringField(TEXT("direction"), P->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
                    PO->SetStringField(TEXT("category"), P->PinType.PinCategory.ToString());
                    PO->SetStringField(TEXT("subcategory"), P->PinType.PinSubCategory.ToString());
                    PO->SetStringField(TEXT("type_object"), PathOf(P->PinType.PinSubCategoryObject.Get()));
                    PO->SetNumberField(TEXT("container_type"), static_cast<int32>(P->PinType.ContainerType));
                    PO->SetBoolField(TEXT("is_reference"), P->PinType.bIsReference);
                    PO->SetStringField(TEXT("default"), P->DefaultValue);
                    PO->SetStringField(TEXT("default_object"), PathOf(P->DefaultObject));
                    PO->SetStringField(TEXT("default_text"), P->DefaultTextValue.ToString());
                    TArray<TSharedPtr<FJsonValue>> Links;
                    for (UEdGraphPin* L : P->LinkedTo)
                    {
                        if (!L || !L->GetOwningNode()) continue;
                        FObj LO = MakeShared<FJsonObject>();
                        LO->SetStringField(TEXT("node"), L->GetOwningNode()->NodeGuid.ToString());
                        LO->SetStringField(TEXT("node_path"), L->GetOwningNode()->GetPathName());
                        LO->SetStringField(TEXT("pin"), L->PinId.ToString());
                        Links.Add(JV(LO));
                    }
                    PO->SetArrayField(TEXT("links"), Links);
                    Pins.Add(JV(PO));
                }
                NO->SetArrayField(TEXT("pins"), Pins);
                Nodes.Add(JV(NO));
            }
            GO->SetArrayField(TEXT("nodes"), Nodes);
            OutGraphs.Add(JV(GO));
        }
        Result->SetArrayField(TEXT("graphs"), OutGraphs);
        FString Snapshot;
        FJsonSerializer::Serialize(Result.ToSharedRef(), TJsonWriterFactory<>::Create(&Snapshot));
        FTCHARToUTF8 SnapshotBytes(*Snapshot);
        Result->SetStringField(TEXT("revision"), FMD5::HashBytes(reinterpret_cast<const uint8*>(SnapshotBytes.Get()), SnapshotBytes.Length()));
        return Result;
    }

    FObj Dispatch(const FObj& Request)
    {
        FObj R = MakeShared<FJsonObject>();
        FString Action;
        Request->TryGetStringField(TEXT("action"), Action);
        if (Action == TEXT("ping"))
        {
            R->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
            R->SetStringField(TEXT("project"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
            R->SetBoolField(TEXT("read_only"), false);
            R->SetStringField(TEXT("plugin_version"), TEXT("0.5.1"));
            R->SetStringField(TEXT("profile"), TEXT("core"));
        }
        else if (Action == TEXT("pie_control") || Action == TEXT("list_actors") || Action == TEXT("list_components") || Action == TEXT("read_runtime_property"))
        {
            double Expires = 0;
            if (Action == TEXT("pie_control") && (!Request->TryGetNumberField(TEXT("expires_unix"), Expires) || Expires < FDateTime::UtcNow().ToUnixTimestamp())) return BlueprintWrite::Error(TEXT("PIE request expired."));
            return RuntimeOps::Run(Action, Request);
        }
        else if (Action == TEXT("inspect_skeleton_edit") || Action == TEXT("edit_skeleton") || Action == TEXT("save_skeleton")) return SkeletonWrite::Run(Action, Request);
        else if (Action == TEXT("evaluate_ta_asset")) return TAProductionOps::Evaluate(Request);
        else if (Action == TEXT("create_ta_asset")) return TAProductionOps::Create(Request);
        else if (Action == TEXT("inspect_ta_asset")) return TAAssetOps::Inspect(Request);
        else if (Action == TEXT("edit_ta_asset")) return TAAssetOps::Edit(Request, false);
        else if (Action == TEXT("save_ta_asset")) return TAAssetOps::Edit(Request, true);
        else if (Action == TEXT("inspect_animation_asset")) return AnimationAssetOps::Inspect(Request);
        else if (Action == TEXT("sample_animation_bone")) return AnimationAssetOps::Sample(Request);
        else if (Action == TEXT("read_animation_track")) return AnimationAssetOps::ReadTrack(Request);
        else if (Action == TEXT("edit_animation_asset")) return AnimationAssetOps::Edit(Request, false);
        else if (Action == TEXT("save_animation_asset")) return AnimationAssetOps::Edit(Request, true);
        else if (Action == TEXT("copy_animation_curve")) return AnimationAssetOps::CopyCurve(Request);
        else if (Action == TEXT("inspect_data_table")) return DataTableOps::Inspect(Request);
        else if (Action == TEXT("edit_data_table")) return DataTableOps::Edit(Request);
        else if (Action == TEXT("save_data_table")) return DataTableOps::Save(Request);
        else if (Action == TEXT("read_animation_pose")) return AnimationAssetOps::RuntimePose(Request);
        else if (Action == TEXT("inspect_asset")) return StructuredAssetOps::Inspect(Request);
        else if (Action == TEXT("copy_anim_nodes")) return BlueprintWrite::CopyAnimNodes(Request);
        else if (Action == TEXT("paste_anim_nodes")) return BlueprintWrite::PasteAnimNodes(Request);
        else if (Action == TEXT("select_blueprint_node")) return EditorSelectionOps::SelectBlueprintNode(Request);
        else if (Action == TEXT("list_assets")) return AssetOps::List(Request);
        else if (Action == TEXT("create_blueprint"))
        {
            double Expires = 0;
            if (!Request->TryGetNumberField(TEXT("expires_unix"), Expires) || Expires < FDateTime::UtcNow().ToUnixTimestamp())
                return BlueprintWrite::Error(TEXT("Create request expired."));
            R = AssetOps::Create(Request);
            if (!R->HasField(TEXT("error"))) R->SetStringField(TEXT("revision"), Inspect(R->GetStringField(TEXT("asset")))->GetStringField(TEXT("revision")));
            return R;
        }
        else if (Action == TEXT("list_blueprints") || Action == TEXT("list_skeletons"))
        {
            auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
            FARFilter Filter;
            Filter.ClassNames.Add(Action == TEXT("list_skeletons") ? USkeleton::StaticClass()->GetFName() : UBlueprint::StaticClass()->GetFName());
            Filter.bRecursiveClasses = true;
            Filter.PackagePaths.Add(TEXT("/Game"));
            Filter.bRecursivePaths = true;
            TArray<FAssetData> Assets;
            Registry.GetAssets(Filter, Assets);
            TArray<TSharedPtr<FJsonValue>> Items;
            for (const FAssetData& A : Assets)
            {
                FObj Item = MakeShared<FJsonObject>();
                Item->SetStringField(TEXT("path"), A.ObjectPath.ToString());
                Item->SetStringField(TEXT("class"), A.AssetClass.ToString());
                Items.Add(JV(Item));
            }
            R->SetArrayField(TEXT("assets"), Items);
        }
        else if (Action == TEXT("inspect_skeleton")) return SkeletonRead::Inspect(Request);
        else if (Action == TEXT("inspect_blueprint"))
        {
            FString Path;
            Request->TryGetStringField(TEXT("asset_path"), Path);
            return Inspect(Path);
        }
        else if (Action == TEXT("edit_blueprint") || Action == TEXT("duplicate_blueprint") || Action == TEXT("compile_blueprint") || Action == TEXT("save_blueprint"))
        {
            double Expires = 0;
            if (!Request->TryGetNumberField(TEXT("expires_unix"), Expires) || Expires < FDateTime::UtcNow().ToUnixTimestamp())
                return BlueprintWrite::Error(TEXT("Write request expired or has no expiry. Read state before retrying."));
            const FString Path = BlueprintWrite::Str(Request, TEXT("asset_path"));
            FObj Before = Inspect(Path);
            if (Before->HasField(TEXT("error"))) return Before;
            if (BlueprintWrite::Str(Request, TEXT("expected_revision")) != Before->GetStringField(TEXT("revision")))
                return BlueprintWrite::Error(TEXT("Revision mismatch. Inspect again before editing; no changes applied."));
            UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Path);
            R = BlueprintWrite::Run(BP, Action, Request);
            if (!R->HasField(TEXT("error")))
            {
                FString ResultPath = Path;
                R->TryGetStringField(TEXT("asset"), ResultPath);
                R->SetStringField(TEXT("asset"), ResultPath);
                R->SetStringField(TEXT("revision"), Inspect(ResultPath)->GetStringField(TEXT("revision")));
            }
        }
        else R->SetStringField(TEXT("error"), TEXT("Unknown action."));
        return R;
    }

    bool Tick(float)
    {
        TArray<FString> Files;
        IFileManager::Get().FindFiles(Files, *(Root / TEXT("requests/*.json")), true, false);
        // Bound work per frame; all UObject access happens on the editor thread.
        if (Files.Num() == 0) return true;
        const FString Name = Files[0];
        FString Input;
        FObj Request;
        FObj Response;
        if (IFileManager::Get().FileExists(*(Root / TEXT("cancelled") / Name)))
            Response = BlueprintWrite::Error(TEXT("Request cancelled before editor dispatch; no action performed."));
        else if (FFileHelper::LoadFileToString(Input, *(Root / TEXT("requests") / Name)) &&
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Input), Request) && Request.IsValid())
            Response = Dispatch(Request);
        else
        {
            Response = MakeShared<FJsonObject>();
            Response->SetStringField(TEXT("error"), TEXT("Invalid request JSON."));
        }
        FString Output;
        FJsonSerializer::Serialize(Response.ToSharedRef(), TJsonWriterFactory<>::Create(&Output));
        const FString Dest = Root / TEXT("responses") / Name;
        if (FFileHelper::SaveStringToFile(Output, *(Dest + TEXT(".tmp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            IFileManager::Get().Move(*Dest, *(Dest + TEXT(".tmp")), true);
            IFileManager::Get().Delete(*(Root / TEXT("requests") / Name));
            IFileManager::Get().Delete(*(Root / TEXT("cancelled") / Name));
        }
        return true;
    }
public:
    virtual void StartupModule() override
    {
        Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UEBlueprintBridge"));
        IFileManager::Get().MakeDirectory(*(Root / TEXT("requests")), true);
        IFileManager::Get().MakeDirectory(*(Root / TEXT("responses")), true);
        Handle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FUEBlueprintBridge::Tick), 0.1f);
    }
    virtual void ShutdownModule() override { FTicker::GetCoreTicker().RemoveTicker(Handle); }
};
IMPLEMENT_MODULE(FUEBlueprintBridge, UEBlueprintBridge)
