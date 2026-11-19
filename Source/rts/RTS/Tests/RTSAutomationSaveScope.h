#pragma once
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Editor.h"
#include "Campaign/RTSCampaign.h"

// Keep this alive until AFTER FEndPlayMapCommand: EndPlay may still save.
class FRTSAutomationSaveScope
{
 FString PreviousBase;
 int32 PreviousActive=0;
 bool Cleaned=false;
public:
 const FString Slot;
 explicit FRTSAutomationSaveScope(const TCHAR* Label)
  :PreviousBase(ARTSCampaign::BaseSlotName()),PreviousActive(ARTSCampaign::ActiveSlot()),
   Slot(FString(TEXT("Automation_"))+Label+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits))
 {ARTSCampaign::SetSaveSlotOverride(*Slot);ARTSCampaign::SetActiveSlot(0);}
 void Cleanup()
 {
  if(Cleaned || (GEditor && GEditor->PlayWorld))return;
  const FString Directory=FPaths::ProjectSavedDir()/TEXT("SaveGames");
  TArray<FString> Files;IFileManager::Get().FindFiles(Files,*(Directory/TEXT("*")),true,false);
  for(const FString& File:Files)
   if(File.StartsWith(Slot+TEXT(".")) || File.StartsWith(Slot+TEXT("_")))
    IFileManager::Get().Delete(*(Directory/File),false,true);
  ARTSCampaign::SetSaveSlotOverride(*PreviousBase);ARTSCampaign::SetActiveSlot(PreviousActive);Cleaned=true;
 }
 ~FRTSAutomationSaveScope(){Cleanup();}
};
class FRTSAutomationSaveCleanup : public IAutomationLatentCommand
{
 TSharedPtr<FRTSAutomationSaveScope> Scope;
public:
 explicit FRTSAutomationSaveCleanup(TSharedPtr<FRTSAutomationSaveScope> In):Scope(MoveTemp(In)){}
 bool Update() override
 {if(GEditor && GEditor->PlayWorld)return false;Scope->Cleanup();return true;}
};
#endif
