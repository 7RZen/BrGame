#include "BrGameMode.h"
#include "BrAudioSubsystem.h"
#include "BrSafeZone.h"
#include "BrCharacter.h"
#include "BrGameState.h"
#include "BrGameInstance.h"
#include "BrSessionSubsystem.h"
#include "BrFlowRules.h"
#include "TimerManager.h"
#include "BrHUD.h"
#include "BrLog.h"
#include "BrPlayerController.h"
#include "BrPlayerState.h"
#include "BrDeveloperSettings.h"
#include "BrLifeComponent.h"
#include "BrLifeRules.h"
#include "BrCorpse.h"
#include "BrHealthComponent.h"
#include "BrInventoryComponent.h"
#include "BrPickupManager.h"
#include "String/LexFromString.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/GameSession.h"
#include "NavigationSystem.h"
#include "Kismet/GameplayStatics.h"

ABrGameMode::ABrGameMode()
{
	DefaultPawnClass = ABrCharacter::StaticClass();
	PlayerControllerClass = ABrPlayerController::StaticClass();
	GameStateClass = ABrGameState::StaticClass();
	PlayerStateClass = ABrPlayerState::StaticClass();
	HUDClass = ABrHUD::StaticClass();
	bUseSeamlessTravel = true;
}

void ABrGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	InitialMatchSettings = FBrMatchSettings();
	InitialMatchSettings.MapId = UBrDeveloperSettings::Get()->InitialMapId();
	if (const UBrGameInstance* GI = GetGameInstance<UBrGameInstance>())
	{
		if (GI->bCarrySettings)
		{
			InitialMatchSettings = GI->HostSettings;
		}
	}
	const FString Value = UGameplayStatics::ParseOption(Options, TEXT("CrateDropInterval"));
	if (!Value.IsEmpty())
	{
		float Seconds = 30;
		if (LexTryParseString(Seconds, *Value) && FMath::IsFinite(Seconds))
		{
			InitialMatchSettings.CrateDropInterval = FBrMatchSettings::ClampCrateDropInterval(Seconds);
		}
		else
		{
			UE_LOG(LogBr, Warning, TEXT("Invalid host CrateDropInterval '%s'; using 30 seconds."), *Value);
		}
	}
	auto ReadInt = [&](const TCHAR* Key, int32& Target)
	{
		const FString Text = UGameplayStatics::ParseOption(Options, Key);
		if (!Text.IsEmpty() && !LexTryParseString(Target, *Text))
			UE_LOG(LogBr, Warning, TEXT("Invalid host option %s"), Key);
	};
	ReadInt(TEXT("MaxPlayers"), InitialMatchSettings.MaxPlayers);
	ReadInt(TEXT("NumGroups"), InitialMatchSettings.NumGroups);
	if (UGameplayStatics::ParseOption(Options, TEXT("Mode")) == TEXT("Grouped"))
	{
		InitialMatchSettings.Mode = EBrMode::Grouped;
	}
	const FString MapOption = UGameplayStatics::ParseOption(Options, TEXT("MapId"));
	if (!MapOption.IsEmpty())
	{
		if (UBrDeveloperSettings::Get()->IsMapSelectionValid(FName(*MapOption)))
		{
			InitialMatchSettings.MapId = FName(*MapOption);
		}
		else UE_LOG(LogBr, Warning, TEXT("Rejected unknown MapId option '%s'"), *MapOption);
	}
	if (IsLobbyMode())
	{
		InitialMatchSettings.ResolvedMapId = NAME_None;
	}
	else if (InitialMatchSettings.ResolvedMapId.IsNone())
	{
		for (const FBrMapEntry& Entry : UBrDeveloperSettings::Get()->ValidMaps())
		{
			if (MapName.EndsWith(Entry.Map.ToSoftObjectPath().GetAssetName()))
			{
				InitialMatchSettings.ResolvedMapId = Entry.Id;
			}
		}
	}
	InitialMatchSettings.NumGroups = FMath::Clamp(InitialMatchSettings.NumGroups, 2, 1024);
	InitialMatchSettings.Sanitize();
	Super::InitGame(MapName, Options, ErrorMessage);
	// Our live admission policy counts gameplay players AND spectators. Do not retain
	// GameSession's independent default (16-player) cap when host selected unlimited.
	if (GameSession)
	{
		GameSession->MaxPlayers = 0;
		GameSession->MaxSpectators = MAX_int32;
	}
}

void ABrGameMode::InitGameState()
{
	Super::InitGameState();
	if (ABrGameState* GS = GetGameState<ABrGameState>())
	{
		GS->SetMatchSettings(InitialMatchSettings);
		GS->bLobby = IsLobbyMode();
		GS->GroupColors.Init(INDEX_NONE, InitialMatchSettings.NumGroups);
		if (const UBrGameInstance* GI = GetGameInstance<UBrGameInstance>())
		{
			if (GI->bCarrySettings && GI->GroupColors.Num() == InitialMatchSettings.NumGroups)
			{
				GS->GroupColors = GI->GroupColors;
				ColorChoosers = GI->ColorChoosers;
			}
		}
	}
	else UE_LOG(LogBr, Error, TEXT("Cannot initialize match settings: BrGameState is missing."));
}

