#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BrCombatDefinitions.h"
#include "BrMovement.h"
#include "BrDamageSource.h"
#include "BrInventoryComponent.generated.h"

USTRUCT(BlueprintType)
struct FBrAmmoPool
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Inventory")
	TObjectPtr<UBrAmmoDefinition> AmmoType;
	UPROPERTY(BlueprintReadOnly, Category="Inventory")
	int32 Count = 0;
};

/** World-free accounting; pools are keyed by asset identity, not weapon identity. */
struct FBrAmmoAccounting
{
	static int32 Reserve(const TArray<FBrAmmoPool>& Pools, const UBrAmmoDefinition* Type)
	{
		if (!Type) return 0;
		for (const FBrAmmoPool& Pool : Pools) if (Pool.AmmoType == Type) return Pool.Count;
		return 0;
	}
	static void Add(TArray<FBrAmmoPool>& Pools, UBrAmmoDefinition* Type, int32 Count)
	{
		if (!Type || Count <= 0) return;
		for (FBrAmmoPool& Pool : Pools)
		{
			if (Pool.AmmoType == Type)
			{
				Pool.Count = static_cast<int32>(FMath::Min<int64>(MAX_int32, static_cast<int64>(Pool.Count) + Count));
				return;
			}
		}
		FBrAmmoPool& Pool = Pools.AddDefaulted_GetRef();
		Pool.AmmoType = Type;
		Pool.Count = Count;
	}
};

/** Persistent gear only: no timers, in-flight shots or reload state. Step 4 can transfer this. */
USTRUCT(BlueprintType)
struct FBrInventoryState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Inventory")
	TObjectPtr<UBrWeaponDefinition> Weapon;
	UPROPERTY(BlueprintReadOnly, Category="Inventory")
	int32 Loaded = 0;
	UPROPERTY(BlueprintReadOnly, Category="Inventory")
	TArray<FBrAmmoPool> Ammo;
	UPROPERTY(BlueprintReadOnly, Category="Inventory")
	TObjectPtr<UBrThrowableDefinition> Throwable;
	UPROPERTY(BlueprintReadOnly, Category="Inventory")
	int32 Grenades = 0;
};

USTRUCT()
struct FBrServerBullet
{
	GENERATED_BODY()
	FVector Position = FVector::ZeroVector;
	FVector Origin = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	float Remaining = 0;
	EBrPosture ShooterPosture = EBrPosture::Standing;
	UPROPERTY()
	TObjectPtr<UBrWeaponDefinition> Definition;
	UPROPERTY()
	FBrDamageSource Source;
	TArray<TWeakObjectPtr<AActor>> Ignored;
};

UCLASS(ClassGroup=(BrGame), meta=(BlueprintSpawnableComponent))
class BRGAME_API UBrInventoryComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UBrInventoryComponent();
	UPROPERTY(ReplicatedUsing=OnRep_Weapon, BlueprintReadOnly, Category="Inventory")
	TObjectPtr<UBrWeaponDefinition> Weapon;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Inventory")
	TObjectPtr<UBrThrowableDefinition> Throwable;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Inventory")
	int32 Loaded = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Inventory")
	TArray<FBrAmmoPool> Ammo;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Inventory")
	int32 Grenades = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Inventory")
	bool bReloading = false;

	int32 GetReserve() const;
	bool GiveWeapon(UBrWeaponDefinition* Definition);
	static bool IsValidWeaponDefinition(const UBrWeaponDefinition* Definition);
	/** Authority-only equip with explicit ammo; pickup payloads never receive fresh magazines implicitly. */
	bool EquipWeapon(UBrWeaponDefinition* Definition, int32 LoadedRounds, int32 ReserveRounds);
	void GiveAmmo(int32 Count);
	void GiveGrenades(int32 Count);
	FBrInventoryState CaptureState() const;
	void RestoreState(const FBrInventoryState& State);
	void StartFire();
	void StopFire();
	void RequestReload();
	void RequestGrenade();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FBrServerAimTest;
#endif
	UPROPERTY()
	TArray<FBrServerBullet> Bullets;
	FTimerHandle FireTimer;
	FTimerHandle ReloadTimer;
	double NextAllowedFire = 0;
	double NextLocalFire = 0;
	double NextGrenade = 0;
	bool bRestoredState = false;
	void AttemptFire();
	void FinishReload();
	void CancelReload();
	bool IsAlive() const;
	void AddAmmo(UBrAmmoDefinition* Type, int32 Count);
	static bool ValidAim(const FVector& Aim);
	UFUNCTION()
	void OnRep_Weapon();
	UFUNCTION()
	void HealthDepleted();
	UFUNCTION(Server, Reliable)
	void ServerFire(FVector_NetQuantizeNormal Aim, float MoveTimeStamp);
	UFUNCTION(Server, Reliable)
	void ServerReload();
	UFUNCTION(Server, Reliable)
	void ServerGrenade(FVector_NetQuantizeNormal Aim, float MoveTimeStamp);
};
