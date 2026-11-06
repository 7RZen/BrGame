#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BrAudioRules.h"
#include "BrAudioSubsystem.generated.h"

class USoundBase;
class USoundAttenuation;
class ABrPlayerController;
class ABrCharacter;
class UAudioComponent;
class UBrVoiceSet;

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
	UAudioComponent* Play(EBrSoundKind Kind, TSoftObjectPtr<USoundBase> Sound, const FVector& Location);
	/** Sole non-owner path, independent of actor relevancy/channel lifecycle. */
	int32 Emit(AActor* Source, EBrSoundKind Kind, TSoftObjectPtr<USoundBase> Sound,
		const FVector& Location, const AController* SourceController = nullptr, bool bIncludeOwner = false);
	/** Reliable per-listener handles disclose neither an actor nor an emitter identity. */
	uint32 StartReload(AActor* Source, TSoftObjectPtr<USoundBase> Sound);
	void StopReload(AActor* Source, uint32 Id);
	void ForgetReload(uint32 Id);
	void PlayReload(uint32 Id, TSoftObjectPtr<USoundBase> Sound, const FVector& Location);
	void StopLocalReload(uint32 Id);
	UBrVoiceSet* VoiceSet(int32 Index);
	/** Shared production route; also inspectable without a live audio device. */
	class USoundClass* SoundClass(EBrSoundKind Kind);
	void SpeakDown(ABrCharacter* Attacker);
	static ABrCharacter* ListenerPawn(const ABrPlayerController* PC);
	static FVector ListenerPosition(const ABrPlayerController* PC);
	TSoftObjectPtr<USoundBase> StepSound(EBrSoundKind Kind) const;
	/** Same local path lookup for predicted fire and remote Kind + Sound RPCs. Unknown sounds use 1x. */
	float WeaponFireVolume(const FSoftObjectPath& SoundPath);
private:
	struct FPendingListener
	{
		TWeakObjectPtr<ABrPlayerController> Controller;
		TWeakObjectPtr<ABrCharacter> Pawn;
		TArray<FBrSoundType> Types;
		TArray<FBrSoundEvent> Events;
	};
	TArray<FPendingListener> Pending;
	struct FReloadListeners
	{
		TWeakObjectPtr<AActor> Source;
		TArray<TWeakObjectPtr<ABrPlayerController>> Listeners;
	};
	TMap<uint32, FReloadListeners> ReloadListeners;
	TMap<uint32, TWeakObjectPtr<UAudioComponent>> LocalReloads;
	uint32 NextReloadId = 0;
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
	UPROPERTY(Transient)
	TMap<FSoftObjectPath, TObjectPtr<class UBrWeaponDefinition>> AudioWeapons;
	TSet<FSoftObjectPath> FailedAudioWeapons;
	TSet<FSoftObjectPath> ConflictingFireSounds;
	UPROPERTY(Transient)
	TMap<FSoftObjectPath, TObjectPtr<UBrVoiceSet>> VoiceLibrary;
	TSet<FSoftObjectPath> FailedVoiceSets;
	UPROPERTY(Transient)
	TObjectPtr<class USoundClass> VoiceClass;
	bool bTriedVoiceClass = false;
};