void ABrGameMode::HandleMatchHasStarted()
{
	Super::HandleMatchHasStarted();
	MatchBeganTime = GetWorld()->GetTimeSeconds();
	GetWorldTimerManager().SetTimer(FlowTimer, this, &ABrGameMode::FlowService, 0.5f, true);
	UE_LOG(LogBr, Display, TEXT("Flow ready: %s Lobby=%d"), *GetWorld()->GetMapName(), IsLobbyMode());
	if (IsLobbyMode())
	{
		return;
	}
	SafeZone = GetWorld()->SpawnActor<ABrSafeZone>();
	if (!SafeZone) UE_LOG(LogBr, Error, TEXT("Could not create authoritative safe zone."));
	if (!HasAuthority() || IsValid(PickupManager))
	{
		return;
	}
	PickupManager = GetWorld()->SpawnActor<ABrPickupManager>();
	if (!PickupManager)
	{
		UE_LOG(LogBr, Error, TEXT("Could not create server pickup manager."));
		return;
	}
	PickupManager->Initialize(GetNumPlayers());
}

void ABrGameMode::PostLogin(APlayerController* NewPlayer)
{
	if (const ABrGameState* GS = GetGameState<ABrGameState>())
	{
		if (GS->MatchSettings.MaxPlayers > 0 && GS->PlayerArray.Num() > GS->MatchSettings.MaxPlayers)
		{
			if (GameSession)
			{
				GameSession->KickPlayer(NewPlayer, FText::FromString(TEXT("Server is full.")));
			}
			return;
		}
	}
	Super::PostLogin(NewPlayer);
	UE_LOG(LogBr, Display, TEXT("PostLogin: Controller=%s Pawn=%s"),
	       *GetNameSafe(NewPlayer), *GetNameSafe(NewPlayer ? NewPlayer->GetPawn() : nullptr));
}

bool ABrGameMode::CanDamage(AController* InstigatorController, const AActor* Victim) const
{
	return CanDamageSource(CaptureDamageSource(InstigatorController), Victim);
}

FBrDamageSource ABrGameMode::CaptureDamageSource(AController* Controller) const
{
	FBrDamageSource Source;
	const ABrPlayerState* PS = Controller ? Controller->GetPlayerState<ABrPlayerState>() : nullptr;
	if (HasAuthority() && PS && IsAdmitted(Controller))
	{
		Source.PlayerState = const_cast<ABrPlayerState*>(PS);
		Source.PlayerId = PS->GetPlayerId();
		Source.Team = PS->Team;
		Source.RoundKey = RoundKey(Controller);
	}
	return Source;
}

bool ABrGameMode::CanDamageSource(const FBrDamageSource& Source, const AActor* Victim, bool bZoneDamage) const
{
	const ABrCharacter* Character = Cast<ABrCharacter>(Victim);
	const ABrGameState* GS = GetGameState<ABrGameState>();
	if (GS && GS->bResults)
	{
		return false;
	}
	const ABrPlayerState* PS = Character ? Character->GetPlayerState<ABrPlayerState>() : nullptr;
	if (!bZoneDamage && PS && GS && GS->MatchSettings.Mode == EBrMode::Grouped && !GS->MatchSettings.bFriendlyFire &&
		!Source.IsSelf(PS->GetPlayerId()) && Source.IsFriendly(PS->Team))
	{
		return false;
	}
	return HasAuthority() && IsValid(Victim) && (!Character ||
		(Character->GetLife()->IsAlive() && (bZoneDamage || !Character->GetLife()->IsInvulnerable())));
}

void ABrGameMode::CreditDown(const FBrDamageSource& Source, const ABrCharacter* Victim)
{
	if (!HasAuthority() || !Source.IsValid())
	{
		return;
	}
	FBrRoundRecord* Record = RoundRecords.Find(Source.RoundKey);
	if (!Record || Record->PlayerId != Source.PlayerId)
	{
		return;
	}
	++Record->DownsInflicted;
	if (AController* Controller = Record->ActiveController.Get())
	{
		if (ABrPlayerState* PS = Controller->GetPlayerState<ABrPlayerState>())
		{
			PS->DownsInflicted = Record->DownsInflicted;
			PS->ForceNetUpdate();
			if (Victim && !CaptureOpponentDowner(Source, Victim, false).IsEmpty())
				if (ABrCharacter* Attacker = Cast<ABrCharacter>(Controller->GetPawn()))
					if (UBrAudioSubsystem* Audio = GetWorld()->GetSubsystem<UBrAudioSubsystem>()) Audio->SpeakDown(Attacker);
		}
	}
}

bool ABrGameMode::IsEnemy(const ABrCharacter* A, const ABrCharacter* B) const { return A != B && !AreTeammates(A, B); }

bool ABrGameMode::AreTeammates(const ABrCharacter* A, const ABrCharacter* B) const
{
	const ABrGameState* GS = GetGameState<ABrGameState>();
	const ABrPlayerState* PA = A ? A->GetPlayerState<ABrPlayerState>() : nullptr;
	const ABrPlayerState* PB = B ? B->GetPlayerState<ABrPlayerState>() : nullptr;
	return GS && GS->MatchSettings.Mode == EBrMode::Grouped && PA && PB && PA->Team >= 0 && PA->Team == PB->Team;
}

bool ABrGameMode::IsLocationInSafeZone(const FVector& Location) const
{
	return !IsValid(SafeZone) || SafeZone->IsInside(Location);
}

bool ABrGameMode::AreRespawnsDisabled() const { return IsValid(SafeZone) && SafeZone->IsSuddenDeath(); }

void ABrGameMode::OnPlayerEliminated(ABrPlayerState* Player)
{
	if (Player)
	{
		RecordPlayer(Cast<AController>(Player->GetOwner()));
	}
	UE_LOG(LogBr, Display, TEXT("OnPlayerEliminated: %s"), *GetNameSafe(Player));
	// Coalesced on FlowService: simultaneous eliminations in the same frame can produce a draw.
}

