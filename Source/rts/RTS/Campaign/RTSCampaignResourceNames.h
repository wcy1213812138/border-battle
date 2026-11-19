#pragma once
#include "Core/RTSTypes.h"
namespace RTSCampaignResourceNames
{
 inline const TCHAR* Name(ERTSResource R){const TCHAR* Names[]={TEXT("木材"),TEXT("食物"),TEXT("铁矿"),TEXT("精铁"),TEXT("琥珀"),TEXT("水晶"),TEXT("金币"),TEXT("法力")};const int32 I=int32(R);return I>=0 && I<8?Names[I]:TEXT("未知");}
 inline FString Cost(const TMap<ERTSResource,int32>& Values){FString S;for(int32 I=0;I<int32(ERTSResource::Count);++I){const int32 N=Values.FindRef(ERTSResource(I));if(N>0)S+=FString::Printf(TEXT("%s%d "),Name(ERTSResource(I)),N);}return S;}
}
