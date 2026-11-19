#include "Environment/RTSCampaignVisuals.h"
#include "Campaign/RTSCampaign.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    UStaticMesh* CampaignMesh(const FString& Name)
    {
        // Keep loaded meshes alive across GC; cache misses too. Never LoadObject every update.
        static TMap<FString, TStrongObjectPtr<UStaticMesh>> Cache;
        if (const TStrongObjectPtr<UStaticMesh>* Found = Cache.Find(Name)) { return Found->Get(); }
        UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr,
            *FString::Printf(TEXT("/Game/RTS/CampaignArt/%s.%s"), *Name, *Name));
        Cache.Add(Name, TStrongObjectPtr<UStaticMesh>(Mesh));
        return Mesh;
    }
    void Apply(UStaticMeshComponent* Component, UStaticMesh* Mesh)
    {
        if (Component->GetStaticMesh() != Mesh)
        {
            Component->EmptyOverrideMaterials();
            Component->SetStaticMesh(Mesh);
        }
    }
}

bool RTSCampaignVisuals::Building(UStaticMeshComponent* Body, UStaticMeshComponent* Roof,
    FName Id, uint8 Team, float Footprint, float Progress, int32& AppliedTeam)
{
    if (!Body || !Roof || !Body->GetWorld()
        || !Cast<ARTSCampaignGameMode>(Body->GetWorld()->GetAuthGameMode())) { return false; }
    const float Reference = Id == TEXT("TownHall") ? 280.f : Id == TEXT("ResearchInstitute") ? 250.f
        : Id == TEXT("Barracks") ? 240.f : Id == TEXT("ArcheryRange") ? 240.f
        : Id == TEXT("SupportBarracks") ? 240.f : Id == TEXT("Farm") ? 220.f
        : Id == TEXT("Tower") ? 160.f : 0.f;
    if (Reference <= 0) { return false; }
    UStaticMesh* BodyMesh = CampaignMesh(TEXT("SM_") + Id.ToString() + TEXT("_Body"));
    UStaticMesh* TeamMesh = CampaignMesh(TEXT("SM_") + Id.ToString() + TEXT("_Team"));
    if (!BodyMesh || !TeamMesh) { return false; }
    const bool bChanged = Body->GetStaticMesh() != BodyMesh || Roof->GetStaticMesh() != TeamMesh;
    Apply(Body, BodyMesh); Apply(Roof, TeamMesh);
    const float Scale = Footprint / Reference;
    const float Growth = FMath::Clamp(Progress / .85f, .025f, 1.f);
    Body->SetRelativeLocation(FVector::ZeroVector);
    Body->SetRelativeScale3D(FVector(Scale, Scale, Scale * Growth));
    Roof->SetRelativeLocation(FVector::ZeroVector);
    Roof->SetRelativeScale3D(FVector(Scale, Scale, Scale * Growth));
    Roof->SetVisibility(Progress >= .85f, true);
    if (bChanged || AppliedTeam != Team)
    {
        for (UStaticMeshComponent* Component : {Body, Roof})
        {
            const TArray<FStaticMaterial>& Slots = Component->GetStaticMesh()->GetStaticMaterials();
            for (int32 i = 0; i < Slots.Num(); ++i)
            {
                if (Slots[i].MaterialSlotName != TEXT("team")) { continue; }
                UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Slots[i].MaterialInterface, Component);
                if (!Material) { continue; }
                Material->SetVectorParameterValue(TEXT("Color"), Team == 0 ? FLinearColor(.013f,.107f,.477f)
                    : Team == 1 ? FLinearColor(.55f,.025f,.012f) : FLinearColor(.20f,.19f,.10f));
                Component->SetMaterial(i, Material);
            }
        }
        AppliedTeam = Team;
    }
    return true;
}

bool RTSCampaignVisuals::Resource(UStaticMeshComponent* Visual, ERTSResource Type)
{
    if (!Visual || !ARTSCampaign::Get(Visual)) { return false; }
    const TCHAR* Name = Type == ERTSResource::Wood ? TEXT("Wood") : Type == ERTSResource::Food ? TEXT("Food")
        : Type == ERTSResource::IronOre ? TEXT("IronOre") : Type == ERTSResource::RefinedIron ? TEXT("RefinedIron")
        : Type == ERTSResource::Amber ? TEXT("Amber") : Type == ERTSResource::Crystal ? TEXT("Crystal")
        : Type == ERTSResource::Gold ? TEXT("Gold") : Type == ERTSResource::Mana ? TEXT("Mana") : nullptr;
    if (!Name) { return false; }
    UStaticMesh* Mesh = CampaignMesh(FString(TEXT("SM_Resource")) + Name);
    if (!Mesh) { return false; }
    Apply(Visual, Mesh);
    Visual->SetRelativeScale3D(FVector::OneVector);
    Visual->SetRelativeLocation(FVector(0, 0, -90));
    return true;
}
