#include "Campaign/RTSWorldLabel.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ARTSWorldLabel::ARTSWorldLabel()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Pole = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pole"));
	Pole->SetupAttachment(Root);
	Pole->SetRelativeLocation(FVector(0, 0, 80));
	Pole->SetRelativeScale3D(FVector(0.12f, 0.12f, 1.6f));
	Pole->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Pole->SetGenerateOverlapEvents(false);
	Pole->SetCanEverAffectNavigation(false);
	Pole->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		Pole->SetStaticMesh(CylinderMesh.Object);
	}
}

void ARTSWorldLabel::Setup(const FString& InLabel, const FLinearColor& InColor)
{
	LabelText = InLabel;
	LabelColor = InColor;
}

bool ARTSWorldLabel::IsVisualOnly() const
{
	if (!Pole)
	{
		return true;
	}
	return Pole->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Pole->CanEverAffectNavigation();
}
