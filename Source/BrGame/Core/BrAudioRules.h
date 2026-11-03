#pragma once
#include "CoreMinimal.h"
#include "BrMovement.h"
#include "Sound/SoundConcurrency.h"
#include "BrAudioRules.generated.h"

UENUM()
enum class EBrSoundKind : uint8
{
	Fire, Explosion, Downed, RunStep, WalkStep, CrouchStep, Pickup
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
		bOutSuccess = Value <= static_cast<uint8>(EBrSoundKind::Pickup) && !Ar.IsError();
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
	static EBrSoundKind StepKind(EBrPosture Posture, bool bWalking)
	{
		return Posture == EBrPosture::Crouching ? EBrSoundKind::CrouchStep :
			bWalking ? EBrSoundKind::WalkStep : EBrSoundKind::RunStep;
	}
	static float StepRange(EBrPosture Posture, bool bWalking, float Run, float Walk, float Crouch)
	{
		return Posture == EBrPosture::Prone ? 0 : Posture == EBrPosture::Crouching ? Crouch : bWalking ? Walk : Run;
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
