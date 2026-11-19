#pragma once
#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

namespace RTSCampaignPalette
{
 inline TAutoConsoleVariable<int32> Purple(TEXT("rts.Campaign.PurpleColor"),0,TEXT("Highland faction palette: 0 purple,1 yellow,2 red,3 pink,4 teal"));
 inline TAutoConsoleVariable<int32> Yellow(TEXT("rts.Campaign.YellowColor"),1,TEXT("Valley faction palette: 0 purple,1 yellow,2 red,3 pink,4 teal"));
 inline TAutoConsoleVariable<int32> Red(TEXT("rts.Campaign.RedColor"),2,TEXT("Red faction palette: 0 purple,1 yellow,2 red,3 pink,4 teal"));
 inline TAutoConsoleVariable<int32> Pink(TEXT("rts.Campaign.PinkColor"),3,TEXT("Pink faction palette: 0 purple,1 yellow,2 red,3 pink,4 teal"));
 inline TAutoConsoleVariable<int32>& Variable(int32 Faction)
 {
  return Faction==0?Purple:Faction==1?Yellow:Faction==2?Red:Pink;
 }
 inline int32 Index(int32 Faction){return FMath::Clamp(Variable(Faction).GetValueOnGameThread(),0,4);}
 inline FLinearColor Color(int32 Faction)
 {
  const FLinearColor Colors[]={FLinearColor(.52f,.12f,.85f),FLinearColor(1,.72f,.035f),FLinearColor(.9f,.04f,.06f),FLinearColor(1,.18f,.55f),FLinearColor(.04f,.8f,.6f)};
  return Colors[Index(Faction)];
 }
 inline FString Name(int32 Faction)
 {
  const TCHAR* Names[]={TEXT("Purple"),TEXT("Yellow"),TEXT("Red"),TEXT("Pink"),TEXT("Teal")};return Names[Index(Faction)];
 }
}