void ABrGameMode::LeaveCorpse(const ABrCharacter* Character)
{
	if (!HasAuthority() || !IsValid(Character))
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Ground = Character->GetActorLocation() -
		FVector(0, 0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	if (ABrCorpse* Corpse = GetWorld()->SpawnActor<ABrCorpse>(Ground, FRotator(0, Character->GetActorRotation().Yaw, 0),
	                                                          Params))
	{
		Corpse->SetGear(Character->GetInventory()->CaptureState());
	}
	else UE_LOG(LogBr, Error, TEXT("Failed to leave permanent corpse for %s"), *Character->GetName());
}

bool ABrGameMode::FindRespawnLocation(const ABrCharacter* Character, FVector& OutLocation,
                                      const ABrPlayerState* SpawnPS, bool bIgnoreZone) const
{
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	const UCapsuleComponent* Capsule = Character->GetClass()->GetDefaultObject<ABrCharacter>()->GetCapsuleComponent();
	TArray<FVector> Starts;
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		Starts.Add(It->GetActorLocation());
	}
	TArray<FVector> Candidates;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(BrSpawnClearance), false, Character);
	auto AddCandidate = [&](const FVector& Center)
	{
		if (!bIgnoreZone && !IsLocationInSafeZone(Center))
		{
			return;
		}
		if (!GetWorld()->OverlapBlockingTestByProfile(Center, FQuat::Identity, TEXT("Pawn"),
		                                              FCollisionShape::MakeCapsule(
			                                              Capsule->GetScaledCapsuleRadius(),
			                                              Capsule->GetScaledCapsuleHalfHeight()), Query))
		{
			Candidates.Add(Center);
		}
	};
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const bool bNavReady = Nav && Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) &&
		!UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld());
	if (bNavReady)
	{
		for (int32 Index = 0; Index < FMath::Clamp(Settings->SpawnCandidates, 1, 256); ++Index)
		{
			const FVector Anchor = Starts.IsEmpty()
				                       ? Character->GetActorLocation()
				                       : Starts[FMath::RandHelper(Starts.Num())];
			FNavLocation Projected, Point;
			if (Nav->ProjectPointToNavigation(Anchor, Projected) &&
				Nav->GetRandomReachablePointInRadius(Projected.Location, FMath::Max(1.0f, Settings->SpawnSearchRadius),
				                                     Point))
			{
				AddCandidate(Point.Location + FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight() + 4));
			}
		}
	}
	if (Candidates.IsEmpty())
	{
		UE_LOG(LogBr, Display, TEXT("Respawn nav candidates unavailable/blocked; testing PlayerStarts."));
		for (const FVector& Start : Starts)
		{
			AddCandidate(Start + FVector(0, 0, 4));
		}
	}
	if (!bIgnoreZone && IsValid(SafeZone))
	{
		// Prefer a safe landing in the announced next circle before enemy-distance scoring.
		const FBrZoneCircle Current = SafeZone->Circle();
		const FBrZoneCircle Target = SafeZone->State.Target;
		if (Candidates.IsEmpty())
		{
			// Center, then a bounded disk spiral. Projection can move a point outside the
			// circle: always recheck the final capsule center through AddCandidate.
			for (int32 Index = 0; Index <= 64; ++Index)
			{
				const double Angle = Index * 2.399963229728653; // Golden-angle disk sampling.
				const double Radius = Current.Radius * 0.95 * FMath::Sqrt(Index / 64.0);
				const FVector Probe = Current.Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Radius;
				if (bNavReady)
				{
					FNavLocation Point;
					if (Nav->ProjectPointToNavigation(Probe, Point, FVector(500, 500, 2000)))
					{
						AddCandidate(Point.Location + FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight() + 4));
					}
				}
				else
				{
					FHitResult Floor;
					if (GetWorld()->LineTraceSingleByChannel(Floor, Probe + FVector(0, 0, 1000),
					                                         Probe - FVector(0, 0, 2000), ECC_WorldStatic,
					                                         Query) && Floor.ImpactNormal.Z >= 0.7)
					{
						AddCandidate(Floor.ImpactPoint + FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight() + 4));
					}
				}
			}
			UE_LOG(LogBr, Display, TEXT("Zone spawn fallback: safe spiral candidates=%d Navigation=%d"),
			       Candidates.Num(), bNavReady);
		}
		const bool bHasTarget = Candidates.ContainsByPredicate([&](const FVector& P)
		{
			return FBrZoneRules::SpawnRank(P, Current, Target) == 0;
		});
		if (bHasTarget)
		{
			Candidates.RemoveAll([&](const FVector& P) { return FBrZoneRules::SpawnRank(P, Current, Target) != 0; });
		}
	}
	TArray<FBrSpawnThreat> Threats;
	for (TActorIterator<ABrCharacter> It(GetWorld()); It; ++It)
	{
		FBrSpawnThreat& Threat = Threats.AddDefaulted_GetRef();
		Threat.Location = It->GetActorLocation();
		Threat.bSelf = *It == Character;
		const ABrPlayerState* OtherPS = It->GetPlayerState<ABrPlayerState>();
		Threat.bEnemy = SpawnPS
			                ? !(SpawnPS->Team >= 0 && OtherPS && SpawnPS->Team == OtherPS->Team)
			                : IsEnemy(Character, *It);
		Threat.bAliveOrDowned = It->GetLife()->State != EBrLifeState::Eliminated;
	}
	const int32 Best = FBrLifeRules::PickSpawn(Candidates, Threats, Settings->MinSpawnDistanceFromEnemies);
	if (Best == INDEX_NONE)
	{
		UE_LOG(LogBr, Warning,
		       TEXT("Respawn failed: no capsule-clear safe point after PlayerStarts/center/spiral search."));
		return false;
	}
	OutLocation = Candidates[Best];
	// Living teammates are the group's anchor, including for late join. Test nav and capsule
	// clearance for each cluster candidate; never force a member into geometry or another pawn.
	if (SpawnPS && SpawnPS->Team >= 0 && bNavReady)
	{
		for (TActorIterator<ABrCharacter> It(GetWorld()); It; ++It)
		{
			const ABrPlayerState* OtherPS = It->GetPlayerState<ABrPlayerState>();
			if (*It == Character || !OtherPS || OtherPS->Team != SpawnPS->Team || !It->GetLife()->IsAlive())
			{
				continue;
			}
			for (int32 I = 0; I < 24; ++I)
			{
				FNavLocation Point;
				if (!Nav->GetRandomReachablePointInRadius(It->GetActorLocation(),
				                                          FMath::Max(100.0f, Settings->TeamSpawnRadius), Point))
				{
					continue;
				}
				const int32 Before = Candidates.Num();
				AddCandidate(Point.Location + FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight() + 4));
				if (Candidates.Num() > Before && (bIgnoreZone || !IsValid(SafeZone) ||
					!SafeZone->State.Target.IsInside(OutLocation) || SafeZone->State.Target.
					                                                           IsInside(Candidates.Last())))
				{
					OutLocation = Candidates.Last();
					return true;
				}
			}
		}
	}
	const double DistanceSquared = FBrLifeRules::SpawnDistanceSquared(OutLocation, Threats);
	if (DistanceSquared == TNumericLimits<double>::Max())
	{
		UE_LOG(LogBr, Display, TEXT("Respawn location: Candidates=%d No enemies"), Candidates.Num());
	}
	else
	{
		UE_LOG(LogBr, Display, TEXT("Respawn location: Candidates=%d MinEnemyDistance=%.0f"),
		       Candidates.Num(), FMath::Sqrt(DistanceSquared));
	}
	return true;
}

