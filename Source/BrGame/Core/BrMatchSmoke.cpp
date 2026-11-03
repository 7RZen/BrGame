#include "BrGameMode.h"
#include "BrGameInstance.h"
#include "BrGameState.h"
#include "BrPlayerState.h"
#include "BrPlayerController.h"
#include "BrCharacter.h"
#include "BrCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "BrDeveloperSettings.h"
#include "BrInventoryComponent.h"
#include "BrCombatDefinitions.h"
#include "BrHealthComponent.h"
#include "BrLifeComponent.h"
#include "BrCombatMath.h"
#include "BrPickup.h"
#include "BrPickupManager.h"
#include "BrPickupDefinition.h"
#include "BrLog.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "CollisionQueryParams.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#if !UE_BUILD_SHIPPING
namespace
{
	int32 MatchSmokeRound = 0;
	bool bMatchSmokeFailed = false;
	FBrHitChanceRow SavedStanding;
	FBrHitChanceRow SavedCrouching;
	FBrHitChanceRow SavedProne;
	bool bSavedHitChance = false;

	void RestoreHitChance()
	{
		if (!bSavedHitChance) return;
		if (UBrDeveloperSettings* Settings = const_cast<UBrDeveloperSettings*>(UBrDeveloperSettings::Get()))
		{
			Settings->StandingHitChance = SavedStanding;
			Settings->CrouchingHitChance = SavedCrouching;
			Settings->ProneHitChance = SavedProne;
		}
		bSavedHitChance = false;
	}

	void RestoreFriendlyFire(ABrGameState* State, bool& bChanged, bool Original)
	{
		if (!bChanged) return;
		if (State) State->MatchSettings.bFriendlyFire = Original;
		bChanged = false;
	}

	void FailSmoke(const FString& Reason)
	{
		RestoreHitChance();
		bMatchSmokeFailed = true;
		UE_LOG(LogBr, Error, TEXT("E2E MATCH SMOKE FAIL: %s"), *Reason);
	}

	bool CheckSpawnClear(UWorld* World, const ABrCharacter* Character)
	{
		if (!World || !Character) return false;
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(BrMatchSmokeSpawn), false, Character);
		// Leave a small margin above the ground-contacting capsule so the floor itself is not
		// mistaken for an obstruction; walls/props crossing the body remain detectable.
		return !World->OverlapAnyTestByObjectType(Character->GetActorLocation(), FQuat::Identity, Objects,
			FCollisionShape::MakeCapsule(38.0f, 78.0f), Query);
	}

	bool CheckAliveMeshPose(const ABrCharacter* Character)
	{
		return Character &&
			FMath::IsNearlyZero(Character->GetMesh()->GetRelativeRotation().Pitch, 2.f) &&
			FMath::IsNearlyZero(Character->GetBaseRotationOffset().Rotator().Pitch, 2.f) &&
			FMath::IsNearlyEqual(Character->GetBaseTranslationOffset().Z,
				-Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), 0.1f);
	}

	bool ExercisePickups(UWorld* World, ABrCharacter* Character)
	{
		UBrDeveloperSettings* Settings = const_cast<UBrDeveloperSettings*>(UBrDeveloperSettings::Get());
		if (!World || !Character || !Settings) return false;
		const auto FindPickup = [Settings](FName Name) -> UBrPickupDefinition*
		{
			const TSoftObjectPtr<UBrPickupDefinition>* Entry = Settings->PickupRegistry.Find(Name);
			return Entry ? Entry->LoadSynchronous() : nullptr;
		};
		const auto Collect = [World, Character](UBrPickupDefinition* Definition)
		{
			ABrPickup* Pickup = ABrPickup::Spawn(World,
				FTransform(Character->GetActorLocation() + FVector(0, 0, 250)), Definition, FBrPickupPayload());
			return Pickup && Pickup->TryCollect(Character);
		};
		const TSoftObjectPtr<UBrWeaponDefinition>* RifleEntry = Settings->WeaponRegistry.Find(TEXT("AssaultRifle"));
		UBrWeaponDefinition* Rifle = RifleEntry ? RifleEntry->LoadSynchronous() : nullptr;
		if (!Rifle || !Collect(FindPickup(TEXT("AssaultRifle"))) || Character->GetInventory()->Weapon != Rifle)
			return false;

		Character->GetHealth()->SetHealth(Character->GetHealth()->MaxHealth - 5.0f);
		if (!Collect(FindPickup(TEXT("Health100"))) ||
			!FMath::IsNearlyEqual(Character->GetHealth()->Health, Character->GetHealth()->MaxHealth))
			return false;

		UBrInventoryComponent* Inventory = Character->GetInventory();
		Inventory->GiveGrenades(1);
		if (!Collect(FindPickup(TEXT("Grenade"))) || Inventory->Grenades != 2) return false;
		ABrPickup* AtCap = ABrPickup::Spawn(World,
			FTransform(Character->GetActorLocation() + FVector(0, 0, 250)), FindPickup(TEXT("Grenade")), FBrPickupPayload());
		const bool bAtCapRejected = AtCap && !AtCap->TryCollect(Character) && Inventory->Grenades == 2;
		if (AtCap) AtCap->Destroy();
		return bAtCapRejected;
	}
}
#endif

