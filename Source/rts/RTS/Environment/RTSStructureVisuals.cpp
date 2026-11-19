#include "Environment/RTSStructureVisuals.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	UStaticMesh* MeshAsset(const FString& Name)
	{ return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/RTS/Structures/%s.%s"), *Name, *Name)); }
	void ApplyMesh(UStaticMeshComponent* Component, UStaticMesh* Mesh)
	{
		if (Component->GetStaticMesh() != Mesh)
		{ Component->EmptyOverrideMaterials(); Component->SetStaticMesh(Mesh); }
	}
}
bool RTSStructureVisuals::Building(UStaticMeshComponent* Body, UStaticMeshComponent* Roof, FName Id, uint8 Team, float Footprint, float Progress, int32& AppliedTeam)
{
	const float Reference = Id == TEXT("TownHall") ? 280.f : Id == TEXT("Barracks") ? 240.f : Id == TEXT("Farm") ? 200.f : Id == TEXT("Tower") ? 160.f : 0.f;
	if (Reference <= 0) { return false; }
	UStaticMesh* BodyAsset = MeshAsset(TEXT("SM_") + Id.ToString() + TEXT("_Body"));
	UStaticMesh* RoofAsset = MeshAsset(TEXT("SM_") + Id.ToString() + TEXT("_Team"));
	if (!BodyAsset || !RoofAsset) { return false; }
	const bool bChanged = Body->GetStaticMesh() != BodyAsset || Roof->GetStaticMesh() != RoofAsset;
	ApplyMesh(Body, BodyAsset); ApplyMesh(Roof, RoofAsset);
	const float Scale = Footprint / Reference;
	const float BodyProgress = FMath::Clamp(Progress / 0.8f, 0.0f, 1.0f);
	const float RoofProgress = FMath::Clamp((Progress - 0.42f) / 0.58f, 0.0f, 1.0f);
	Body->SetRelativeLocation(FVector::ZeroVector);
	Body->SetRelativeScale3D(FVector(Scale, Scale, Scale * BodyProgress));
	Roof->SetRelativeLocation(FVector::ZeroVector);
	Roof->SetRelativeScale3D(FVector(Scale, Scale, Scale * RoofProgress));
	Roof->SetVisibility(RoofProgress > 0.0f, true);
	if (bChanged || AppliedTeam != Team)
	{
		for (UStaticMeshComponent* Component : {Body, Roof})
		{
			const TArray<FStaticMaterial>& Slots = Component->GetStaticMesh()->GetStaticMaterials();
			for (int32 i = 0; i < Slots.Num(); ++i)
			{
				if (Slots[i].MaterialSlotName != TEXT("team")) { continue; }
				// Base this on the asset, never on a temporary hit-flash material.
				UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Slots[i].MaterialInterface, Component);
				Material->SetVectorParameterValue(TEXT("Color"), Team == 0 ? FLinearColor(.045f, .20f, .60f)
					: Team == 1 ? FLinearColor(.60f, .065f, .035f) : FLinearColor(.31f, .30f, .24f));
				Component->SetMaterial(i, Material);
			}
		}
		AppliedTeam = Team;
	}
	return true;
}
bool RTSStructureVisuals::Resource(UStaticMeshComponent* Visual, bool bWood)
{
	UStaticMesh* Mesh = MeshAsset(bWood ? TEXT("SM_ResourceWood") : TEXT("SM_ResourceFood"));
	if (!Mesh) { return false; }
	ApplyMesh(Visual, Mesh);
	Visual->SetRelativeScale3D(FVector::OneVector);
	Visual->SetRelativeLocation(FVector(0, 0, -90));
	return true;
}
