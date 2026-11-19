#include "Economy/RTSResourceNode.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Environment/RTSStructureVisuals.h"
#include "Environment/RTSCampaignVisuals.h"
#include "Units/RTSUnit.h"
#include "Data/RTSUnitData.h"
#include "EngineUtils.h"

namespace
{
	UStaticMesh* MeshPath(const TCHAR* Path) { return Path ? LoadObject<UStaticMesh>(nullptr, Path) : nullptr; }
	UMaterialInterface* BasicShapeMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> Cached;
		if (!Cached.IsValid()) { Cached = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")); }
		return Cached.Get();
	}
	UMaterialInterface* StructureMaterial(const TCHAR* Name)
	{
		return LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/RTS/Structures/%s.%s"), Name, Name));
	}
	FLinearColor ResourceColor(ERTSResource Type)
	{
		switch (Type)
		{
		case ERTSResource::Wood: return FLinearColor(0.10f, 0.42f, 0.08f);
		case ERTSResource::Food: return FLinearColor(0.85f, 0.62f, 0.10f);
		case ERTSResource::IronOre: return FLinearColor(0.34f, 0.42f, 0.52f);
		case ERTSResource::RefinedIron: return FLinearColor(0.62f, 0.70f, 0.78f);
		case ERTSResource::Amber: return FLinearColor(0.95f, 0.42f, 0.05f);
		case ERTSResource::Crystal:return FLinearColor(.12f,.7f,1.f);
		case ERTSResource::Gold:return FLinearColor(1.f,.65f,.05f);
		case ERTSResource::Mana:return FLinearColor(.6f,.15f,1.f);
		default: return FLinearColor(0.6f, 0.6f, 0.6f);
		}
	}
	/** Only presentation: bigger pads/rings for higher tiers. Collision and nav footprint never change. */
	float PadRadius(int32 Tier, bool bVineyard)
	{
		if (bVineyard) return 330.f;
		return Tier >= 3 ? 245.f : Tier == 2 ? 190.f : 150.f;
	}
}

