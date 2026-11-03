#include "BrCharacter.h"
#include "BrCharacterMovementComponent.h"
#include "BrPlayerController.h"
#include "BrGameMode.h"
#include "BrInventoryComponent.h"
#include "BrDeveloperSettings.h"
#include "BrLog.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void ABrPlayerController::ClientSprintSmoke_Implementation(bool bStart, FVector Location)
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke"))) return;
	ABrCharacter* BrPawn = Cast<ABrCharacter>(GetPawn());
	if (!BrPawn) return;
	BrPawn->GetInventory()->CancelCombat();
	BrPawn->GetBrMovement()->SetSprinting(false);
	BrPawn->GetBrMovement()->StopMovementImmediately();
	BrPawn->GetBrMovement()->SetComponentTickEnabled(true);
	BrPawn->GetBrMovement()->SetMovementMode(MOVE_Walking);
	BrPawn->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	SetControlRotation(FRotator::ZeroRotator);
	if (bStart) BrPawn->BeginSprintSmoke();
#endif
}

#if !UE_BUILD_SHIPPING
void ABrCharacter::BeginSprintSmoke()
{
	bSprintSmokeRunning = true;
	SprintSmokeElapsed = 0;
	SprintSmokeCorrections = 0;
}

void ABrCharacter::ServiceSprintSmoke(float DeltaSeconds)
{
	if (!bSprintSmokeRunning || !IsLocallyControlled()) return;
	const float Previous = SprintSmokeElapsed;
	SprintSmokeElapsed += DeltaSeconds;
	// Settle the fixture's teleport/floor correction before measuring native network movement.
	if (SprintSmokeElapsed < 1) return;
	AddMovementInput(FVector::ForwardVector, 1);
	if (Previous < 2 && SprintSmokeElapsed >= 2) SprintStarted();
	if (Previous < 7.4f && SprintSmokeElapsed >= 7.4f) SprintStopped();
	// Allow partial regen, then exercise a real shot during a fresh sprint.
	if (Previous < 9 && SprintSmokeElapsed >= 9) SprintStarted();
	if (Previous < 9.5f && SprintSmokeElapsed >= 9.5f)
	{
		if (!GetBrMovement()->IsSprinting())
			UE_LOG(LogBr, Error, TEXT("E2E SPRINT CLIENT FAIL: fire fixture was not sprinting"));
		FireStarted();
	}
	if (Previous < 10 && SprintSmokeElapsed >= 10) FireStopped();
	if (SprintSmokeElapsed >= 11)
	{
		bSprintSmokeRunning = false;
		GetBrMovement()->SetSprinting(false);
		GetInventory()->CancelCombat();
		UE_LOG(LogBr, Display, TEXT("E2E SPRINT CLIENT %s: Corrections=%d"),
			SprintSmokeCorrections == 0 ? TEXT("PASS") : TEXT("FAIL"), SprintSmokeCorrections);
	}
}

