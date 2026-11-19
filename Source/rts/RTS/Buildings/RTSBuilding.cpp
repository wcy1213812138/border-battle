#include "Buildings/RTSBuilding.h"
#include "Combat/RTSAttackComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSHitFeedbackComponent.h"
#include "Audio/RTSAudioFeedbackComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Economy/RTSProducerComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Units/RTSUnit.h"
#include "UObject/ConstructorHelpers.h"
#include "Economy/RTSResourceNode.h"
#include "Match/RTSMatchSubsystem.h"
#include "Environment/RTSStructureVisuals.h"
#include "Environment/RTSCampaignVisuals.h"
#include "Data/RTSUnitData.h"
#include "Economy/RTSGathererComponent.h"
#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSCampaignRemnants.h"
#include "NavigationSystem.h"

ARTSBuilding::ARTSBuilding()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;

	USceneComponent* GroundRoot = CreateDefaultSubobject<USceneComponent>(TEXT("GroundRoot"));
	SetRootComponent(GroundRoot);
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	Collision->SetupAttachment(GroundRoot);
	Collision->SetBoxExtent(FVector(150.0f, 150.0f, 100.0f));
	Collision->SetRelativeLocation(FVector(0.0f, 0.0f, 100.0f));
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Collision->SetGenerateOverlapEvents(false);
	// The project's dynamic NavMesh updates when this footprint is placed or removed.
	Collision->SetCanEverAffectNavigation(true);

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(GroundRoot);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCanEverAffectNavigation(false);
	Roof = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Roof"));
	Roof->SetupAttachment(GroundRoot);
	Roof->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Roof->SetCanEverAffectNavigation(false);

	// Engine primitives are presentation only; no project art assets drive gameplay.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = Cube.Object;
	CylinderMesh = Cylinder.Object;
	Body->SetStaticMesh(CubeMesh);
	Roof->SetStaticMesh(Cone.Object);
	if (Material.Succeeded())
	{
		Body->SetMaterial(0, Material.Object);
		Roof->SetMaterial(0, Material.Object);
	}

	Health = CreateDefaultSubobject<URTSHealthComponent>(TEXT("Health"));
	Attack = CreateDefaultSubobject<URTSAttackComponent>(TEXT("Attack"));
	Producer = CreateDefaultSubobject<URTSProducerComponent>(TEXT("Producer"));
	HitFeedback = CreateDefaultSubobject<URTSHitFeedbackComponent>(TEXT("HitFeedback"));
	CreateDefaultSubobject<URTSAudioFeedbackComponent>(TEXT("AudioFeedback"));
}

void ARTSBuilding::Configure(FName Id, uint8 Team, bool bInCompleted)
{
	// Configuration is for deferred spawning; never refill a live or damaged building.
	if (HasActorBegunPlay() || bHealthInitialized) { return; }
	BuildingId = Id;
	TeamId = Team;
	bCompleted = bInCompleted;
	ConstructionSeconds = 0.0f;
}

void ARTSBuilding::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	ApplyConfiguration();
}

void ARTSBuilding::BeginPlay()
{
	Super::BeginPlay();
	Health->OnDied.AddUniqueDynamic(this, &ARTSBuilding::HandleDied);
	// Retry if the subsystem was not available in PostInitializeComponents.
	ApplyConfiguration();
	if (ARTSCampaign::Get(this))
	{
		const TCHAR* Label = BuildingId == TEXT("TownHall") ? TEXT("主基地 · 招工 / 设施")
			: BuildingId == TEXT("Barracks") ? TEXT("兵营")
			: BuildingId == TEXT("ArcheryRange") ? TEXT("靶场")
			: BuildingId == TEXT("SupportBarracks") ? TEXT("后勤兵营")
			: BuildingId == TEXT("ResearchInstitute") ? TEXT("研究所 · 成长 / 研究") : nullptr;
		// World labels are drawn by the HUD with the Chinese runtime font; TextRender cannot render CJK.
		CampaignWorldLabel = Label;
	}
}

