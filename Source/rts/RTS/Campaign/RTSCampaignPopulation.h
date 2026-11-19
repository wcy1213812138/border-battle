#pragma once

// Every new housing purchase grants eight slots. Legacy capacity is preserved,
// never retroactively charged or removed. Recruitment has no hidden role caps.
namespace RTSCampaignPopulation
{
constexpr int MaxPurchases = 11;
constexpr int PerPurchase = 8;
inline bool IsValid(int Purchases) { return Purchases >= 0 && Purchases <= MaxPurchases; }
inline int Bounded(int Purchases) { return Purchases < 0 ? 0 : Purchases > MaxPurchases ? MaxPurchases : Purchases; }
inline int RestoredBonus(int Version, bool Housing, int SavedBonus) { return Version >= 15 ? SavedBonus : Housing ? 32 : 0; }
inline int Limit(bool Housing, int Purchases, int LegacyBonus = 0) { const int N = Housing ? 32 + Bounded(Purchases) * PerPurchase + LegacyBonus : 24; return N > 120 ? 120 : N; }
inline int WoodCost(int Purchases) { return 160 + Bounded(Purchases) * 40; }
inline int FoodCost(int Purchases) { return 120 + Bounded(Purchases) * 30; }
}
