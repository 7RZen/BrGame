#include "BrGameMode.h"
#include "BrPlayerController.h"
#include "BrMenuWidget.h"
#include "BrDeveloperSettings.h"
#include "BrCharacter.h"
#include "BrCombatDefinitions.h"
#include "BrHealthComponent.h"
#include "BrCharacterMovementComponent.h"
#include "TimerManager.h"
#include "BrDevCheatPolicy.h"
#include "BrLog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "BrPlayerState.h"
#include "BrGameState.h"
#include "BrFogComponent.h"
#include "BrLifeComponent.h"
#include "BrSafeZone.h"
#include "AIController.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	struct FBrShotPoint
	{
		const TCHAR* Name;
		double X;
		double Y;
	};
	constexpr FBrShotPoint ShotPoints[] = {
		{TEXT("town_street"), -1000.0, 0.0},
		{TEXT("town_interior"), -2900.0, -1800.0},
		{TEXT("field"), -6000.0, -6000.0},
		{TEXT("dense"), -6200.0, 4800.0},
		{TEXT("dense_explored"), -5200.0, 4800.0},
		{TEXT("forest"), 7000.0, -6000.0},
	};
	constexpr int32 ShotPointCount = UE_ARRAY_COUNT(ShotPoints);
	constexpr float ShotSettleSeconds = 2.0f;
}

void ABrGameMode::ServiceShotSmoke()
{
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("BrMenuShot")))
	{
		ServiceMenuShot();
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("BrMinimapShot")))
	{
		ServiceMinimapShot();
		return;
	}
	ABrPlayerController* Host = Cast<ABrPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!Host || !FBrDevCheatPolicy::CanExecute(HasAuthority(), Host->IsLocalController(), GetNetMode())) return;
	if (!Cast<ABrCharacter>(Host->GetPawn())) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (FParse::Param(FCommandLine::Get(), TEXT("BrHitShot")))
	{
		if (Now - MatchBeganTime < 6 || Now < ShotSmokeNextTime || ShotSmokeStage > 6) return;
		if (ShotSmokeStage == 6)
		{
			UE_LOG(LogBr, Display, TEXT("SHOT SMOKE PASS: 3 hit feedback screenshots requested"));
			++ShotSmokeStage;
			return;
		}
		ABrCharacter* Victim = CastChecked<ABrCharacter>(Host->GetPawn());
		ABrCharacter* Other = nullptr;
		for (APlayerState* Entry : GetGameState<ABrGameState>()->PlayerArray)
			if (Entry && Entry != Host->PlayerState)
				if (ABrCharacter* C = Cast<ABrCharacter>(Entry->GetPawn())) { Other = C; break; }
		if (!Other) return;
		Victim->GetBrMovement()->DisableMovement();
		Other->GetBrMovement()->DisableMovement();
		Victim->GetLife()->InvulnerabilityEndTime = Other->GetLife()->InvulnerabilityEndTime = 0;
		Victim->GetHealth()->SetHealth(Victim->GetHealth()->MaxHealth);
		Other->GetHealth()->SetHealth(Other->GetHealth()->MaxHealth);
		Victim->SetActorLocation(FVector(-6000, -6000, 100), false, nullptr, ETeleportType::TeleportPhysics);
		Other->SetActorLocation(FVector(-5700, -5850, 100), false, nullptr, ETeleportType::TeleportPhysics);
		const int32 HitIndex = ShotSmokeStage / 2;
		const float Yaw = HitIndex == 2 ? 125.f : 37.f;
		Host->SetControlRotation(FRotator(0, Yaw, 0));
		Victim->FaceRotation(FRotator(0, Yaw, 0));
		if (ShotSmokeStage % 2 == 0)
		{
			// Let camera, fog and remote pose settle before applying a transient hit.
			++ShotSmokeStage;
			ShotSmokeNextTime = Now + 3;
			return;
		}
		const bool bExplosion = HitIndex == 1;
		const FBrDamageEvent Event(CaptureDamageSource(Other->GetController()),
			bExplosion ? UBrExplosionDamageType::StaticClass() : UBrBulletDamageType::StaticClass(), Other->GetActorLocation());
		Victim->TakeDamage(15, Event, Other->GetController(), Other);
		if (HitIndex == 2)
		{
			const FBrDamageEvent Second(CaptureDamageSource(Other->GetController()), UBrBulletDamageType::StaticClass(),
				Victim->GetActorLocation() + FVector(-700, -600, 0));
			Victim->TakeDamage(10, Second, Other->GetController(), Other);
		}
		const FBrDamageEvent Remote(CaptureDamageSource(Host), UBrBulletDamageType::StaticClass(), Victim->GetActorLocation());
		Other->TakeDamage(10, Remote, Host, Victim);
		const TCHAR* Names[] = {TEXT("bullet"), TEXT("grenade"), TEXT("turned_multi")};
		const FString CaptureName = Names[HitIndex];
		// Next tick's timers run after character presentation, while the 0.18s reaction is live.
		GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [CaptureName]()
		{
			UE_LOG(LogBr, Display, TEXT("Shot smoke capture: Name=%s"), *CaptureName);
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("HitFeedback_%s"), *CaptureName), false, false);
		}));
		++ShotSmokeStage;
		ShotSmokeNextTime = Now + 3;
		return;
	}
	if (Now - MatchBeganTime < 4.0f || Now < ShotSmokeNextTime) return;
	if (ShotSmokeStage > ShotPointCount * 2) return;
	if (ShotSmokeStage == ShotPointCount * 2)
	{
		UE_LOG(LogBr, Display, TEXT("SHOT SMOKE PASS: %d screenshots requested"), ShotPointCount);
		++ShotSmokeStage;
		return;
	}
	const int32 Index = ShotSmokeStage / 2;
	if (ShotSmokeStage % 2 == 0)
	{
		const FBrShotPoint& Point = ShotPoints[Index];
		Host->ServerBrCheat(TEXT("Teleport"), FName(*FString::SanitizeFloat(Point.X)), Point.Y);
		if (FParse::Param(FCommandLine::Get(), TEXT("BrAimingShot")))
		{
			// Relative-input mode has no screen-cursor fixture. Exercise different facing angles directly.
			const FRotator Facing(0, Index * 37.f, 0);
			Host->SetControlRotation(Facing);
			Host->GetPawn()->FaceRotation(Facing);
		}
		UE_LOG(LogBr, Display, TEXT("Shot smoke positioned: Name=%s X=%.0f Y=%.0f"), Point.Name, Point.X, Point.Y);
		ShotSmokeNextTime = Now + ShotSettleSeconds;
	}
	else
	{
		UE_LOG(LogBr, Display, TEXT("Shot smoke capture: Name=%s"), ShotPoints[Index].Name);
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Step10_%s"), ShotPoints[Index].Name), false, false);
		ShotSmokeNextTime = Now + ShotSettleSeconds;
	}
	++ShotSmokeStage;