void ARTSBuilding::ApplyConfiguration()
{
	if (!bHasBalance)
	{
		URTSBalanceSubsystem* Subsystem = URTSBalanceSubsystem::Get(this);
		const FRTSBalanceRow* Row = Subsystem ? Subsystem->Find(BuildingId) : nullptr;
		if (!Row) { return; }
		Balance = *Row;
		bHasBalance = true;
	}

	if (!bHealthInitialized)
	{
		bHealthInitialized = true;
		Health->SetMaxHealth(Balance.MaxHealth);
	}
	Attack->SetStats(Balance.AttackDamage, Balance.AttackRange, Balance.AttackInterval, Balance.AttackHitDelay);
	Attack->SetProjectileStats(Balance.bRangedAttack, Balance.ProjectileSpeed);
	Collision->SetBoxExtent(CollisionExtent(BuildingId, GetFootprint()));
	// SetBoxExtent updates physics/bounds, but not the octree geometry cached
	// when deferred components registered with the constructor's default box.
	// Publish the actual footprint; otherwise small buildings retain an oversized
	// nav hole and melee units can never reach their real wall.
	if(Collision->IsRegistered() && Collision->CanEverAffectNavigation())UNavigationSystemV1::UpdateComponentInNavOctree(*Collision);
	if (bCompleted || Balance.BuildSeconds <= 0.0f)
	{
		bCompleted = true;
		ConstructionSeconds = FMath::Max(0.0f, Balance.BuildSeconds);
	}
	UpdateVisuals();
}

bool ARTSBuilding::IsAlive() const
{
	return bHasBalance && Health && !Health->IsDead() && !IsActorBeingDestroyed();
}

bool ARTSBuilding::IsDropOff() const
{
	return IsAlive() && bCompleted && Balance.bDropOff;
}

float ARTSBuilding::GetBuildProgress() const
{
	if (bCompleted) { return 1.0f; }
	return bHasBalance && Balance.BuildSeconds > 0.0f
		? FMath::Clamp(ConstructionSeconds / Balance.BuildSeconds, 0.0f, 1.0f) : 0.0f;
}

float ARTSBuilding::GetFootprint() const
{
	return bHasBalance ? FMath::Max(1.0f, Balance.Footprint) : 300.0f;
}

FVector ARTSBuilding::CollisionExtent(FName Id, float Footprint)
{
	const float Half = Footprint * 0.5f;
	const float Width = Id == TEXT("Tower") ? 0.62f : 0.88f;
	const float Depth = Id == TEXT("Barracks") ? 0.64f : Width;
	return FVector(Half * Width, Half * Depth, 100);
}

void ARTSBuilding::GetSimpleCollisionCylinder(float& CollisionRadius, float& CollisionHalfHeight) const
{
	const FVector ActorScale = GetActorScale3D().GetAbs();
	const FVector Extent = CollisionExtent(BuildingId, GetFootprint());
	CollisionRadius = FMath::Min(Extent.X, Extent.Y) * FMath::Max(ActorScale.X, ActorScale.Y);
	CollisionHalfHeight = 100.0f * ActorScale.Z;
}

FString ARTSBuilding::GetConstructionStatus() const
{
	if (!IsAlive()) { return TEXT("已摧毁"); }
	if (bCompleted) { return TEXT("已完成"); }
	bool bAssigned = false;
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		if (!It->IsAlive() || It->GetTeam() != TeamId || !It->GetUnitData() || !It->GetUnitData()->bWorker) { continue; }
		const FRTSOrder& Order = It->GetCurrentOrder();
		if (Order.Type != ERTSOrderType::Build || Order.TargetActor.Get() != this) { continue; }
		bAssigned = true;
		if (It->GetGatherer() && It->GetGatherer()->IsWorking()) { return TEXT("正在施工"); }
	}
	return bAssigned ? TEXT("工人前往工地") : TEXT("等待工人｜选工人右键续建");
}


