#include "Units/RTSPeasantShowcase.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Data/RTSInfantryCatalog.h"

void ARTSPeasantShowcase::BeginPlay()
{
	Super::BeginPlay();
	BuildTierAppearance();
}

void ARTSPeasantShowcase::RefreshTier(int32 Value)
{
	Value = FMath::Clamp(Value, 1, 5);
	if (PreviewTier == Value) { return; }
	PreviewTier = Value;
	if (!HasActorBegunPlay()) { return; }
	TArray<UActorComponent*> Parts = GetInstanceComponents();
	for (UActorComponent* Part : Parts)
	{
		if (Part && (Part->GetName().StartsWith(TEXT("TierPart_")) || Part == BackFrame || Part == CrownFrame))
		{ RemoveInstanceComponent(Part); Part->DestroyComponent(); }
	}
	BackFrame = nullptr; CrownFrame = nullptr;
	BuildTierAppearance();
}

void ARTSPeasantShowcase::BuildTierAppearance()
{
	USkeletalMeshComponent* CharacterMesh = GetMesh();
	CharacterMesh->RefreshBoneTransforms();
	const FLinearColor Gold(0.95f, 0.56f, 0.08f);
	const RTSInfantryCatalog::FEntry* Entry = RTSInfantryCatalog::Find(PreviewKind);
	const FString Style = Entry ? Entry->Style : TEXT("Worker");
	const bool bSword = Style == TEXT("Heavy");
	const bool bArcher = Style == TEXT("Ranged");
	const bool bCaster = Style == TEXT("Caster");
	const FLinearColor Cloth = PreviewTier == 5 ? FLinearColor(0.38f, 0.08f, 0.72f) : PreviewTier == 4 ? FLinearColor(0.8f, 0.025f, 0.06f) : FLinearColor(0.025f, 0.32f, 0.85f);
	const FLinearColor Trim = PreviewTier == 5 ? FLinearColor(0.7f, 0.95f, 1.0f) : PreviewTier == 4 ? Gold : FLinearColor(0.65f, 0.8f, 0.9f);
	UMaterialInterface* Matte = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RTS/Materials/M_RTSHitFlash.M_RTSHitFlash"));
	if (!Glow) { Glow = Matte; }
	TMap<FLinearColor, UMaterialInstanceDynamic*> MatteMaterials;
	TMap<FLinearColor, UMaterialInstanceDynamic*> GlowMaterials;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	int32 PartIndex = 0;
	auto Part = [&](const TCHAR* Shape, USceneComponent* Parent, const FVector& Position, const FVector& Scale,
		const FRotator& Rotation, FLinearColor Color, bool bGlow = false)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("TierPart_%d"), PartIndex++));
		AddInstanceComponent(Component);
		Component->SetupAttachment(Parent);
		Component->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Shape));
		Component->SetRelativeLocation(Position); Component->SetRelativeScale3D(Scale); Component->SetRelativeRotation(Rotation);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision); Component->SetCanEverAffectNavigation(false);
		Component->SetCastShadow(false);
		Component->SetVisibleInRayTracing(false);
		Component->bAffectDistanceFieldLighting = false;
		Component->bAffectDynamicIndirectLighting = false;
		auto& Cache = bGlow ? GlowMaterials : MatteMaterials;
		UMaterialInstanceDynamic*& MID = Cache.FindOrAdd(Color);
		if (!MID) { MID = UMaterialInstanceDynamic::Create(bGlow ? Glow : Matte, this); MID->SetVectorParameterValue(TEXT("Color"), Color); }
		Component->SetMaterial(0, MID); Component->RegisterComponent();
		return Component;
	};
	// Actor-space centimetres: do not inherit imported bone scale/orientation.
	auto BoneFrame = [&](const TCHAR* FrameName)
	{
		USceneComponent* Frame = NewObject<USceneComponent>(this, FName(FrameName));
		AddInstanceComponent(Frame); Frame->SetupAttachment(GetRootComponent());
		Frame->SetAbsolute(false, false, true); Frame->RegisterComponent();
		return Frame;
	};
	const TCHAR* Cube = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* Sphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const TCHAR* Cylinder = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* Cone = TEXT("/Engine/BasicShapes/Cone.Cone");
	if (PreviewTier >= 2)
	{
		BackFrame = BoneFrame(TEXT("PreviewBackFrame"));
		USceneComponent* Back = BackFrame;
		// Front clothing details share the upper-body frame, visual-only.
		const FLinearColor Leather(0.33f, 0.19f, 0.08f);
		if (PreviewTier == 2)
		{
			for (int32 Side : {-1, 1})
			{
				Part(Cube, Back, FVector(46, Side * 12, -19), FVector(0.045, 0.065, 0.43), FRotator(0, 0, Side * 10), Leather);
			}
			Part(Cube, Back, FVector(43, 22, -48), FVector(0.16, 0.20, 0.22), FRotator::ZeroRotator, Leather);
		}
		else
		{
			Part(Cube, Back, FVector(46, 0, -23), FVector(0.055, PreviewTier == 3 ? 0.29 : 0.39, 0.43), FRotator::ZeroRotator, Cloth);
			Part(Cube, Back, FVector(50, 0, -25), FVector(0.035, 0.055, 0.38), FRotator::ZeroRotator, Trim);
			Part(Cube, Back, FVector(46, 0, -47), FVector(0.08, 0.47, 0.075), FRotator::ZeroRotator, Trim);
			Part(Sphere, Back, FVector(53, 0, -13), FVector(0.08, 0.16, 0.18), FRotator::ZeroRotator, Trim);
			if (PreviewTier >= 4)
			{
				for (int32 Side : {-1, 1})
				{
					Part(Cube, Back, FVector(48, Side * 16, -60), FVector(0.045, 0.115, 0.27), FRotator(0, 0, Side * 9), Cloth);
					Part(Cube, Back, FVector(51, Side * 16, -72), FVector(0.035, 0.14, 0.055), FRotator::ZeroRotator, Trim);
				}
			}
			if (PreviewTier == 5)
			{
				Part(Sphere, Back, FVector(58, 0, -13), FVector(0.07, 0.10, 0.12), FRotator::ZeroRotator, FLinearColor(0.06f, 0.8f, 0.9f), true);
			}
		}
		if (bSword)
		{
			const FLinearColor Metal = PreviewTier < 4 ? FLinearColor(0.4f, 0.55f, 0.68f) : Trim;
			Part(Cube, Back, FVector(51, 0, -24), FVector(0.10, 0.46, 0.45), FRotator::ZeroRotator, Metal);
			for (int32 Side : {-1, 1})
			{
				Part(Sphere, Back, FVector(17, Side * 31, -2), FVector(0.39, 0.34, 0.25), FRotator::ZeroRotator, Metal);
				Part(Cube, Back, FVector(44, Side * 24, -54), FVector(0.10, 0.18, 0.28), FRotator(0, 0, Side * 14), Metal);
			}
		}
		if (bArcher)
		{
			Part(Cube, Back, FVector(51, 0, -24), FVector(0.055, 0.065, 0.48), FRotator(0, 0, 30), Leather);
			Part(Cylinder, Back, FVector(-12, -29, -5), FVector(0.18, 0.18, 0.60), FRotator(0, 0, -18), Leather);
			for (int32 Arrow : {-1, 1})
			{
				Part(Cube, Back, FVector(-12, -29 + Arrow * 5, 28), FVector(0.018, 0.018, 0.36), FRotator(0, 0, -18), Trim);
				Part(Cone, Back, FVector(-12, -29 + Arrow * 5, 47), FVector(0.055, 0.055, 0.09), FRotator::ZeroRotator, Trim);
			}
		}
		if (Style == TEXT("Light") || bCaster)
		{
			Part(Cube, Back, FVector(48, 0, -22), FVector(0.06, bCaster ? 0.36 : 0.24, bCaster ? 0.60 : 0.40), FRotator::ZeroRotator, PreviewTier < 3 ? Leather : Cloth);
			Part(Sphere, Back, FVector(52, 0, -10), FVector(0.07, 0.14, 0.16), FRotator::ZeroRotator, Trim);
		}
		if (PreviewTier == 2 && PreviewKind == TEXT("Peasant"))
		{
			Part(Cube, Back, FVector(-12, 0, -12), FVector(0.36, 0.52, 0.55), FRotator::ZeroRotator, FLinearColor(0.72f, 0.30f, 0.055f));
			Part(Cylinder, Back, FVector(-10, 0, 23), FVector(0.22, 0.22, 0.66), FRotator(0, 0, 90), FLinearColor(0.9f, 0.68f, 0.25f));
			Part(Cube, Back, FVector(-17, 32, 4), FVector(0.055, 0.055, 0.85), FRotator(0, 0, 22), FLinearColor(0.33f, 0.19f, 0.08f));
			for (int32 Side : {-1, 1})
			{
				Part(Cube, Back, FVector(-32, Side * 17, -12), FVector(0.025, 0.055, 0.58), FRotator::ZeroRotator, FLinearColor(0.9f, 0.68f, 0.25f));
			}
		}
		else if (PreviewTier >= 3)
		{
			const float Width = (PreviewTier == 3 ? 1.15f : PreviewTier == 4 ? 1.35f : 1.5f) * (bArcher ? 0.82f : 1.0f);
			const float Length = bArcher ? (PreviewTier == 3 ? 0.49f : 0.62f) : PreviewTier == 3 ? 0.70f : 0.88f;
			for (int32 Strip = -2; Strip <= 2; ++Strip)
			{
				const float Side = Strip * 11.0f * Width;
				Part(Cube, Back, FVector(-15, Side, -23), FVector(0.07, 0.13 * Width, Length), FRotator(-24, Strip * 5, 0), Cloth);
				Part(Cube, Back, FVector(-33, Side * 1.3f, -23 - Length * 50), FVector(0.08, 0.18 * Width, 0.14), FRotator(-24, Strip * 5, 0), Trim);
			}
			for (int32 Side : {-1, 1})
			{
				Part(Sphere, Back, FVector(18, Side * 29, 1), FVector(0.36, 0.34, 0.22), FRotator::ZeroRotator, Trim);
				Part(Cube, Back, FVector(-24, Side * 26 * Width, -22), FVector(0.09, 0.055, Length), FRotator(-24, Side * 10, 0), Trim);
			}
			Part(Cube, Back, FVector(-35, 0, -22), FVector(0.09, 0.20, 0.38), FRotator(-24, 0, 0), Trim);
			Part(Cube, Back, FVector(-37, 0, -14), FVector(0.10, 0.36, 0.085), FRotator(-24, 0, 0), Trim);
		}
	}
	if (PreviewTier >= 4)
	{
		CrownFrame = BoneFrame(TEXT("PreviewCrownFrame"));
		USceneComponent* Crown = CrownFrame;
		for (int32 Index = 0; Index < 10; ++Index)
		{
			const double Angle = Index * UE_TWO_PI / 10;
			const FVector Radial(FMath::Cos(Angle), FMath::Sin(Angle), 0);
			Part(Cube, Crown, Radial * 18, FVector(0.085, 0.14, 0.10), FRotator(0, FMath::RadiansToDegrees(Angle), 0), Trim);
			if (Index % 2 == 0) { Part(Cone, Crown, Radial * 18 + FVector(0, 0, 14), FVector(0.12, 0.12, PreviewTier == 5 ? 0.34 : 0.25), FRotator::ZeroRotator, Trim); }
		}
		Part(Sphere, Crown, FVector(21, 0, 4), FVector(0.12, 0.12, 0.18), FRotator::ZeroRotator, FLinearColor(0.06f, 0.8f, 0.9f), PreviewTier == 5);
	}
	UpdateAccessoryFrames();
}

void ARTSPeasantShowcase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateAccessoryFrames();
}

void ARTSPeasantShowcase::UpdateAccessoryFrames()
{
	const FRotator Facing = GetActorRotation();
	const FVector Feet = GetActorLocation() - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	auto Place = [&](USceneComponent* Frame, FName Bone, float Height, FVector Offset)
	{
		if (!Frame) { return; }
		FVector Anchor = GetMesh()->GetSocketLocation(Bone);
		// Guard against an unevaluated/off-screen animation pose at scene startup.
		if (Anchor.ContainsNaN() || Anchor.Z < Feet.Z + 50 || Anchor.Z > Feet.Z + 220
			|| FVector::Dist2D(Anchor, Feet) > 80) { Anchor = Feet + FVector(0, 0, Height); }
		Frame->SetWorldLocationAndRotation(Anchor + Facing.RotateVector(Offset), Facing);
		Frame->SetWorldScale3D(FVector::OneVector);
	};
	Place(BackFrame, TEXT("Bip001-Neck"), 128, FVector(-23, 0, -8));
	Place(CrownFrame, TEXT("Bip001-Head"), 145, FVector(0, 0, 45));
}
