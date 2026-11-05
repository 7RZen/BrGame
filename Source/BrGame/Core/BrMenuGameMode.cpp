#include "BrGameMode.h"
#include "BrMenuRules.h"
#include "BrPlayerController.h"
#include "BrPlayerState.h"
#include "BrCharacter.h"
#include "BrLifeComponent.h"
#include "BrGameState.h"
#include "BrDeveloperSettings.h"
#include "BrLog.h"
#include "Components/CapsuleComponent.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"

bool FBrUnstuckRules::PathClear(const UWorld* World, const AActor* Player,
	const FVector& Origin, const FVector& Candidate, float Radius, float HalfHeight)
{
	if (!World) return false;
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	FCollisionQueryParams PathQuery(SCENE_QUERY_STAT(BrUnstuckPath), false, Player);
	TArray<FOverlapResult> Embedded;
	World->OverlapMultiByProfile(Embedded, Origin, FQuat::Identity, TEXT("Pawn"), Shape, PathQuery);
	for (const FOverlapResult& Overlap : Embedded)
		if (Overlap.bBlockingHit && Overlap.Component.IsValid())
			PathQuery.AddIgnoredComponent(Overlap.Component.Get());
	FHitResult Hit;
	return !World->SweepSingleByProfile(Hit, Origin, Candidate, FQuat::Identity, TEXT("Pawn"), Shape, PathQuery);
}

bool ABrGameMode::ForfeitPlayer(ABrPlayerController* Player)
{
	const ABrGameState* GS = GetGameState<ABrGameState>();
	if (!HasAuthority() || !Player || !IsAdmitted(Player) || !GS) return false;
	if (GS->bLobby || GS->bResults) return true; // Leaving these phases is penalty-free.
	RecordPlayer(Player);
	FBrRoundRecord* Record = RoundRecords.Find(RoundKey(Player));
	if (!Record || Record->ActiveController.Get() != Player) return false;
	if (Record->bForfeited) return true;
	// Mark the ledger BEFORE elimination/winner callbacks and subsequent Destroyed snapshots.
	ResolveQuestRecord(*Record, true);
	FBrReconnectRules::Forfeit(*Record);
	if (ABrCharacter* Pawn = Cast<ABrCharacter>(Player->GetPawn()))
	{
		if (Pawn->GetLife()->State != EBrLifeState::Eliminated) Pawn->GetLife()->Eliminate();
	}
	else
	{
		if (!Record->bOfflineCorpseLeft) LeaveRecordedCorpse(*Record);
		if (ABrPlayerState* PS = Player->GetPlayerState<ABrPlayerState>())
		{
			PS->bEliminated = true;
			PS->ForceNetUpdate();
		}
	}
	Record->bOfflineCorpseLeft = true;
	UE_LOG(LogBr, Display, TEXT("Menu intentional forfeit: %s Eliminated=1"), *Record->PlayerName);
	// FlowService coalesces this with ordinary eliminations, allowing same-frame draws.
	Player->ServiceSpectator();
	return true;
}

float ABrGameMode::GetUnstuckCooldown(AController* Player) const
{
	const FBrRoundRecord* Record = RoundRecords.Find(RoundKey(Player));
	return Record ? Record->UnstuckCooldownEnd : 0;
}
void ABrGameMode::SetUnstuckCooldown(AController* Player, float Deadline)
{
	if (HasAuthority())
		if (FBrRoundRecord* Record = RoundRecords.Find(RoundKey(Player))) Record->UnstuckCooldownEnd = Deadline;
}

bool ABrGameMode::FindUnstuckLocation(const ABrCharacter* Character, FVector& Location) const
{
	if (!HasAuthority() || !IsValid(Character)) return false;
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const bool bNavReady = Nav && Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) &&
		!UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld());
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	const FVector Origin = Character->GetActorLocation();
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius(), Height = Capsule->GetScaledCapsuleHalfHeight();
	const float Range = FMath::Clamp(S->UnstuckSearchRadius, 100.f, 2000.f);
	const float Step = FMath::Clamp(S->UnstuckSearchStep, 50.f, 250.f);
	const bool bInside = IsLocationInSafeZone(Origin);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(BrUnstuck), false, Character);
	double Best = TNumericLimits<double>::Max();
	int32 ProjectedCount = 0, GroundCount = 0, ZoneCount = 0, ClearCount = 0;
	for (float Ring = Step; Ring <= Range; Ring += Step)
	{
		const int32 Count = FMath::CeilToInt(2 * UE_PI * Ring / Step);
		for (int32 I = 0; I < Count; ++I)
		{
			const double Angle = 2 * UE_PI * I / Count;
			const FVector Probe = Origin + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Ring;
			FNavLocation Point;
			// The authored map can have a nav data actor but no baked tiles. Like the
			// existing spawn path, require actual walkable ground/clearance, not a
			// successful-looking projection fallback or a forced geometry teleport.
			const bool bProjected = bNavReady && Nav->ProjectPointToNavigation(Probe - FVector(0, 0, Height),
				Point, FVector(Step / 2, Step / 2, Height * 2));
			if (bProjected) ++ProjectedCount;
			const FVector GroundProbe = bProjected ? Point.Location : Probe - FVector(0, 0, Height);
			FHitResult Floor;
			if (!GetWorld()->LineTraceSingleByChannel(Floor, GroundProbe + FVector(0, 0, Height),
				GroundProbe - FVector(0, 0, Height), ECC_WorldStatic, Query) || Floor.ImpactNormal.Z < 0.7) continue;
			++GroundCount;
			const FVector Candidate = Floor.ImpactPoint + FVector(0, 0, Height + 4);
			const double Distance = FVector::DistSquared(Origin, Candidate);
			if (Distance >= Best || Distance > FMath::Square(Range) || FVector::DistSquared2D(Origin, Candidate) < FMath::Square(Radius * 2) ||
				!FBrUnstuckRules::ZoneAllowed(bInside, IsLocationInSafeZone(Candidate))) continue;
			++ZoneCount;
			if (GetWorld()->OverlapBlockingTestByProfile(Candidate, FQuat::Identity, TEXT("Pawn"),
				FCollisionShape::MakeCapsule(Radius, Height), Query)) continue;
			++ClearCount;
			bool bPlayerOverlap = false;
			for (TActorIterator<ABrCharacter> It(GetWorld()); It; ++It)
				if (*It != Character && It->GetLife()->State != EBrLifeState::Eliminated &&
					FMath::Abs(It->GetActorLocation().Z - Candidate.Z) < Height + It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() &&
					FVector::DistSquared2D(Candidate, It->GetActorLocation()) <
						FMath::Square(Radius + It->GetCapsuleComponent()->GetScaledCapsuleRadius()))
					{ bPlayerOverlap = true; break; }
			if (bPlayerOverlap) continue;
			if (!FBrUnstuckRules::PathClear(GetWorld(), Character, Origin, Candidate, Radius, Height)) continue;
			Best = Distance;
			Location = Candidate;
		}
	}
	UE_LOG(LogBr, Verbose, TEXT("Unstuck search: Origin=%s Inside=%d Nav=%d Ground=%d ZoneDistance=%d Clear=%d Found=%d"),
		*Origin.ToString(), bInside, ProjectedCount, GroundCount, ZoneCount, ClearCount, Best != TNumericLimits<double>::Max());
	return Best != TNumericLimits<double>::Max();
}
