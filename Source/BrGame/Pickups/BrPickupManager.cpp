#include "BrPickupManager.h"
#include "BrPickup.h"
#include "BrPickupDefinition.h"
#include "BrPickupRules.h"
#include "BrPickupSpawnPoint.h"
#include "BrDeveloperSettings.h"
#include "BrGameMode.h"
#include "BrGameState.h"
#include "BrLog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

ABrPickupManager::ABrPickupManager()
{
	bReplicates = false;
	PrimaryActorTick.bCanEverTick = false;
}

void ABrPickupManager::Initialize(int32 PlayersAtStart)
{
	if (!HasAuthority() || bInitialized) return;
	bInitialized = true;
	InitialPlayers = PlayersAtStart;
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	Table = Settings->PickupTable.LoadSynchronous();
	Stream.Initialize(FMath::Rand());
	if (!Table || !Table->Roll(Stream))
	{
		UE_LOG(LogBr, Error, TEXT("Pickup manager cannot start: missing table or no valid positive-weight definitions: %s"),
			*Settings->PickupTable.ToString());
		return;
	}
	// Match start can precede navmesh initialization/rebuild (including Lvl_TopDown).
	GetWorld()->GetTimerManager().SetTimer(PlacementTimer, this, &ABrPickupManager::PlaceSlots, 0.25f, false);
}

void ABrPickupManager::PlaceSlots()
{
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	const int32 PlayersAtStart = InitialPlayers;
	const int32 Desired = FBrPickupRules::Count(Settings->PickupCountBase, Settings->PickupCountPerPlayer, PlayersAtStart);
	bool bHasAuthoredPoints = false;
	for (TActorIterator<ABrPickupSpawnPoint> It(GetWorld()); It; ++It) { bHasAuthoredPoints = true; break; }
	if (!bHasAuthoredPoints && Desired > 0)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		FNavLocation Probe;
		if (!Nav || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()) || !Nav->GetRandomPoint(Probe))
		{
			if (++NavigationWaits < 40)
			{
				GetWorld()->GetTimerManager().SetTimer(PlacementTimer, this, &ABrPickupManager::PlaceSlots, 0.25f, false);
				return;
			}
			UE_LOG(LogBr, Error, TEXT("Pickup navigation remained unavailable after 10 seconds; add BrPickupSpawnPoints or rebuild navigation."));
		}
	}
	TArray<FVector> Floors;
	const double Spacing = FMath::Max(0.0f, Settings->MinPickupSpacing);
	const double CellSize = FMath::Max(1.0, Spacing);
	TMap<FIntPoint, TArray<FVector>> OccupiedCells;
	auto AddFloor = [&](const FVector& Floor)
	{
		if (Floors.Num() >= Desired) return;
		const FIntPoint Cell(FMath::FloorToInt(Floor.X / CellSize), FMath::FloorToInt(Floor.Y / CellSize));
		for (int32 X = -1; X <= 1; ++X)
		{
			for (int32 Y = -1; Y <= 1; ++Y)
			{
				if (const TArray<FVector>* Nearby = OccupiedCells.Find(Cell + FIntPoint(X, Y)))
				{
					for (const FVector& Other : *Nearby)
						if (FVector::DistSquared2D(Floor, Other) < FMath::Square(Spacing)) return;
				}
			}
		}
		if (GetWorld()->OverlapBlockingTestByProfile(Floor + FVector(0, 0, 100), FQuat::Identity, TEXT("Pawn"),
			FCollisionShape::MakeCapsule(42, 96))) return;
		Floors.Add(Floor);
		OccupiedCells.FindOrAdd(Cell).Add(Floor);
	};
	TArray<FVector> AuthoredFloors;
	for (TActorIterator<ABrPickupSpawnPoint> It(GetWorld()); It; ++It)
	{
		bHasAuthoredPoints = true;
		AuthoredFloors.Add(It->GetActorLocation());
	}
	while (!AuthoredFloors.IsEmpty() && Floors.Num() < Desired)
	{
		const int32 Index = Stream.RandHelper(AuthoredFloors.Num());
		AddFloor(AuthoredFloors[Index]);
		AuthoredFloors.RemoveAtSwap(Index);
	}
	if (!bHasAuthoredPoints)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		TArray<FVector> Anchors;
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It) Anchors.Add(It->GetActorLocation());
		if (Nav && Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
		{
			if (Anchors.IsEmpty())
			{
				FNavLocation Anywhere;
				if (Nav->GetRandomPoint(Anywhere)) Anchors.Add(Anywhere.Location);
			}
			const int64 Attempts = static_cast<int64>(Desired) * 30;
			int32 ConsecutiveFailures = 0;
			// Finite maps can saturate regardless of player count; avoid blocking the server on futile searches.
			for (int64 Attempt = 0; Attempt < Attempts && Floors.Num() < Desired &&
				!Anchors.IsEmpty() && ConsecutiveFailures < 512; ++Attempt)
			{
				const int32 PreviousCount = Floors.Num();
				FNavLocation Anchor, Point;
				if (Nav->ProjectPointToNavigation(Anchors[Stream.RandHelper(Anchors.Num())], Anchor) &&
					Nav->GetRandomReachablePointInRadius(Anchor.Location, FMath::Max(1.0f, Settings->PickupSearchRadius), Point))
					AddFloor(Point.Location);
				ConsecutiveFailures = Floors.Num() > PreviousCount ? 0 : ConsecutiveFailures + 1;
			}
		}
		else UE_LOG(LogBr, Error, TEXT("Pickup manager has no spawn points or navigation data."));
	}
	if (Floors.Num() < Desired)
	{
		UE_LOG(LogBr, Warning, TEXT("Pickup placement found only %d/%d clear, spaced points (Source=%s)."),
			Floors.Num(), Desired, bHasAuthoredPoints ? TEXT("SpawnPoints") : TEXT("Navigation"));
	}
	Slots.Reserve(Floors.Num());
	int32 Spawned = 0;
	for (const FVector& Floor : Floors)
	{
		FBrPickupSlot& Slot = Slots.AddDefaulted_GetRef();
		Slot.Transform = FTransform(Floor + FVector(0, 0, 35));
		if (Reroll(Slot)) ++Spawned;
		else Slot.ReadyTime = GetWorld()->GetTimeSeconds() + RespawnDelay();
	}
	ScheduleNext();
	UE_LOG(LogBr, Display, TEXT("Pickups spawned: Count=%d Slots=%d PlayersAtStart=%d Source=%s"),
		Spawned, Slots.Num(), PlayersAtStart, bHasAuthoredPoints ? TEXT("SpawnPoints") : TEXT("Navigation"));
}