bool ABrGameMode::RespawnPlayer(ABrCharacter* OldPawn, const FBrInventoryState& Gear, bool bVoluntary)
{
	if (!HasAuthority() || AreRespawnsDisabled() || !IsValid(OldPawn) || OldPawn->GetLife()->State !=
		EBrLifeState::Downed)
	{
		return false;
	}
	AController* PC = OldPawn->GetController();
	ABrPlayerState* PS = OldPawn->GetPlayerState<ABrPlayerState>();
	if (!PC || !PS || !PreparePlayer(PC) || PS->bEliminated || PS->DeathCount >= UBrDeveloperSettings::Get()->MaxDeaths)
	{
		return false;
	}
	FBrRoundRecord& Record = RoundRecords.FindChecked(RoundKey(PC));
	if (!Record.SpawnRetries.CanAttempt(GetWorld()->GetTimeSeconds()))
	{
		return false;
	}
	FVector Location;
	if (!FindRespawnLocation(OldPawn, Location))
	{
		FailedSpawn(PC);
		return false;
	}
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	const int32 Voluntary = PS->VoluntaryRespawnCount + (bVoluntary ? 1 : 0);
	const float MaxHealth = bVoluntary
		                        ? FBrLifeRules::MaxHealth(Settings->BaseMaxHealth, Voluntary,
		                                                  Settings->MaxHealthPenaltyPerRespawn,
		                                                  Settings->MinMaxHealthFraction)
		                        : OldPawn->GetHealth()->MaxHealth;
	const FTransform Transform(FRotator(0, OldPawn->GetActorRotation().Yaw, 0), Location);
	ABrCharacter* NewPawn = GetWorld()->SpawnActorDeferred<ABrCharacter>(OldPawn->GetClass(), Transform, PC,
	                                                                     nullptr,
	                                                                     ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!NewPawn)
	{
		UE_LOG(LogBr, Warning, TEXT("Respawn pawn allocation failed."));
		FailedSpawn(PC);
		return false;
	}
	NewPawn->GetInventory()->RestoreState(Gear);
	NewPawn->GetHealth()->SetMaxHealth(MaxHealth);
	NewPawn->GetHealth()->SetHealth(MaxHealth);
	NewPawn->GetLife()->GrantSpawnProtection();
	UGameplayStatics::FinishSpawningActor(NewPawn, Transform);
	PC->Possess(NewPawn);
	if (PC->GetPawn() != NewPawn)
	{
		UE_LOG(LogBr, Error, TEXT("Respawn possession failed; death counters not charged."));
		NewPawn->Destroy();
		PC->Possess(OldPawn);
		FailedSpawn(PC);
		return false;
	}
	Record.SpawnRetries.Reset();
	ResolveQuestRecord(Record, false); // Successful possession only, independent of DeathCount tuning.
	PS->VoluntaryRespawnCount = Voluntary;
	if (bVoluntary || Settings->bTimeoutRespawnCountsAsDeath)
	{
		++PS->DeathCount;
	}
	PS->ForceNetUpdate();
	LeaveCorpse(OldPawn);
	RecordPlayer(PC);
	UE_LOG(LogBr, Display,
	       TEXT("Life Respawn: %s -> %s Kind=%s Deaths=%d Voluntary=%d MaxHealth=%.1f Loaded=%d Reserve=%d"),
	       *OldPawn->GetName(), *NewPawn->GetName(), bVoluntary ? TEXT("Voluntary") : TEXT("Timeout"),
	       PS->DeathCount, Voluntary, MaxHealth, NewPawn->GetInventory()->Loaded,
	       NewPawn->GetInventory()->GetReserve());
	OldPawn->Destroy();
	return true;
}