void ARTSBuilding::AddConstructionWork(float Seconds)
{
	if (!IsAlive() || bCompleted || !FMath::IsFinite(Seconds) || Seconds <= 0.0f) { return; }
	const float Required = FMath::Max(0.0f, Balance.BuildSeconds);
	ConstructionSeconds = FMath::Min(ConstructionSeconds + Seconds, Required);
	bCompleted = ConstructionSeconds >= Required;
	if (bCompleted)
	{ if (URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(this)) { Match->NotifyBuildingCompleted(TeamId); } }
	UpdateVisuals();
	OnBuildingChanged.Broadcast();
}
bool ARTSBuilding::RestoreConstructionProgress(float Progress)
{
	if(!IsAlive() || !FMath::IsFinite(Progress) || Progress<0 || Progress>1)return false;
	ConstructionSeconds=FMath::Max(0.f,Balance.BuildSeconds)*Progress;bCompleted=Progress>=1;
	UpdateVisuals();OnBuildingChanged.Broadcast();return true;
}
bool ARTSBuilding::RestoreFoodSource(const FVector& Feet,int32 Remaining,float Remainder,bool bStaged)
{
	if(!IsAlive() || !bCompleted || BuildingId!=TEXT("Farm") || FoodSource.IsValid() || Remaining<0 || Remaining>300 || !FMath::IsFinite(Remainder) || Remainder<0 || Remainder>=1)return false;
	const FTransform Transform(Feet+FVector(0,0,90));
	auto* Node=GetWorld()->SpawnActorDeferred<ARTSResourceNode>(ARTSResourceNode::StaticClass(),Transform,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if(!Node)return false;
	if(bStaged){Node->Tags.AddUnique(TEXT("RTS_CampaignRestoreStaged"));Node->SetActorEnableCollision(false);Node->SetActorHiddenInGame(true);}
	Node->Configure(ERTSResource::Food,300);Node->SetRenewable(true);Node->SetOwningTeam(TeamId);Node->FinishSpawning(Transform);Node->RestoreEconomyState(Remaining,Remainder);
	FoodSource=Node;bFarmSupplied=true;return true;
}

void ARTSBuilding::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	if (TeamId == NewTeamId.GetId()) { return; }
	TeamId = NewTeamId.GetId();
	UpdateVisuals();
}

void ARTSBuilding::UpdateVisuals()
{
	const bool bTower = BuildingId == TEXT("Tower");
	const bool bFarm = BuildingId == TEXT("Farm");
	const bool bBarracks = BuildingId == TEXT("Barracks");
	const float Width = GetFootprint();
	const float Progress = GetBuildProgress();
	if (RTSCampaignVisuals::Building(Body, Roof, BuildingId, TeamId, Width, Progress, AppliedVisualTeam)) { return; }
	// Reuse the detailed structure kits without changing the gameplay building identity.
	const FName VisualId = (BuildingId == TEXT("ArcheryRange") || BuildingId == TEXT("SupportBarracks")) ? FName(TEXT("Barracks"))
		: BuildingId == TEXT("ResearchInstitute") ? FName(TEXT("TownHall")) : BuildingId;
	if (RTSStructureVisuals::Building(Body, Roof, VisualId, TeamId, Width, Progress, AppliedVisualTeam)) { return; }
	const float Growth = FMath::Lerp(0.12f, 1.0f, Progress);
	const float Height = (bTower ? 180.0f : bFarm ? 55.0f : bBarracks ? 100.0f : 130.0f) * Growth;
	const float BodyWidth = Width * (bTower ? 0.55f : 0.85f);
	const float BodyDepth = BodyWidth * (bBarracks ? 0.65f : 1.0f);
	Body->SetStaticMesh(bTower ? CylinderMesh.Get() : CubeMesh.Get());
	Body->SetRelativeScale3D(FVector(BodyWidth / 100.0f, BodyDepth / 100.0f, Height / 100.0f));
	Body->SetRelativeLocation(FVector(0.0f, 0.0f, Height * 0.5f));
	const float RoofHeight = (bTower ? 60.0f : bFarm ? 30.0f : 65.0f) * Growth;
	Roof->SetRelativeScale3D(FVector(BodyWidth / 90.0f, BodyDepth / 90.0f, RoofHeight / 100.0f));
	Roof->SetRelativeLocation(FVector(0.0f, 0.0f, Height + RoofHeight * 0.5f));

	const FLinearColor Color = bTower ? FLinearColor(0.42f, 0.46f, 0.65f)
		: bFarm ? FLinearColor(0.3f, 0.65f, 0.18f)
		: bBarracks ? FLinearColor(0.65f, 0.22f, 0.12f) : FLinearColor(0.75f, 0.56f, 0.2f);
	UMaterialInstanceDynamic* BodyMaterial = Cast<UMaterialInstanceDynamic>(Body->GetMaterial(0));
	if (!BodyMaterial) { BodyMaterial = Body->CreateAndSetMaterialInstanceDynamic(0); }
	if (BodyMaterial) { BodyMaterial->SetVectorParameterValue(TEXT("Color"), Color); }
	UMaterialInstanceDynamic* RoofMaterial = Cast<UMaterialInstanceDynamic>(Roof->GetMaterial(0));
	if (!RoofMaterial) { RoofMaterial = Roof->CreateAndSetMaterialInstanceDynamic(0); }
	if (RoofMaterial) { RoofMaterial->SetVectorParameterValue(TEXT("Color"), Color * 0.65f); }
}

