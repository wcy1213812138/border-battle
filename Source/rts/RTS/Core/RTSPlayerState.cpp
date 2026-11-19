#include "Core/RTSPlayerState.h"
#include "Economy/RTSResourceComponent.h"

ARTSPlayerState::ARTSPlayerState()
{
	Resources = CreateDefaultSubobject<URTSResourceComponent>(TEXT("Resources"));
}
