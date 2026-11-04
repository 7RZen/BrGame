#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BrVisibilitySubsystem.generated.h"

class ABrCharacter;
class ABrPlayerState;
class ABrPlayerController;
class UNetConnection;

struct FBrVisionGroup
{
	TArray<TWeakObjectPtr<ABrCharacter>> Sources;
	TSet<TWeakObjectPtr<AActor>> Visible;
	TMap<TWeakObjectPtr<AActor>, double> LastSeen;
};

/** Server cache only. Local clients use the same LOS primitives, never this cache. */
UCLASS()
class BRGAME_API UBrVisibilitySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;
	bool Relevant(const AActor* Actor, const AActor* Viewer) const;
	/** Keep discovery scheduled until this relevant connection actually gets a channel. */
	void RequestDiscovery(AActor* Actor, ABrPlayerController* Viewer);
	static bool Active(const UWorld* World);
	static ABrPlayerState* ViewerState(const ABrPlayerController* PC);
	static int64 Group(const ABrPlayerState* PS);
	static bool Friendly(const ABrPlayerState* A, const ABrPlayerState* B);
	static bool LOS(UWorld* World, const ABrCharacter* Source, const FVector& Position, const AActor* Target = nullptr);
	static bool Source(const ABrCharacter* Character);
	static constexpr ECollisionChannel VisionChannel = ECC_GameTraceChannel2;

private:
	void Update();
	void RetryDiscoveryAfterNetFlush();

	struct FPendingDiscovery
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<UNetConnection> Connection;
	};

	TArray<FPendingDiscovery> PendingDiscoveries;
	FDelegateHandle PostNetFlushHandle;
	bool PositionVisible(const FBrVisionGroup& Vision, const FVector& Position, const AActor* Target = nullptr) const;
	FTimerHandle Timer;
	TMap<int64, FBrVisionGroup> Groups;
	TArray<TWeakObjectPtr<AActor>> Actors;
	double NextLog = 0;
};
