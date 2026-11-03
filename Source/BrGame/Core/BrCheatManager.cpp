#include "BrCheatManager.h"
#include "BrPlayerController.h"

void UBrCheatManager::BrFogToggle() { Send(TEXT("FogToggle"), NAME_None, 0); }
void UBrCheatManager::BrTeleport(float X, float Y) { Send(TEXT("Teleport"), FName(*FString::SanitizeFloat(X)), Y); }

void UBrCheatManager::BrZoneNextStage() { Send(TEXT("ZoneNext"), NAME_None, 0); }
void UBrCheatManager::BrZoneSetTimeScale(float Scale) { Send(TEXT("ZoneScale"), NAME_None, Scale); }
void UBrCheatManager::BrZonePause() { Send(TEXT("ZonePause"), NAME_None, 0); }

void UBrCheatManager::Send(FName Command, FName Weapon, float Amount)
{
#if !UE_BUILD_SHIPPING
	if (ABrPlayerController* PC = Cast<ABrPlayerController>(GetOuterAPlayerController()))
	{
		PC->ServerBrCheat(Command, Weapon, Amount);
	}
#endif
}

void UBrCheatManager::BrGiveWeapon(FName Weapon) { Send(TEXT("Weapon"), Weapon, 0); }
void UBrCheatManager::BrGiveGrenades(int32 Count) { Send(TEXT("Grenades"), NAME_None, Count); }
void UBrCheatManager::BrGiveAmmo(int32 Count) { Send(TEXT("Ammo"), NAME_None, Count); }
void UBrCheatManager::BrSetHealth(float Amount) { Send(TEXT("Health"), NAME_None, Amount); }
void UBrCheatManager::BrDamageSelf(float Amount) { Send(TEXT("Damage"), NAME_None, Amount); }
void UBrCheatManager::BrDownSelf() { Send(TEXT("Down"), NAME_None, 0); }
void UBrCheatManager::BrDownPlayer(FName Player) { Send(TEXT("DownPlayer"), Player, 0); }
void UBrCheatManager::BrDownAll() { Send(TEXT("DownAll"), NAME_None, 0); }
void UBrCheatManager::BrSetDeaths(int32 Count) { Send(TEXT("Deaths"), NAME_None, Count); }
void UBrCheatManager::BrSpawnPickup(FName Name) { Send(TEXT("SpawnPickup"), Name, 0); }
void UBrCheatManager::BrRespawnAllPickups() { Send(TEXT("RespawnPickups"), NAME_None, 0); }
