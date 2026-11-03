#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BrAudioRules.h"
#include "BrAudioSubsystem.generated.h"

class USoundBase;
class USoundAttenuation;
class ABrPlayerController;
class ABrCharacter;

/** Positional events carry sound asset/type and position only, never source actors. */
UCLASS()
class BRGAME_API UBrAudioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;
	float Range(EBrSoundKind Kind) const;
	void Play(EBrSoundKind Kind, TSoftObjectPtr<USoundBase> Sound, const FVector& Location);
	/** Sole non-owner path, independent of actor relevancy/channel lifecycle. */
	int32 Emit(AActor* Source, EBrSoundKind Kind, TSoftObjectPtr<USoundBase> Sound,
		const FVector& Location, const AController* SourceController = nullptr);
	static ABrCharacter* ListenerPawn(const ABrPlayerController* PC);
	static FVector ListenerPosition(const ABrPlayerController* PC);
	TSoftObjectPtr<USoundBase> StepSound(EBrSoundKind Kind) const;
private:
	struct FPendingListener
	{
		TWeakObjectPtr<ABrPlayerController> Controller;
		TWeakObjectPtr<ABrCharacter> Pawn;
		TArray<FBrSoundType> Types;
		TArray<FBrSoundEvent> Events;
	};
	TArray<FPendingListener> Pending;
	FTimerHandle BatchTimer;
	void Flush();
	static constexpr int32 EventsPerBatch = 128;
#if !UE_BUILD_SHIPPING
	void ServiceWindowSmoke();
	FTimerHandle WindowSmokeTimer;
	int32 WindowSmokeStage = 0;
#endif
	UPROPERTY(Transient)
	TMap<FSoftObjectPath, TObjectPtr<USoundBase>> Sounds;
	UPROPERTY(Transient)
	TMap<EBrSoundKind, TObjectPtr<USoundAttenuation>> Attenuations;
	UPROPERTY(Transient)
	TMap<EBrSoundKind, TObjectPtr<USoundConcurrency>> Concurrencies;
	TSet<FSoftObjectPath> FailedSounds;
};