float ABrPickupManager::RespawnDelay() const
{
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	if (!GS)
	{
		UE_LOG(LogBr, Error, TEXT("Pickup respawn has no BrGameState; using the documented 30-second default."));
		return 30;
	}
	return FBrMatchSettings::ClampPickupRespawnTime(GS->MatchSettings.PickupRespawnTime);
}

bool ABrPickupManager::Reroll(FBrPickupSlot& Slot)
{
	UBrPickupDefinition* Definition = Table ? Table->Roll(Stream) : nullptr;
	if (!Definition)
	{
		UE_LOG(LogBr, Error, TEXT("Pickup slot reroll has no valid definition."));
		return false;
	}
	if (IsValid(Slot.Pickup))
	{
		Slot.Pickup->ResetPickup(Definition);
	}
	else
	{
		Slot.Pickup = ABrPickup::Spawn(GetWorld(), Slot.Transform, Definition, FBrPickupPayload(), true);
		if (!Slot.Pickup) return false;
		Slot.Pickup->OnConsumed.AddUObject(this, &ABrPickupManager::Consumed);
	}
	return true;
}

void ABrPickupManager::Consumed(ABrPickup* Pickup)
{
	for (FBrPickupSlot& Slot : Slots)
	{
		if (Slot.Pickup == Pickup)
		{
			Slot.ReadyTime = GetWorld()->GetTimeSeconds() + RespawnDelay();
			ScheduleNext();
			return;
		}
	}
	UE_LOG(LogBr, Error, TEXT("Pickup consumed without a registered manager slot."));
}