void ARTSBuilding::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsAlive() && bCompleted && BuildingId == TEXT("Farm") && !bFarmSupplied)
	{
		FVector Feet;
		URTSBalanceSubsystem* Subsystem = URTSBalanceSubsystem::Get(this);
		if (Subsystem && Subsystem->FindSpawnPoint(GetActorLocation(), GetFootprint() * 0.5f + 120, Feet))
		{
			const FTransform Transform(Feet + FVector(0, 0, 90));
			ARTSResourceNode* Node = GetWorld()->SpawnActorDeferred<ARTSResourceNode>(ARTSResourceNode::StaticClass(), Transform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Node) { Node->Configure(ERTSResource::Food, 300); Node->SetRenewable(true); Node->SetOwningTeam(TeamId); Node->FinishSpawning(Transform); FoodSource = Node; bFarmSupplied = true; }
		}
	}
	if (!IsAlive() || !bCompleted || BuildingId != TEXT("Tower") || !Attack->IsReady()) { return; }
	ARTSUnit* Nearest = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		ARTSUnit* Unit = *It;
		if (!Unit->IsAlive() || !URTSAttackComponent::IsHostileTarget(this, Unit)
			|| !Attack->IsInRange(Unit) || !Attack->HasLineOfSight(Unit)) { continue; }
		const float Distance = FVector::DistSquared2D(GetActorLocation(), Unit->GetActorLocation());
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Nearest = Unit;
		}
	}
	if (Nearest) { Attack->TryStartAttack(Nearest); }
}

void ARTSBuilding::HandleDied(URTSHealthComponent* HealthComp, AActor* Killer)
{
	if (const auto* Campaign=ARTSCampaign::Get(this))
	{
		UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_BUILDING_DESTROYED kind=%s team=%d faction=%d day=%d killer=%s at=%s"),*BuildingId.ToString(),TeamId,Campaign->GetActorFaction(this),Campaign->GetDay()+1,*GetNameSafe(Killer),*GetActorLocation().ToString());
		// Destroying the actor is not enough for an enemy base: the map authors fort silhouettes and a HUD
		// flag pole beside it, and only this death event can retire them. Losing a TownHall retires the base.
		if(TeamId==1&&Campaign->GetActorFaction(this)>=0)RTSCampaignRemnants::ClearDefeatedFactionBuilding(GetWorld(),Campaign->GetActorFaction(this),BuildingId);
	}
	Attack->CancelAttack();
	Producer->SetComponentTickEnabled(false);
	Producer->Deactivate();
	SetActorTickEnabled(false);
	OnBuildingChanged.Broadcast();
	Destroy(); // Queued production is lost on death; no resource refund.
}

void ARTSBuilding::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (FoodSource.IsValid()) { FoodSource->Destroy(); }
	Attack->CancelAttack();
	Producer->SetComponentTickEnabled(false);
	Health->OnDied.RemoveDynamic(this, &ARTSBuilding::HandleDied);
	Super::EndPlay(EndPlayReason);
}
