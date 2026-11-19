#include "Units/RTSTrainingDummy.h"
#include "Combat/RTSHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float DummyRadius = 30.0f;
	constexpr float DummyBodyHeight = 140.0f;
	constexpr float DummyHeadRadius = 22.0f;
	constexpr float CapsuleHalfHeight = 90.0f;
	constexpr float FallAngle = 85.0f;
}

ARTSTrainingDummy::ARTSTrainingDummy()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(DummyRadius + 10.0f, CapsuleHalfHeight);
	Capsule->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	// Units path straight to it and stop on overlap; no navmesh hole needed for one dummy.
	Capsule->SetCanEverAffectNavigation(false);
	RootComponent = Capsule;

	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	Pivot->SetupAttachment(Capsule);
	Pivot->SetRelativeLocation(FVector(0.0, 0.0, -CapsuleHalfHeight));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	// Engine basic shapes are 100cm and centered on their origin.
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Pivot);
	if (CylinderMesh.Succeeded())
	{
		Body->SetStaticMesh(CylinderMesh.Object);
	}
	Body->SetRelativeScale3D(FVector(DummyRadius * 2.0f / 100.0f, DummyRadius * 2.0f / 100.0f, DummyBodyHeight / 100.0f));
	Body->SetRelativeLocation(FVector(0.0, 0.0, DummyBodyHeight * 0.5f));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCanEverAffectNavigation(false);

	Head = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Head"));
	Head->SetupAttachment(Pivot);
	if (SphereMesh.Succeeded())
	{
		Head->SetStaticMesh(SphereMesh.Object);
	}
	Head->SetRelativeScale3D(FVector(DummyHeadRadius * 2.0f / 100.0f));
	Head->SetRelativeLocation(FVector(0.0, 0.0, DummyBodyHeight + DummyHeadRadius * 0.8f));
	Head->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Head->SetCanEverAffectNavigation(false);

	Health = CreateDefaultSubobject<URTSHealthComponent>(TEXT("Health"));
}

void ARTSTrainingDummy::BeginPlay()
{
	Super::BeginPlay();

	BodyMID = Body->CreateAndSetMaterialInstanceDynamic(0);
	if (BodyMID)
	{
		BodyMID->SetVectorParameterValue(TEXT("Color"), BodyColor);
	}

	Health->OnHealthChanged.AddDynamic(this, &ARTSTrainingDummy::HandleHealthChanged);
	Health->OnDied.AddDynamic(this, &ARTSTrainingDummy::HandleDied);
	Health->SetMaxHealth(MaxHealth);
}

void ARTSTrainingDummy::HandleHealthChanged(URTSHealthComponent* HealthComp, float NewHealth, float Delta)
{
	// Brief white flash on damage (spec: "受击闪白").
	if (Delta < 0.0f && !Health->IsDead() && BodyMID)
	{
		BodyMID->SetVectorParameterValue(TEXT("Color"), FLinearColor::White);
		GetWorldTimerManager().SetTimer(FlashTimer, this, &ARTSTrainingDummy::EndHitFlash, 0.1f, false);
	}
}

void ARTSTrainingDummy::EndHitFlash()
{
	if (BodyMID)
	{
		BodyMID->SetVectorParameterValue(TEXT("Color"), BodyColor);
	}
}

void ARTSTrainingDummy::HandleDied(URTSHealthComponent* HealthComp, AActor* Killer)
{
	GetWorldTimerManager().ClearTimer(FlashTimer);
	EndHitFlash();
	Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Fall away from whoever landed the last hit.
	FVector Away = Killer ? (GetActorLocation() - Killer->GetActorLocation()).GetSafeNormal2D() : FVector::ZeroVector;
	if (Away.IsNearlyZero())
	{
		Away = GetActorForwardVector();
	}
	FallYaw = Away.Rotation().Yaw - GetActorRotation().Yaw;
	FallElapsed = 0.0f;
	SetActorTickEnabled(true);

	GetWorldTimerManager().SetTimer(RespawnTimer, this, &ARTSTrainingDummy::Respawn, FallDuration + RespawnDelay, false);
}

void ARTSTrainingDummy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	FallElapsed += DeltaSeconds;
	const float Alpha = FMath::Clamp(FallElapsed / FMath::Max(FallDuration, 0.01f), 0.0f, 1.0f);
	// Ease-in so it accelerates like a falling post.
	const float Tilt = FallAngle * Alpha * Alpha;
	// Negative pitch tips +Z toward the pivot's +X (the fall direction).
	Pivot->SetRelativeRotation(FRotator(-Tilt, FallYaw, 0.0));

	if (Alpha >= 1.0f)
	{
		SetActorTickEnabled(false);
	}
}

void ARTSTrainingDummy::Respawn()
{
	SetActorTickEnabled(false);
	Pivot->SetRelativeRotation(FRotator::ZeroRotator);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Health->ResetHealth();
}
