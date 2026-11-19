#pragma once
#include "CoreMinimal.h"
#include "Core/RTSTypes.h"
#include "RTSEconomySnapshot.generated.h"

USTRUCT()
struct FRTSGathererSnapshot
{
 GENERATED_BODY()
 UPROPERTY() int32 Carried=0;
 UPROPERTY() ERTSResource CargoType=ERTSResource::Wood;
 UPROPERTY() bool bReturning=false;
 UPROPERTY() float WorkElapsed=0;
};
USTRUCT()
struct FRTSPaidTrainingSnapshot
{
 GENERATED_BODY()
 UPROPERTY() FName Kind;
 UPROPERTY() TMap<ERTSResource,int32> Cost;
 UPROPERTY() float TrainSeconds=0;
};
USTRUCT()
struct FRTSProductionSnapshot
{
 GENERATED_BODY()
 UPROPERTY() TArray<FRTSPaidTrainingSnapshot> Orders;
 UPROPERTY() float TrainingSeconds=0;
 UPROPERTY() bool bSpawnBlocked=false;
};

USTRUCT()
struct FRTSResourceCaptureSnapshot
{
 GENERATED_BODY()
 UPROPERTY() float Seconds=0;
 UPROPERTY() uint8 Team=RTSTeam::Neutral;
};
