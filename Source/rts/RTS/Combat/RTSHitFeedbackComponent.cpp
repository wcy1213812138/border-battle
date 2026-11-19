#include "Combat/RTSHitFeedbackComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

URTSHitFeedbackComponent::URTSHitFeedbackComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/RTS/Materials/M_RTSHitFlash.M_RTSHitFlash"));
	FlashMaterial = Material.Object;
}
void URTSHitFeedbackComponent::BeginPlay()
{
	Super::BeginPlay();
	BoundHealth = GetOwner()->FindComponentByClass<URTSHealthComponent>();
	if (BoundHealth.IsValid()) { BoundHealth->OnHealthChanged.AddDynamic(this, &URTSHitFeedbackComponent::HealthChanged); }
	if (FlashMaterial)
	{
		UMaterialInstanceDynamic* White = UMaterialInstanceDynamic::Create(FlashMaterial, this);
		White->SetVectorParameterValue(TEXT("Color"), FLinearColor(3.f, 3.f, 3.f, 1.f));
		FlashMaterial = White;
	}
}
void URTSHitFeedbackComponent::HealthChanged(URTSHealthComponent* Health, float NewHealth, float Delta)
{
	if (Delta >= 0 || !FlashMaterial || !GetWorld()) { return; }
	if (!bFlashing)
	{
		TArray<UMeshComponent*> Components; GetOwner()->GetComponents(Components);
		for (UMeshComponent* Mesh : Components)
		{
			// Keep selection rings/markers untouched. Only the character body or building body/roof flashes.
			if (!Cast<USkeletalMeshComponent>(Mesh) && Mesh->GetFName() != TEXT("Body") && Mesh->GetFName() != TEXT("Roof")) { continue; }
			for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
			{
				Meshes.Add(Mesh); MaterialSlots.Add(Slot); PreviousMaterials.Add(Mesh->GetMaterial(Slot));
				Mesh->SetMaterial(Slot, FlashMaterial);
			}
		}
		bFlashing = !Meshes.IsEmpty();
	}
	GetWorld()->GetTimerManager().SetTimer(FlashTimer, this, &URTSHitFeedbackComponent::Restore, 0.12f, false);
}
void URTSHitFeedbackComponent::Restore()
{
	for (int32 i = 0; i < Meshes.Num(); ++i)
	{
		if (IsValid(Meshes[i]) && Meshes[i]->GetMaterial(MaterialSlots[i]) == FlashMaterial) { Meshes[i]->SetMaterial(MaterialSlots[i], PreviousMaterials[i]); }
	}
	Meshes.Reset(); MaterialSlots.Reset(); PreviousMaterials.Reset(); bFlashing = false;
}
void URTSHitFeedbackComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (BoundHealth.IsValid()) { BoundHealth->OnHealthChanged.RemoveDynamic(this, &URTSHitFeedbackComponent::HealthChanged); }
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(FlashTimer); }
	Restore();
	Super::EndPlay(Reason);
}
