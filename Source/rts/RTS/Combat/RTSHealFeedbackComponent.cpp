#include "Combat/RTSHealFeedbackComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"
#include "Combat/RTSHealthComponent.h"

URTSHealFeedbackComponent::URTSHealFeedbackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}
void URTSHealFeedbackComponent::Pulse(bool bCaster)
{
	if (!GetOwner() || !GetOwner()->GetRootComponent()) { return; }
	if (Parts.IsEmpty())
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RTS/Materials/M_RTSHitFlash.M_RTSHitFlash"));
		UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Base || !Sphere || !Cube) { return; }
		Material = UMaterialInstanceDynamic::Create(Base, this);
		for (int32 Index = 0; Index < 12; ++Index)
		{
			UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(GetOwner(), *FString::Printf(TEXT("HealFX_%d"), Index));
			GetOwner()->AddInstanceComponent(Part); Part->SetupAttachment(GetOwner()->GetRootComponent());
			Part->SetStaticMesh(Index < 8 ? Sphere : Cube); Part->SetMaterial(0, Material);
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); Part->SetCanEverAffectNavigation(false);
			Part->SetCastShadow(false); Part->SetVisibleInRayTracing(false);
			Part->bAffectDistanceFieldLighting = false; Part->bAffectDynamicIndirectLighting = false;
			Part->RegisterComponent(); Parts.Add(Part);
		}
	}
	Age = 0; bPlaying = true; bCasterPulse = bCaster; ++PulseCount;
	for (UStaticMeshComponent* Part : Parts) { Part->SetVisibility(true); }
	SetComponentTickEnabled(true);
	TickComponent(0, LEVELTICK_All, nullptr);
}
void URTSHealFeedbackComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bPlaying) { return; }
	Age += DeltaTime;
	const float Duration = bCasterPulse ? 0.4f : 0.9f;
	const float Alpha = FMath::Clamp(Age / Duration, 0.f, 1.f);
	const URTSHealthComponent* Health = GetOwner()->FindComponentByClass<URTSHealthComponent>();
	if (Alpha >= 1 || (Health && Health->IsDead()))
	{
		for (UStaticMeshComponent* Part : Parts) { Part->SetVisibility(false); }
		bPlaying = false; SetComponentTickEnabled(false); return;
	}
	Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.08f, 2.5f, 0.3f) * (1.f - Alpha));
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		UStaticMeshComponent* Part = Parts[Index];
		if (Index < 8)
		{
			const float Angle = Index * UE_TWO_PI / 8 + Age * 2.f;
			const float Radius = bCasterPulse ? 12.f : 43.f + Alpha * 7.f;
			Part->SetRelativeLocation(FVector((bCasterPulse ? 42.f : 0.f) + FMath::Cos(Angle) * Radius,
				FMath::Sin(Angle) * Radius, (bCasterPulse ? 35.f : -50.f) + Alpha * (bCasterPulse ? 12.f : 115.f) + (Index % 3) * 12.f));
			Part->SetRelativeScale3D(FVector((bCasterPulse ? 0.055f : 0.07f) * (1.f - Alpha * 0.85f)));
		}
		else
		{
			const int32 Cross = (Index - 8) / 2;
			Part->SetRelativeLocation(FVector(25, Cross == 0 ? -30 : 30, -15 + Cross * 25 + Alpha * 90));
			Part->SetRelativeScale3D((Index % 2 == 0 ? FVector(0.035, 0.055, 0.22) : FVector(0.035, 0.20, 0.055)) * (1.f - Alpha * 0.7f));
			Part->SetVisibility(!bCasterPulse);
		}
	}
}
