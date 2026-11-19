// Editor-only integration regression: same order entry point as human right-click.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Units/RTSUnit.h"
#include "Units/RTSUnitAIController.h"
#include "Units/RTSCrowdFollowingComponent.h"
#include "Orders/RTSOrderLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Orders/RTSLocalFormationPath.h"
#include "Tests/RTSMixedScenario.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "HAL/IConsoleManager.h"
#include "Core/RTSHumanController.h"
#include "Selection/RTSSelectionComponent.h"
#include "Engine/GameViewportClient.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FRTSMixedCountsMovementTest, "MiniRTS.Movement.MixedCounts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMixedGroupMovementTest, "MiniRTS.Movement.MixedTypesEnterAndSettle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSGroupMovementTest, "MiniRTS.Movement.EnterRectangleThenClaimAndSettle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSSlotReservationTest, "MiniRTS.Movement.SlotReservations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSSlotReservationTest::RunTest(const FString& Parameters)
{
	FRTSMoveGroup Group;
	Group.HalfDepth = 72.0f;
	Group.HalfWidth = 72.0f;
	Group.ClaimMargin = 72.0f;
	Group.Slots = URTSOrderLibrary::ComputeFormationSlots(FVector::ZeroVector, FVector::ForwardVector, 4, 144.0f);
	Group.Claimed.Init(false, 4);
	TestEqual(TEXT("Cannot claim outside rectangle"), Group.ClaimSlot(FVector(-1000.0, 0.0, 0.0)), INDEX_NONE);
	TSet<int32> Assigned;
	for (int32 i = 0; i < 4; ++i)
	{
		const int32 Slot = Group.ClaimSlot(FVector(-140.0, 0.0, 0.0));
		TestTrue(TEXT("Each arrival gets a unique slot"), Slot != INDEX_NONE && !Assigned.Contains(Slot));
		Assigned.Add(Slot);
	}
	TestEqual(TEXT("Full rectangle cannot overbook"), Group.ClaimSlot(FVector::ZeroVector), INDEX_NONE);
	const int32 Released = *Assigned.CreateConstIterator();
	Group.ReleaseSlot(Released);
	TestEqual(TEXT("Released slot can be claimed"), Group.ClaimSlot(FVector::ZeroVector), Released);
	return true;
}

namespace
{
	class FCheckRTSFormation : public IAutomationLatentCommand
	{
	public:
		explicit FCheckRTSFormation(FAutomationTestBase* InTest, bool bInMixed = false,
			TArray<int32> InCounts = {8, 4, 4}, int32 InMixedRows = 0)
			: Test(InTest), Counts(bInMixed ? MoveTemp(InCounts) : TArray<int32>{16, 0, 0}), ExpectedMixedRows(InMixedRows), bMixed(bInMixed) {}

		virtual bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World || !World->HasBegunPlay())
			{
				if (FPlatformTime::Seconds() - PIEWaitStarted < 30.0) { return false; }
				Test->AddError(TEXT("PIE world missing."));
				return true;
			}
			if (!bIssued)
			{
				if (!CheckMouseInput(World)) { return true; }
				UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				if (!Nav || !Nav->IsInitialized() || Nav->IsNavigationBuildInProgress() || World->GetTimeSeconds() < 2)
				{
					if (FPlatformTime::Seconds() - PIEWaitStarted > 30) { Test->AddError(TEXT("Movement fixture NavMesh timeout")); return true; }
					return false;
				}
				if (!ConfigureMixedTypes(World)) { return true; }
				Issue(World);
				bIssued = true;
			}
			if (bSetupFailed) { return true; }

