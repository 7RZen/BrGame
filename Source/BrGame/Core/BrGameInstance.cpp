#include "BrGameInstance.h"
#include "BrAimingRules.h"
#include "BrDeveloperSettings.h"
#include "BrAudioRules.h"
#include "BrSoundEventRules.h"
#include "BrPlayerController.h"
#include "AudioDevice.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundClass.h"
#include "BrSessionSubsystem.h"
#include "BrLog.h"
#include "BrFlowRules.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/PendingNetGame.h"
#include "Kismet/GameplayStatics.h"
#include "String/LexFromString.h"

void UBrGameInstance::Init()
{
	Super::Init();
	MouseSensitivity = FBrAimingRules::Sensitivity(UBrDeveloperSettings::Get()->DefaultMouseSensitivity);
	if (UGameplayStatics::DoesSaveGameExist(TEXT("BrAudio"), 0))
	{
		const UBrAudioProfile* Audio = Cast<UBrAudioProfile>(UGameplayStatics::LoadGameFromSlot(TEXT("BrAudio"), 0));
		if (Audio && Audio->Version >= 1 && Audio->Version <= 3)
		{
			MasterVolume = FBrAudioRules::Volume(Audio->Master);
			EffectsVolume = FBrAudioRules::Volume(Audio->Effects);
			// Version 1 had no sensitivity: retain the developer default.
			if (Audio->Version >= 2) MouseSensitivity = FBrAimingRules::Sensitivity(Audio->MouseSensitivity);
			if (Audio->Version >= 3)
			{
				VoiceVolume = FBrAudioRules::Volume(Audio->Voice);
				VoiceIndex = FBrSoundEventRules::VoiceIndex(Audio->VoiceIndex, UBrDeveloperSettings::Get()->VoiceSets.Num());
			}
		}
		else UE_LOG(LogBr, Error, TEXT("Local audio settings cannot be loaded (unsupported version or invalid save)"));
	}
	if (UGameplayStatics::DoesSaveGameExist(TEXT("BrProfile"), 0))
	{
		if (const UBrProfile* Profile = Cast<UBrProfile>(UGameplayStatics::LoadGameFromSlot(TEXT("BrProfile"), 0)))
			PlayerName = Profile->PlayerName;
		else UE_LOG(LogBr, Error, TEXT("Local player profile could not be loaded"));
	}
	if (GEngine)
	{
		NetworkHandle = GEngine->OnNetworkFailure().AddUObject(this, &UBrGameInstance::NetworkFailed);
		TravelHandle = GEngine->OnTravelFailure().AddUObject(this, &UBrGameInstance::TravelFailed);
	}
}
void UBrGameInstance::Shutdown()
{
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkHandle);
		GEngine->OnTravelFailure().Remove(TravelHandle);
	}
	Super::Shutdown();
}
void UBrGameInstance::SetAudioVolumes(float Master, float Effects)
{
	MasterVolume = FBrAudioRules::Volume(Master);
	EffectsVolume = FBrAudioRules::Volume(Effects);
	ApplyAudioVolumes();
	SaveLocalSettings();
}

void UBrGameInstance::SetMouseSensitivity(float Value)
{
	MouseSensitivity = FBrAimingRules::Sensitivity(Value);
	SaveLocalSettings();
}

void UBrGameInstance::SetVoiceVolume(float Value)
{
	VoiceVolume = FBrAudioRules::Volume(Value);
	ApplyAudioVolumes();
	SaveLocalSettings();
}

void UBrGameInstance::SetVoiceIndex(int32 Index)
{
	VoiceIndex = FBrSoundEventRules::VoiceIndex(Index, UBrDeveloperSettings::Get()->VoiceSets.Num());
	SaveLocalSettings();
	if (ABrPlayerController* PC = Cast<ABrPlayerController>(GetFirstLocalPlayerController()))
		PC->SendVoicePreference();
}

void UBrGameInstance::SaveLocalSettings()
{
	UBrAudioProfile* Profile = Cast<UBrAudioProfile>(UGameplayStatics::CreateSaveGameObject(UBrAudioProfile::StaticClass()));
	if (!Profile) { UE_LOG(LogBr, Error, TEXT("Cannot allocate audio profile")); return; }
	Profile->Master = MasterVolume;
	Profile->Effects = EffectsVolume;
	Profile->Voice = VoiceVolume;
	Profile->VoiceIndex = VoiceIndex;
	Profile->MouseSensitivity = MouseSensitivity;
	if (!UGameplayStatics::SaveGameToSlot(Profile, TEXT("BrAudio"), 0))
		UE_LOG(LogBr, Error, TEXT("Cannot save local volume/sensitivity settings"));
}

