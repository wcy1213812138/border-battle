#pragma once
#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Fonts/CompositeFont.h"
#include "Misc/Paths.h"

namespace RTSUIFont
{
	inline FSlateFontInfo Chinese(int32 Size)
	{
		// Explicit CJK coverage, independent of editor language and OS font fallback.
		static const TSharedPtr<const FCompositeFont> Font = MakeShared<FCompositeFont>(FName("Regular"),
			FPaths::EngineContentDir() / TEXT("Slate/Fonts/DroidSansFallback.ttf"), EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		return FSlateFontInfo(Font, Size);
	}
}