FString ABrGameMode::RoundKey(const AController* Player) const
{
	const ABrPlayerState* PS = Player ? Player->GetPlayerState<ABrPlayerState>() : nullptr;
	const UBrSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UBrSessionSubsystem>();
	// Defense in depth for every ledger read/write, including seamless travellers.
	// Null identities remain unauthenticated; Steam IDs must match their transport.
	if (!Sessions || !Sessions->VerifyPlayerIdentity(Player))
	{
		return FString();
	}
	if (!PS || !PS->GetUniqueId().IsValid() || !PS->GetUniqueId().IsV1())
	{
		return FString();
	}
	const FUniqueNetIdPtr Id = PS->GetUniqueId().GetV1();
	return Id.IsValid() ? Id->ToString() : FString();
}

void ABrGameMode::ClearRoundRecords()
{
	if (!HasAuthority())
	{
		return;
	}
	RoundRecords.Empty();
	AcceptedQuestOffers.Reset();
	if (IsValid(PickupManager)) PickupManager->ClearRoundPickups();
	for (const TWeakObjectPtr<AController>& Entry : AdmittedControllers)
		if (AController* Controller = Entry.Get())
			if (ABrPlayerState* PS = Controller->GetPlayerState<ABrPlayerState>()) PS->ResetRound();
	// Clear inactive bookkeeping as well; otherwise engine reactivation could resurrect the prior round.
	for (APlayerState* Inactive : InactivePlayerArray)
	{
		if (IsValid(Inactive))
		{
			Inactive->Destroy();
		}
	}
	InactivePlayerArray.Empty();
}

void ABrGameMode::RecordPlayer(AController* Player)
{
	if (!HasAuthority() || !Player || !IsAdmitted(Player) || GetWorld()->bIsTearingDown)
	{
		return;
	}
	const FString Key = RoundKey(Player);
	FBrRoundRecord* Record = RoundRecords.Find(Key);
	// Only the admitted controller may update this identity. Rejected duplicate logins cannot reset it.
	if (!Record || Record->ActiveController != TWeakObjectPtr<AController>(Player))
	{
		return;
	}
	const ABrPlayerState* PS = Player->GetPlayerState<ABrPlayerState>();
	if (!PS)
	{
		return;
	}
	Record->Life = FBrReconnectRules::Merge(Record->Life, PS->GetLifeBookkeeping());
	Record->Life.bEliminated |= Record->bForfeited;
	Record->DownsInflicted = PS->DownsInflicted;
	Record->ArmorPlates = PS->GetArmorPlates();
	if (const ABrCharacter* Pawn = Cast<ABrCharacter>(Player->GetPawn()))
	{
		Record->bHasPawnSnapshot = true;
		Record->bOfflineCorpseLeft = false;
		Record->State = Pawn->GetLife()->State;
		if (Record->State == EBrLifeState::Alive && !PS->bEliminated)
		{
			Record->SpawnRetries.Reset();
		}
		Record->Gear = Record->State == EBrLifeState::Downed
			               ? Pawn->GetLife()->GetDownedGear()
			               : Pawn->GetInventory()->CaptureState();
		Record->Health = Pawn->GetHealth()->Health;
		Record->MaxHealth = Pawn->GetHealth()->MaxHealth;
		Record->DownedEndTime = Pawn->GetLife()->DownedEndTime;
		Record->ProtectionEndTime = Pawn->GetLife()->InvulnerabilityEndTime;
		Record->GroundLocation = Pawn->GetActorLocation() -
			FVector(0, 0, Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		Record->Yaw = Pawn->GetActorRotation().Yaw;
	}
	if (Record->Life.bEliminated)
	{
		Record->State = EBrLifeState::Eliminated;
		Record->Health = 0;
		Record->ArmorPlates.Reset();
		Record->DownedEndTime = Record->ProtectionEndTime = 0;
	}
}

void ABrGameMode::Logout(AController* Exiting)
{
	RecordPlayer(Exiting); // PC::Destroyed already captured gear before the base class removed its pawn.
	if (FBrRoundRecord* Record = RoundRecords.Find(RoundKey(Exiting)))
	{
		if (Record->ActiveController == TWeakObjectPtr<AController>(Exiting))
		{
			Record->ActiveController.Reset();
			if (Record->ReconnectDeadline < 0)
			{
				Record->ReconnectDeadline = GetWorld()->GetTimeSeconds() + ReconnectGrace();
			}
			UE_LOG(LogBr, Display, TEXT("Round disconnect: Id=%s Deaths=%d Voluntary=%d Eliminated=%s State=%d"),
			       *RoundKey(Exiting), Record->Life.Deaths, Record->Life.VoluntaryRespawns,
			       Record->Life.bEliminated ? TEXT("true") : TEXT("false"), static_cast<int32>(Record->State));
		}
	}
	AdmittedControllers.Remove(Exiting);
	Super::Logout(Exiting);
	if (IsLobbyMode())
	{
		UpdateLobby();
	}
}

void ABrGameMode::LeaveRecordedCorpse(const FBrRoundRecord& Record)
{
	if (!Record.bHasPawnSnapshot)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ABrCorpse* Corpse = GetWorld()->SpawnActor<ABrCorpse>(Record.GroundLocation,
	                                                          FRotator(0, Record.Yaw, 0), Params))
	{
		Corpse->SetGear(Record.Gear);
	}
	else
	{
		UE_LOG(LogBr, Error, TEXT("Offline timeout could not leave a corpse."));
	}
}

