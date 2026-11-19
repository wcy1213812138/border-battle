#pragma once
#include "CoreMinimal.h"
class USaveGame;

/** Synchronous, same-directory replacement. Never deletes the last valid primary first. */
namespace RTSCampaignStorage
{
 // Bounded campaign-only reader; raw UE deserialization is not safe for corrupt files.
 bool Encode(USaveGame* Save,TArray<uint8>& Bytes);
 USaveGame* Decode(const TArray<uint8>& Bytes);
 USaveGame* Read(const FString& Slot);
 bool Write(USaveGame* Save,const FString& Slot,TFunctionRef<bool(const USaveGame*)> Validate,FString& Error);
 FString PreviousSlot(const FString& Slot);
 /** Absolute path of a slot file, whether it currently exists or not. */
 FString SlotPath(const FString& Slot);
 /** Availability plus size/time of a slot file; bOutExists stays false when the file is missing. */
 bool Describe(const FString& Slot,bool& bOutExists,int64& OutSize,FDateTime& OutModified);
 /** Deletes exactly one slot file; the caller decides which backups belong to that chain. */
 bool Delete(const FString& Slot,FString& Error);
}
