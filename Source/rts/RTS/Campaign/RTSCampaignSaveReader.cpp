#include "Campaign/RTSCampaignStorage.h"
#include "Campaign/RTSCampaign.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/SecureHash.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyTypeName.h"
#include "UObject/ObjectVersion.h"

namespace
{
 constexpr int32 MaxFileBytes=4*1024*1024;
 constexpr int32 FooterBytes=36;
 constexpr uint8 FooterMagic[8]={'R','T','S','S','A','V','1','2'};

 // Never hand untrusted bytes to UObject::Serialize. Its FString/FName, container and
 // property-type readers can assert or allocate before reporting an archive error.
 // This deliberately supports only the tagged, pointer-free campaign schema we own.
 class FSnapshotReader
 {
  const TArray<uint8>& Bytes;
  int32 Position=0,Limit=0,Budget=50000;
 public:
  explicit FSnapshotReader(const TArray<uint8>& In,int32 End):Bytes(In),Limit(End){}
  int32 Tell() const{return Position;}
  template<typename T> bool Number(T& Value)
  {
   if(Position<0 || Position>Limit || int64(sizeof(T))>Limit-Position)return false;
   FMemory::Memcpy(&Value,Bytes.GetData()+Position,sizeof(T));Position+=sizeof(T);return true;
  }
  bool Skip(int32 Count){if(Count<0 || Count>Limit-Position)return false;Position+=Count;return true;}
  bool String(FString& Value,int32 Max=1023)
  {
   int32 Count=0;if(!Number(Count) || Count==MIN_int32)return false;Value.Reset();
   const int64 Length=FMath::Abs(int64(Count));const int32 Width=Count<0?2:1;
   if(Length>Max+1 || Length*Width>Limit-Position)return false;
   if(!Length)return true;
   Value.Reserve(int32(Length)-1);
   for(int32 I=0;I<Length;++I)
   {
    uint16 Char=0;if(Width==2){if(!Number(Char))return false;}else{uint8 Byte=0;if(!Number(Byte))return false;Char=Byte;}
    if(I==Length-1){if(Char!=0)return false;}else{if(Char==0)return false;Value.AppendChar(TCHAR(Char));}
   }
   return true;
  }
  bool Type(FString& Text,int32 Depth=0)
  {
   if(Depth>8 || --Budget<0)return false;
   FString Name;int32 Children=0;if(!String(Name,128) || Name.IsEmpty() || !Number(Children) || Children<0 || Children>8)return false;
   Text+=Name;
   if(Children){Text+=TEXT("(");for(int32 I=0;I<Children;++I){if(I)Text+=TEXT(",");if(!Type(Text,Depth+1))return false;}Text+=TEXT(")");}
   return true;
  }
  bool EnumValue(UEnum* Enum,int64& Value)
  {
   FString Name;if(!Enum || !String(Name,128))return false;
   Value=Enum->GetValueByNameString(Name,EGetByNameFlags::None);
   return Value!=INDEX_NONE && Enum->IsValidEnumValue(Value) && Value!=Enum->GetMaxEnumValue();
  }
  bool Value(FProperty* Property,void* Destination,int32 Depth)
  {
   if(Depth>12 || --Budget<0)return false;
   if(auto* P=CastField<FEnumProperty>(Property)){int64 V=0;if(!EnumValue(P->GetEnum(),V))return false;P->GetUnderlyingProperty()->SetIntPropertyValue(Destination,V);return true;}
   if(auto* P=CastField<FByteProperty>(Property))
   {
    if(P->Enum){int64 V=0;if(!EnumValue(P->Enum,V) || V<0 || V>255)return false;P->SetPropertyValue(Destination,uint8(V));return true;}
    uint8 V=0;if(!Number(V))return false;P->SetPropertyValue(Destination,V);return true;
   }
   if(auto* P=CastField<FBoolProperty>(Property)){uint8 V=0;if(!Number(V) || V>1)return false;P->SetPropertyValue(Destination,V!=0);return true;}
   if(auto* P=CastField<FIntProperty>(Property)){int32 V=0;if(!Number(V))return false;P->SetPropertyValue(Destination,V);return true;}
   if(auto* P=CastField<FFloatProperty>(Property)){float V=0;if(!Number(V) || !FMath::IsFinite(V))return false;P->SetPropertyValue(Destination,V);return true;}
   if(auto* P=CastField<FDoubleProperty>(Property)){double V=0;if(!Number(V) || !FMath::IsFinite(V))return false;P->SetPropertyValue(Destination,V);return true;}
   if(auto* P=CastField<FNameProperty>(Property)){FString V;if(!String(V,128))return false;P->SetPropertyValue(Destination,FName(*V));return true;}
   if(auto* P=CastField<FStrProperty>(Property)){FString V;if(!String(V,128))return false;P->SetPropertyValue(Destination,V);return true;}
   if(auto* P=CastField<FStructProperty>(Property))
   {
    if(P->Struct==TBaseStructure<FVector>::Get()){FVector V;if(!Number(V.X) || !Number(V.Y) || !Number(V.Z) || V.ContainsNaN())return false;*static_cast<FVector*>(Destination)=V;return true;}
    return Properties(P->Struct,Destination,Depth+1);
   }
   if(auto* P=CastField<FArrayProperty>(Property))
   {
    int32 Count=0;if(!Number(Count) || Count<0 || Count>1000 || Count>Limit-Position)return false;
    FScriptArrayHelper Array(P,Destination);Array.Resize(Count);
    for(int32 I=0;I<Count;++I)if(!Value(P->Inner,Array.GetRawPtr(I),Depth+1))return false;
    return true;
   }
   if(auto* P=CastField<FMapProperty>(Property))
   {
    int32 Removed=0,Count=0;if(!Number(Removed) || (Removed!=0 && Removed!=-1) || !Number(Count) || Count<0 || Count>64 || Count>Limit-Position)return false;
    FScriptMapHelper Map(P,Destination);Map.EmptyValues(Count);
    for(int32 I=0;I<Count;++I)
    {
     const int32 Index=Map.AddDefaultValue_Invalid_NeedsRehash();
     if(!Value(P->KeyProp,Map.GetKeyPtr(Index),Depth+1) || !Value(P->ValueProp,Map.GetValuePtr(Index),Depth+1))return false;
     for(int32 J=0;J<Index;++J)if(P->KeyProp->Identical(Map.GetKeyPtr(J),Map.GetKeyPtr(Index)))return false;
    }
    Map.Rehash();return true;
   }
   return false; // No object references, native serializers or arbitrary property conversion.
  }
  bool Properties(const UStruct* Struct,void* Destination,int32 Depth)
  {
   TSet<FString> Seen;
   for(int32 Tags=0;Tags<64;++Tags)
   {
    FString Name;if(!String(Name,128))return false;if(Name==TEXT("None"))return true;
    if(Name.IsEmpty() || Seen.Contains(Name))return false;Seen.Add(Name);
    FProperty* Property=nullptr;for(TFieldIterator<FProperty> It(Struct);It;++It)if(It->GetName()==Name){Property=*It;break;}
    if(!Property || Property->ArrayDim!=1)return false;
    FString TypeName;int32 Size=0;uint8 Flags=0;
    if(!Type(TypeName) || !Number(Size) || Size<0 || !Number(Flags) || (Flags&~uint8(0x18)) || Size>Limit-Position)return false;
    TStringBuilder<256> Expected;Expected<<UE::FPropertyTypeName(Property);if(TypeName!=Expected.ToString())return false;
    void* Target=Property->ContainerPtrToValuePtr<void>(Destination);
    if(auto* Bool=CastField<FBoolProperty>(Property)){if(Size!=0)return false;Bool->SetPropertyValue(Target,(Flags&0x10)!=0);continue;}
    if(Flags&0x10)return false;
    const int32 OldLimit=Limit;Limit=Position+Size;
    if(!Value(Property,Target,Depth) || Position!=Limit)return false;
    Limit=OldLimit;
   }
   return false;
  }
  bool Header()
  {
   uint32 Magic=0;int32 Format=0,UE4=0,UE5=0;uint16 Major=0,Minor=0,Patch=0;uint32 Change=0;FString Branch,Class;
   if(!Number(Magic) || Magic!=0x53415647 || !Number(Format) || Format!=3 || !Number(UE4) || UE4!=522 || !Number(UE5) || UE5!=int32(EUnrealEngineObjectUE5Version::AUTOMATIC_VERSION))return false;
   if(!Number(Major) || !Number(Minor) || !Number(Patch) || Major!=5 || Minor!=8 || !Number(Change) || !String(Branch,128))return false;
   int32 CustomFormat=0,Count=0;
   if(!Number(CustomFormat) || CustomFormat!=3 || !Number(Count) || Count<0 || Count>512 || !Skip(Count*20))return false;
   return String(Class,128) && Class==TEXT("/Script/rts.RTSCampaignSave");
  }
 };
}