			bool bAllSettled = Units.Num() == ExpectedCount;
			TSet<int32> Reservations;
			const FRTSMoveGroup* SharedGroup = nullptr;
			double MaxError = 0.0;
			TSet<int32> MovingRows;
			for (const TWeakObjectPtr<ARTSUnit>& Weak : Units)
			{
				const ARTSUnit* Unit = Weak.Get();
				if (!Unit) { Test->AddError(TEXT("Unit disappeared.")); return true; }
				const AAIController* AI = Cast<AAIController>(Unit->GetController());
				const URTSCrowdFollowingComponent* Crowd = AI ? Cast<URTSCrowdFollowingComponent>(AI->GetPathFollowingComponent()) : nullptr;
				if (!Crowd) { Test->AddError(TEXT("RTS crowd following missing.")); return true; }
				if (Unit->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
				{
					Test->AddError(TEXT("Unit collision was disabled."));
					return true;
				}
				const FRTSMoveGroup* Group = Unit->GetMoveGroup();
				const int32 Slot = Unit->GetFormationSlotIndex();
				if (!Group) { bAllSettled = false; continue; }
				if (!SharedGroup) { SharedGroup = Group; }
				if (SharedGroup != Group) { Test->AddError(TEXT("Units do not share one rectangle.")); return true; }
				if (Slot == INDEX_NONE)
				{
					bAllSettled = false;
					// Validated local detours may use precise following before claiming.
					// The physical collision check above applies throughout transit.
					continue;
				}
				if (Reservations.Contains(Slot) || !Group->Slots.IsValidIndex(Slot))
				{
					Test->AddError(TEXT("Duplicate or invalid formation reservation."));
					return true;
				}
				Reservations.Add(Slot);
				if (!Group->SlotUnitTypes.IsValidIndex(Slot) || Group->SlotUnitTypes[Slot] != Unit->GetFormationUnitType())
				{
					Test->AddError(TEXT("Unit claimed a different type's coordinate."));
					return true;
				}
				if (!Group->IsInsideClaimArea(Unit->GetSlotClaimLocation()))
				{
					Test->AddError(TEXT("Unit claimed before entering rectangle."));
					return true;
				}
				const double Error = FVector::Dist2D(Unit->GetActorLocation(), Group->Slots[Slot]);
				const double HeightError = FMath::Abs(Unit->GetActorLocation().Z - Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - Group->Slots[Slot].Z);
				bAllSettled &= Error <= 15.0 && HeightError < 80.0 && Unit->GetCurrentOrder().Type == ERTSOrderType::None;
				if (Unit->GetCurrentOrder().Type == ERTSOrderType::Move && Unit->GetVelocity().SizeSquared2D() > 25.0)
				{
					const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(ExpectedCount)));
					MovingRows.Add(Slot / Columns);
				}
				if (FPlatformTime::Seconds() - Started > 30.0 && Error > 15.0 && !Diagnosed.Contains(Unit))
				{
					Diagnosed.Add(Unit);
					FHitResult Hit;
					FCollisionQueryParams Params(SCENE_QUERY_STAT(FormationTestDiagnostic), false, Unit);
					const FVector End = Group->Slots[Slot] + FVector(0, 0, Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
					World->SweepSingleByChannel(Hit, Unit->GetActorLocation(), End, FQuat::Identity, ECC_Pawn,
						FCollisionShape::MakeSphere(Unit->GetSimpleCollisionRadius()), Params);
					Test->AddInfo(FString::Printf(TEXT("Blocked %s slot=%d location=%s goal=%s hit=%s"),
						*Unit->GetName(), Slot, *Unit->GetActorLocation().ToCompactString(), *Group->Slots[Slot].ToCompactString(), *GetNameSafe(Hit.GetActor())));
					const ARTSUnit* Blocker = Cast<ARTSUnit>(Hit.GetActor());
					Test->AddInfo(FString::Printf(TEXT("Traffic diagnostic mover=%d yields=%u recovering=%d blockerYields=%u blockerRecovering=%d pathGoal=%s"),
						static_cast<int32>(AI->GetMoveStatus()), Unit->GetTrafficYieldCount(), Unit->IsRecoveringFromCongestion(),
						Blocker ? Blocker->GetTrafficYieldCount() : 0, Blocker && Blocker->IsRecoveringFromCongestion(), *Crowd->GetPathDestination().ToCompactString()));
					TArray<FVector> Detour;
					const bool bDetour = RTSLocalFormationPath::Find(Unit, Group->Slots[Slot], Detour);
					Test->AddInfo(FString::Printf(TEXT("Local detour valid=%d points=%d first=%s"), bDetour, Detour.Num(), Detour.Num() ? *Detour[0].ToCompactString() : TEXT("none")));
					if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
					{
						FNavLocation StartNav;
						const FVector Feet = Unit->GetActorLocation() - FVector(0, 0, Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
						Nav->ProjectPointToNavigation(Feet, StartNav, FVector(80, 80, 150));
						UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, Feet, Group->Slots[Slot], const_cast<ARTSUnit*>(Unit));
						Test->AddInfo(FString::Printf(TEXT("Navigation start=%s valid=%d partial=%d points=%d"),
							*StartNav.Location.ToCompactString(), Path && Path->IsValid(), Path && Path->IsPartial(), Path ? Path->PathPoints.Num() : 0));
					}
				}
				MaxError = FMath::Max(MaxError, Error);
			}
			PeakMovingRows = FMath::Max(PeakMovingRows, MovingRows.Num());
			if (bAllSettled)
			{
				if (StableSince == 0.0) { StableSince = FPlatformTime::Seconds(); }
				if (FPlatformTime::Seconds() - StableSince < 2.0) { return false; }
				Test->AddInfo(FString::Printf(TEXT("Round %d: %d unique slots, claims inside rectangle, all settled; max error %.2f cm; peak concurrent moving rows %d."), Round + 1, ExpectedCount, MaxError, PeakMovingRows));
				if (bMixed) { Test->TestTrue(TEXT("Different rows moved into formation concurrently"), PeakMovingRows > 1); }
				if (++Round == 2) { return true; }
				Issue(World);
				return false;
			}
			StableSince = 0.0;
			if (FPlatformTime::Seconds() - Started < 105.0) { return false; }
			for (const TWeakObjectPtr<ARTSUnit>& Weak : Units)
			{
				if (const ARTSUnit* Unit = Weak.Get())
				{
					const FRTSMoveGroup* Group = Unit->GetMoveGroup();
					const int32 Slot = Unit->GetFormationSlotIndex();
					const double Error = Group && Group->Slots.IsValidIndex(Slot) ? FVector::Dist2D(Unit->GetActorLocation(), Group->Slots[Slot]) : -1.0;
					Test->AddInfo(FString::Printf(TEXT("%s slot=%d error=%.2f order=%d location=%s"),
						*Unit->GetName(), Slot, Error, static_cast<int32>(Unit->GetCurrentOrder().Type), *Unit->GetActorLocation().ToCompactString()));
				}
			}
			Test->AddError(FString::Printf(TEXT("Round %d did not settle all %d units within 105 seconds."), Round + 1, ExpectedCount));
			return true;
		}

	private:
		bool CheckMouseInput(UWorld* World)
		{
			const ARTSHumanController* PC = Cast<ARTSHumanController>(World->GetFirstPlayerController());
			const UGameViewportClient* Viewport = World->GetGameViewport();
			if (!PC || !Viewport) { Test->AddError(TEXT("RTS input viewport missing.")); return false; }
			const bool bVisible = Test->TestTrue(TEXT("RTS cursor remains visible"), PC->bShowMouseCursor);
			const bool bUnlocked = Test->TestTrue(TEXT("RTS cursor is not locked inside viewport"), Viewport->GetMouseLockMode() == EMouseLockMode::DoNotLock);
			const bool bTemporaryCapture = Test->TestTrue(TEXT("RTS only captures mouse during a drag"),
				Viewport->GetMouseCaptureMode() == EMouseCaptureMode::CaptureDuringMouseDown);
			return bVisible && bUnlocked && bTemporaryCapture;
		}

		bool FindLargeGoal(UWorld* World, const TArray<ARTSUnit*>& Live, const FVector& Preferred, FVector& OutGoal)
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (!Nav || Live.IsEmpty()) { return false; }
			FVector Centroid = FVector::ZeroVector;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(FormationTestGround), false);
			for (ARTSUnit* Unit : Live) { Centroid += Unit->GetActorLocation(); Params.AddIgnoredActor(Unit); }
			Centroid /= Live.Num();
			const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Live.Num())));
			const int32 Rows = FMath::DivideAndRoundUp(Live.Num(), Columns);
			const float Radius = Live[0]->GetSimpleCollisionRadius();
			const float HalfHeight = Live[0]->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const float Spacing = FMath::Max(120.0f, Radius * 3.6f);
			TArray<FVector> Candidates;
			for (int32 X = -2000; X <= 2000; X += 150)
			{
				for (int32 Y = -2000; Y <= 2000; Y += 150) { Candidates.Add(FVector(X, Y, 0)); }
			}
			Candidates.Sort([&Preferred](const FVector& A, const FVector& B)
			{
				return FVector::DistSquared2D(A, Preferred) < FVector::DistSquared2D(B, Preferred);
			});
			for (const FVector& Candidate : Candidates)
			{
				if (FVector::Dist2D(Candidate, Centroid) < 950.0) { continue; }
				const FVector Forward = (Candidate - Centroid).GetSafeNormal2D();
				const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
				bool bClear = true;
				// Include the left entry corridor, not only the final slot footprint.
				for (int32 Row = 0; Row <= (Rows - 1) * 2 && bClear; ++Row)
				{
					for (int32 Col = -3; Col <= (Columns - 1) * 2; ++Col)
					{
						const FVector Feet = Candidate + Forward * ((Rows - 1 - Row) * Spacing * 0.5)
							+ Right * ((Col - Columns + 1) * Spacing * 0.5);
						FNavLocation Projected;
						if (!Nav->ProjectPointToNavigation(Feet, Projected, FVector(20, 20, 100))
							|| FVector::Dist2D(Feet, Projected.Location) > 10.0 || FMath::Abs(Projected.Location.Z - 10.0) > 25.0
							|| World->OverlapBlockingTestByChannel(Projected.Location + FVector(0, 0, HalfHeight + 3.0),
								FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Radius + 5.0f, HalfHeight), Params))
						{
							bClear = false;
							break;
						}
					}
				}
				if (bClear) { OutGoal = Candidate; return true; }
			}
			return false;
		}

		bool ConfigureMixedTypes(UWorld* World)
		{
			const FString Command = FString::Printf(TEXT("rts.TestMixed %d %d %d"), Counts[0], Counts[1], Counts[2]);
			if (!IConsoleManager::Get().ProcessUserConsoleInput(*Command, *GLog, World))
			{
				Test->AddError(TEXT("Playable mixed scenario console command was not handled."));
				return false;
			}
			TArray<ARTSUnit*> Created;
			for (TActorIterator<ARTSUnit> It(World); It; ++It)
			{
				if (It->GetTeam() == RTSTeam::Player && It->IsAlive()) { Created.Add(*It); }
			}
			if (const ARTSHumanController* PC = Cast<ARTSHumanController>(World->GetFirstPlayerController()))
			{
				Test->TestEqual(TEXT("Playable command selects all generated units"), PC->GetSelection()->GetSelectedCount(), Counts[0] + Counts[1] + Counts[2]);
			}
			Units.Reset();
			for (ARTSUnit* Unit : Created) { Units.Add(Unit); }
			ExpectedCount = Counts[0] + Counts[1] + Counts[2];
			Test->TestEqual(TEXT("Scenario spawned requested total"), Units.Num(), ExpectedCount);
			TMap<FName, int32> ActualCounts;
			TSet<const USkeletalMesh*> Meshes;
			for (ARTSUnit* Unit : Created)
			{
				++ActualCounts.FindOrAdd(Unit->GetFormationUnitType());
				Meshes.Add(Unit->GetMesh()->GetSkeletalMeshAsset());
			}
			int32 TypeCount = 0;
			for (int32 Count : Counts) { TypeCount += Count > 0; }
			Test->TestEqual(TEXT("Requested types have distinct visible models"), Meshes.Num(), TypeCount);
			for (int32 Type = 0; Type < 3; ++Type)
			{
				if (!Counts[Type]) { continue; }
				const TCHAR* Name = Type == 0 ? TEXT("Swordman") : Type == 1 ? TEXT("Archer") : TEXT("Peasant");
				int32 Actual = 0;
				for (const auto& Pair : ActualCounts) { if (Pair.Key.ToString().Contains(Name)) { Actual += Pair.Value; } }
				Test->TestEqual(FString::Printf(TEXT("Requested %s count"), Name), Actual, Counts[Type]);
			}
			return true;
		}

		void Issue(UWorld* World)
		{
			TArray<ARTSUnit*> Live;
			for (const auto& Weak : Units) { if (ARTSUnit* Unit = Weak.Get()) { Live.Add(Unit); } }
			// Keep the existing movement route; large groups validate the destination footprint.
			FVector Goal = bMixed
				? (Round == 0 ? FVector(-1000.0, 800.0, 0.0) : FVector(-1050.0, -850.0, 0.0))
				: (Round == 0 ? FVector(-1000.0, 1000.0, 0.0) : FVector(-1050.0, -850.0, 0.0));
			if (ExpectedCount > 16 && !FindLargeGoal(World, Live, Goal, Goal))
			{
				Test->AddError(TEXT("No clear flat destination large enough for the full 24-unit formation."));
				bSetupFailed = true;
				return;
			}
			Test->AddInfo(FString::Printf(TEXT("Round %d destination: %s"), Round + 1, *Goal.ToCompactString()));
			Diagnosed.Reset();
			PeakMovingRows = 0;
			URTSOrderLibrary::IssueMoveOrder(World, Live, Goal);
			for (ARTSUnit* Unit : Live)
			{
				Test->TestEqual(TEXT("New order releases old reservation"), Unit->GetFormationSlotIndex(), INDEX_NONE);
			}
			if (bMixed && Live.Num())
			{
				const FRTSMoveGroup* Group = Live[0]->GetMoveGroup();
				if (Group && Group->SlotUnitTypes.Num() == ExpectedCount)
				{
					float MaxRadius = 0.0f;
					for (const ARTSUnit* Unit : Live) { MaxRadius = FMath::Max(MaxRadius, Unit->GetStandingCollisionRadius()); }
					const TArray<FVector> ExpectedSlots = URTSOrderLibrary::ComputeFormationSlots(
						Goal, Group->Forward, ExpectedCount, FMath::Max(120.0f, MaxRadius * 3.6f));
					for (int32 i = 0; i < ExpectedSlots.Num(); ++i)
					{
						Test->TestTrue(TEXT("Mixed formation has exactly the single-type spacing"), Group->Slots[i].Equals(ExpectedSlots[i], 0.01));
					}
					const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(ExpectedCount)));
					int32 MixedRows = 0;
					for (int32 Row = 0; Row < ExpectedCount; Row += Columns)
					{
						TSet<FName> RowTypes;
						for (int32 i = Row; i < FMath::Min(Row + Columns, ExpectedCount); ++i) { RowTypes.Add(Group->SlotUnitTypes[i]); }
						MixedRows += RowTypes.Num() > 1;
					}
					Test->TestEqual(TEXT("Only expected remainder rows are mixed"), MixedRows, ExpectedMixedRows);
				}
				else { Test->AddError(TEXT("Mixed formation type layout missing.")); }
			}
			Started = FPlatformTime::Seconds();
			StableSince = 0.0;
		}

		FAutomationTestBase* Test;
		TSet<const ARTSUnit*> Diagnosed;
		TArray<TWeakObjectPtr<ARTSUnit>> Units;
		TArray<int32> Counts;
		int32 ExpectedCount = 16;
		int32 ExpectedMixedRows = 0;
		bool bMixed = false;
		int32 PeakMovingRows = 0;
		bool bIssued = false;
		int32 Round = 0;
		bool bSetupFailed = false;
		double PIEWaitStarted = FPlatformTime::Seconds();
		double Started = 0.0;
		double StableSince = 0.0;
	};
}