#endif
}

#if !UE_BUILD_SHIPPING
void ABrGameMode::ServiceMenuShot()
{
	ABrPlayerController* Host = Cast<ABrPlayerController>(GetWorld()->GetFirstPlayerController());
	ABrCharacter* Pawn = Host ? Cast<ABrCharacter>(Host->GetPawn()) : nullptr;
	if (!HasAuthority() || !Pawn || !Host->IsLocalController()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - MatchBeganTime < 5 || Now < ShotSmokeNextTime || ShotSmokeStage > 6) return;
	if (ShotSmokeStage == 0) Host->ToggleMenu();
	else if (ShotSmokeStage == 2)
	{
		if (UBrMenuWidget* Menu = Host->GetMenuWidget()) Menu->Act(TEXT("Settings"));
	}
	else if (ShotSmokeStage == 4)
	{
		Host->CloseMenu();
		Host->ServerUnstuck();
	}
	else if (ShotSmokeStage == 6)
	{
		UE_LOG(LogBr, Display, TEXT("SHOT SMOKE PASS: 3 menu screenshots requested"));
		++ShotSmokeStage;
		return;
	}
	else
	{
		static const TCHAR* Names[] = {TEXT("esc_menu"), TEXT("settings_bindings"), TEXT("unstuck_progress")};
		const TCHAR* Name = Names[ShotSmokeStage / 2];
		if (ShotSmokeStage == 5 && Host->UnstuckEnd <= 0)
		{
			UE_LOG(LogBr, Error, TEXT("Menu screenshot requires active unstuck progress"));
			ShotSmokeStage = 7;
			return;
		}
		UE_LOG(LogBr, Display, TEXT("Shot smoke capture: Name=%s"), Name);
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Menu_%s"), Name), true, false);
	}
	++ShotSmokeStage;
	ShotSmokeNextTime = Now + 2;
}