void ABrPickupManager::ScheduleNext()
{
	GetWorld()->GetTimerManager().ClearTimer(RespawnTimer);
	float Next = TNumericLimits<float>::Max();
	for (const FBrPickupSlot& Slot : Slots) if (Slot.ReadyTime > 0) Next = FMath::Min(Next, Slot.ReadyTime);
	if (Next != TNumericLimits<float>::Max())
		GetWorld()->GetTimerManager().SetTimer(RespawnTimer, this, &ABrPickupManager::ServiceRespawns,
			FMath::Max(0.01f, Next - GetWorld()->GetTimeSeconds()), false);
}

void ABrPickupManager::ServiceRespawns()
{
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	if (GS && GS->bResults) return;
	const ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
	const float Now = GetWorld()->GetTimeSeconds();
	for (FBrPickupSlot& Slot : Slots)
	{
		if (Slot.ReadyTime > 0 && Now >= Slot.ReadyTime)
		{
			// Circles are nested: an expired outside slot remains empty for the round.
			if (Mode && !Mode->IsLocationInSafeZone(Slot.Transform.GetLocation())) { Slot.ReadyTime = 0; continue; }
			Slot.ReadyTime = 0; // Collection during ResetPickup may set a new deadline synchronously.
			if (!Reroll(Slot)) Slot.ReadyTime = Now + RespawnDelay();
		}
	}
	ScheduleNext();
}

void ABrPickupManager::RespawnAllPickups()
{
	if (!HasAuthority()) return;
	const ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
	for (FBrPickupSlot& Slot : Slots)
	{
		if (Mode && !Mode->IsLocationInSafeZone(Slot.Transform.GetLocation())) continue;
		Slot.ReadyTime = 0;
		if (!Reroll(Slot)) Slot.ReadyTime = GetWorld()->GetTimeSeconds() + RespawnDelay();
	}
	ScheduleNext();
	UE_LOG(LogBr, Display, TEXT("Pickup slots rerolled: %d (dropped weapons are not respawn slots)"), Slots.Num());
}

void ABrPickupManager::RegisterRoundDrop(ABrPickup* Pickup)
{
	if (!HasAuthority() || !IsValid(Pickup)) return;
	RoundDrops.Add(Pickup);
	Pickup->OnDestroyed.AddUniqueDynamic(this, &ABrPickupManager::RoundDropDestroyed);
}

void ABrPickupManager::RoundDropDestroyed(AActor* DestroyedActor)
{
	RoundDrops.Remove(Cast<ABrPickup>(DestroyedActor)); // Collected/expired drops don't grow the tracking set.
}

void ABrPickupManager::ClearRoundPickups()
{
	if (!HasAuthority()) return;
	GetWorld()->GetTimerManager().ClearTimer(RespawnTimer);
	GetWorld()->GetTimerManager().ClearTimer(PlacementTimer);
	const TSet<TWeakObjectPtr<ABrPickup>> Drops = MoveTemp(RoundDrops);
	for (const TWeakObjectPtr<ABrPickup>& Drop : Drops)
	{
		if (ABrPickup* Pickup = Drop.Get())
		{
			Pickup->OnDestroyed.RemoveDynamic(this, &ABrPickupManager::RoundDropDestroyed);
			Pickup->Destroy();
		}
	}
	for (FBrPickupSlot& Slot : Slots)
	{
		if (IsValid(Slot.Pickup))
		{
			Slot.Pickup->OnConsumed.RemoveAll(this);
			Slot.Pickup->Destroy();
		}
	}
	Slots.Empty();
	Table = nullptr;
	bInitialized = false;
	NavigationWaits = InitialPlayers = 0;
}

void ABrPickupManager::EndPlay(const EEndPlayReason::Type Reason)
{
	ClearRoundPickups();
	Super::EndPlay(Reason);
}
