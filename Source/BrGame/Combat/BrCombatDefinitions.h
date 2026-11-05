#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/DamageType.h"
#include "BrCombatDefinitions.generated.h"

class UStaticMesh;
class USoundBase;

UCLASS(BlueprintType)
class BRGAME_API UBrAmmoDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="Ammo")
	FText DisplayName;
	/** Reserve-only cap; zero means unlimited. Designer/server tuning, never a player option. */
	UPROPERTY(EditAnywhere, Category="Ammo", meta=(ClampMin="0"))
	int32 ReserveCap = 0;
};

UCLASS(BlueprintType)
class BRGAME_API UBrWeaponDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="Weapon")
	FText DisplayName;
	UPROPERTY(EditAnywhere, Category="Weapon", meta=(ClampMin="0"))
	float Damage = 10;
	UPROPERTY(EditAnywhere, Category="Weapon", meta=(ClampMin="0.1", Units="Hz"))
	float FireRate = 4;
	UPROPERTY(EditAnywhere, Category="Weapon")
	bool bAutomatic = false;
	UPROPERTY(EditAnywhere, Category="Weapon", meta=(ClampMin="1"))
	int32 MagazineSize = 12;
	/** Includes the loaded magazine. */
	UPROPERTY(EditAnywhere, Category="Weapon", meta=(ClampMin="1"))
	int32 StartingMagazines = 2;
	UPROPERTY(EditAnywhere, Category="Weapon", meta=(ClampMin="0.01", Units="s"))
	float ReloadTime = 1.5f;
	UPROPERTY(EditAnywhere, Category="Weapon", meta=(ClampMin="1", Units="cm/s"))
	float ProjectileSpeed = 15000;
	UPROPERTY(EditAnywhere, Category="Weapon", meta=(ClampMin="1", Units="cm"))
	float MaxRange = 2500;
	UPROPERTY(EditAnywhere, Category="Weapon", meta=(ClampMin="0.1", ClampMax="1"))
	float RunSpeedMultiplier = 1;
	UPROPERTY(EditAnywhere, Category="Weapon")
	TObjectPtr<UBrAmmoDefinition> AmmoType;
	UPROPERTY(EditAnywhere, Category="Cosmetics")
	TObjectPtr<UStaticMesh> Mesh;
	UPROPERTY(EditAnywhere, Category="Cosmetics")
	FVector VisualScale = FVector(0.5f, 0.08f, 0.08f);
	UPROPERTY(EditAnywhere, Category="Audio")
	TSoftObjectPtr<USoundBase> FireSound;
};

UCLASS(BlueprintType)
class BRGAME_API UBrThrowableDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="Throwable")
	FText DisplayName;
	UPROPERTY(EditAnywhere, Category="Throwable", meta=(ClampMin="0"))
	float Damage = 70;
	UPROPERTY(EditAnywhere, Category="Throwable", meta=(ClampMin="1", Units="cm"))
	float BlastRadius = 136;
	UPROPERTY(EditAnywhere, Category="Throwable", meta=(ClampMin="1", Units="cm/s"))
	float ThrowSpeed = 1500;
	UPROPERTY(EditAnywhere, Category="Throwable", meta=(ClampMin="0"))
	int32 MaxCarried = 2;
	UPROPERTY(EditAnywhere, Category="Throwable")
	bool bDamageFalloff = false;
	UPROPERTY(EditAnywhere, Category="Cosmetics")
	TObjectPtr<UStaticMesh> Mesh;
	UPROPERTY(EditAnywhere, Category="Audio")
	TSoftObjectPtr<USoundBase> ExplosionSound;
};

UCLASS()
class BRGAME_API UBrBulletDamageType : public UDamageType
{
	GENERATED_BODY()
};

UCLASS()
class BRGAME_API UBrExplosionDamageType : public UDamageType
{
	GENERATED_BODY()
};
