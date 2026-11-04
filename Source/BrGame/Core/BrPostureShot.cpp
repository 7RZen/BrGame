#include "BrCharacter.h"
#include "BrCharacterMovementComponent.h"
#include "BrGameMode.h"
#include "BrGameState.h"
#include "BrPlayerController.h"
#include "BrPlayerState.h"
#include "BrLog.h"
#include "BrDeveloperSettings.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#if !UE_BUILD_SHIPPING
void ABrCharacter::ServicePostureShot(float DeltaSeconds)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("BrPostureShot")) ||
		!GetPlayerState() || GetPlayerState()->GetPlayerName() != TEXT("ShotHost")) return;
	// Do not consume the standing capture at the random match spawn before the fixture starts.
	if (FVector::Dist2D(GetActorLocation(), FVector(-6000, -6000, 0)) > 1000 ||
		FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, 37.f)) > 1) return;
	ABrPlayerController* Viewer = Cast<ABrPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!Viewer || Viewer->GetViewTarget() != this) return;
	// Use the same orthographic camera for the remote spectator, not AActor's
	// fallback perspective view for an inactive non-owner camera.
	SetSpectatorCameraActive();
	float Width = 2500;
	FParse::Value(FCommandLine::Get(), TEXT("BrPostureShotWidth="), Width);
	TargetOrthoWidth = ClampCameraWidth(Width);
	TopDownCamera->SetOrthoWidth(TargetOrthoWidth);
	const bool Moving = GetVelocity().Size2D() > 20;
	const int32 Pose = GetPosture() == EBrPosture::Standing ? 0 :
		GetPosture() == EBrPosture::Crouching ? (Moving ? 2 : 1) : (Moving ? 4 : 3);
	if (Pose != PostureShotLast) { PostureShotSettle = 0; PostureShotLast = Pose; }
	PostureShotSettle += DeltaSeconds;
	if (PostureShotSettle < 1.2f || (PostureShotMask & (1 << Pose))) return;
	PostureShotMask |= 1 << Pose;
	const TCHAR* Names[] = { TEXT("standing"), TEXT("crouch_idle"), TEXT("crouch_move"), TEXT("prone_idle"), TEXT("prone_move") };
	const FString Name = FString::Printf(TEXT("%s_%s"), Names[Pose], HasAuthority() ? TEXT("gameplay") : TEXT("observer"));
	UE_LOG(LogBr, Display, TEXT("Posture shot capture: Name=%s Head=%.1f Pelvis=%.1f Speed=%.1f"),
		*Name, GetMesh()->GetBoneLocation(TEXT("head")).Z - (GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),
		GetMesh()->GetBoneLocation(TEXT("pelvis")).Z - (GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), GetVelocity().Size2D());
	FScreenshotRequest::RequestScreenshot(TEXT("Posture_") + Name, false, false);
}
#endif

void ABrGameMode::ServicePostureShot()
{
#if !UE_BUILD_SHIPPING
	ABrPlayerController* Host = Cast<ABrPlayerController>(GetWorld()->GetFirstPlayerController());
	ABrCharacter* Pawn = Host ? Cast<ABrCharacter>(Host->GetPawn()) : nullptr;
	if (!Pawn || !HasAuthority() || !Host->IsLocalController()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - MatchBeganTime < 6 || ShotSmokeStage > 5) return;
	ABrPlayerController* Remote = nullptr;
	for (APlayerState* PS : GetGameState<ABrGameState>()->PlayerArray)
		if (PS && PS != Host->PlayerState) Remote = Cast<ABrPlayerController>(PS->GetOwner());
	if (!Remote || !Remote->GetPawn()) return;
	// Initial relevancy/ClientRestart can arrive after the first view-target RPC.
	// Reassert the fixture view while the new observer finishes entering the world.
	Remote->ClientSetViewTarget(Pawn);
	if (Now >= ShotSmokeNextTime)
	{
		if (ShotSmokeStage == 5)
		{
			UE_LOG(LogBr, Display, TEXT("SHOT SMOKE PASS: posture gameplay and remote observer captures"));
			++ShotSmokeStage;
			return;
		}
		const EBrPosture Posture = ShotSmokeStage == 0 ? EBrPosture::Standing :
			ShotSmokeStage <= 2 ? EBrPosture::Crouching : EBrPosture::Prone;
		Pawn->GetBrMovement()->StopMovementImmediately();
		Pawn->GetBrMovement()->GroundFriction = 0;
		Pawn->GetBrMovement()->BrakingDecelerationWalking = 0;
		Pawn->GetBrMovement()->RequestPosture(Posture);
		if (Posture != EBrPosture::Standing) Pawn->GetBrMovement()->Crouch(false);
		Pawn->SetActorLocation(FVector(-6000, -6000, Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4),
			false, nullptr, ETeleportType::TeleportPhysics);
		Host->SetControlRotation(FRotator(0, 37, 0));
		Pawn->FaceRotation(FRotator(0, 37, 0));
		if (ShotSmokeStage == 2 || ShotSmokeStage == 4)
			Pawn->GetBrMovement()->Velocity = Pawn->GetActorForwardVector() * 120;
		Remote->GetPawn()->SetActorLocation(Pawn->GetActorLocation() + FVector(300, 300, 96), false, nullptr, ETeleportType::TeleportPhysics);
		Remote->ClientSetViewTarget(Pawn);
		Pawn->ForceNetUpdate();
		++ShotSmokeStage;
		ShotSmokeNextTime = Now + (ShotSmokeStage == 1 ? 8 : 4);
	}
#endif
}