ARTSResourceNode::ARTSResourceNode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	PrimaryActorTick.bStartWithTickEnabled = false;
	Collision = CreateDefaultSubobject<UCapsuleComponent>(TEXT("ResourceCollision"));
	SetRootComponent(Collision);
	Collision->InitCapsuleSize(45, 90);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);
	Collision->SetCanEverAffectNavigation(true);
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ResourceVisual"));
	Visual->SetupAttachment(Collision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (Cone.Succeeded()) { Visual->SetStaticMesh(Cone.Object); }
	if (Material.Succeeded()) { Visual->SetMaterial(0, Material.Object); }
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCanEverAffectNavigation(false);
}
bool ARTSResourceNode::UpdateCampaignCapture(float Seconds)
{
 if(!bTierConfigured || OwningTeam==254){CaptureSeconds=0;CapturingTeam=RTSTeam::Neutral;return false;}
 bool Present[2]={false,false};
 for(TActorIterator<ARTSUnit> It(GetWorld());It;++It)
 {
  const auto* D=It->GetUnitData();const uint8 Team=It->GetTeam();
  if(!It->IsAlive() || Team>1 || !D || D->bWorker || D->bHealer || D->AttackDamage<=0)continue;
  const FVector Feet=It->GetActorLocation()-FVector(0,0,It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
  if(FVector::Dist2D(Feet,GetActorLocation())<700 && FMath::Abs(Feet.Z-(GetActorLocation().Z-90))<140)Present[Team]=true;
 }
 if(Present[0] && Present[1])return false;
 const uint8 Team=Present[0]?0:Present[1]?1:RTSTeam::Neutral;
 if(Team==RTSTeam::Neutral || Team==OwningTeam){CaptureSeconds=0;CapturingTeam=RTSTeam::Neutral;return false;}
 if(CapturingTeam!=Team){CapturingTeam=Team;CaptureSeconds=0;}
 CaptureSeconds=FMath::Min(5.f,CaptureSeconds+FMath::Max(0.f,Seconds));
 if(CaptureSeconds<5)return false;
 SetOwningTeam(Team);CaptureSeconds=0;CapturingTeam=RTSTeam::Neutral;return true;
}
void ARTSResourceNode::Configure(ERTSResource Type, int32 Amount)
{
	ResourceType = Type; Remaining = FMath::Max(0, Amount); Capacity = Remaining; RecoveryRemainder = 0;
	if (bVisualReady) { ApplyResourceVisual(); BuildDecorations(); LabelRemaining = -1; RefreshLabel(); }
}
void ARTSResourceNode::ApplyOreVisual()
{
	Visual->SetStaticMesh(MeshPath(ResourceType==ERTSResource::Amber || ResourceType==ERTSResource::Crystal || ResourceType==ERTSResource::Mana?TEXT("/Game/RTS/Environment/SM_FacetedRock.SM_FacetedRock"):TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	Visual->SetRelativeLocation(FVector(0,0,-30));
	Visual->SetRelativeScale3D(ResourceType==ERTSResource::Gold?FVector(2,2,.8):FVector(1.3,1.3,2.1));
	Visual->SetMaterial(0,BasicShapeMaterial());
	if(auto* Material=Visual->CreateAndSetMaterialInstanceDynamic(0))Material->SetVectorParameterValue(TEXT("Color"),ResourceColor(ResourceType));
}
void ARTSResourceNode::ApplyResourceVisual()
{
	if (!Visual) { return; }
	FVector BaseScale = Visual->GetRelativeScale3D();
	FVector BaseLocation = Visual->GetRelativeLocation();
	if (bVineyard)
	{
		// Reuse the existing campaign field mesh as the bed, then dress it with vines/fruit below.
		if (UStaticMesh* Field = MeshPath(TEXT("/Game/RTS/CampaignArt/SM_ResourceFood.SM_ResourceFood"))) { Visual->SetStaticMesh(Field); }
		BaseLocation = FVector(0, 0, -90);
		BaseScale = FVector(2.4, 2.4, 1.6);
		if (UMaterialInterface* Soil = StructureMaterial(TEXT("MI_Structure_soil"))) { Visual->SetMaterial(0, Soil); }
	}
	if (!bVineyard && RTSCampaignVisuals::Resource(Visual, ResourceType))
	{
		BaseScale = FVector::OneVector; BaseLocation = FVector(0, 0, -90);
	}
	else if (!bVineyard && ResourceType >= ERTSResource::IronOre)
	{
		ApplyOreVisual();
		BaseScale = Visual->GetRelativeScale3D(); BaseLocation = Visual->GetRelativeLocation();
	}
	else if (!bVineyard && RTSStructureVisuals::Resource(Visual, ResourceType == ERTSResource::Wood))
	{
		BaseScale = Visual->GetRelativeScale3D();
		BaseLocation = Visual->GetRelativeLocation();
	}
	else if (!bVineyard)
	{
		BaseScale = ResourceType == ERTSResource::Wood ? FVector(1.2, 1.2, 2.3) : FVector(1.2, 1.2, 0.8);
		BaseLocation = FVector::ZeroVector;
		Visual->SetRelativeLocation(BaseLocation);
		if (UMaterialInstanceDynamic* Material = Visual->CreateAndSetMaterialInstanceDynamic(0))
		{ Material->SetVectorParameterValue(TEXT("Color"), ResourceType == ERTSResource::Wood ? FLinearColor(0.1f, 0.45f, 0.05f) : FLinearColor(0.9f, 0.5f, 0.08f)); }
	}
	// Tier grading is visual-only: the collision capsule and nav footprint stay 45/90.
	const float TierScale = (bTierConfigured && !bVineyard) ? (ResourceTier >= 3 ? 1.8f : ResourceTier == 2 ? 1.5f : 1.25f) : 1.0f;
	Visual->SetRelativeLocation(BaseLocation);
	Visual->SetRelativeScale3D(BaseScale * TierScale);
}
void ARTSResourceNode::BeginPlay()
{
	Super::BeginPlay();
	ApplyResourceVisual();
	bVisualReady = true;
	BuildDecorations();
	LabelRemaining = -1;
	RefreshLabel();
}
UStaticMeshComponent* ARTSResourceNode::AddVisualPart(FName PartName, const TCHAR* MeshPathName, const FVector& Location, const FVector& Scale, const FRotator& Rotation, UMaterialInterface* Material)
{
	if (!Collision) { return nullptr; }
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, PartName);
	if (!Part) { return nullptr; }
	AddInstanceComponent(Part);
	Part->SetupAttachment(Collision);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetCanEverAffectNavigation(false);
	Part->SetGenerateOverlapEvents(false);
	Part->SetStaticMesh(MeshPath(MeshPathName));
	Part->SetMaterial(0, Material ? Material : BasicShapeMaterial());
	Part->SetRelativeLocation(Location);
	Part->SetRelativeScale3D(Scale);
	Part->SetRelativeRotation(Rotation);
	Part->RegisterComponent();
	return Part;
}
void ARTSResourceNode::SetPartColor(UStaticMeshComponent* Part, const FLinearColor& Color)
{
	if (!Part) { return; }
	if (UMaterialInstanceDynamic* M = Part->CreateAndSetMaterialInstanceDynamic(0)) { M->SetVectorParameterValue(TEXT("Color"), Color); }
}
void ARTSResourceNode::BuildDecorations()
{
	if (!bTierConfigured || !bVisualReady || !Collision) { return; }
	if (GroundPad && DecorTier == ResourceTier && bDecorVineyard == bVineyard)
	{
		for (TObjectPtr<UStaticMeshComponent>& Bar : SelectBars) { if (Bar) { Bar->SetVisibility(bSelected, true); } }
		return;
	}
	// Rebuild only when the tier/vineyard identity changes; components are presentation-only.
	if (GroundPad) { GroundPad->DestroyComponent(); GroundPad = nullptr; }
	for (TObjectPtr<UStaticMeshComponent>& Bar : SelectBars) { if (Bar) { Bar->DestroyComponent(); } }
	SelectBars.Reset();
	for (TObjectPtr<UStaticMeshComponent>& Part : VineParts) { if (Part) { Part->DestroyComponent(); } }
	VineParts.Reset();
	DecorTier = ResourceTier; bDecorVineyard = bVineyard;

	const float Radius = PadRadius(ResourceTier, bVineyard);
	GroundPad = AddVisualPart(TEXT("ResourceGroundPad"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), FVector(0, 0, -88),
		FVector(Radius / 50.f, Radius / 50.f, 0.06f), FRotator::ZeroRotator, bVineyard ? StructureMaterial(TEXT("MI_Structure_soil")) : nullptr);
	if (!bVineyard && GroundPad) { SetPartColor(GroundPad, ResourceColor(ResourceType) * 0.75f); }

	// Eight short bars around the pad read as a selection ring once the node is selected.
	for (int32 I = 0; I < 8; ++I)
	{
		const float Angle = 22.5f + 45.f * I;
		const FVector Offset = FVector(FMath::Cos(FMath::DegreesToRadians(Angle)), FMath::Sin(FMath::DegreesToRadians(Angle)), 0) * (Radius + 20.f);
		UStaticMeshComponent* Bar = AddVisualPart(*FString::Printf(TEXT("ResourceSelectBar%d"), I), TEXT("/Engine/BasicShapes/Cube.Cube"),
			FVector(Offset.X, Offset.Y, -84), FVector((Radius * 0.92f) / 100.f, 0.12f, 0.05f), FRotator(0, Angle + 90.f, 0), nullptr);
		if (Bar) { SetPartColor(Bar, FLinearColor(1.f, 0.92f, 0.55f)); Bar->SetVisibility(bSelected, true); SelectBars.Add(Bar); }
	}

	if (bVineyard)
	{
		UMaterialInterface* Leaf = StructureMaterial(TEXT("MI_Structure_leaf"));
		UMaterialInterface* Fruit = StructureMaterial(TEXT("MI_Structure_fruit"));
		// Three vine rows plus posts and grape clusters; all non-colliding visual parts.
		for (int32 Row = 0; Row < 3; ++Row)
		{
			UStaticMeshComponent* Bed = AddVisualPart(*FString::Printf(TEXT("ResourceVineRow%d"), Row), TEXT("/Engine/BasicShapes/Cube.Cube"),
				FVector(0, (Row - 1) * 150.f, -72), FVector(4.6f, 0.5f, 0.7f), FRotator::ZeroRotator, Leaf ? Leaf : nullptr);
			if (Bed && !Leaf) { SetPartColor(Bed, FLinearColor(0.12f, 0.45f, 0.10f)); }
			if (Bed) { VineParts.Add(Bed); }
		}
		for (int32 I = 0; I < 6; ++I)
		{
			const float X = I < 3 ? -180.f : 180.f;
			const float Y = ((I % 3) - 1) * 150.f;
			UStaticMeshComponent* Post = AddVisualPart(*FString::Printf(TEXT("ResourceVinePost%d"), I), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
				FVector(X, Y, -30), FVector(0.12f, 0.12f, 1.0f), FRotator::ZeroRotator, nullptr);
			if (Post) { SetPartColor(Post, FLinearColor(0.28f, 0.18f, 0.09f)); VineParts.Add(Post); }
			UStaticMeshComponent* Cluster = AddVisualPart(*FString::Printf(TEXT("ResourceGrapeCluster%d"), I), TEXT("/Engine/BasicShapes/Sphere.Sphere"),
				FVector(X, Y, -45), FVector(0.30f, 0.30f, 0.36f), FRotator::ZeroRotator, Fruit ? Fruit : nullptr);
			if (Cluster && !Fruit) { SetPartColor(Cluster, FLinearColor(0.32f, 0.06f, 0.34f)); }
			if (Cluster) { VineParts.Add(Cluster); }
		}
	}

	LabelRemaining = -1;
	RefreshLabel();
}
void ARTSResourceNode::RefreshLabel()
{
	if (LabelRemaining == Remaining && LabelOwiningTeam == OwningTeam) { return; }
	LabelRemaining = Remaining; LabelOwiningTeam = OwningTeam;
	// Campaign labels are drawn by the HUD with the Chinese runtime font: TextRender only ships Latin/EURO fonts.
	WorldLabelText = FString::Printf(TEXT("%s · %d级"), *GetResourceDisplayName(), ResourceTier);
	WorldLabelText += FString::Printf(TEXT("\n%d/%d"), Remaining, Capacity);
	if (IsRenewable()) { WorldLabelText += FString::Printf(TEXT("  恢复%.1f/秒"), GetRecoveryPerSecond()); }
	if (bTierConfigured) { WorldLabelText += OwningTeam == RTSTeam::Player ? TEXT("\n我方控制 · 可采集") : TEXT("\n可直接采集 · 注意敌军袭击"); }
	WorldLabelColor = ResourceTier >= 3 ? FLinearColor(1.f, 0.82f, 0.35f) : ResourceTier == 2 ? FLinearColor(0.78f, 0.92f, 0.70f) : FLinearColor::White;
}
FLinearColor ARTSResourceNode::GetWorldLabelColor() const { return WorldLabelColor; }
void ARTSResourceNode::SetOwningTeam(uint8 Team)
{
	if (OwningTeam == Team) { return; }
	OwningTeam = Team;
	CaptureSeconds=0;CapturingTeam=RTSTeam::Neutral;
	LabelRemaining = -1;
	RefreshLabel();
	OnResourceChanged.Broadcast();
}
void ARTSResourceNode::ConfigureResourceTier(int32 Tier, bool bVineyardIn)
{
	ResourceTier = FMath::Clamp(Tier, 1, 3);
	bVineyard = bVineyardIn;
	bTierConfigured = true;
	LabelRemaining = -1;
	if (bVisualReady) { ApplyResourceVisual(); BuildDecorations(); RefreshLabel(); }
}
void ARTSResourceNode::SetSelected(bool bValue)
{
	bSelected = bValue;
	for (TObjectPtr<UStaticMeshComponent>& Bar : SelectBars) { if (Bar) { Bar->SetVisibility(bSelected, true); } }
}
FString ARTSResourceNode::GetResourceDisplayName() const
{
	if (bVineyard) { return OwningTeam == RTSTeam::Player ? TEXT("葡萄种植园") : TEXT("野生葡萄园"); }
	switch (ResourceType)
	{
	case ERTSResource::Wood: return ResourceTier >= 3 ? TEXT("古木林") : ResourceTier == 2 ? TEXT("密林") : TEXT("树林");
	case ERTSResource::Food: return ResourceTier >= 3 ? TEXT("丰产麦田") : ResourceTier == 2 ? TEXT("改良麦田") : TEXT("小麦田");
	case ERTSResource::IronOre: return ResourceTier >= 3 ? TEXT("富铁矿脉") : ResourceTier == 2 ? TEXT("铁矿脉") : TEXT("铁矿点");
	case ERTSResource::RefinedIron: return ResourceTier >= 3 ? TEXT("精铁富矿") : ResourceTier == 2 ? TEXT("精铁矿脉") : TEXT("精铁堆");
	case ERTSResource::Amber: return ResourceTier >= 3 ? TEXT("富琥珀矿脉") : ResourceTier == 2 ? TEXT("琥珀矿脉") : TEXT("琥珀结块");
	case ERTSResource::Crystal: return TEXT("水晶矿脉");
	case ERTSResource::Gold: return TEXT("金币矿脉");
	case ERTSResource::Mana: return TEXT("法力泉");
	default: return TEXT("资源点");
	}
}
FString ARTSResourceNode::GetResourcePortraitPath() const
{
	// Campaign-art portrait for the vineyard node.
	if (bVineyard) { return TEXT("/Game/RTS/CampaignArt/UI/T_ResourceVineyard_Portrait"); }
	const TCHAR* Name = ResourceType == ERTSResource::Wood ? TEXT("Wood") : ResourceType == ERTSResource::Food ? TEXT("Food")
		: ResourceType == ERTSResource::IronOre ? TEXT("IronOre") : ResourceType == ERTSResource::RefinedIron ? TEXT("RefinedIron")
		: ResourceType == ERTSResource::Amber ? TEXT("Amber") : ResourceType==ERTSResource::Crystal?TEXT("Crystal"):ResourceType==ERTSResource::Gold?TEXT("Gold"):ResourceType==ERTSResource::Mana?TEXT("Mana"):nullptr;
	return Name ? FString::Printf(TEXT("/Game/RTS/CampaignArt/UI/T_Resource%s_Portrait"), Name) : FString();
}
int32 ARTSResourceNode::Extract(int32 Requested)
{
	const int32 Taken = FMath::Clamp(Requested, 0, Remaining);
	Remaining -= Taken;
	if (Taken > 0)
	{
		if (Remaining == 0 && !bRenewable) { SetActorHiddenInGame(true); SetActorEnableCollision(false); }
		LabelRemaining = -1; RefreshLabel();
		OnResourceChanged.Broadcast();
	}
	return Taken;
}

void ARTSResourceNode::SetRenewable(bool bValue)
{
	bRenewable = bValue;
	SetActorTickEnabled(bValue);
	RecoveryRemainder = 0;
	if (bValue) { SetActorHiddenInGame(false); SetActorEnableCollision(true); }
}

void ARTSResourceNode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	RefreshLabel();
	if (!bRenewable || Remaining >= Capacity) { RecoveryRemainder = 0; return; }
	RecoveryRemainder += FMath::Max(0.f, DeltaSeconds) * RecoveryPerSecond;
	const int32 Restored = FMath::Min(Capacity - Remaining, FMath::FloorToInt(RecoveryRemainder));
	if (Restored > 0)
	{
		Remaining += Restored; RecoveryRemainder -= Restored;
		if (Remaining == Capacity) { RecoveryRemainder = 0; }
		LabelRemaining = -1; RefreshLabel();
		OnResourceChanged.Broadcast();
	}
}

void ARTSResourceNode::ConfigureEconomy(float Multiplier,float Recovery,int32 MaxAmount,int32 Slots)
{
	GatherMultiplier=FMath::IsFinite(Multiplier)?FMath::Clamp(Multiplier,.1f,100.f):1;
	RecoveryPerSecond=FMath::IsFinite(Recovery)?FMath::Max(0.f,Recovery):0;
	Capacity=FMath::Max(1,MaxAmount); Remaining=FMath::Min(Remaining,Capacity); GatherSlots=FMath::Max(0,Slots);
	LabelRemaining=-1; RefreshLabel();
}
void ARTSResourceNode::RestoreEconomyState(int32 Amount,float Remainder)
{
	Remaining=FMath::Clamp(Amount,0,Capacity); RecoveryRemainder=FMath::IsFinite(Remainder)?FMath::Clamp(Remainder,0.f,.99999f):0;
	LabelRemaining=-1; RefreshLabel();
}
bool ARTSResourceNode::TryReserveGatherer(ARTSUnit* Unit)
{
	Gatherers.RemoveAll([this](const TWeakObjectPtr<ARTSUnit>& W){ return !W.IsValid() || !W->IsAlive() || W->GetCurrentOrder().Type!=ERTSOrderType::Gather || W->GetCurrentOrder().TargetActor.Get()!=this; });
	if (!Unit || !Unit->IsAlive()) return false;
	if (GatherSlots==0 || Gatherers.Contains(Unit)) return true;
	if (Gatherers.Num()>=GatherSlots) return false;
	Gatherers.Add(Unit); return true;
}
FRTSResourceCaptureSnapshot ARTSResourceNode::CaptureOwnership() const
{
	FRTSResourceCaptureSnapshot S;S.Seconds=CaptureSeconds;S.Team=CapturingTeam;return S;
}
bool ARTSResourceNode::IsValidOwnership(const FRTSResourceCaptureSnapshot& S,uint8 Owner)
{
	return FMath::IsFinite(S.Seconds) && S.Seconds>=0 && S.Seconds<5 && (S.Seconds==0?S.Team==RTSTeam::Neutral:(S.Team<=1 && S.Team!=Owner && Owner!=254));
}
bool ARTSResourceNode::RestoreOwnership(const FRTSResourceCaptureSnapshot& S)
{
	if(!IsValidOwnership(S,OwningTeam))return false;CaptureSeconds=S.Seconds;CapturingTeam=S.Team;return true;
}