#if !UE_BUILD_SHIPPING
void ABrPlayerController::ReportMatchSmokeReady(ABrCharacter* ObserverPawn)
{
	if (HasAuthority() || !IsLocalController() || !ObserverPawn || ObserverPawn != GetPawn() ||
		IsMatchSmokeReady() || !FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke"))) return;
	MatchSmokeReadyPawn = ObserverPawn;
	ServerMatchSmokeReady(ObserverPawn);
}
#endif

void ABrPlayerController::ServerMatchSmokeReady_Implementation(ABrCharacter* ObserverPawn)
{
#if !UE_BUILD_SHIPPING
	const ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
	if (!FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke")) || IsLocalController() ||
		!Mode || Mode->IsLobbyMode() || !IsValid(ObserverPawn) || ObserverPawn != GetPawn() ||
		ObserverPawn->GetWorld() != GetWorld()) return;
	MatchSmokeReadyPawn = ObserverPawn;
	UE_LOG(LogBr, Display, TEXT("E2E MESH OBSERVER READY: %s"), *GetNameSafe(ObserverPawn));
#endif
}

#if !UE_BUILD_SHIPPING
void ABrCharacter::ServiceMeshPoseSmoke(float DeltaSeconds)
{
	// Each machine samples its own mesh, never the authoritative pawn via an RPC.
	// Previous-frame presentation includes movement smoothing; do not reapply it here.
	if (!FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke"))) return;
	if (IsLocallyControlled())
	{
		// Admission precedes client world BeginPlay (sometimes by seconds). Do
		// not let the fixture down/revive entirely before this machine can tick.
		if (ABrPlayerController* PC = Cast<ABrPlayerController>(GetController())) PC->ReportMatchSmokeReady(this);
		return;
	}
	if (bMeshPoseSmokeFailed) return;
	const bool bDowned = GetLife()->State == EBrLifeState::Downed;
	if (!bDowned && (!bMeshPoseSmokeDownPassed || !GetLife()->IsAlive() || bMeshPoseSmokeRevivePassed)) return;
	if (bDowned != bMeshPoseSmokeDowned)
	{
		bMeshPoseSmokeDowned = bDowned;
		MeshPoseSmokeSeconds = 0;
		MeshPoseSmokeSamples = 0;
		if (bDowned) bMeshPoseSmokeDownPassed = bMeshPoseSmokeRevivePassed = false;
	}
	MeshPoseSmokeSeconds += DeltaSeconds;
	++MeshPoseSmokeSamples;
	// Allow a short replication/smoothing settling interval, then check every tick.
	if (MeshPoseSmokeSeconds < 0.2f) return;
	const float ExpectedPitch = bDowned ? 90.f : 0.f;
	const float Pitch = GetMesh()->GetRelativeRotation().Pitch;
	const float ExpectedZ = -GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + (bDowned ? 12.f : 0.f);
	if (!FMath::IsNearlyEqual(Pitch, ExpectedPitch, 2.f) ||
		!FMath::IsNearlyEqual(GetBaseRotationOffset().Rotator().Pitch, ExpectedPitch, 2.f) ||
		!FMath::IsNearlyEqual(GetBaseTranslationOffset().Z, ExpectedZ, 0.1f))
	{
		bMeshPoseSmokeFailed = true;
		UE_LOG(LogBr, Error, TEXT("E2E MESH POSE FAIL: Pawn=%s Authority=%d Downed=%d Pitch=%.2f BasePitch=%.2f BaseZ=%.2f ExpectedZ=%.2f"),
			*GetName(), HasAuthority(), bDowned, Pitch, GetBaseRotationOffset().Rotator().Pitch,
			GetBaseTranslationOffset().Z, ExpectedZ);
		return;
	}
	if (MeshPoseSmokeSeconds >= 0.8f && MeshPoseSmokeSamples >= 10 &&
		(bDowned ? !bMeshPoseSmokeDownPassed : !bMeshPoseSmokeRevivePassed))
	{
		if (bDowned) bMeshPoseSmokeDownPassed = true;
		else bMeshPoseSmokeRevivePassed = true;
		UE_LOG(LogBr, Display, TEXT("E2E MESH POSE %s PASS: Pawn=%s Authority=%d NonOwner=1 Seconds=%.2f Samples=%d Pitch=%.2f"),
			bDowned ? TEXT("DOWNED") : TEXT("REVIVED"), *GetName(), HasAuthority(),
			MeshPoseSmokeSeconds, MeshPoseSmokeSamples, Pitch);
	}
}
#endif

void ABrGameMode::ServiceMatchSmoke()
{
#if !UE_BUILD_SHIPPING
	if (!HasAuthority() || bMatchSmokeFailed || !FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke"))) return;
	UWorld* World = GetWorld();
	ABrGameState* GS = GetGameState<ABrGameState>();
	ABrPlayerController* Host = Cast<ABrPlayerController>(World ? World->GetFirstPlayerController() : nullptr);
	if (!World || !GS || !Host || !Host->IsLocalController()) return;

	if (IsLobbyMode())
	{
		if (MatchSmokeRound == 2)
		{
			UE_LOG(LogBr, Display, TEXT("E2E MATCH SMOKE PASS: two rounds, clean reset, %s"),
				GS->MatchSettings.Mode == EBrMode::Grouped ? TEXT("grouped") : TEXT("all-vs-all"));
			if (UBrGameInstance* GI = GetGameInstance<UBrGameInstance>()) GI->bSmokeFinished = true;
			MatchSmokeRound = 3;
			return;
		}
		if (GS->PlayerArray.Num() < 3) return;
		if (MatchSmokeRound == 0)
		{
			MatchSmokeRound = 1;
			UE_LOG(LogBr, Display, TEXT("E2E MATCH SMOKE: starting first round with %d players"), GS->PlayerArray.Num());
			StartRound(Host);
		}
		else if (MatchSmokeRound == 1)
		{
			for (APlayerState* Entry : GS->PlayerArray)
			{
				const ABrPlayerState* PS = Cast<ABrPlayerState>(Entry);
				if (PS && (PS->DeathCount != 0 || PS->VoluntaryRespawnCount != 0 || PS->bEliminated))
				{
					FailSmoke(FString::Printf(TEXT("Round ledger was not reset for %s"), *PS->GetPlayerName()));
					return;
				}
			}
			UE_LOG(LogBr, Display, TEXT("E2E SECOND ROUND CLEAN: deaths, penalties and elimination reset"));
			MatchSmokeRound = 2;
			StartRound(Host);
		}
		return;
	}

	if (GS->bResults) return;
	const float Now = World->GetTimeSeconds();
	if (MatchSmokeStage == 0)
	{
		// A log handshake lets the runner join its third remote player during the
		// live match. Continue on admission, or fail after a bounded deadline.
		if (MatchSmokeDeadline == 0)
		{
			MatchSmokeDeadline = Now + 25.0f;
			UE_LOG(LogBr, Display, TEXT("E2E LATE JOIN WINDOW: active match; waiting for third remote client"));
			return;
		}
		if (GS->PlayerArray.Num() < 4)
		{
			if (Now >= MatchSmokeDeadline) FailSmoke(TEXT("Late join did not produce host plus three remote contestants."));
			return;
		}
		for (APlayerState* Entry : GS->PlayerArray)
			if (Entry && Entry->GetPlayerName() == TEXT("MatchLateJoin"))
			{
				const ABrPlayerController* Observer = Cast<ABrPlayerController>(Entry->GetPlayerController());
				if (!Observer || !Observer->IsMatchSmokeReady())
				{
					if (Now >= MatchSmokeDeadline) FailSmoke(TEXT("Non-owning mesh observer did not reach client BeginPlay."));
					return;
				}
			}
		for (APlayerState* Entry : GS->PlayerArray)
			if (Entry && Entry->GetPlayerName() == TEXT("MatchLateJoin"))
			{
				ABrPlayerController* Observer = Cast<ABrPlayerController>(Entry->GetPlayerController());
				if (!ServiceHitSmoke(Host, Observer)) return;
				if (!ServiceAudioSmoke(Host, Observer)) return;
			}
		for (APlayerState* Entry : GS->PlayerArray)
		{
			const ABrPlayerState* PS = Cast<ABrPlayerState>(Entry);
			const ABrCharacter* Pawn = PS ? Cast<ABrCharacter>(PS->GetPawn()) : nullptr;
			if (!Pawn || !CheckSpawnClear(World, Pawn) || !CheckAliveMeshPose(Pawn))
			{
				FailSmoke(FString::Printf(TEXT("Missing or obstructed round spawn for %s"), *GetNameSafe(PS)));
				return;
			}
			if (PS->GetPlayerName() == TEXT("MatchLateJoin"))
			{
				double NearestEnemySquared = TNumericLimits<double>::Max();
				for (APlayerState* OtherEntry : GS->PlayerArray)
				{
					const ABrPlayerState* OtherPS = Cast<ABrPlayerState>(OtherEntry);
					const ABrCharacter* OtherPawn = OtherPS ? Cast<ABrCharacter>(OtherPS->GetPawn()) : nullptr;
					if (OtherPawn && OtherPawn != Pawn && IsEnemy(Pawn, OtherPawn))
						NearestEnemySquared = FMath::Min(NearestEnemySquared,
							FVector::DistSquared2D(Pawn->GetActorLocation(), OtherPawn->GetActorLocation()));
				}
				const float RequiredDistance = UBrDeveloperSettings::Get()->MinSpawnDistanceFromEnemies;
				if (NearestEnemySquared < FMath::Square(RequiredDistance))
				{
					FailSmoke(FString::Printf(TEXT("Late join spawn too close to enemy: %.0f cm (required %.0f)"),
						FMath::Sqrt(NearestEnemySquared), RequiredDistance));
					return;
				}
				UE_LOG(LogBr, Display, TEXT("E2E LATE JOIN SPAWN PASS: MinEnemyDistance=%.0f"),
					FMath::Sqrt(NearestEnemySquared));
			}
		}
		UE_LOG(LogBr, Display, TEXT("E2E SPAWNS PASS: Players=%d geometry-clear=1"), GS->PlayerArray.Num());
		if (!ExercisePickups(World, Cast<ABrCharacter>(Host->GetPawn())))
		{
			FailSmoke(TEXT("Weapon/health/grenade pickup rule check failed."));
			return;
		}
		UE_LOG(LogBr, Display, TEXT("E2E PICKUPS PASS: weapon swap, health cap and grenade cap"));
		// Collection was tested above. Isolate subsequent exact gear-retention
		// assertions from random live map pickups at respawn destinations.
		// Each new round still creates/tests its normal pickup population.
		if (PickupManager) PickupManager->ClearRoundPickups();

		ABrCharacter* Shooter = Cast<ABrCharacter>(Host->GetPawn());
		if (!Shooter)
		{
			FailSmoke(TEXT("Host shooter pawn is missing."));
			return;
		}
		ABrCharacter* Target = nullptr;
		for (APlayerState* Entry : GS->PlayerArray)
			if (ABrPlayerState* PS = Cast<ABrPlayerState>(Entry))
				if (!PS->bHost && PS->GetPlayerName() != TEXT("MatchLateJoin"))
				{
					ABrCharacter* Candidate = Cast<ABrCharacter>(PS->GetPawn());
					if (Candidate && IsEnemy(Shooter, Candidate)) { Target = Candidate; break; }
				}
		if (!Target || !Shooter->GetInventory()->Weapon)
		{
			FailSmoke(TEXT("Could not resolve shooter, target or weapon."));
			return;
		}

		bool bLineFound = false;
		FVector Destination = Target->GetActorLocation();
		for (int32 I = 0; I < 24 && !bLineFound; ++I)
		{
			const float Angle = 2.0f * UE_PI * I / 24.0f;
			const FVector Offset(FMath::Cos(Angle) * 500.0f, FMath::Sin(Angle) * 500.0f, 0);
			Destination = Shooter->GetActorLocation() + Offset;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(BrMatchSmokeLOS), false, Shooter);
			Query.AddIgnoredActor(Target);
			FHitResult Hit;
			bLineFound = !World->LineTraceSingleByChannel(Hit, Shooter->GetActorLocation(), Destination,
				ECC_Visibility, Query);
		}
		if (!bLineFound)
		{
			FailSmoke(TEXT("No unobstructed short-range shot lane found."));
			return;
		}
		Target->SetActorLocation(Destination, false, nullptr, ETeleportType::TeleportPhysics);
		Target->ForceNetUpdate();
		const FVector Aim = (Destination - Shooter->GetActorLocation()).GetSafeNormal2D();
		Host->SetControlRotation(Aim.Rotation());
		Shooter->SetActorRotation(Aim.Rotation());
		UBrDeveloperSettings* Settings = const_cast<UBrDeveloperSettings*>(UBrDeveloperSettings::Get());
		MatchSmokeTarget = Target;
		MatchSmokeTargetHealth = Target->GetHealth()->Health;
		MatchSmokeExpectedDamage = Shooter->GetInventory()->Weapon->Damage *
			(Target->GetPosture() == EBrPosture::Prone ? Settings->ProneDamageMultiplier :
				Target->GetPosture() == EBrPosture::Crouching ? Settings->CrouchingDamageMultiplier :
				Settings->StandingDamageMultiplier);
		SavedStanding = Settings->StandingHitChance;
		SavedCrouching = Settings->CrouchingHitChance;
		SavedProne = Settings->ProneHitChance;
		bSavedHitChance = true;
		for (FBrHitChanceRow* Row : {&Settings->StandingHitChance, &Settings->CrouchingHitChance, &Settings->ProneHitChance})
			Row->Standing = Row->Crouching = Row->Prone = 100.0f;
		Shooter->GetInventory()->StopFire();
		Shooter->GetInventory()->StartFire(); // Real authoritative bullet path; only hit chance is forced.
		Shooter->GetInventory()->StopFire();  // Keep the test to one deterministic bullet, even for automatic weapons.
		MatchSmokeDeadline = Now + 4.0f;
		MatchSmokeStage = 1;
		return;
	}
	if (MatchSmokeStage == 1)
	{
		ABrCharacter* Target = MatchSmokeTarget.Get();
		if (Target && FMath::IsNearlyEqual(MatchSmokeTargetHealth - Target->GetHealth()->Health,
			MatchSmokeExpectedDamage, 0.1f))
		{
			RestoreHitChance();
			UE_LOG(LogBr, Display, TEXT("E2E SHOT PASS: Damage=%.1f HitChanceForced=100 Target=%s"),
				MatchSmokeExpectedDamage, *GetNameSafe(Target));
			ABrCharacter* Reviver = nullptr;
			for (APlayerState* Entry : GS->PlayerArray)
				if (ABrPlayerState* PS = Cast<ABrPlayerState>(Entry))
					if (ABrCharacter* Candidate = Cast<ABrCharacter>(PS->GetPawn()))
						if (Candidate != Target && Candidate->GetLife()->IsAlive() &&
							(GS->MatchSettings.Mode != EBrMode::Grouped || AreTeammates(Candidate, Target)))
						{
							Reviver = Candidate;
							break;
						}
			if (!Reviver)
			{
				FailSmoke(TEXT("Could not find a living teammate/helper for revive."));
				return;
			}
			if (GS->MatchSettings.Mode == EBrMode::Grouped)
			{
				MatchSmokeOriginalReviveRule = GS->MatchSettings.ReviveRule;
				GS->MatchSettings.ReviveRule = EBrReviveRule::TeammatesOnly;
				bMatchSmokeReviveRuleChanged = true;
			}
			MatchSmokeReviver = Reviver;
			Reviver->SetActorLocation(Target->GetActorLocation() + FVector(100, 0, 0),
				false, nullptr, ETeleportType::TeleportPhysics);
			// Exercise both low postures, then a late crouch callback while downed.
			Target->GetBrMovement()->RequestPosture(
				MatchSmokeRound == 1 ? EBrPosture::Crouching : EBrPosture::Prone);
			Target->GetBrMovement()->Crouch(false);
			if (!Target->bIsCrouched)
			{
				FailSmoke(TEXT("Could not crouch/prone before down."));
				return;
			}
			Target->GetLife()->Down();
			const float HeightAdjust = Target->GetClass()->GetDefaultObject<ABrCharacter>()->
				GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() -
				Target->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
			Target->OnStartCrouch(HeightAdjust, HeightAdjust * Target->GetCapsuleComponent()->GetShapeScale());
			// The late-join observer was spawned outside enemy fog range. Moving it
			// into this lane now also exercises initial Downed OnRep on relevancy entry.
			for (APlayerState* Entry : GS->PlayerArray)
				if (Entry && Entry->GetPlayerName() == TEXT("MatchLateJoin"))
					if (ABrCharacter* Observer = Cast<ABrCharacter>(Entry->GetPawn()))
					{
						Observer->SetActorLocation(Reviver->GetActorLocation(), false, nullptr, ETeleportType::TeleportPhysics);
						Observer->ForceNetUpdate();
					}
			Reviver->GetLife()->StartInteract();
			MatchSmokeDeadline = Now + 5.0f;
			MatchSmokeStage = 2;
			return;
		}
		if (Target && Target->GetHealth()->Health < MatchSmokeTargetHealth &&
			!FMath::IsNearlyEqual(MatchSmokeTargetHealth - Target->GetHealth()->Health, MatchSmokeExpectedDamage, 0.1f))
		{
			FailSmoke(FString::Printf(TEXT("Shot damage mismatch: expected %.1f, actual %.1f"),
				MatchSmokeExpectedDamage, MatchSmokeTargetHealth - Target->GetHealth()->Health));
			return;
		}
		if (Now >= MatchSmokeDeadline) FailSmoke(TEXT("Real server-side shot did not reduce target health within four seconds."));
		return;
	}
	if (MatchSmokeStage == 2)
	{
		ABrCharacter* Target = MatchSmokeTarget.Get();
		ABrCharacter* Reviver = MatchSmokeReviver.Get();
		if (Target && Target->GetLife()->State == EBrLifeState::Downed)
		{
			// Exercise OnEndCrouch after Down, including replicated posture updates.
			Target->GetBrMovement()->RequestPosture(EBrPosture::Standing);
			Target->GetBrMovement()->UnCrouch(false);
			Target->ForceNetUpdate();
		}
		if (Target && Reviver && Target->GetLife()->IsAlive())
		{
			MatchSmokeRevivedHealth = Target->GetHealth()->Health;
			const float Expected = Target->GetHealth()->MaxHealth *
				FMath::Clamp(UBrDeveloperSettings::Get()->RevivedHealthFraction, 0.01f, 1.0f);
			if (!FMath::IsNearlyEqual(MatchSmokeRevivedHealth, Expected, 0.1f))
			{
				FailSmoke(FString::Printf(TEXT("Revive health mismatch: expected %.1f, actual %.1f"), Expected, MatchSmokeRevivedHealth));
				return;
			}
			UE_LOG(LogBr, Display, TEXT("E2E REVIVE PASS: Health=%.1f TeammatesOnly=%d"),
				MatchSmokeRevivedHealth, GS->MatchSettings.Mode == EBrMode::Grouped);
			if (bMatchSmokeReviveRuleChanged)
			{
				GS->MatchSettings.ReviveRule = MatchSmokeOriginalReviveRule;
				bMatchSmokeReviveRuleChanged = false;
			}
			// Keep Alive replicated long enough for non-owning machines to sample
			// the restored pose over multiple smoothing ticks before the next down.
			MatchSmokeDeadline = Now + 1.5f;
			MatchSmokeStage = 20;
			return;
		}
		if (Now >= MatchSmokeDeadline) FailSmoke(TEXT("Teammate revive did not complete before timeout."));
		return;
	}
	if (MatchSmokeStage == 20)
	{
		if (Now < MatchSmokeDeadline) return;
		ABrCharacter* Target = MatchSmokeTarget.Get();
		if (Target && Target->GetLife()->IsAlive())
		{
			// Revived low-posture transitions must not retain the +12 downed offset.
			for (EBrPosture Posture : {EBrPosture::Crouching, EBrPosture::Prone, EBrPosture::Standing})
			{
				Target->GetBrMovement()->RequestPosture(Posture);
				if (Posture == EBrPosture::Standing) Target->GetBrMovement()->UnCrouch(false);
				else Target->GetBrMovement()->Crouch(false);
				if (!FMath::IsNearlyEqual(Target->GetBaseTranslationOffset().Z,
					-Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), 0.1f) ||
					!FMath::IsNearlyZero(Target->GetMesh()->GetRelativeRotation().Pitch, 2.f))
				{
					FailSmoke(TEXT("Post-revive crouch/prone mesh offset mismatch."));
					return;
				}
			}
			MatchSmokeTargetController = Target->GetController();
			MatchSmokeGearWeapon = Target->GetInventory()->Weapon;
			MatchSmokeGearLoaded = Target->GetInventory()->Loaded;
			MatchSmokeGearReserve = Target->GetInventory()->GetReserve();
			MatchSmokeGearGrenades = Target->GetInventory()->Grenades;
			Target->GetLife()->Down();
			Target->GetLife()->StartRespawnHold();
			MatchSmokeDeadline = Now + 5.0f;
			MatchSmokeStage = 3;
			return;
		}
		FailSmoke(TEXT("Revived pawn disappeared during mesh observation."));
		return;
	}
	if (MatchSmokeStage == 3)
	{
		AController* Controller = MatchSmokeTargetController.Get();
		ABrCharacter* OldTarget = MatchSmokeTarget.Get();
		ABrCharacter* Respawned = Controller ? Cast<ABrCharacter>(Controller->GetPawn()) : nullptr;
		const ABrPlayerState* PS = Controller ? Controller->GetPlayerState<ABrPlayerState>() : nullptr;
		if (Respawned && Respawned != OldTarget && Respawned->GetLife()->IsAlive() && PS)
		{
			const bool bGearKept = Respawned->GetInventory()->Weapon == MatchSmokeGearWeapon.Get() &&
				Respawned->GetInventory()->Loaded == MatchSmokeGearLoaded &&
				Respawned->GetInventory()->GetReserve() == MatchSmokeGearReserve &&
				Respawned->GetInventory()->Grenades == MatchSmokeGearGrenades;
			const float ExpectedMax = UBrDeveloperSettings::Get()->BaseMaxHealth *
				(1.0f - UBrDeveloperSettings::Get()->MaxHealthPenaltyPerRespawn);
			if (!CheckAliveMeshPose(Respawned) || PS->VoluntaryRespawnCount != 1 ||
				!FMath::IsNearlyEqual(Respawned->GetHealth()->MaxHealth, ExpectedMax, 0.1f) ||
				!bGearKept || !Respawned->GetLife()->IsInvulnerable())
			{
				FailSmoke(TEXT("Voluntary respawn penalty, gear retention or invulnerability assertion failed."));
				return;
			}
			UE_LOG(LogBr, Display, TEXT("E2E RESPAWN PASS: MaxHealth=%.1f GearKept=1 Invulnerable=1"),
				Respawned->GetHealth()->MaxHealth);
			ABrCharacter* Shooter = Cast<ABrCharacter>(Host->GetPawn());
			ABrCharacter* Near = nullptr;
			ABrCharacter* Outside = nullptr;
			for (APlayerState* Entry : GS->PlayerArray)
				if (ABrPlayerState* OtherPS = Cast<ABrPlayerState>(Entry))
					if (!OtherPS->bHost)
						if (ABrCharacter* Candidate = Cast<ABrCharacter>(OtherPS->GetPawn()))
							if (Candidate != Respawned)
							{
								if (!Near) Near = Candidate;
								else if (!Outside) Outside = Candidate;
							}
			if (!Shooter || !Near || !Outside)
			{
				FailSmoke(TEXT("Could not establish near/outside grenade damage targets."));
				return;
			}
			bool bClearLane = false;
			FVector Aim = FVector::ForwardVector;
			for (int32 I = 0; I < 24 && !bClearLane; ++I)
			{
				const float Angle = 2.0f * UE_PI * I / 24.0f;
				Aim = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0);
				FCollisionQueryParams Query(SCENE_QUERY_STAT(BrMatchSmokeGrenadeLane), false);
				for (APlayerState* OtherEntry : GS->PlayerArray)
					if (OtherEntry && OtherEntry->GetPawn()) Query.AddIgnoredActor(OtherEntry->GetPawn());
				FHitResult Hit;
				bClearLane = !World->LineTraceSingleByChannel(Hit, Shooter->GetActorLocation(),
					Shooter->GetActorLocation() + Aim * 1000.0f, ECC_Visibility, Query);
			}
			if (!bClearLane)
			{
				FailSmoke(TEXT("No unobstructed grenade test lane found."));
				return;
			}
			Near->SetActorLocation(Shooter->GetActorLocation() + Aim * 300.0f,
				false, nullptr, ETeleportType::TeleportPhysics);
			Outside->SetActorLocation(Shooter->GetActorLocation() + Aim * 650.0f,
				false, nullptr, ETeleportType::TeleportPhysics);
			Near->GetHealth()->SetHealth(Near->GetHealth()->MaxHealth);
			Outside->GetHealth()->SetHealth(Outside->GetHealth()->MaxHealth);
			if (GS->MatchSettings.Mode == EBrMode::Grouped)
			{
				MatchSmokeOriginalFriendlyFire = GS->MatchSettings.bFriendlyFire;
				GS->MatchSettings.bFriendlyFire = true;
				bMatchSmokeFriendlyFireChanged = true;
			}
			const UBrThrowableDefinition* Grenade = UBrDeveloperSettings::Get()->HandGrenade.LoadSynchronous();
			if (!Grenade || Shooter->GetInventory()->Grenades <= 0)
			{
				FailSmoke(TEXT("Hand grenade definition or inventory is missing."));
				return;
			}
			Host->SetControlRotation(Aim.Rotation());
			Shooter->SetActorRotation(Aim.Rotation());
			MatchSmokeGrenadeTarget = Near;
			MatchSmokeGrenadeOutside = Outside;
			MatchSmokeGrenadeTargetHealth = Near->GetHealth()->Health;
			MatchSmokeGrenadeOutsideHealth = Outside->GetHealth()->Health;
			MatchSmokeExpectedGrenadeDamage = Grenade->Damage * UBrDeveloperSettings::Get()->StandingDamageMultiplier;
			const int32 GrenadesBefore = Shooter->GetInventory()->Grenades;
			Shooter->GetInventory()->RequestGrenade();
			if (Shooter->GetInventory()->Grenades != GrenadesBefore - 1)
			{
				FailSmoke(TEXT("Real grenade throw did not consume exactly one grenade."));
				return;
			}
			MatchSmokeDeadline = Now + 4.0f;
			MatchSmokeStage = 4;
			return;
		}
		if (Now >= MatchSmokeDeadline) FailSmoke(TEXT("Voluntary respawn did not complete before timeout."));
		return;
	}
	if (MatchSmokeStage == 4)
	{
		ABrCharacter* Near = MatchSmokeGrenadeTarget.Get();
		ABrCharacter* Outside = MatchSmokeGrenadeOutside.Get();
		if (Near && Outside && FMath::IsNearlyEqual(MatchSmokeGrenadeTargetHealth - Near->GetHealth()->Health,
			MatchSmokeExpectedGrenadeDamage, 0.1f))
		{
			if (!FMath::IsNearlyEqual(MatchSmokeGrenadeOutsideHealth, Outside->GetHealth()->Health, 0.1f))
			{
				FailSmoke(TEXT("Grenade damaged a player outside the configured blast radius."));
				return;
			}
			RestoreFriendlyFire(GS, bMatchSmokeFriendlyFireChanged, MatchSmokeOriginalFriendlyFire);
			UE_LOG(LogBr, Display, TEXT("E2E GRENADE PASS: Impact=1 Damage=%.1f OutsideRadiusUnaffected=1"),
				MatchSmokeExpectedGrenadeDamage);
			AController* TimeoutController = Outside->GetController();
			ABrPlayerState* TimeoutPS = TimeoutController ? TimeoutController->GetPlayerState<ABrPlayerState>() : nullptr;
			if (!TimeoutController || !TimeoutPS)
			{
				FailSmoke(TEXT("Could not resolve timeout-respawn fixture controller."));
				return;
			}
			MatchSmokeTarget = Outside;
			MatchSmokeTargetController = TimeoutController;
			MatchSmokeExpectedMaxHealth = Outside->GetHealth()->MaxHealth;
			MatchSmokeGearWeapon = Outside->GetInventory()->Weapon;
			MatchSmokeGearLoaded = Outside->GetInventory()->Loaded;
			MatchSmokeGearReserve = Outside->GetInventory()->GetReserve();
			MatchSmokeGearGrenades = Outside->GetInventory()->Grenades;
			MatchSmokeTimeoutDeaths = TimeoutPS->DeathCount;
			Outside->GetLife()->Down(); // No hold input: this must resolve by the configured timeout.
			MatchSmokeDeadline = Now + UBrDeveloperSettings::Get()->DownedDuration + 5.0f;
			MatchSmokeStage = 5;
			return;
		}
		if (Now >= MatchSmokeDeadline) FailSmoke(TEXT("Thrown grenade did not impact and apply configured damage before timeout."));
		return;
	}
	if (MatchSmokeStage == 5)
	{
		AController* Controller = MatchSmokeTargetController.Get();
		ABrCharacter* OldTarget = MatchSmokeTarget.Get();
		ABrCharacter* Respawned = Controller ? Cast<ABrCharacter>(Controller->GetPawn()) : nullptr;
		const ABrPlayerState* PS = Controller ? Controller->GetPlayerState<ABrPlayerState>() : nullptr;
		if (Respawned && Respawned != OldTarget && Respawned->GetLife()->IsAlive() && PS)
		{
			const bool bGearKept = Respawned->GetInventory()->Weapon == MatchSmokeGearWeapon.Get() &&
				Respawned->GetInventory()->Loaded == MatchSmokeGearLoaded &&
				Respawned->GetInventory()->GetReserve() == MatchSmokeGearReserve &&
				Respawned->GetInventory()->Grenades == MatchSmokeGearGrenades;
			const int32 ExpectedDeaths = MatchSmokeTimeoutDeaths +
				(UBrDeveloperSettings::Get()->bTimeoutRespawnCountsAsDeath ? 1 : 0);
			if (!CheckAliveMeshPose(Respawned) ||
				!FMath::IsNearlyEqual(Respawned->GetHealth()->MaxHealth, MatchSmokeExpectedMaxHealth, 0.1f) ||
				!bGearKept || PS->DeathCount != ExpectedDeaths || !Respawned->GetLife()->IsInvulnerable())
			{
				FailSmoke(TEXT("Timeout respawn penalty, death accounting, gear retention or protection assertion failed."));
				return;
			}
			UE_LOG(LogBr, Display, TEXT("E2E TIMEOUT RESPAWN PASS: MaxHealth=%.1f Deaths=%d GearKept=1 Invulnerable=1"),
				Respawned->GetHealth()->MaxHealth, PS->DeathCount);
			for (APlayerState* Entry : GS->PlayerArray)
				if (ABrPlayerState* OtherPS = Cast<ABrPlayerState>(Entry))
					if (!OtherPS->bHost)
						if (ABrCharacter* Pawn = Cast<ABrCharacter>(OtherPS->GetPawn())) Pawn->GetLife()->Eliminate();
			MatchSmokeStage = 6;
			return;
		}
		if (Now >= MatchSmokeDeadline) FailSmoke(TEXT("Downed timeout did not respawn the player before the bounded deadline."));
		return;
	}
#endif
}