bool ABrGameMode::PreparePlayer(AController* Player)
{
	if (!HasAuthority() || !IsAdmitted(Player) || !Player || Player->IsPendingKillPending())
	{
		return false;
	}
	ABrPlayerState* PS = Player->GetPlayerState<ABrPlayerState>();
	const FString Key = RoundKey(Player);
	if (!PS || Key.IsEmpty())
	{
		UE_LOG(LogBr, Error, TEXT("Round spawn denied: %s has no valid UniqueNetId (no IP/name fallback)."),
		       *GetNameSafe(Player));
		return false;
	}
	FBrRoundRecord* Record = RoundRecords.Find(Key);
	if (Record && Record->bAwaitingInitialization)
	{
		ServiceRoundRecord(Key, *Record);
	}
	if (!Record)
	{
		FBrRoundRecord Initial;
		Initial.Life = PS->GetLifeBookkeeping();
		Initial.bMatchStartContestant = !HasMatchStarted();
		Initial.Team = PS->Team;
		Initial.TeamColor = PS->TeamColor;
		Initial.PlayerId = PS->GetPlayerId();
		Initial.PlayerName = PS->GetPlayerName();
		Initial.DownsInflicted = PS->DownsInflicted;
		Initial.MaxHealth = FBrLifeRules::MaxHealth(UBrDeveloperSettings::Get()->BaseMaxHealth,
		                                            PS->VoluntaryRespawnCount,
		                                            UBrDeveloperSettings::Get()->MaxHealthPenaltyPerRespawn,
		                                            UBrDeveloperSettings::Get()->MinMaxHealthFraction);
		Initial.Health = Initial.MaxHealth;
		Initial.ArmorPlates = PS->GetArmorPlates();
		Initial.ActiveController = Player;
		Record = &RoundRecords.Add(Key, Initial);
	}
	else if (Record->ActiveController != TWeakObjectPtr<AController>(Player))
	{
		if (Record->ActiveController.IsValid())
		{
			UE_LOG(LogBr, Warning, TEXT("Round spawn denied: identity %s is already connected."), *Key);
			return false;
		}
		ServiceRoundRecord(Key, *Record); // No timer-frame loophole at the grace boundary.
		PS->RestoreLifeBookkeeping(Record->Life);
		PS->StoreArmorPlates(Record->ArmorPlates);
		PS->SetTeam(Record->Team, Record->TeamColor);
		PS->SetPlayerId(Record->PlayerId);
		PS->DownsInflicted = Record->DownsInflicted;
		Record->ActiveController = Player;
		UE_LOG(LogBr, Display, TEXT("Round reconnect: Id=%s Deaths=%d Voluntary=%d Eliminated=%s State=%d"),
		       *Key, PS->DeathCount, PS->VoluntaryRespawnCount, PS->bEliminated ? TEXT("true") : TEXT("false"),
		       static_cast<int32>(Record->State));
	}
	if (Record->bAwaitingInitialization || Record->bForfeited)
	{
		PS->RestoreLifeBookkeeping(Record->Life);
		PS->StoreArmorPlates(Record->ArmorPlates);
		PS->SetTeam(Record->Team, Record->TeamColor);
		PS->SetPlayerId(Record->PlayerId);
		PS->DownsInflicted = Record->DownsInflicted;
	}
	PS->RestoreQuests(Record->Journal);
	PS->SetPlayerName(Record->PlayerName);
	Record->bAwaitingInitialization = false;
	Record->ReconnectDeadline = -1;
	Participants.Add(InitialMatchSettings.Mode == EBrMode::Grouped ? Record->Team : Record->PlayerId);
	return !PS->bEliminated && !Record->Life.bEliminated;
}

float ABrGameMode::ReconnectGrace() const
{
	const float Grace = UBrDeveloperSettings::Get()->ReconnectGracePeriod;
	return FMath::IsFinite(Grace) ? FMath::Max(0.1f, Grace) : 60.0f;
}

bool ABrGameMode::CanRestart(AController* Controller) const
{
	const ABrGameState* GS = GetGameState<ABrGameState>();
	APlayerController* PC = Cast<APlayerController>(Controller);
	const FBrRoundRecord* Record = RoundRecords.Find(RoundKey(Controller));
	return (!Record || Record->SpawnRetries.CanAttempt(GetWorld()->GetTimeSeconds())) &&
		HasAuthority() && GS && !IsLobbyMode() && !bTravelling && !GS->bResults &&
		PC && !PC->IsPendingKillPending() && FBrFlowRules::CanRetrySpawn(
			IsAdmitted(Controller), PC->HasClientLoadedCurrentWorld());
}

void ABrGameMode::FailedSpawn(AController* Controller)
{
	FBrRoundRecord* Record = RoundRecords.Find(RoundKey(Controller));
	if (!Record || Record->Life.bEliminated)
	{
		return;
	}
	const bool bExhausted = Record->SpawnRetries.Failed(GetWorld()->GetTimeSeconds());
	UE_LOG(LogBr, Warning, TEXT("Safe spawn retry: %s Attempt=%d/%d Next=%.2f Exhausted=%d"),
	       *Record->PlayerName, Record->SpawnRetries.Failures, FBrSpawnRetryBudget::MaxAttempts,
	       Record->SpawnRetries.NextAttemptTime, bExhausted);
	if (!bExhausted)
	{
		return;
	}
	// Technical spawn-forfeit: never retain an eligible, permanently unspawnable
	// contestant. No voluntary penalty or fabricated successful respawn is charged.
	UE_LOG(LogBr, Warning, TEXT("Safe spawn forfeit: %s (bounded retries exhausted; SuddenDeath=%d)"),
	       *Record->PlayerName, AreRespawnsDisabled());
	if (ABrCharacter* Pawn = Cast<ABrCharacter>(Controller->GetPawn()))
	{
		Pawn->GetLife()->Eliminate();
		return;
	}
	ABrPlayerState* PS = Controller->GetPlayerState<ABrPlayerState>();
	ResolveQuestRecord(*Record, true);
	FBrReconnectRules::Forfeit(*Record);
	if (PS)
	{
		PS->bEliminated = true;
		PS->ResetArmor();
		PS->ForceNetUpdate();
	}
	BlockSpawn(Controller);
	OnPlayerEliminated(PS);
}

