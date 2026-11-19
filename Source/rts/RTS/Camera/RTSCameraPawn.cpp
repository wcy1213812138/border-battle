#include "Camera/RTSCameraPawn.h"
#include "Core/RTSTypes.h"
#include "Components/SceneComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Engine/LocalPlayer.h"
#include "Match/RTSMatchSubsystem.h"
#include "Campaign/RTSCampaignMapSpace.h"

ARTSCameraPawn::ARTSCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(Root);
	SpringArm->SetRelativeRotation(FRotator(-55.0f, 0.0f, 0.0f));
	SpringArm->TargetArmLength = DefaultArmLength;
	SpringArm->bDoCollisionTest = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 12.0f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->FieldOfView = 60.0f;

	TargetArmLength = DefaultArmLength;
}
void ARTSCameraPawn::BeginPlay()
{
 Super::BeginPlay();
 if(GetWorld() && GetWorld()->GetMapName().Contains(TEXT("L_BorderIncremental")))MaxArmLength=9000.f;
 float SavedScale=1.f;
 if(GConfig->GetFloat(TEXT("MiniRTS.Camera"),TEXT("PanSpeedScale"),SavedScale,GGameUserSettingsIni))
  PanSpeedScale=FMath::Clamp(SavedScale,.5f,2.f);
}

void ARTSCameraPawn::BuildInput()
{
	if (MappingContext)
	{
		return;
	}

	PanAction = NewObject<UInputAction>(this, TEXT("IA_RTSPan"));
	PanAction->ValueType = EInputActionValueType::Axis2D;
	ZoomAction = NewObject<UInputAction>(this, TEXT("IA_RTSZoom"));
	ZoomAction->ValueType = EInputActionValueType::Axis1D;

	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_RTSCamera"));

	// Axis2D: X = right, Y = forward. Swizzle routes a 1D key into Y.
	auto MapPan = [this](const FKey& Key, bool bNegate, bool bForwardAxis)
	{
		FEnhancedActionKeyMapping& M = MappingContext->MapKey(PanAction, Key);
		if (bForwardAxis)
		{
			UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(MappingContext);
			Swizzle->Order = EInputAxisSwizzle::YXZ;
			M.Modifiers.Add(Swizzle);
		}
		if (bNegate)
		{
			M.Modifiers.Add(NewObject<UInputModifierNegate>(MappingContext));
		}
	};
	MapPan(EKeys::D, false, false);
	MapPan(EKeys::A, true, false);
	MapPan(EKeys::W, false, true);
	MapPan(EKeys::S, true, true);
	MapPan(EKeys::Right, false, false);
	MapPan(EKeys::Left, true, false);
	MapPan(EKeys::Up, false, true);
	MapPan(EKeys::Down, true, true);

	MappingContext->MapKey(ZoomAction, EKeys::MouseWheelAxis);
}

void ARTSCameraPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	BuildInput();

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EIC->BindAction(PanAction, ETriggerEvent::Triggered, this, &ARTSCameraPawn::OnPan);
		EIC->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &ARTSCameraPawn::OnZoom);
	}
	else
	{
		UE_LOG(LogRTS, Error, TEXT("ARTSCameraPawn requires EnhancedInputComponent."));
	}
}

void ARTSCameraPawn::PawnClientRestart()
{
	Super::PawnClientRestart();
	BuildInput();

	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(MappingContext, 0);
		}
	}
}

void ARTSCameraPawn::OnPan(const FInputActionValue& Value)
{
	if (const URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(this); Match && !Match->AllowsGameplay()) { return; }
	PendingPan += Value.Get<FVector2D>();
}

void ARTSCameraPawn::OnZoom(const FInputActionValue& Value)
{
	if (const URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(this); Match && !Match->AllowsGameplay()) { return; }
	const float Wheel = Value.Get<float>();
	TargetArmLength = FMath::Clamp(TargetArmLength - Wheel * ZoomStep, MinArmLength, MaxArmLength);
}

void ARTSCameraPawn::FlashAttackWarning()
{
 if(!bAttackTintActive){bPreviousTintOverride=Camera->PostProcessSettings.bOverride_SceneColorTint;PreviousSceneTint=Camera->PostProcessSettings.SceneColorTint;PreviousPostProcessWeight=Camera->PostProcessBlendWeight;bAttackTintActive=true;}
 AttackWarningSeconds=1.2f;
 Camera->PostProcessSettings.bOverride_SceneColorTint=true;Camera->PostProcessSettings.SceneColorTint=FLinearColor(1.f,.38f,.38f);Camera->PostProcessBlendWeight=1.f;
}
void ARTSCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if(bAttackTintActive)
	{
	 AttackWarningSeconds=FMath::Max(0.f,AttackWarningSeconds-DeltaSeconds);
	 const float Strength=FMath::Clamp(AttackWarningSeconds/1.2f,0.f,1.f);
	 Camera->PostProcessSettings.SceneColorTint=FMath::Lerp(PreviousSceneTint,FLinearColor(1.f,.38f,.38f),Strength);
	 if(AttackWarningSeconds<=0){Camera->PostProcessSettings.bOverride_SceneColorTint=bPreviousTintOverride;Camera->PostProcessSettings.SceneColorTint=PreviousSceneTint;Camera->PostProcessBlendWeight=PreviousPostProcessWeight;bAttackTintActive=false;}
	}
	if (const URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(this); Match && !Match->AllowsGameplay()) { PendingPan = FVector2D::ZeroVector; return; }

	FVector2D Pan = PendingPan;
	PendingPan = FVector2D::ZeroVector;

	if (bEdgeScroll)
	{
		if (const APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			int32 SizeX = 0, SizeY = 0;
			float MouseX = 0.f, MouseY = 0.f;
			PC->GetViewportSize(SizeX, SizeY);
			if (SizeX > 0 && SizeY > 0 && PC->GetMousePosition(MouseX, MouseY))
			{
				if (MouseX <= EdgeSize) { Pan.X -= 1.f; }
				else if (MouseX >= SizeX - EdgeSize) { Pan.X += 1.f; }
				if (MouseY <= EdgeSize) { Pan.Y += 1.f; }
				else if (MouseY >= SizeY - EdgeSize) { Pan.Y -= 1.f; }
			}
		}
	}

	if (!Pan.IsNearlyZero())
	{
		Pan = Pan.GetClampedToMaxSize(1.0f);
		const float ZoomScale = SpringArm->TargetArmLength / DefaultArmLength;
		const FRotator Yaw(0.f, SpringArm->GetComponentRotation().Yaw, 0.f);
		const FVector Forward = Yaw.RotateVector(FVector::ForwardVector);
		const FVector Right = Yaw.RotateVector(FVector::RightVector);
		AddActorWorldOffset((Forward * Pan.Y + Right * Pan.X) * PanSpeed * PanSpeedScale * ZoomScale * DeltaSeconds);
	}

	SpringArm->TargetArmLength = FMath::FInterpTo(SpringArm->TargetArmLength, TargetArmLength, DeltaSeconds, ZoomInterpSpeed);
}

float ARTSCameraPawn::PanSpeedScale = 1.0f;
void ARTSCameraPawn::SetPanSpeedScale(float Scale)
{
	PanSpeedScale = FMath::Clamp(Scale, 0.5f, 2.0f);
	GConfig->SetFloat(TEXT("MiniRTS.Camera"), TEXT("PanSpeedScale"), PanSpeedScale, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}
float ARTSCameraPawn::GetPanSpeedScale()
{
	return PanSpeedScale;
}
