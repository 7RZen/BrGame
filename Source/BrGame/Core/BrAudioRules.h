#pragma once
#include "CoreMinimal.h"
#include "BrMovement.h"
#include "Sound/SoundConcurrency.h"
#include "BrAudioRules.generated.h"

UENUM()
enum class EBrSoundKind : uint8
{
	Fire, Explosion, Downed, RunStep, SprintStep, CrouchStep, Pickup, Slide, ArmorBreak,
	Reload, DryFire, Pain, PainArmor, VoiceLine
};

USTRUCT()
struct FBrSoundType
{
	GENERATED_BODY()
	UPROPERTY()
	EBrSoundKind Kind = EBrSoundKind::Fire;
	UPROPERTY()
	TSoftObjectPtr<class USoundBase> Sound;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
	{
		uint8 Value = static_cast<uint8>(Kind);
		Ar << Value;
		FSoftObjectPath Path = Sound.ToSoftObjectPath();
		Ar << Path;
		if (Ar.IsLoading())
		{
			Kind = static_cast<EBrSoundKind>(Value);
			Sound = TSoftObjectPtr<USoundBase>(Path);
		}
		bOutSuccess = Value <= static_cast<uint8>(EBrSoundKind::VoiceLine) && !Ar.IsError();
		return true;
	}
};

/** Batch-local type dictionary: no source actor, ownership or velocity goes on the wire. */
USTRUCT()
struct FBrSoundEvent
{
	GENERATED_BODY()
	UPROPERTY()
	uint8 TypeIndex = 0;
	UPROPERTY()
	FVector_NetQuantize Location = FVector::ZeroVector;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
	{
		Ar << TypeIndex;
		bOutSuccess = true;
		// Whole-centimetre signed coordinates, ZigZag + packed integers.
		// No NetCore-only inline vector helpers or new module dependency needed.
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			uint32 Packed = 0;
			if (Ar.IsSaving())
			{
				const double Value = Location[Axis];
				if (!FMath::IsFinite(Value) || Value < MIN_int32 + 1.0 || Value > MAX_int32 - 1.0)
				{
					bOutSuccess = false;
					return true;
				}
				const int32 Coordinate = FMath::RoundToInt(Value);
				Packed = (static_cast<uint32>(Coordinate) << 1) ^ (Coordinate < 0 ? MAX_uint32 : 0U);
			}
			Ar.SerializeIntPacked(Packed);
			if (Ar.IsLoading()) Location[Axis] = static_cast<int32>((Packed >> 1) ^ (0U - (Packed & 1U)));
		}
		bOutSuccess = !Ar.IsError();
		return true;
	}
};

template<> struct TStructOpsTypeTraits<FBrSoundType> : TStructOpsTypeTraitsBase2<FBrSoundType>
{
	enum { WithNetSerializer = true };
};
template<> struct TStructOpsTypeTraits<FBrSoundEvent> : TStructOpsTypeTraitsBase2<FBrSoundEvent>
{
	enum { WithNetSerializer = true };
};

USTRUCT()
struct FBrAudioVoicePolicy
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Audio", meta=(ClampMin="1", ClampMax="128"))
	int32 MaxCount = 12;
	UPROPERTY(EditAnywhere, Category="Audio")
	TEnumAsByte<EMaxConcurrentResolutionRule::Type> ResolutionRule = EMaxConcurrentResolutionRule::StopFarthestThenOldest;
	UPROPERTY(EditAnywhere, Category="Audio", meta=(ClampMin="0", ClampMax="100"))
	float Priority = 20;
	UPROPERTY(EditAnywhere, Category="Audio")
	TSoftObjectPtr<USoundConcurrency> Concurrency;
};

/** Shared by authority targeting, local attenuation and automation. Distances are cm. */
struct FBrAudioRules
{
	/** Developer gain is independent of the player's 0..1 comfort sliders. */
	static float Gain(float Value) { return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.f, 2.f) : 1.f; }
	template<typename SettingsType>
	static float KindVolume(const SettingsType& S, EBrSoundKind Kind)
	{
		switch (Kind)
		{
		case EBrSoundKind::Fire: return Gain(S.FireVolume);
		case EBrSoundKind::Explosion: return Gain(S.ExplosionVolume);
		case EBrSoundKind::Downed: return Gain(S.DownedVolume);
		case EBrSoundKind::RunStep: return Gain(S.RunStepVolume);
		case EBrSoundKind::SprintStep: return Gain(S.SprintStepVolume);
		case EBrSoundKind::CrouchStep: return Gain(S.CrouchStepVolume);
		case EBrSoundKind::Pickup: return Gain(S.PickupVolume);
		case EBrSoundKind::Slide: return Gain(S.SlideVolume);
		case EBrSoundKind::ArmorBreak: return Gain(S.ArmorBreakVolume);
		case EBrSoundKind::Reload: return Gain(S.ReloadVolume);
		case EBrSoundKind::DryFire: return Gain(S.DryFireVolume);
		case EBrSoundKind::Pain: return Gain(S.PainVolume);
		case EBrSoundKind::PainArmor: return Gain(S.PainArmorVolume);
		case EBrSoundKind::VoiceLine: return Gain(S.VoiceLineVolume);
		default: return 1.f;
		}
	}
	static float FireVolume(float KindGain, float WeaponGain, float Variation)
	{
		// Clamp inputs, not the product: two 2x gains deliberately produce 4x.
		return Gain(KindGain) * Gain(WeaponGain) *
			(1.f + (FMath::IsFinite(Variation) ? FMath::Clamp(Variation, -0.2f, 0.2f) : 0.f));
	}
	static EBrSoundKind StepKind(EBrPosture Posture, bool bSprinting)
	{
		return Posture == EBrPosture::Crouching ? EBrSoundKind::CrouchStep :
			bSprinting ? EBrSoundKind::SprintStep : EBrSoundKind::RunStep;
	}
	static float StepRange(EBrPosture Posture, bool bSprinting, float Run, float Sprint, float Crouch)
	{
		return Posture == EBrPosture::Prone ? 0 : Posture == EBrPosture::Crouching ? Crouch : bSprinting ? Sprint : Run;
	}
	static bool InRange(const FVector& Source, const FVector& Listener, float Range)
	{
		return Range > 0 && FVector::DistSquared2D(Source, Listener) < FMath::Square(Range);
	}
	static bool SendOther(bool bListenerValid, bool bInRange, bool bOwnConnection)
	{
		return bListenerValid && bInRange && !bOwnConnection;
	}
	static bool ListenerEligible(bool bWorldReady, bool bAdmitted, bool bPawnValid,
		bool bSpectating, bool bSelectedTargetValid)
	{
		return bWorldReady && bAdmitted && (bSpectating ? bSelectedTargetValid : bPawnValid);
	}
	static float StepInterval(float Speed, float Stride, float MinimumSpeed)
	{
		return Speed >= FMath::Max(1.f, MinimumSpeed) ? FMath::Max(1.f, Stride) / Speed : 0;
	}
	static float Volume(float Value) { return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.f, 1.f) : 1.f; }
};
