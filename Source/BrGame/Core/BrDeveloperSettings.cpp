#include "BrDeveloperSettings.h"
#include "Animation/AnimInstance.h"
#include "Engine/SkeletalMesh.h"
#include "BrInputConfig.h"

UBrDeveloperSettings::UBrDeveloperSettings()
{
	auto Voice = [](FBrAudioVoicePolicy& Policy, int32 Limit, float Priority, const TCHAR* Name)
	{
		Policy.MaxCount = Limit;
		Policy.Priority = Priority;
		Policy.Concurrency = TSoftObjectPtr<USoundConcurrency>(FSoftObjectPath(
			FString::Printf(TEXT("/Game/BrGame/Audio/SCN_%s.SCN_%s"), Name, Name)));
	};
	Voice(FireVoice, 12, 20, TEXT("Fire"));
	Voice(ExplosionVoice, 6, 60, TEXT("Explosion"));
	Voice(DownedVoice, 4, 100, TEXT("Downed"));
	Voice(FootstepVoice, 16, 90, TEXT("Footstep"));
	Voice(PickupVoice, 4, 70, TEXT("Pickup"));
	ZoneStages = { {90, 60, 0.6f, 2}, {60, 45, 0.55f, 3}, {45, 30, 0.5f, 5},
		{30, 20, 0.45f, 7}, {20, 20, 0, 10} };
	CrouchingHitChance.Standing = 80;
	CrouchingHitChance.Crouching = 60;
	CrouchingHitChance.Prone = 40;
	ProneHitChance.Standing = 100;
	ProneHitChance.Crouching = 80;
	ProneHitChance.Prone = 60;
	InputConfig = TSoftObjectPtr<UBrInputConfig>(
		FSoftObjectPath(TEXT("/Game/BrGame/Input/DA_BrInputConfig.DA_BrInputConfig")));
	DefaultCharacterMesh = TSoftObjectPtr<USkeletalMesh>(
		FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple")));
	DefaultCharacterAnimClass = TSoftClassPtr<UAnimInstance>(
		FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C")));
}

const FBrAudioVoicePolicy& UBrDeveloperSettings::VoicePolicy(EBrSoundKind Kind) const
{
	switch (Kind)
	{
	case EBrSoundKind::Fire: return FireVoice;
	case EBrSoundKind::Explosion: return ExplosionVoice;
	case EBrSoundKind::Downed: return DownedVoice;
	case EBrSoundKind::Pickup: return PickupVoice;
	default: return FootstepVoice;
	}
}

const UBrDeveloperSettings* UBrDeveloperSettings::Get()
{
	return GetDefault<UBrDeveloperSettings>();
}