bool ABrGameMode::ServiceSprintSmoke(ABrPlayerController* Runner)
{
	if (SprintSmokeStage == 4) return true;
	ABrCharacter* Pawn = Runner ? Cast<ABrCharacter>(Runner->GetPawn()) : nullptr;
	if (!Pawn || Runner->IsLocalController()) return false;
	UBrCharacterMovementComponent* Move = Pawn->GetBrMovement();
	const float Now = GetWorld()->GetTimeSeconds();
	if (SprintSmokeStage == 0)
	{
		SprintSmokeOriginalLocation = Pawn->GetActorLocation();
		// L_Town's reserved east-west road is an existing, network-addressable
		// static movement base. Validate clearance; never skip collisions/corrections.
		const FVector Start(-8500, 0, 102);
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(BrSprintSmokeLane), false, Pawn);
		if (GetWorld()->SweepSingleByObjectType(Hit, Start, Start + FVector(8000, 0, 0), FQuat::Identity,
			FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeCapsule(42, 94), Query))
		{
			UE_LOG(LogBr, Error, TEXT("E2E SPRINT SERVER FAIL: road obstructed by %s"), *GetNameSafe(Hit.GetActor()));
			return false;
		}
		Pawn->SetActorLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
		Runner->SetControlRotation(FRotator::ZeroRotator);
		Pawn->SetActorRotation(FRotator::ZeroRotator);
		Move->RequestPosture(EBrPosture::Standing);
		Move->SetMovementMode(MOVE_Walking);
		Move->StopMovementImmediately();
		Pawn->ForceNetUpdate();
		Runner->ClientSprintSmoke(true, Start);
		SprintSmokeDeadline = Now;
		SprintSmokeLoaded = Pawn->GetInventory()->Loaded;
		SprintSmokeStage = 1;
		return false;
	}
	const float Elapsed = Now - SprintSmokeDeadline;
	if (Move->IsSprinting() && SprintSmokeFirstActive == 0) SprintSmokeFirstActive = Elapsed;
	if (Elapsed > 3 && Elapsed < 6)
		SprintSmokePeakSpeed = FMath::Max(SprintSmokePeakSpeed, static_cast<float>(Pawn->GetVelocity().Size2D()));
	if (SprintSmokeStage == 1 && Elapsed >= 7.3f)
	{
		const float Expected = UBrDeveloperSettings::Get()->BaseRunSpeed * UBrDeveloperSettings::Get()->SprintSpeedMultiplier *
			Pawn->GetWeaponRunSpeedMultiplier();
		if (SprintSmokeFirstActive < 1.8f || SprintSmokeFirstActive > 2.8f ||
			!FMath::IsNearlyEqual(SprintSmokePeakSpeed, Expected, 5.f) || Move->IsSprinting() ||
			Move->GetSprintState().Stamina > .3f)
		{
			UE_LOG(LogBr, Error, TEXT("E2E SPRINT SERVER FAIL: Speed=%.1f Expected=%.1f First=%.2f Active=%d Stamina=%.3f"),
				SprintSmokePeakSpeed, Expected, SprintSmokeFirstActive, Move->IsSprinting(), Move->GetSprintState().Stamina);
			return false;
		}
		UE_LOG(LogBr, Display, TEXT("E2E SPRINT SPEED PASS: Remote=1 Speed=%.1f Ratio=1.3 EmptyAfter=%.2f"),
			SprintSmokePeakSpeed, Elapsed - SprintSmokeFirstActive);
		SprintSmokeStage = 2;
	}
	if (SprintSmokeStage == 2 && Pawn->GetInventory()->Loaded < SprintSmokeLoaded)
	{
		const double Delay = Pawn->GetInventory()->GetLastSprintFireRecovery();
		// Exact acceptance timestamp, not this 0.5-second service's last gait sample.
		// The upper bound also rejects a shot following only the OLD exhaustion stop.
		if (Delay < UBrDeveloperSettings::Get()->SprintToFireDelay - .04f ||
			Delay > UBrDeveloperSettings::Get()->SprintToFireDelay + .15f)
		{
			UE_LOG(LogBr, Error, TEXT("E2E SPRINT SERVER FAIL: shot recovery %.3f"), Delay);
			return false;
		}
		UE_LOG(LogBr, Display, TEXT("E2E SPRINT FIRE PASS: Delay=%.3f Remote=1"), Delay);
		SprintSmokeStage = 3;
	}
	if (Elapsed >= 12)
	{
		if (SprintSmokeStage != 3)
		{
			UE_LOG(LogBr, Error, TEXT("E2E SPRINT SERVER FAIL: deferred shot not received"));
			return false;
		}
		Move->SetSprinting(false);
		Move->StopMovementImmediately();
		Pawn->SetActorLocation(SprintSmokeOriginalLocation, false, nullptr, ETeleportType::TeleportPhysics);
		Pawn->ForceNetUpdate();
		Runner->ClientSprintSmoke(false, SprintSmokeOriginalLocation);
		SprintSmokeStage = 4;
		return true;
	}
	return false;
}
#endif
