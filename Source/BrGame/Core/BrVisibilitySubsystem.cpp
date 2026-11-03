#include "BrVisibilitySubsystem.h"
#include "BrCharacter.h"
#include "BrGrenade.h"
#include "BrDeveloperSettings.h"
#include "BrFogRules.h"
#include "BrGameState.h"
#include "BrPlayerController.h"
#include "BrPlayerState.h"
#include "BrLifeComponent.h"
#include "BrLog.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "Engine/ActorChannel.h"
#include "Engine/PackageMapClient.h"
#include "Engine/NetworkObjectList.h"
#include "EngineUtils.h"
#include "TimerManager.h"

bool UBrVisibilitySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}
void UBrVisibilitySubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	if (World.GetNetMode() == NM_Client) return;
	PostNetFlushHandle = World.OnPostTickFlush().AddUObject(this, &ThisClass::RetryDiscoveryAfterNetFlush);
	World.GetTimerManager().SetTimer(Timer, this, &ThisClass::Update,
		1.f / FMath::Clamp(UBrDeveloperSettings::Get()->VisibilityUpdateRate, 1.f, 60.f), true);
	UE_LOG(LogBr, Display, TEXT("Fog server visibility: classic replication, explicit hidden-channel closure"));
}
void UBrVisibilitySubsystem::Deinitialize()
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(Timer);
		GetWorld()->OnPostTickFlush().Remove(PostNetFlushHandle);
	}
	PendingDiscoveries.Reset();
	Groups.Empty();
	Super::Deinitialize();
}
bool UBrVisibilitySubsystem::Active(const UWorld* World)
{
	const ABrGameState* GS = World ? World->GetGameState<ABrGameState>() : nullptr;
	return GS && !GS->bLobby && !GS->bResults && !GS->bFogDisabled;
}
ABrPlayerState* UBrVisibilitySubsystem::ViewerState(const ABrPlayerController* PC)
{
	if (!PC) return nullptr;
	return FBrFogRules::Viewer(PC->IsFlowSpectating(), PC->GetPlayerState<ABrPlayerState>(), PC->GetSpectatorTarget());
}
int64 UBrVisibilitySubsystem::Group(const ABrPlayerState* PS)
{
	if (!PS) return MIN_int64;
	const ABrGameState* GS = PS->GetWorld()->GetGameState<ABrGameState>();
	return FBrFogRules::Group(GS && GS->MatchSettings.Mode == EBrMode::Grouped, PS->Team, PS->GetPlayerId());
}
bool UBrVisibilitySubsystem::Friendly(const ABrPlayerState* A, const ABrPlayerState* B)
{
	return A && B && Group(A) == Group(B);
}
bool UBrVisibilitySubsystem::Source(const ABrCharacter* Character)
{
	return IsValid(Character) && Character->GetPlayerState() && Character->GetLife()->State != EBrLifeState::Eliminated;
}
bool UBrVisibilitySubsystem::LOS(UWorld* World, const ABrCharacter* From, const FVector& Position, const AActor* Target)
{
	FCollisionQueryParams Query(SCENE_QUERY_STAT(BrVision), false, From);
	if (Target) Query.AddIgnoredActor(Target);
	// Shoulder-height plane; downed viewers retain vision, but don't trace along the floor.
	FVector Origin = From->GetActorLocation();
	Origin.Z += 20;
	const FVector Side = FVector::CrossProduct((Position - Origin).GetSafeNormal2D(), FVector::UpVector) * 32;
	for (const FVector Offset : {FVector(0, 0, 20), Side + FVector(0, 0, 20), -Side + FVector(0, 0, 20)})
		if (!World->LineTraceTestByChannel(Origin, Position + Offset, VisionChannel, Query)) return true;
	return false;
}
bool UBrVisibilitySubsystem::PositionVisible(const FBrVisionGroup& Vision, const FVector& Position, const AActor* Target) const
{
	const double Radius = FMath::Max(100.f, UBrDeveloperSettings::Get()->VisionRadius);
	for (const TWeakObjectPtr<ABrCharacter>& Entry : Vision.Sources)
		if (const ABrCharacter* From = Entry.Get())
			if (FVector::DistSquared2D(From->GetActorLocation(), Position) <= Radius * Radius && LOS(GetWorld(), From, Position, Target)) return true;
	return false;
}
bool UBrVisibilitySubsystem::Relevant(const AActor* Actor, const AActor* Viewer) const
{
	if (!Active(GetWorld())) return true;
	const ABrPlayerController* PC = Cast<ABrPlayerController>(Viewer);
	// Own pawn must survive the eliminated -> spectator-target handoff, even
	// during the brief interval where there is not yet a selected target.
	if (PC && PC->GetPawn() == Actor) return true;
	if (PC && PC->IsFlowSpectating() && PC->GetFlowSpectatorPawn() == Actor) return true;
	const ABrPlayerState* PS = ViewerState(PC);
	if (!PS) return false; // No omniscient spectator fallback.
	if (const ABrCharacter* Character = Cast<ABrCharacter>(Actor))
		if (PC->GetPawn() == Character || Friendly(PS, Character->GetPlayerState<ABrPlayerState>())) return true;
	const FBrVisionGroup* Vision = Groups.Find(Group(PS));
	if (!Vision) return false; // Fail closed until first cache update.
	return Vision->Visible.Contains(Actor);
}
void UBrVisibilitySubsystem::RequestDiscovery(AActor* Actor, ABrPlayerController* Viewer)
{
	UNetConnection* Connection = Viewer ? Viewer->GetNetConnection() : nullptr;
	if (!IsValid(Actor) || !Connection || !Relevant(Actor, Viewer)) return;
	if (const UActorChannel* Channel = Connection->FindActorChannelRef(Actor))
		if (!Channel->Closing) return;
	if (!PendingDiscoveries.ContainsByPredicate([Actor, Connection](const FPendingDiscovery& Entry)
		{ return Entry.Actor == Actor && Entry.Connection == Connection; }))
	{
		PendingDiscoveries.Add({Actor, Connection});
		UE_LOG(LogBr, Verbose, TEXT("Fog discovery queued: Viewer=%s Actor=%s NetGUID=%s"),
			*Viewer->GetName(), *Actor->GetName(), *Connection->Driver->GetNetGuidCache()->GetNetGUID(Actor).ToString());
	}
}
void UBrVisibilitySubsystem::RetryDiscoveryAfterNetFlush()
{
	UNetDriver* Driver = GetWorld()->GetNetDriver();
	if (!Driver || Driver->GetWorld() != GetWorld() || GetWorld()->bIsTearingDown) return;
	// Classic listen-server throttling only carries skipped connections forward if
	// they ALREADY have a channel (NetDriver.cpp ServerReplicateActors). A hidden or
	// never-sent stationary pawn has none: its infrequent consider-list frames can
	// repeatedly land on a different connection. One ForceNetUpdate is insufficient.
	// After replication clears bPendingNetUpdate, retain just missing, relevant
	// discoveries until their connection is serviced. Normal relevancy, level-init,
	// dormancy and bandwidth gates still apply; never ForceNetRelevant.
	for (int32 Index = PendingDiscoveries.Num() - 1; Index >= 0; --Index)
	{
		AActor* Actor = PendingDiscoveries[Index].Actor.Get();
		UNetConnection* Connection = PendingDiscoveries[Index].Connection.Get();
		const UActorChannel* Channel = Actor && Connection ? Connection->FindActorChannelRef(Actor) : nullptr;
		const bool bOpen = Channel && !Channel->Closing;
		if (!Actor || !Connection || Connection->IsClosingOrClosed() ||
			Connection->Driver != Driver || !Relevant(Actor, Connection->PlayerController) || bOpen)
		{
			if (Actor && Connection && bOpen)
				UE_LOG(LogBr, Verbose, TEXT("Fog discovery opened: Viewer=%s Actor=%s NetGUID=%s Channel=%d"),
					*GetNameSafe(Connection->PlayerController), *Actor->GetName(),
					*Driver->GetNetGuidCache()->GetNetGUID(Actor).ToString(), Channel->ChIndex);
			PendingDiscoveries.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			continue;
		}
		if (Driver->IsLevelInitializedForActor(Actor, Connection))
			if (const TSharedPtr<FNetworkObjectInfo> Info = Driver->GetNetworkObjectList().Find(Actor))
				Info->bPendingNetUpdate = true;
	}
}
void UBrVisibilitySubsystem::Update()
{
	const double Start = FPlatformTime::Seconds();
	if (GetWorld()->GetNetMode() == NM_Client) return;
	if (!Active(GetWorld())) { Groups.Reset(); return; }
	for (auto& Pair : Groups) { Pair.Value.Sources.Reset(); Pair.Value.Visible.Reset(); }
	Actors.Reset();
	for (TActorIterator<ABrCharacter> It(GetWorld()); It; ++It)
	{
		Actors.Add(*It);
		if (Source(*It)) Groups.FindOrAdd(Group(It->GetPlayerState<ABrPlayerState>())).Sources.Add(*It);
	}
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	const double Now = GetWorld()->GetTimeSeconds();
	for (auto& Pair : Groups)
	{
		FBrVisionGroup& Vision = Pair.Value;
		for (const TWeakObjectPtr<AActor>& Entry : Actors)
		{
			ABrCharacter* Target = Cast<ABrCharacter>(Entry.Get());
			if (!Target) continue;
			const double* Previous = Vision.LastSeen.Find(Target);
			const double Last = Previous ? *Previous : -DBL_MAX;
			bool bVisible = Pair.Key == Group(Target->GetPlayerState<ABrPlayerState>());
			for (const TWeakObjectPtr<ABrCharacter>& SourceEntry : Vision.Sources)
			{
				const ABrCharacter* From = SourceEntry.Get();
				if (!From || bVisible) break;
				const double D2 = FVector::DistSquared2D(From->GetActorLocation(), Target->GetActorLocation());
				if (D2 > FMath::Square(Settings->VisionRadius * 1.1)) continue;
				bVisible = FBrFogRules::Visible(false, D2, Settings->VisionRadius,
					LOS(GetWorld(), From, Target->GetActorLocation(), Target), Now, Last, Settings->VisionGrace);
				if (bVisible && D2 <= FMath::Square(Settings->VisionRadius)) Vision.LastSeen.Add(Target, Now);
			}
			if (bVisible) Vision.Visible.Add(Target);
		}
		for (auto It = Vision.LastSeen.CreateIterator(); It; ++It)
			if (!It.Key().IsValid() || Now - It.Value() > Settings->VisionGrace) It.RemoveCurrent();
	}
	for (TActorIterator<ABrGrenade> It(GetWorld()); It; ++It) Actors.Add(*It);
	for (auto& Pair : Groups)
		for (const TWeakObjectPtr<AActor>& Entry : Actors)
			if (ABrGrenade* Grenade = Cast<ABrGrenade>(Entry.Get()))
				if (FBrFogRules::Grenade(PositionVisible(Pair.Value, Grenade->GetActorLocation(), Grenade)))
					Pair.Value.Visible.Add(Grenade);
	// RelevantTimeout is driver-wide and even permits continued replication. Close only
	// our hidden dynamic actors explicitly, rather than reducing timeout for the whole game.
	if (UNetDriver* Driver = GetWorld()->GetNetDriver())
		for (UNetConnection* Connection : Driver->ClientConnections)
		{
			if (!Connection || !Connection->PlayerController) continue;
			for (const TWeakObjectPtr<AActor>& Actor : Actors)
				if (Actor.IsValid())
				{
					if (Relevant(Actor.Get(), Connection->PlayerController))
					{
						RequestDiscovery(Actor.Get(), Cast<ABrPlayerController>(Connection->PlayerController));
						continue;
					}
					if (UActorChannel* Channel = Connection->FindActorChannelRef(Actor))
						if (!Channel->Closing)
						{
							UE_LOG(LogBr, Verbose, TEXT("Fog channel closed: Viewer=%s Actor=%s NetGUID=%s Channel=%d"),
								*Connection->PlayerController->GetName(), *Actor->GetName(),
								*Driver->GetNetGuidCache()->GetNetGUID(Actor.Get()).ToString(), Channel->ChIndex);
							Channel->Close(EChannelCloseReason::Relevancy);
						}
				}
		}
	if (Now >= NextLog)
	{
		UE_LOG(LogBr, Verbose, TEXT("Fog visibility: Groups=%d Actors=%d Update=%.3fms"), Groups.Num(), Actors.Num(),
			(FPlatformTime::Seconds() - Start) * 1000);
		NextLog = Now + 10;
	}
}