bool RTSCampaignStorage::Encode(USaveGame* Save,TArray<uint8>& Bytes)
{
 Bytes.Reset();
 if(!Cast<URTSCampaignSave>(Save) || !UGameplayStatics::SaveGameToMemory(Save,Bytes) || Bytes.Num()>MaxFileBytes-FooterBytes)return false;
 const uint32 Version=1,Length=Bytes.Num();uint8 Digest[FSHA1::DigestSize];FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num(),Digest);
 Bytes.Append(FooterMagic,8);Bytes.Append(reinterpret_cast<const uint8*>(&Version),4);Bytes.Append(reinterpret_cast<const uint8*>(&Length),4);Bytes.Append(Digest,FSHA1::DigestSize);return true;
}
USaveGame* RTSCampaignStorage::Decode(const TArray<uint8>& Bytes)
{
 if(Bytes.Num()<64 || Bytes.Num()>MaxFileBytes)return nullptr;
 int32 End=Bytes.Num();bool bIntegrity=false;
 if(Bytes.Num()>=FooterBytes && FMemory::Memcmp(Bytes.GetData()+Bytes.Num()-FooterBytes,FooterMagic,8)==0)
 {
  End-=FooterBytes;uint32 Version=0,Length=0;FMemory::Memcpy(&Version,Bytes.GetData()+End+8,4);FMemory::Memcpy(&Length,Bytes.GetData()+End+12,4);
  uint8 Digest[FSHA1::DigestSize];FSHA1::HashBuffer(Bytes.GetData(),End,Digest);
  if(Version!=1 || Length!=uint32(End) || FMemory::Memcmp(Digest,Bytes.GetData()+End+16,FSHA1::DigestSize)!=0)return nullptr;
  bIntegrity=true;
 }
 FSnapshotReader Reader(Bytes,End);if(!Reader.Header())return nullptr;
 // Native campaign saves have no class-control extensions or persistent object GUID.
 uint8 Control=0;if(!Reader.Number(Control) || Control!=0)return nullptr;
 auto* Save=NewObject<URTSCampaignSave>();
 if(!Reader.Properties(URTSCampaignSave::StaticClass(),Save,0))return nullptr;
 int32 GuidPresent=0;if(!Reader.Number(GuidPresent) || GuidPresent!=0 || Reader.Tell()!=End)return nullptr;
 if(Save->Version<3 || Save->Version>URTSCampaignSave::SnapshotVersion || (Save->Version>=12 && !bIntegrity))return nullptr;
 return Save;
}
