#pragma once
#include "CoreMinimal.h"
#include "UI/RTSUIFont.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

// Code-native visual tokens: no external textures or gameplay dependencies.
namespace RTSUIStyle
{
	inline FLinearColor Ink() { return FLinearColor(0.88f, 0.90f, 0.86f); }
	inline FLinearColor Muted() { return FLinearColor(0.52f, 0.62f, 0.63f); }
	inline FLinearColor Gold() { return FLinearColor(0.88f, 0.66f, 0.30f); }
	inline FLinearColor Accent() { return FLinearColor(0.20f, 0.72f, 0.58f); }
	inline void Text(UTextBlock* Label, int32 Size, FLinearColor Color = Ink())
	{
		Label->SetFont(RTSUIFont::Chinese(Size)); Label->SetColorAndOpacity(Color);
		Label->SetShadowOffset(FVector2D(0, 1)); Label->SetShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.5f));
	}
	inline void Panel(UBorder* Border, bool bStrong = false)
	{
		Border->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.018f, 0.031f, 0.038f, bStrong ? 0.99f : 0.96f),
			8.f, FLinearColor(0.17f, 0.24f, 0.25f), 1.f));
		Border->SetBrushColor(FLinearColor::White); Border->SetPadding(FMargin(14, 12));
	}
	inline FButtonStyle ButtonStyle(bool bSelected = false, bool bDanger = false)
	{
		const FLinearColor Edge = bDanger ? FLinearColor(0.62f, 0.23f, 0.18f) : bSelected ? Accent() : FLinearColor(0.24f, 0.32f, 0.33f);
		const FLinearColor Base = bDanger ? FLinearColor(0.15f, 0.055f, 0.05f) : bSelected ? FLinearColor(0.035f, 0.19f, 0.15f) : FLinearColor(0.055f, 0.087f, 0.10f);
		FButtonStyle Style;
		Style.SetNormal(FSlateRoundedBoxBrush(Base, 5.f, Edge, 1.f));
		Style.SetHovered(FSlateRoundedBoxBrush(Base + FLinearColor(0.045f, 0.065f, 0.06f, 0), 5.f, Gold(), 1.f));
		Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.025f, 0.12f, 0.10f), 5.f, Accent(), 1.f));
		Style.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(0.035f, 0.045f, 0.05f), 5.f, FLinearColor(0.10f, 0.13f, 0.14f), 1.f));
		Style.SetNormalForeground(Ink()); Style.SetHoveredForeground(FLinearColor::White);
		Style.SetPressedForeground(Ink()); Style.SetDisabledForeground(Muted());
		Style.SetNormalPadding(FMargin(12, 8)); Style.SetPressedPadding(FMargin(12, 9, 12, 7));
		return Style;
	}
	inline void Button(UButton* Value, bool bSelected = false, bool bDanger = false)
	{ Value->SetStyle(ButtonStyle(bSelected, bDanger)); Value->SetBackgroundColor(FLinearColor::White); }
	inline FVector2D ModalSize(FVector2D View)
	{ return FVector2D(FMath::Max(240., FMath::Min(620., View.X - 64)), FMath::Max(240., FMath::Min(700., View.Y - 200))); }
	inline float CommandWidth(float ViewWidth)
	{ return FMath::Clamp(ViewWidth - 420.f, 280.f, 560.f); }
}
