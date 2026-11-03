#include "BrZoneBounds.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerStart.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "BrLog.h"

ABrZoneBounds::ABrZoneBounds()
{
	Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("PlayableBounds"));
	SetRootComponent(Bounds);
	Bounds->SetBoxExtent(FVector(5000, 5000, 500));
	Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bounds->SetCanEverAffectNavigation(false);
	PrimaryActorTick.bCanEverTick = false;
}
FBox ABrZoneBounds::GetPlayableBounds() const { return Bounds->Bounds.GetBox(); }

FBox ABrZoneBounds::FindPlayableBounds(UWorld* World)
{
	FBox Result(ForceInit);
	const TCHAR* Source = TEXT("ZoneBounds");
	for (TActorIterator<ABrZoneBounds> It(World); It; ++It) Result += It->GetPlayableBounds();
	if (!Result.IsValid)
	{
		Source = TEXT("NavMeshBounds");
		for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It) Result += It->GetComponentsBoundingBox(true);
	}
	if (!Result.IsValid)
	{
		Source = TEXT("PlayerStarts/Geometry");
		for (TActorIterator<APlayerStart> It(World); It; ++It) Result += It->GetActorLocation();
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
			if (It->GetActorEnableCollision()) Result += It->GetComponentsBoundingBox(true);
	}
	if (!Result.IsValid || Result.GetSize().X < 1 || Result.GetSize().Y < 1)
	{
		UE_LOG(LogBr, Error, TEXT("No usable playable bounds; author a BrZoneBounds. Using 100m emergency bounds."));
		Result = FBox(FVector(-5000, -5000, -500), FVector(5000, 5000, 500));
	}
	UE_LOG(LogBr, Display, TEXT("Zone bounds: Source=%s Center=%s Radius=%.0f"), Source,
		*Result.GetCenter().ToString(), Result.GetExtent().Size2D());
	return Result;
}