void ABrGameMode::ServiceRoundRecord(const FString& Key, FBrRoundRecord& Record)
{
	const bool bUnavailable = Record.bAwaitingInitialization || !Record.ActiveController.IsValid();
	if (!bUnavailable || Record.bForfeited)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Record.ReconnectDeadline < 0)
	{
		Record.ReconnectDeadline = Now + ReconnectGrace();
	}
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	if (FBrReconnectRules::ResolveOfflineTimeout(Record.Life, Record.State, Record.DownedEndTime,
	                                             FMath::Min(Now, Record.ReconnectDeadline),
	                                             AreRespawnsDisabled() ? 0 : Settings->MaxDeaths,
	                                             Settings->bTimeoutRespawnCountsAsDeath))
	{
		ResolveQuestRecord(Record, Record.Life.bEliminated);
		LeaveRecordedCorpse(Record);
		Record.bOfflineCorpseLeft = true;
		Record.DownedEndTime = Record.ProtectionEndTime = 0;
		Record.Health = Record.Life.bEliminated ? 0 : Record.MaxHealth;
		if (Record.Life.bEliminated) Record.ArmorPlates.Reset();
		UE_LOG(LogBr, Display, TEXT("Offline timeout: Id=%s Deaths=%d Eliminated=%d"), *Key, Record.Life.Deaths,
		       Record.Life.bEliminated);
	}
	if (FBrReconnectRules::GraceExpired(bUnavailable, Record.bForfeited, Record.ReconnectDeadline, Now))
	{
		if (!Record.bOfflineCorpseLeft)
		{
			LeaveRecordedCorpse(Record);
		}
		Record.bOfflineCorpseLeft = true;
		ResolveQuestRecord(Record, true);
		FBrReconnectRules::Forfeit(Record);
		UE_LOG(LogBr, Display, TEXT("Flow forfeit: %s Id=%s Deadline=%.2f Now=%.2f"),
		       *Record.PlayerName, *Key, Record.ReconnectDeadline, Now);
		if (Record.bAwaitingInitialization && !IsAdmitted(Record.ActiveController.Get()))
		{
			if (APlayerController* PC = Cast<APlayerController>(Record.ActiveController.Get()))
			{
				if (GameSession)
				{
					GameSession->KickPlayer(
						PC, FText::FromString(TEXT("Match loading grace expired; reconnect to spectate.")));
				}
			}
		}
	}
}

void ABrGameMode::ServiceRoundRecords()
{
	for (auto& Pair : RoundRecords)
	{
		ServiceRoundRecord(Pair.Key, Pair.Value);
	}
}

void ABrGameMode::BlockSpawn(AController* Player)
{
	if (!Player)
	{
		return;
	}
	if (APawn* Pawn = Player->GetPawn())
	{
		Player->UnPossess();
		Pawn->Destroy();
	}
	if (APlayerController* PC = Cast<APlayerController>(Player))
	{
		PC->StartSpectatingOnly();
		PC->ClientGotoState(NAME_Spectating);
	}
	UE_LOG(LogBr, Display, TEXT("Round spawn blocked: %s (spectator only)"), *GetNameSafe(Player));
}

void ABrGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	if (!AdmitController(NewPlayer))
	{
		return;
	}
	AssignPlayer(NewPlayer);
	if (ABrPlayerController* PC = Cast<ABrPlayerController>(NewPlayer))
	{
		PC->ResetFlowRequests();
		PC->ClientEnterFlow();
	}
	if (IsLobbyMode())
	{
		UpdateLobby();
		return;
	}
	if (const ABrGameState* GS = GetGameState<ABrGameState>())
	{
		if (GS->bResults)
		{
			if (RoundRecords.Contains(RoundKey(NewPlayer)))
			{
				PreparePlayer(NewPlayer);
			}
			BlockSpawn(NewPlayer);
			return;
		}
	}
	// Super::PostLogin calls this AFTER FindInactivePlayer; restore before any normal login spawn.
	if (!PreparePlayer(NewPlayer))
	{
		BlockSpawn(NewPlayer);
		return;
	}
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void ABrGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!CanRestart(NewPlayer))
	{
		return;
	}
	if (IsLobbyMode() || GetGameState<ABrGameState>()->bResults)
	{
		return;
	}
	if (!PreparePlayer(NewPlayer))
	{
		BlockSpawn(NewPlayer);
		return;
	}
	if (IsValid(NewPlayer->GetPawn()))
	{
		return; // Existing downed pawns only respawn through life rules.
	}
	if (GetWorld()->GetTimeSeconds() < 0.5f)
	{
		UE_LOG(LogBr, Display, TEXT("Flow spawn deferred until world initialization: %s"), *GetNameSafe(NewPlayer));
		return;
	}
	// No PlayerStart is required when navigation/ground spiral placement succeeds.
	if (AActor* Start = FindPlayerStart(NewPlayer))
	{
		RestartPlayerAtPlayerStart(NewPlayer, Start);
	}
	else
	{
		RestartPlayerAtTransform(NewPlayer, FTransform::Identity);
	}
	RecordPlayer(NewPlayer);
}

