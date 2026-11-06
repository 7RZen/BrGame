#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "GameFramework/SaveGame.h"
#include "BrMatchSettings.h"
#include "BrGameInstance.generated.h"

UCLASS()
class BRGAME_API UBrProfile : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FString PlayerName = TEXT("Player");
};

UCLASS()
class BRGAME_API UBrAudioProfile : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame)
	int32 Version = 3;
	UPROPERTY(SaveGame)
	float Master = 1;
	UPROPERTY(SaveGame)
	float Effects = 1;
	UPROPERTY(SaveGame)
	float Voice = 1;
	UPROPERTY(SaveGame)
	int32 VoiceIndex = 0;
	UPROPERTY(SaveGame)
	float MouseSensitivity = 0.2f;
};

UCLASS()
class BRGAME_API UBrGameInstance : public UGameInstance
{
	GENERATED_BODY()
public:
#if !UE_BUILD_SHIPPING
	bool bMenuSmokeRejoin = false;
	// Fixture progress only; selection must survive via the production carry path.
	int32 MapSelectorSmokeStage = 0;
#endif
	virtual void Init() override;
	virtual void Shutdown() override;
	FString PlayerName = TEXT("Player");
	FString FlowError;
	void SaveName(const FString& Name);
	UFUNCTION(BlueprintCallable, Category="Settings|Audio")
	void SetAudioVolumes(float Master, float Effects);
	UFUNCTION(BlueprintPure, Category="Settings|Audio")
	float GetMasterVolume() const { return MasterVolume; }
	UFUNCTION(BlueprintPure, Category="Settings|Audio")
	float GetEffectsVolume() const { return EffectsVolume; }
	void ApplyAudioVolumes();
	float GetVoiceVolume() const { return VoiceVolume; }
	void SetVoiceVolume(float Value);
	int32 GetVoiceIndex() const { return VoiceIndex; }
	void SetVoiceIndex(int32 Index);
	float GetMouseSensitivity() const { return MouseSensitivity; }
	void SetMouseSensitivity(float Value);
	void Host();
	bool Join(const FString& Address);
	void Leave(const FString& Message = FString());
	// Travel endpoints owned by the session coordinator.
	void HostLocal();
	bool JoinLocal(const FString& Address, const FString& Proof);
	void LeaveLocal(const FString& Message);
	void PrepareConnection() { bReturning = false; FlowError.Empty(); }
	UPROPERTY()
	FBrMatchSettings HostSettings;
	UPROPERTY()
	TArray<int32> GroupColors;
	UPROPERTY()
	TArray<TWeakObjectPtr<AController>> ColorChoosers;
	bool bCarrySettings = false;
	bool bSmokeFinished = false;
private:
	float MasterVolume = 1, EffectsVolume = 1;
	float VoiceVolume = 1;
	int32 VoiceIndex = 0;
	float MouseSensitivity = 0.2f;
	void SaveLocalSettings();
	UPROPERTY(Transient)
	TObjectPtr<class USoundMix> LoadedVolumeMix;
	UPROPERTY(Transient)
	TObjectPtr<class USoundClass> LoadedMasterClass;
	UPROPERTY(Transient)
	TObjectPtr<class USoundClass> LoadedEffectsClass;
	UPROPERTY(Transient)
	TObjectPtr<class USoundClass> LoadedVoiceClass;
	bool bTriedVoiceClass = false;
	FDelegateHandle NetworkHandle, TravelHandle;
	bool bReturning = false;
	void NetworkFailed(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);
	void TravelFailed(UWorld* World, ETravelFailure::Type Type, const FString& Error);
};
