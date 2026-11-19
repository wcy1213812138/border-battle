#include "UI/RTSCommandMarker.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float DiscThickness = 0.02f; // engine cylinder is 100cm tall -> 2cm
}

ARTSCommandMarker::ARTSCommandMarker()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);

	Disc = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Disc"));
	RootComponent = Disc;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		Disc->SetStaticMesh(CylinderMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (ShapeMaterial.Succeeded())
	{
		Disc->SetMaterial(0, ShapeMaterial.Object);
	}
	Disc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Disc->SetCanEverAffectNavigation(false);
	Disc->SetCastShadow(false);
}

ARTSCommandMarker* ARTSCommandMarker::SpawnAt(UWorld* World, const FVector& Location, const FLinearColor& Color, float Diameter)
{
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// Lift slightly so it doesn't z-fight with the ground.
	ARTSCommandMarker* Marker = World->SpawnActor<ARTSCommandMarker>(Location + FVector(0.0, 0.0, 2.0), FRotator::ZeroRotator, Params);
	if (Marker)
	{
		Marker->StartScale = Diameter / 100.0f;
		Marker->Disc->SetRelativeScale3D(FVector(Marker->StartScale, Marker->StartScale, DiscThickness));
		if (UMaterialInstanceDynamic* MID = Marker->Disc->CreateAndSetMaterialInstanceDynamic(0))
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}
	return Marker;
}

void ARTSCommandMarker::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Elapsed += DeltaSeconds;
	const float Alpha = FMath::Clamp(Elapsed / FMath::Max(Duration, 0.01f), 0.0f, 1.0f);
	if (Alpha >= 1.0f)
	{
		Destroy();
		return;
	}
	// Shrink with ease-out so the click point reads clearly at first.
	const float Scale = StartScale * (1.0f - Alpha * Alpha);
	Disc->SetRelativeScale3D(FVector(Scale, Scale, DiscThickness));
}
