#pragma once

#include "CoreMinimal.h"
#include "BrLifeRules.h"
#include "BrMatchSettings.generated.h"

UENUM()
enum class EBrMode : uint8 { AllVsAll, Grouped };
UENUM()
enum class EBrTeamSelection : uint8 { PlayerChoice, AutoEven };
UENUM()
enum class EBrSessionVisibility : uint8 { Public, FriendsOnly, InviteOnly };

USTRUCT(BlueprintType)
struct FBrMatchSettings
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Match")
	float PickupRespawnTime = 30;
	UPROPERTY()
	EBrMode Mode = EBrMode::AllVsAll;
	UPROPERTY()
	int32 NumGroups = 2;
	UPROPERTY()
	EBrTeamSelection TeamSelection = EBrTeamSelection::AutoEven;
	UPROPERTY()
	bool bFriendlyFire = false;
	UPROPERTY()
	int32 MaxPlayers = 0;
	UPROPERTY()
	EBrSessionVisibility Visibility = EBrSessionVisibility::Public;
	UPROPERTY()
	bool bPasswordProtected = false;
	UPROPERTY()
	EBrReviveRule ReviveRule = EBrReviveRule::Anyone;
	void Sanitize()
	{
		NumGroups = FMath::Max(2, NumGroups);
		MaxPlayers = FMath::Max(0, MaxPlayers);
		PickupRespawnTime = ClampPickupRespawnTime(PickupRespawnTime);
	}
	static float ClampPickupRespawnTime(float Seconds)
	{
		return FMath::IsFinite(Seconds) ? FMath::Clamp(Seconds, 1.0f, 3600.0f) : 30.0f;
	}
};
