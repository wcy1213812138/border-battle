#include "Campaign/RTSCampaignCookAssets.h"
#include "Data/RTSInfantryCatalog.h"
#include "UObject/ConstructorHelpers.h"

URTSCampaignCookAssets::URTSCampaignCookAssets()
{
 TSet<FString> Paths;
 for(const auto& Entry:RTSInfantryCatalog::Entries())
 {
  Paths.Add(FString::Printf(TEXT("/Game/Toony_Tiny_RTS_Set/Meshes/Characters_Prebuilt/Units/%s.%s"),Entry.Mesh,Entry.Mesh));
  for(const TCHAR* Suffix:{TEXT("01_idle"),TEXT("03_run"),TEXT("04_attack_A"),TEXT("06_death_A")})
  {
   const FString Name=FString::Printf(TEXT("Anim_%s_%s"),Entry.AnimPrefix,Suffix);
   Paths.Add(FString::Printf(TEXT("/Game/Toony_Tiny_RTS_Set/Animations/Units/%s/%s.%s"),Entry.AnimFolder,*Name,*Name));
  }
 }
 Paths.Add(TEXT("/Game/Toony_Tiny_RTS_Set/Animations/Units/Staff/Anim_staff_07_cast_A.Anim_staff_07_cast_A"));
 for(const TCHAR* Color:{TEXT("Blue"),TEXT("Red"),TEXT("Purple"),TEXT("Yellow"),TEXT("Pink"),TEXT("GreenB")})
  Paths.Add(FString::Printf(TEXT("/Game/Toony_Tiny_RTS_Set/Materials/Units/MI_Units_%s.MI_Units_%s"),Color,Color));
 for(const TCHAR* Shape:{TEXT("Cube"),TEXT("Sphere"),TEXT("Cylinder"),TEXT("Cone"),TEXT("BasicShapeMaterial")})
  Paths.Add(FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"),Shape,Shape));
 for(const FString& Path:Paths)
 {
  ConstructorHelpers::FObjectFinder<UObject> Asset(*Path);
  if(Asset.Object)Assets.Add(Asset.Object);
 }
}
