#pragma once
#include "EngineUtils.h"
#include "Components/ActorComponent.h"

namespace RuntimeOps
{
inline FObj Run(const FString& Action, const FObj& Request)
{
    if (!GEditor) return BlueprintWrite::Error(TEXT("Editor unavailable."));
    FObj R = MakeShared<FJsonObject>();
    if (Action == TEXT("pie_control"))
    {
        const FString Op = BlueprintWrite::Str(Request, TEXT("operation"));
        if (Op == TEXT("start"))
        {
            if (GEditor->PlayWorld) return BlueprintWrite::Error(TEXT("PIE is already running."));
            GEditor->RequestPlaySession(true, nullptr, false);
        }
        else if (Op == TEXT("stop")) { GEditor->CancelRequestPlaySession(); if (GEditor->PlayWorld) GEditor->RequestEndPlayMap(); }
        else if (Op != TEXT("status")) return BlueprintWrite::Error(TEXT("Unknown PIE operation."));
        R->SetBoolField(TEXT("running"), GEditor->PlayWorld != nullptr);
        R->SetBoolField(TEXT("queued"), Op != TEXT("status"));
        return R;
    }
    UWorld* World = GEditor->PlayWorld;
    if (!World) return BlueprintWrite::Error(TEXT("PIE is not running."));
    if (Action == TEXT("list_actors"))
    {
        const FString Search = BlueprintWrite::Str(Request, TEXT("name_contains"));
        TArray<TSharedPtr<FJsonValue>> Items;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* A = *It;
            if (!Search.IsEmpty() && !A->GetName().Contains(Search)) continue;
            if (Items.Num() >= 500) { R->SetBoolField(TEXT("truncated"), true); break; }
            FObj Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("path"), A->GetPathName()); Item->SetStringField(TEXT("class"), A->GetClass()->GetPathName());
            Item->SetStringField(TEXT("name"), A->GetName()); Items.Add(JV(Item));
        }
        R->SetArrayField(TEXT("actors"), Items); return R;
    }
    UObject* Object = FindObject<UObject>(nullptr, *BlueprintWrite::Str(Request, TEXT("object_path")));
    if (!IsValid(Object) || Object->GetWorld() != World || (!Cast<AActor>(Object) && !Cast<UActorComponent>(Object)))
        return BlueprintWrite::Error(TEXT("Select a live Actor or Component in the active PIE world."));
    if (Action == TEXT("list_components"))
    {
        AActor* Actor = Cast<AActor>(Object);
        if (!Actor) return BlueprintWrite::Error(TEXT("object_path must identify an Actor."));
        TArray<UActorComponent*> Components; Actor->GetComponents(Components);
        TArray<TSharedPtr<FJsonValue>> Items;
        for (UActorComponent* C : Components) if (IsValid(C))
        {
            FObj Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("path"), C->GetPathName());
            Item->SetStringField(TEXT("class"), C->GetClass()->GetPathName()); Items.Add(JV(Item));
        }
        R->SetArrayField(TEXT("components"), Items); return R;
    }
    UProperty* P = FindField<UProperty>(Object->GetClass(), *BlueprintWrite::Str(Request, TEXT("property")));
    if (!P || !P->HasAnyPropertyFlags(CPF_BlueprintVisible | CPF_Edit)) return BlueprintWrite::Error(TEXT("Property not found or not exposed."));
    FString Value; P->ExportText_InContainer(0, Value, Object, Object, Object, PPF_None);
    R->SetStringField(TEXT("value_text"), Value); R->SetStringField(TEXT("type"), P->GetCPPType());
    R->SetStringField(TEXT("object_path"), Object->GetPathName()); return R;
}
}