void UBrGameInstance::ApplyAudioVolumes()
{
	UWorld* World = GetWorld();
	if (!World || !World->GetAudioDevice().IsValid()) return;
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	if (!LoadedVolumeMix) LoadedVolumeMix = S->VolumeSoundMix.LoadSynchronous();
	if (!LoadedMasterClass) LoadedMasterClass = S->MasterSoundClass.LoadSynchronous();
	if (!LoadedEffectsClass) LoadedEffectsClass = S->EffectsSoundClass.LoadSynchronous();
	if (!LoadedVolumeMix || !LoadedMasterClass || !LoadedEffectsClass)
	{
		UE_LOG(LogBr, Error, TEXT("Audio volume mix/classes missing; run create_sound_assets.py and check Audio settings"));
		return;
	}
	UGameplayStatics::SetSoundMixClassOverride(World, LoadedVolumeMix, LoadedMasterClass, MasterVolume, 1, 0, true);
	UGameplayStatics::SetSoundMixClassOverride(World, LoadedVolumeMix, LoadedEffectsClass, EffectsVolume, 1, 0, true);
	if (!bTriedVoiceClass)
	{
		bTriedVoiceClass = true;
		LoadedVoiceClass = S->VoiceSoundClass.LoadSynchronous();
		if (!LoadedVoiceClass)
			UE_LOG(LogBr, Warning, TEXT("Voice volume class missing: %s; voice lines use effects volume until content is installed"),
				*S->VoiceSoundClass.ToString());
	}
	if (LoadedVoiceClass)
		UGameplayStatics::SetSoundMixClassOverride(World, LoadedVolumeMix, LoadedVoiceClass, VoiceVolume, 1, 0, true);
	// Base mix is device-owned and idempotent across travel; no modifier stack accumulates.
	UGameplayStatics::SetBaseSoundMix(World, LoadedVolumeMix);
	UE_LOG(LogBr, Verbose, TEXT("Audio volumes applied: Master=%.2f Effects=%.2f"), MasterVolume, EffectsVolume);
}
void UBrGameInstance::SaveName(const FString& Name)
{
	PlayerName = FBrFlowRules::SanitizeName(Name);
	UBrProfile* Profile = Cast<UBrProfile>(UGameplayStatics::CreateSaveGameObject(UBrProfile::StaticClass()));
	if (!Profile) { UE_LOG(LogBr, Error, TEXT("Cannot allocate player profile")); return; }
	Profile->PlayerName = PlayerName;
	if (!UGameplayStatics::SaveGameToSlot(Profile, TEXT("BrProfile"), 0))
		UE_LOG(LogBr, Error, TEXT("Cannot save local player name"));
}
void UBrGameInstance::Host()
{
	GetSubsystem<UBrSessionSubsystem>()->Host();
}
void UBrGameInstance::HostLocal()
{
	bReturning = false;
	FlowError.Empty();
	bCarrySettings = false;
	UGameplayStatics::OpenLevel(this, TEXT("/Game/BrGame/Maps/L_Lobby"), true, TEXT("listen?Name=") + PlayerName);
}
bool UBrGameInstance::Join(const FString& Input)
{
	GetSubsystem<UBrSessionSubsystem>()->JoinIP(Input, FString());
	return true;
}
bool UBrGameInstance::JoinLocal(const FString& Input, const FString& Proof)
{
	const FString Address = Input.TrimStartAndEnd();
	FString IP, PortString;
	Address.Split(TEXT(":"), &IP, &PortString);
	if (IP.IsEmpty()) IP = Address;
	TArray<FString> Octets;
	IP.ParseIntoArray(Octets, TEXT("."), false);
	bool bValid = Octets.Num() == 4;
	for (const FString& Octet : Octets)
	{
		int32 Value = -1;
		bValid &= Octet.IsNumeric() && LexTryParseString(Value, *Octet) && Value >= 0 && Value <= 255;
	}
	int32 Port = 7777;
	if (Address.Contains(TEXT(":")))
		bValid &= PortString.IsNumeric() && LexTryParseString(Port, *PortString) && Port > 0 && Port <= 65535;
	if (!bValid) { FlowError = TEXT("Enter an IPv4 address, optionally :port."); return false; }
	bReturning = false;
	FlowError.Empty();
	if (APlayerController* PC = GetFirstLocalPlayerController())
	{
		PC->ClientTravel(Address + TEXT("?Name=") + PlayerName + TEXT("?BrProof=") + Proof, TRAVEL_Absolute);
		return true;
	}
	FlowError = TEXT("No local player controller.");
	UE_LOG(LogBr, Error, TEXT("%s"), *FlowError);
	return false;
}
void UBrGameInstance::Leave(const FString& Message)
{
	GetSubsystem<UBrSessionSubsystem>()->Leave(Message);
}
void UBrGameInstance::LeaveLocal(const FString& Message)
{
	FlowError = Message;
	bCarrySettings = false;
	bReturning = true;
	UGameplayStatics::OpenLevel(this, TEXT("/Game/BrGame/Maps/L_MainMenu"), true);
}
void UBrGameInstance::NetworkFailed(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error)
{
	// UE broadcasts pending-connection failures with a null world (PreLogin
	// rejection, unreachable address). Scope those by this instance's pending driver.
	const FWorldContext* Context = GetWorldContext();
	const bool bOurPending = !World && Driver && Context && Context->PendingNetGame &&
		Context->PendingNetGame->NetDriver == Driver;
	if ((!bOurPending && World != GetWorld()) || bReturning) return;
	// A remote client's rejection must not tear down a healthy listen host.
	if (World && World->GetNetMode() == NM_ListenServer) return;
	UE_LOG(LogBr, Error, TEXT("Flow network failure: %s"), *Error);
	Leave(TEXT("Connection lost or host left: ") + Error);
}
void UBrGameInstance::TravelFailed(UWorld* World, ETravelFailure::Type Type, const FString& Error)
{
	if (World != GetWorld() || bReturning) return;
	UE_LOG(LogBr, Error, TEXT("Flow travel failure: %s"), *Error);
	Leave(TEXT("Travel failed: ") + Error);
}
