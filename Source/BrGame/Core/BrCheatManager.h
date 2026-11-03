#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CheatManager.h"
#include "BrCheatManager.generated.h"

UCLASS()
class BRGAME_API UBrCheatManager : public UCheatManager
{
	GENERATED_BODY()
public:
	UFUNCTION(Exec)
	void BrFogToggle();
	UFUNCTION(Exec)
	void BrTeleport(float X, float Y);
	UFUNCTION(Exec)
	void BrZoneNextStage();
	UFUNCTION(Exec)
	void BrZoneSetTimeScale(float Scale);
	UFUNCTION(Exec)
	void BrZonePause();
	UFUNCTION(Exec)
	void BrGiveWeapon(FName Weapon);
	UFUNCTION(Exec)
	void BrGiveGrenades(int32 Count);
	UFUNCTION(Exec)
	void BrGiveAmmo(int32 Count);
	UFUNCTION(Exec)
	void BrSetHealth(float Amount);
	UFUNCTION(Exec)
	void BrDamageSelf(float Amount);
	UFUNCTION(Exec)
	void BrDownSelf();
	/** Zero-based GameState.PlayerArray index or exact player name. */
	UFUNCTION(Exec)
	void BrDownPlayer(FName Player);
	UFUNCTION(Exec)
	void BrDownAll();
	UFUNCTION(Exec)
	void BrSetDeaths(int32 Count);
	UFUNCTION(Exec)
	void BrSpawnPickup(FName Name);
	UFUNCTION(Exec)
	void BrRespawnAllPickups();
private:
	void Send(FName Command, FName Weapon, float Amount);
};
