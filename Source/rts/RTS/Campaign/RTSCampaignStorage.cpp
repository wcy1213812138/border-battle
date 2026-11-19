#include "Campaign/RTSCampaignStorage.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/SaveGame.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
 bool Replace(const FString& Destination,const FString& Temporary)
 {
#if PLATFORM_WINDOWS
  // Same-volume rename with replacement: no delete-then-move gap in the primary.
  return ::MoveFileExW(*Temporary,*Destination,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
  return IFileManager::Get().Move(*Destination,*Temporary,true,false,false,true);
#endif
 }
 bool SafeSlot(const FString& Slot)
 {
  if(Slot.IsEmpty() || Slot.Len()>=120)return false;
  for(TCHAR C:Slot)if(!FChar::IsAlnum(C) && C!=TEXT('_') && C!=TEXT('-'))return false;
  return true;
 }
}
USaveGame* RTSCampaignStorage::Read(const FString& Slot)
{
 if(!SafeSlot(Slot))return nullptr;
 const FString Path=FPaths::ProjectSavedDir()/TEXT("SaveGames")/(Slot+TEXT(".sav"));
 const int64 Size=IFileManager::Get().FileSize(*Path);if(Size<64 || Size>4*1024*1024)return nullptr;
 TArray<uint8> Bytes;return FFileHelper::LoadFileToArray(Bytes,*Path)?Decode(Bytes):nullptr;
}
FString RTSCampaignStorage::PreviousSlot(const FString& Slot){return Slot+TEXT("_previous");}
FString RTSCampaignStorage::SlotPath(const FString& Slot)
{
 return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("SaveGames")/(Slot+TEXT(".sav")));
}
bool RTSCampaignStorage::Describe(const FString& Slot,bool& bOutExists,int64& OutSize,FDateTime& OutModified)
{
 bOutExists=false;OutSize=-1;OutModified=FDateTime();
 if(!SafeSlot(Slot))return false;
 IFileManager& Files=IFileManager::Get();
 const FString Path=SlotPath(Slot);
 if(!Files.FileExists(*Path))return true;
 bOutExists=true;OutSize=Files.FileSize(*Path);OutModified=Files.GetTimeStamp(*Path);
 return true;
}
bool RTSCampaignStorage::Delete(const FString& Slot,FString& Error)
{
 Error.Reset();
 if(!SafeSlot(Slot)){Error=TEXT("存档槽名称无效，未删除任何记录。");return false;}
 IFileManager& Files=IFileManager::Get();
 const FString Path=SlotPath(Slot);
 // The .pending staging file belongs to this slot; a cancelled write must not resurrect a deleted record.
 if(Files.FileExists(*Path) && !Files.Delete(*Path,false,true,true))
 {Error=TEXT("无法删除存档文件；可能仍被另一个进程占用。");return false;}
 Files.Delete(*(Path+TEXT(".pending")),false,true,true);
 return true;
}
bool RTSCampaignStorage::Write(USaveGame* Save,const FString& Slot,TFunctionRef<bool(const USaveGame*)> Validate,FString& Error)
{
 Error.Reset();
 if(!SafeSlot(Slot) || !Save || !Validate(Save)){Error=TEXT("存档槽或快照无效，已有记录未改变。");return false;}
 TArray<uint8> Bytes;
 if(!Encode(Save,Bytes)){Error=TEXT("快照序列化失败，已有记录未改变。");return false;}
 const FString Folder=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("SaveGames"));
 const FString Primary=Folder/(Slot+TEXT(".sav")),Backup=Folder/(PreviousSlot(Slot)+TEXT(".sav"));
 const FString Temporary=Primary+TEXT(".pending"),BackupTemporary=Backup+TEXT(".pending");
 IFileManager& Files=IFileManager::Get();
 if(!Files.MakeDirectory(*Folder,true)){Error=TEXT("无法创建存档目录。");return false;}
 ON_SCOPE_EXIT { Files.Delete(*Temporary,false,true,true);Files.Delete(*BackupTemporary,false,true,true); };
 TArray<uint8> ReadBack;
 if(!FFileHelper::SaveArrayToFile(Bytes,*Temporary) || !FFileHelper::LoadFileToArray(ReadBack,*Temporary) || ReadBack!=Bytes || !Validate(Decode(ReadBack)))
 {Error=TEXT("存档写入校验失败，已有记录未改变。");return false;}
 if(Files.FileExists(*Primary))
 {
  TArray<uint8> Previous;
  const int64 Size=Files.FileSize(*Primary);
  if(Size>=0 && Size<=4*1024*1024 && !FFileHelper::LoadFileToArray(Previous,*Primary)){Error=TEXT("无法读取原记录以保护备份，已有记录未改变。");return false;}
  // A corrupt primary never overwrites the last good backup.
  if(Validate(Decode(Previous)))
  {
   if(!FFileHelper::SaveArrayToFile(Previous,*BackupTemporary) || !FFileHelper::LoadFileToArray(ReadBack,*BackupTemporary) || ReadBack!=Previous || !Replace(Backup,BackupTemporary))
   {Error=TEXT("上一份有效记录备份失败，已有记录未改变。");return false;}
  }
 }
 if(!Replace(Primary,Temporary)){Error=TEXT("替换存档失败，原记录仍保留。");return false;}
 return true;
}