// Single-process visual fixture: actual shared local LOS, NOT remote relevancy.
void ABrGameMode::ServiceMinimapShot()
{
	ABrPlayerController* Host = Cast<ABrPlayerController>(GetWorld()->GetFirstPlayerController());
	ABrCharacter* Pawn = Host ? Cast<ABrCharacter>(Host->GetPawn()) : nullptr;
	if (!Pawn || !Host->IsLocalController() || !HasAuthority()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - MatchBeganTime < 5 || Now < ShotSmokeNextTime || ShotSmokeStage > 10) return;
	ABrGameState* GS = GetGameState<ABrGameState>();
	if (ShotSmokeStage == 0)
	{
		Host->CloseMap();
		Pawn->GetCharacterMovement()->DisableMovement(); // Deterministic dev fixture only.
		Pawn->SetActorLocation(FVector(-1000, 0, 100), false, nullptr, ETeleportType::TeleportPhysics);
		Pawn->SetActorRotation(FRotator(0, 37, 0));
		Host->SetControlRotation(FRotator(0, 37, 0));
		if (USpringArmComponent* Arm = Pawn->FindComponentByClass<USpringArmComponent>())
			Arm->SetWorldRotation(FRotator(-65, 37, 0));
		ABrPlayerState* Own = Pawn->GetPlayerState<ABrPlayerState>();
		Own->Team = 0;
		Own->TeamColor = FLinearColor::Blue;
		auto Spawn = [&](int32 Team, int32 Id, FVector Position) -> ABrCharacter*
		{
			AAIController* Controller = GetWorld()->SpawnActor<AAIController>();
			if (!Controller) return nullptr;
			Controller->InitPlayerState();
			ABrPlayerState* PS = Controller->GetPlayerState<ABrPlayerState>();
			if (!PS) { Controller->Destroy(); return nullptr; }
			PS->Team = Team;
			PS->SetPlayerId(Id);
			PS->TeamColor = Team == 0 ? FLinearColor::Blue : FLinearColor::Red;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ABrCharacter* Bot = GetWorld()->SpawnActor<ABrCharacter>(Position, FRotator::ZeroRotator, Params);
			if (!Bot) { Controller->Destroy(); return nullptr; }
			Controller->Possess(Bot);
			Bot->GetCharacterMovement()->DisableMovement();
			return Bot;
		};
		// Enemy outside own 15m radius, but within teammate vision: shared marker.
		MinimapShotFriend = Spawn(0, 910, FVector(1000, 0, 100));
		MinimapShotEnemy = Spawn(1, 911, FVector(2000, 200, 100));
		if (!MinimapShotFriend.IsValid() || !MinimapShotEnemy.IsValid())
		{
			UE_LOG(LogBr, Error, TEXT("Minimap screenshot fixture spawn failed."));
			ShotSmokeStage = 11;
			return;
		}
		if (SafeZone)
		{
			SafeZone->State.Current = {FVector::ZeroVector, 10000};
			SafeZone->State.Target = {FVector(2000, 1000, 0), 5000};
			SafeZone->State.WaitEnd = Now + 1000;
			SafeZone->State.ShrinkEnd = Now + 2000;
			SafeZone->State.DamagePerSecond = 0;
		}
	}
	else if (ShotSmokeStage == 2)
	{
		Pawn->SetActorLocation(FVector(-9700, 0, 100), false, nullptr, ETeleportType::TeleportPhysics);
		MinimapShotFriend->SetActorLocation(FVector(-8700, 200, 100), false, nullptr, ETeleportType::TeleportPhysics);
		MinimapShotEnemy->SetActorLocation(FVector(-7700, 0, 100), false, nullptr, ETeleportType::TeleportPhysics);
		MinimapShotFriend->GetLife()->State = EBrLifeState::Downed;
		MinimapShotFriend->GetLife()->DownedEndTime = Now + 1000;
	}
	else if (ShotSmokeStage == 4) Host->ToggleMap();
	else if (ShotSmokeStage == 6)
	{
		Host->CloseMap();
		GS->MatchSettings.Mode = EBrMode::AllVsAll;
		// Keep enemy visible in own vision to assert the all-vs-all exclusion.
		MinimapShotEnemy->SetActorLocation(FVector(-9000, 0, 100), false, nullptr, ETeleportType::TeleportPhysics);
	}
	else if (ShotSmokeStage == 8) Host->ToggleMap();
	else if (ShotSmokeStage == 10)
	{
		UE_LOG(LogBr, Display, TEXT("SHOT SMOKE PASS: 5 single-process minimap screenshots requested"));
		++ShotSmokeStage;
		return;
	}
	else
	{
		const UBrFogComponent* Fog = Host->FindComponentByClass<UBrFogComponent>();
		if (!Fog || !Fog->GetVisibleMask() || !Fog->CanSee(MinimapShotEnemy.Get()))
		{
			UE_LOG(LogBr, Error, TEXT("Minimap screenshot fixture requires a currently visible enemy."));
			ShotSmokeStage = 11;
			return;
		}
		static const TCHAR* Names[] = {TEXT("grouped"), TEXT("zone_edge"), TEXT("full_map"), TEXT("all_vs_all"), TEXT("all_vs_all_full")};
		const TCHAR* Name = Names[ShotSmokeStage / 2];
		const float Distance = FVector::Dist2D(Pawn->GetActorLocation(), MinimapShotEnemy->GetActorLocation());
		UE_LOG(LogBr, Display, TEXT("Shot smoke capture: Name=%s CameraYaw=%.1f EnemyVisible=1 OwnDistance=%.0f Mode=%d"),
			Name, Pawn->GetCameraYaw(), Distance, int32(GS->MatchSettings.Mode));
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Minimap_%s"), Name), false, false);
	}
	++ShotSmokeStage;
	ShotSmokeNextTime = Now + 3;
}
#endif
