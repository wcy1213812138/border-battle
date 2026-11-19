#pragma once
#include "CoreMinimal.h"
#include "Data/RTSInfantryCatalog.h"

// Stable gameplay IDs remain language-independent; only presentation uses these names.
namespace RTSDisplayNames
{
	inline FText Entity(FName Id)
	{
		if (Id == TEXT("Peasant")) { return NSLOCTEXT("MiniRTS", "Peasant", "工人"); }
		if (Id == TEXT("Swordman")) { return NSLOCTEXT("MiniRTS", "Swordman", "剑士"); }
		if (Id == TEXT("Archer")) { return NSLOCTEXT("MiniRTS", "Archer", "弓箭手"); }
		if (const RTSInfantryCatalog::FEntry* Entry = RTSInfantryCatalog::Find(Id)) { return FText::FromString(Entry->Label); }
		if (Id == TEXT("TownHall")) { return NSLOCTEXT("MiniRTS", "TownHall", "主基地"); }
		if (Id == TEXT("Barracks")) { return NSLOCTEXT("MiniRTS", "Barracks", "兵营"); }
		if (Id == TEXT("ArcheryRange")) { return NSLOCTEXT("MiniRTS", "ArcheryRange", "远程兵营"); }
		if (Id == TEXT("ResearchInstitute")) { return NSLOCTEXT("MiniRTS", "ResearchInstitute", "研究所"); }
		if (Id == TEXT("SupportBarracks")) { return NSLOCTEXT("MiniRTS", "SupportBarracks", "后勤兵营"); }
		if (Id == TEXT("Farm")) { return NSLOCTEXT("MiniRTS", "Farm", "农场"); }
		if (Id == TEXT("Tower")) { return NSLOCTEXT("MiniRTS", "Tower", "防御塔"); }
		return NSLOCTEXT("MiniRTS", "UnknownEntity", "未知单位或建筑");
	}
}
