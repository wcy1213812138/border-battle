#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "RTSCampaignPackageSmoke.generated.h"

/** Opt-in packaged Development verification. Disabled in both Editor and Shipping. */
UCLASS()
class URTSCampaignPackageSmoke : public UGameInstanceSubsystem, public FTickableGameObject
{
 GENERATED_BODY()
public:
 virtual void Initialize(FSubsystemCollectionBase& Collection) override;
 virtual void Deinitialize() override;
 virtual void Tick(float DeltaTime) override;
 virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(URTSCampaignPackageSmoke,STATGROUP_Tickables); }
 virtual bool IsTickable() const override { return bEnabled && !IsTemplate(); }
 virtual bool IsTickableWhenPaused() const override { return true; }
 virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }
private:
 void Finish(bool bSuccess,const FString& Reason);
 bool EnsureBuilt(class ARTSCampaign* Campaign,FName Kind);
 bool bGuideWoodVerified=false;
 bool bEnabled=false;
 FString Mode;
 int32 Stage=0;
 double Began=0,Phase=0;
 TWeakObjectPtr<class ARTSUnit> Worker;
 TWeakObjectPtr<class ARTSUnit> Attacker;
 TWeakObjectPtr<class ARTSUnit> Target;
 TWeakObjectPtr<class ARTSBuilding> Frontline;
 FVector FrontlinePosition=FVector::ZeroVector;
 bool bConstructionObserved=false;
 int32 BeforeFrontlineTraining=0;
 FVector WorkerStart=FVector::ZeroVector;
 int32 SavedWood=0;
 float InitialTargetHealth=0;
 TWeakObjectPtr<class ARTSUnit> BusyGatherer;
 TWeakObjectPtr<class ARTSBuilding> BusySite;
 int32 BusyCargo=0;
 float BusyBuild=0,BusyTrain=0;
};