void ABrGameMode::RestartPlayerAtPlayerStart(AController* NewPlayer, AActor* StartSpot)
{
	if (!CanRestart(NewPlayer))
	{
		return;
	}
	if (!PreparePlayer(NewPlayer))
	{
		BlockSpawn(NewPlayer);
		return;
	}
	if (IsValid(NewPlayer->GetPawn()))
	{
		return;
	}
	Super::RestartPlayerAtPlayerStart(NewPlayer, StartSpot);
	RecordPlayer(NewPlayer);
}

void ABrGameMode::RestartPlayerAtTransform(AController* NewPlayer, const FTransform& SpawnTransform)
{
	if (!CanRestart(NewPlayer))
	{
		return;
	}
	if (!PreparePlayer(NewPlayer))
	{
		BlockSpawn(NewPlayer);
		return;
	}
	if (IsValid(NewPlayer->GetPawn()))
	{
		return;
	}
	Super::RestartPlayerAtTransform(NewPlayer, SpawnTransform);
	RecordPlayer(NewPlayer);
}

APawn* ABrGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	if (!CanRestart(NewPlayer))
	{
		return nullptr;
	}
	if (!PreparePlayer(NewPlayer))
	{
		BlockSpawn(NewPlayer);
		return nullptr;
	}
	if (IsValid(NewPlayer->GetPawn()))
	{
		UE_LOG(LogBr, Warning, TEXT("Round spawn denied: %s already has a pawn."), *GetNameSafe(NewPlayer));
		return nullptr;
	}
	UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer);
	if (!PawnClass || !PawnClass->IsChildOf(ABrCharacter::StaticClass()))
	{
		UE_LOG(LogBr, Error, TEXT("Round spawn requires a BrCharacter pawn class."));
		FailedSpawn(NewPlayer);
		return nullptr;
	}
	const FBrRoundRecord Snapshot = RoundRecords.FindChecked(RoundKey(NewPlayer));
	FTransform Transform = SpawnTransform;
	if (Snapshot.bHasPawnSnapshot && Snapshot.State == EBrLifeState::Downed)
	{
		const float HalfHeight = PawnClass->GetDefaultObject<ABrCharacter>()->GetCapsuleComponent()->
		                                    GetScaledCapsuleHalfHeight();
		Transform.SetLocation(Snapshot.GroundLocation + FVector(0, 0, HalfHeight));
		Transform.SetRotation(FRotator(0, Snapshot.Yaw, 0).Quaternion());
	}
	const ABrPlayerState* PS = NewPlayer->GetPlayerState<ABrPlayerState>();
	if (!Snapshot.bHasPawnSnapshot || Snapshot.State != EBrLifeState::Downed)
	{
		FVector Location;
		if (!FindRespawnLocation(PawnClass->GetDefaultObject<ABrCharacter>(), Location, PS,
		                         Snapshot.bMatchStartContestant && !Snapshot.bHasPawnSnapshot))
		{
			FailedSpawn(NewPlayer);
			return nullptr;
		}
		Transform.SetLocation(Location);
	}
	ABrCharacter* Pawn = GetWorld()->SpawnActorDeferred<ABrCharacter>(PawnClass, Transform, NewPlayer,
	                                                                  nullptr,
	                                                                  Snapshot.bHasPawnSnapshot && Snapshot.State ==
	                                                                  EBrLifeState::Downed
		                                                                  ? ESpawnActorCollisionHandlingMethod::AlwaysSpawn
		                                                                  : ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding);
	if (!Pawn)
	{
		UE_LOG(LogBr, Warning, TEXT("Round pawn spawn failed capsule clearance."));
		FailedSpawn(NewPlayer);
		return nullptr;
	}
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	const float PenalizedHealth = FBrLifeRules::MaxHealth(Settings->BaseMaxHealth, PS->VoluntaryRespawnCount,
	                                                      Settings->MaxHealthPenaltyPerRespawn,
	                                                      Settings->MinMaxHealthFraction);
	const float MaxHealth = Snapshot.bHasPawnSnapshot
		                        ? FMath::Min(Snapshot.MaxHealth, PenalizedHealth)
		                        : PenalizedHealth;
	Pawn->GetHealth()->SetMaxHealth(MaxHealth);
	Pawn->GetHealth()->SetHealth(Snapshot.bHasPawnSnapshot ? FMath::Min(Snapshot.Health, MaxHealth) : MaxHealth);
	if (Snapshot.bHasPawnSnapshot)
	{
		Pawn->GetInventory()->RestoreState(Snapshot.Gear);
		if (Snapshot.State == EBrLifeState::Downed)
		{
			Pawn->GetLife()->RestoreDowned(Snapshot.DownedEndTime);
		}
		else
		{
			Pawn->GetLife()->InvulnerabilityEndTime = Snapshot.ProtectionEndTime;
			// Never renew protection on reconnect.
		}
	}
	UGameplayStatics::FinishSpawningActor(Pawn, Transform);
	RoundRecords.FindChecked(RoundKey(NewPlayer)).SpawnRetries.Reset();
	if (Snapshot.bHasPawnSnapshot)
	{
		UE_LOG(LogBr, Display,
		       TEXT("Round restored pawn: Id=%s MaxHealth=%.1f Health=%.1f Loaded=%d Reserve=%d State=%d"),
		       *RoundKey(NewPlayer), MaxHealth, Pawn->GetHealth()->Health, Pawn->GetInventory()->Loaded,
		       Pawn->GetInventory()->GetReserve(), static_cast<int32>(Pawn->GetLife()->State));
	}
	return Pawn;
}
