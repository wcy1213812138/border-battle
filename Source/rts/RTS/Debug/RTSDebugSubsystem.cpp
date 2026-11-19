#include "Debug/RTSDebugSubsystem.h"
#include "Match/RTSMatchSubsystem.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Economy/RTSResourceComponent.h"
#include "Buildings/RTSBuilding.h"
#include "Units/RTSUnit.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"

URTSDebugSubsystem* URTSDebugSubsystem::Get(const UObject* Context)
{
	UWorld* World = Context ? Context->GetWorld() : nullptr;
	return World ? World->GetSubsystem<URTSDebugSubsystem>() : nullptr;
}
bool URTSDebugSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{ return Type == EWorldType::Game || Type == EWorldType::PIE; }
void URTSDebugSubsystem::TogglePanel()
{
#if !UE_BUILD_SHIPPING
	bPanelOpen = !bPanelOpen;
#endif
}
bool URTSDebugSubsystem::CanMutate(FString& Error) const
{
	Error.Reset();
#if UE_BUILD_SHIPPING
	Error = TEXT("发行版本不提供调试操作。"); return false;
#else
	const URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(this);
	if (!Match || !Match->AllowsGameplay() || UGameplayStatics::IsGamePaused(GetWorld()))
	{ Error = TEXT("请在未暂停的沙盒或对局中使用调试操作。"); return false; }
	return true;
#endif
}
bool URTSDebugSubsystem::AddResources(FString& Error)
{
	if (!CanMutate(Error)) { return false; }
	URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(this);
	URTSResourceComponent* Stock = Balance ? Balance->GetResources(0) : nullptr;
	if (!Stock) { Error = TEXT("己方资源账户尚未就绪。"); return false; }
	URTSMatchSubsystem::Get(this)->MarkDebugUsed();
	Stock->Add(ERTSResource::Wood, 500); Stock->Add(ERTSResource::Food, 500);
	return true;
}
ARTSUnit* URTSDebugSubsystem::SpawnUnit(FName Id, FString& Error)
{
	if (!CanMutate(Error)) { return nullptr; }
	if (Id != TEXT("Peasant") && Id != TEXT("Swordman") && Id != TEXT("Archer"))
	{ Error = TEXT("请选择工人、剑士或弓箭手。"); return nullptr; }
	int32 Count = 0;
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It) { if (It->IsAlive()) { ++Count; } }
	if (Count >= 200) { Error = TEXT("当前存活单位已达200，暂停调试刷兵。"); return nullptr; }
	FVector Center = FVector::ZeroVector; float Radius = 200; float ProjectionWidth = 60;
	if (ARTSBuilding* Base = URTSMatchSubsystem::Get(this)->GetMainBase(0))
	{ Center = Base->GetActorLocation(); Radius += Base->GetFootprint() * 0.5f; ProjectionWidth = Radius; }
	else if (APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->GetPawn())
	{ Center = PC->GetPawn()->GetActorLocation(); }
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Ground;
	if (!Nav || !Nav->ProjectPointToNavigation(Center, Ground, FVector(ProjectionWidth, ProjectionWidth, 400)))
	{ Error = TEXT("镜头或基地附近没有导航地面，请移到可通行区域。"); return nullptr; }
	Center = Ground.Location;
	URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(this);
	FVector Feet;
	if (!Balance || !Balance->FindSpawnPoint(Center, Radius, Feet))
	{ Error = TEXT("附近没有可通行的刷兵位置，请移动镜头或腾出基地出口。"); return nullptr; }
	ARTSUnit* Unit = Balance->SpawnUnit(Id, 0, Feet);
	if (!Unit) { Error = TEXT("单位生成失败，未修改资源。"); return nullptr; }
	URTSMatchSubsystem::Get(this)->MarkDebugUsed();
	return Unit;
}
bool URTSDebugSubsystem::SetSpeed(float Value, FString& Error)
{
	if (!CanMutate(Error)) { return false; }
	if (Value != 0.5f && Value != 1.f && Value != 2.f)
	{ Error = TEXT("时间倍率仅支持0.5、1或2倍。"); return false; }
	if (!FMath::IsNearlyEqual(Value, GetSpeed()))
	{
		UGameplayStatics::SetGlobalTimeDilation(GetWorld(), Value);
		URTSMatchSubsystem::Get(this)->MarkDebugUsed();
	}
	return true;
}
float URTSDebugSubsystem::GetSpeed() const { return UGameplayStatics::GetGlobalTimeDilation(GetWorld()); }
void URTSDebugSubsystem::RestoreSpeed() { UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 1.f); }
void URTSDebugSubsystem::ResetSession() { RestoreSpeed(); bPanelOpen = false; }