bool FRTSGroupMovementTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckRTSFormation(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

bool FRTSMixedGroupMovementTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckRTSFormation(this, true));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

void FRTSMixedCountsMovementTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	Names = {TEXT("06_Sword3_Archer3"), TEXT("07_Sword3_Archer3_Peasant1"),
		TEXT("08_Sword5_Archer3"), TEXT("08_Sword4_Archer2_Peasant2"),
		TEXT("16_Sword8_Archer5_Peasant3"), TEXT("24_Sword12_Archer7_Peasant5")};
	Commands = {TEXT("3 3 0 0"), TEXT("3 3 1 0"), TEXT("5 3 0 0"), TEXT("4 2 2 1"), TEXT("8 5 3 1"), TEXT("12 7 5 1")};
}

bool FRTSMixedCountsMovementTest::RunTest(const FString& Parameters)
{
	TArray<FString> Values;
	Parameters.ParseIntoArrayWS(Values);
	if (Values.Num() != 4) { AddError(TEXT("Mixed count test parameters missing.")); return false; }
	TArray<int32> Counts = {FCString::Atoi(*Values[0]), FCString::Atoi(*Values[1]), FCString::Atoi(*Values[2])};
	const int32 MixedRows = FCString::Atoi(*Values[3]);
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckRTSFormation(this, true, Counts, MixedRows));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
