#pragma once

#include "CoreMinimal.h"
#include "BrMovement.h"
#include "BrCombatMath.generated.h"

USTRUCT(BlueprintType)
struct FBrHitChanceRow
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Hit Chance", meta=(ClampMin="0", ClampMax="100"))
	float Standing = 60;
	UPROPERTY(EditAnywhere, Category="Hit Chance", meta=(ClampMin="0", ClampMax="100"))
	float Crouching = 40;
	UPROPERTY(EditAnywhere, Category="Hit Chance", meta=(ClampMin="0", ClampMax="100"))
	float Prone = 20;

	float Get(EBrPosture Target) const
	{
		return FMath::Clamp(Target == EBrPosture::Prone ? Prone :
			Target == EBrPosture::Crouching ? Crouching : Standing, 0.0f, 100.0f);
	}
};

struct FBrCombatMath
{
	// Sprinting/running are gaits, not postures; both preserve Standing.
	static float HitChance(EBrPosture Shooter, EBrPosture Target,
		const FBrHitChanceRow& Standing, const FBrHitChanceRow& Crouching, const FBrHitChanceRow& Prone)
	{
		return (Shooter == EBrPosture::Prone ? Prone :
			Shooter == EBrPosture::Crouching ? Crouching : Standing).Get(Target);
	}
	static float Damage(float Base, EBrPosture Target, bool bExplosion, bool bGrenadePosture,
		float Standing, float Crouching, float Prone)
	{
		return FMath::Max(0.0f, Base) * (bExplosion && !bGrenadePosture ? 1.0f :
			FMath::Max(0.0f, Target == EBrPosture::Prone ? Prone :
				Target == EBrPosture::Crouching ? Crouching : Standing));
	}
	static int32 ReloadRounds(int32 Loaded, int32 Size, int32 Reserve)
	{
		return FMath::Min(FMath::Max(0, Size - Loaded), FMath::Max(0, Reserve));
	}
	static int32 GrantReserve(int32 Size, int32 Magazines)
	{
		return static_cast<int32>(FMath::Clamp<int64>(
			static_cast<int64>(FMath::Max(1, Size)) * FMath::Max<int64>(0, static_cast<int64>(Magazines) - 1), 0, MAX_int32));
	}
	static bool CanFire(double Now, double NextAllowed)
	{
		// Small jitter tolerance; scheduling from the previous deadline prevents faster sustained fire.
		return FMath::IsFinite(Now) && Now + 0.005 >= NextAllowed;
	}
	static double NextFireTime(double Now, double PreviousDeadline, float Rate)
	{
		return FMath::Max(Now, PreviousDeadline) + 1.0 / FMath::Max(0.1f, Rate);
	}
};
