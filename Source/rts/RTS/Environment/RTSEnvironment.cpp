#include "Environment/RTSEnvironment.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/IConsoleManager.h"

namespace
{
	const FRotator SunRotation(-52.f, -32.f, 0.f);
	const FLinearColor SunColor(1.f, 0.95f, 0.84f);
	const FLinearColor SkyColor(0.88f, 0.94f, 1.f);
	template<typename T> T* Asset(const TCHAR* Name)
	{ return LoadObject<T>(nullptr, *FString::Printf(TEXT("/Game/RTS/Environment/%s.%s"), Name, Name)); }
}
ARTSEnvironment::ARTSEnvironment()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	SetActorEnableCollision(false);
}
ARTSEnvironment* ARTSEnvironment::Get(const UObject* Context)
{
	UWorld* World = Context ? Context->GetWorld() : nullptr;
	if (World) { for (TActorIterator<ARTSEnvironment> It(World); It; ++It) { if (!It->IsActorBeingDestroyed()) { return *It; } } }
	return nullptr;
}
ARTSEnvironment* ARTSEnvironment::Ensure(UWorld* World)
{
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer || !World->GetMapName().EndsWith(TEXT("L_M0Test"))) { return nullptr; }
	if (ARTSEnvironment* Existing = Get(World)) { return Existing; }
	return World->SpawnActor<ARTSEnvironment>();
}
void ARTSEnvironment::BeginPlay()
{
	Super::BeginPlay();
	if (GetWorld()->IsGameWorld() && GetWorld()->GetMapName().EndsWith(TEXT("L_M0Test"))) { SetPresentationEnabled(true); }
}
UMaterialInstanceDynamic* ARTSEnvironment::Tint(UMaterialInterface* Parent, FLinearColor Color)
{
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, this);
	Material->SetVectorParameterValue(TEXT("Color"), Color); Palette.Add(Material); return Material;
}
UInstancedStaticMeshComponent* ARTSEnvironment::Batch(FName Name, UStaticMesh* Mesh, UMaterialInterface* Material)
{
	UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(this, Name);
	AddInstanceComponent(Component); Component->SetupAttachment(GetRootComponent());
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision); Component->SetGenerateOverlapEvents(false);
	Component->SetCanEverAffectNavigation(false); Component->SetCastShadow(false);
	Component->SetStaticMesh(Mesh); Component->SetMaterial(0, Material);
	Component->RegisterComponent(); Batches.Add(Component); return Component;
}
bool ARTSEnvironment::CreateScenery()
{
	if (bBuilt) { return true; }
	UMaterialInterface* Surface = Asset<UMaterialInterface>(TEXT("M_RTSScenery"));
	UMaterialInterface* Terrain = Asset<UMaterialInterface>(TEXT("M_RTSTerrain"));
	UStaticMesh* Crown = Asset<UStaticMesh>(TEXT("SM_PineCrown"));
	UStaticMesh* Trunk = Asset<UStaticMesh>(TEXT("SM_PineTrunk"));
	UStaticMesh* Rock = Asset<UStaticMesh>(TEXT("SM_FacetedRock"));
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Surface || !Terrain || !Crown || !Trunk || !Rock || !Cube)
	{ UE_LOG(LogTemp, Warning, TEXT("MiniRTS环境资产缺失，保留原场景表现。")); return false; }
	Ground = Tint(Terrain, FLinearColor(0.16f, 0.235f, 0.105f));
	Stone = Tint(Surface, FLinearColor(0.255f, 0.29f, 0.27f));
	Sandstone = Tint(Surface, FLinearColor(0.37f, 0.335f, 0.25f));
	UInstancedStaticMeshComponent* Trunks = Batch(TEXT("SceneryTrunks"), Trunk, Tint(Surface, FLinearColor(0.19f, 0.10f, 0.045f)));
	UInstancedStaticMeshComponent* Dark = Batch(TEXT("SceneryPinesDark"), Crown, Tint(Surface, FLinearColor(0.055f, 0.15f, 0.10f)));
	UInstancedStaticMeshComponent* Light = Batch(TEXT("SceneryPinesLight"), Crown, Tint(Surface, FLinearColor(0.11f, 0.23f, 0.12f)));
	UInstancedStaticMeshComponent* Rocks = Batch(TEXT("SceneryRocks"), Rock, Stone);
	UInstancedStaticMeshComponent* Apron = Batch(TEXT("SceneryApron"), Cube, Tint(Terrain, FLinearColor(0.12f, 0.17f, 0.085f)));
	// This surface is BELOW the original floor, and is never walkable or traceable.
	Apron->AddInstance(FTransform(FRotator::ZeroRotator, FVector(0, 0, -85), FVector(120, 120, 0.5f)));
	FRandomStream Random(51031);
	for (int32 Side = 0; Side < 4; ++Side)
	{
		const FRotator Rotation(0, Side * 90.f, 0);
		for (int32 i = 0; i < 48; ++i)
		{
			const float Scale = Random.FRandRange(0.85f, 1.35f);
			const FVector Position = Rotation.RotateVector(FVector(Random.FRandRange(2500.f, 4400.f), -4400.f + i * 185.f, -60.f));
			const FRotator Yaw(0, Random.FRandRange(0, 360), 0);
			Trunks->AddInstance(FTransform(Yaw, Position, FVector(Scale)));
			Dark->AddInstance(FTransform(Yaw, Position + FVector(0, 0, 100 * Scale), FVector(Scale * 1.15f, Scale * 1.15f, Scale)));
			Light->AddInstance(FTransform(Yaw, Position + FVector(0, 0, 210 * Scale), FVector(Scale * 0.80f)));
		}
		for (int32 i = 0; i < 18; ++i)
		{
			const FVector Position = Rotation.RotateVector(FVector(Random.FRandRange(2350.f, 2800.f), -4200.f + i * 485.f, -65.f));
			Rocks->AddInstance(FTransform(FRotator(0, Random.FRandRange(0, 360), 0), Position,
				FVector(Random.FRandRange(0.65f, 1.5f), Random.FRandRange(0.7f, 1.3f), Random.FRandRange(0.6f, 1.2f))));
		}
	}
	bBuilt = true; return true;
}
void ARTSEnvironment::ApplySurfaces()
{
	Surfaces.Reset();
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		UStaticMeshComponent* Mesh = It->GetStaticMeshComponent();
		if (!Mesh || !Mesh->GetStaticMesh()) { continue; }
		const FString Path = Mesh->GetStaticMesh()->GetPathName();
		const bool bFloor = It->GetActorNameOrLabel() == TEXT("RTS Arena Ground")
			|| Path == TEXT("/Engine/MapTemplates/SM_Template_Map_Floor.SM_Template_Map_Floor");
		if (!bFloor && !Path.StartsWith(TEXT("/Game/LevelPrototyping/Meshes/"))) { continue; }
		UMaterialInterface* Material = bFloor ? Ground.Get() : Path.Contains(TEXT("Ramp")) ? Sandstone.Get() : Stone.Get();
		for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
		{
			FRTSEnvironmentSurface& Saved = Surfaces.AddDefaulted_GetRef();
			Saved.Mesh = Mesh; Saved.Slot = Slot; Saved.Original = Mesh->GetMaterial(Slot); Saved.Applied = Material;
			Mesh->SetMaterial(Slot, Material);
		}
	}
}
void ARTSEnvironment::RestoreSurfaces()
{
	for (const FRTSEnvironmentSurface& Saved : Surfaces)
	{
		if (Saved.Mesh.IsValid() && Saved.Mesh->GetMaterial(Saved.Slot) == Saved.Applied)
		{ Saved.Mesh->SetMaterial(Saved.Slot, Saved.Original); }
	}
	Surfaces.Reset();
}
void ARTSEnvironment::ApplyLighting()
{
	if (bLightingApplied) { return; }
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		Sun = Cast<UDirectionalLightComponent>(It->GetLightComponent());
		if (!Sun.IsValid()) { continue; }
		OriginalSunRotation = Sun->GetComponentRotation(); OriginalSunColor = Sun->GetLightColor();
		Sun->SetWorldRotation(SunRotation); Sun->SetLightColor(SunColor); AppliedSunColor = Sun->GetLightColor(); break;
	}
	for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It)
	{
		Sky = It->GetLightComponent();
		if (!Sky.IsValid()) { continue; }
		OriginalSkyColor = Sky->GetLightColor(); Sky->SetLightColor(SkyColor); AppliedSkyColor = Sky->GetLightColor(); break;
	}
	if (!PostProcess.IsValid())
	{
		PostProcess = GetWorld()->SpawnActor<APostProcessVolume>();
		if (PostProcess.IsValid())
		{
			PostProcess->SetOwner(this); PostProcess->SetActorEnableCollision(false);
			PostProcess->bUnbound = true; PostProcess->Priority = 1.f;
			FPostProcessSettings& Settings = PostProcess->Settings;
			Settings.bOverride_BloomIntensity = true; Settings.BloomIntensity = 0.12f;
			Settings.bOverride_MotionBlurAmount = true; Settings.MotionBlurAmount = 0.f;
			Settings.bOverride_VignetteIntensity = true; Settings.VignetteIntensity = 0.12f;
			Settings.bOverride_AutoExposureBias = true; Settings.AutoExposureBias = -0.15f;
		}
	}
	if (PostProcess.IsValid()) { PostProcess->BlendWeight = 1.f; }
	bLightingApplied = true;
}
void ARTSEnvironment::RestoreLighting()
{
	if (!bLightingApplied) { return; }
	if (Sun.IsValid())
	{
		if (Sun->GetComponentRotation().Equals(SunRotation, 0.01f)) { Sun->SetWorldRotation(OriginalSunRotation); }
		if (Sun->GetLightColor().Equals(AppliedSunColor, 0.001f)) { Sun->SetLightColor(OriginalSunColor); }
	}
	if (Sky.IsValid() && Sky->GetLightColor().Equals(AppliedSkyColor, 0.001f)) { Sky->SetLightColor(OriginalSkyColor); }
	if (PostProcess.IsValid()) { PostProcess->BlendWeight = 0.f; }
	Sun.Reset(); Sky.Reset(); bLightingApplied = false;
}
bool ARTSEnvironment::SetPresentationEnabled(bool bValue)
{
	if (bValue && (!GetWorld() || !GetWorld()->IsGameWorld() || !GetWorld()->GetMapName().EndsWith(TEXT("L_M0Test")))) { return false; }
	if (bEnabled == bValue) { return true; }
	if (bValue && !CreateScenery()) { return false; }
	bEnabled = bValue;
	if (bValue) { ApplySurfaces(); if (bWantLighting) { ApplyLighting(); } }
	else { RestoreSurfaces(); RestoreLighting(); }
	for (UInstancedStaticMeshComponent* Component : Batches) { Component->SetVisibility(bValue); }
	return true;
}
void ARTSEnvironment::SetLightingEnabled(bool bValue)
{
	bWantLighting = bValue;
	if (bEnabled && bValue) { ApplyLighting(); } else { RestoreLighting(); }
}
int32 ARTSEnvironment::GetDecorationCount() const
{
	int32 Count = 0;
	for (const UInstancedStaticMeshComponent* Component : Batches) { Count += Component->GetInstanceCount(); }
	return Count;
}
void ARTSEnvironment::EndPlay(const EEndPlayReason::Type Reason)
{
	SetPresentationEnabled(false);
	if (PostProcess.IsValid()) { PostProcess->Destroy(); }
	Super::EndPlay(Reason);
}
namespace
{
	void EnvironmentCommand(const TArray<FString>& Args, UWorld* World, bool bLighting)
	{
		ARTSEnvironment* Environment = ARTSEnvironment::Get(World);
		if (!Environment) { return; }
		if (Args.Num() != 1 || (Args[0] != TEXT("0") && Args[0] != TEXT("1")))
		{ UE_LOG(LogTemp, Display, TEXT("用法：rts.Environment 0/1 或 rts.EnvironmentLighting 0/1；仅影响本次运行表现。")); return; }
		if (bLighting) { Environment->SetLightingEnabled(Args[0] == TEXT("1")); }
		else { Environment->SetPresentationEnabled(Args[0] == TEXT("1")); }
	}
	FAutoConsoleCommandWithWorldAndArgs GEnvironment(TEXT("rts.Environment"), TEXT("切换当前运行的环境表现：0原灰盒，1草地石墙与边界树林。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { EnvironmentCommand(Args, World, false); }));
	FAutoConsoleCommandWithWorldAndArgs GEnvironmentLighting(TEXT("rts.EnvironmentLighting"), TEXT("切换环境日光色调和轻量后处理：0原光照，1环境光照。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { EnvironmentCommand(Args, World, true); }));
}
