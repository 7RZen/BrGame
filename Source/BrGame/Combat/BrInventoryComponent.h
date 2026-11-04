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
		if (!Type)
		{
			return 0;
		}
		for (const FBrAmmoPool& Pool : Pools)
		{
			if (Pool.AmmoType == Type)
			{
				return Pool.Count;
			}
		}
		return 0;
	}

	static void Add(TArray<FBrAmmoPool>& Pools, UBrAmmoDefinition* Type, int32 Count)
	{
		if (!Type || Count <= 0)
		{
			return;
		}
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
	void CancelCombat();

	bool IsCombatHeld() const
	{
		return GetOwner()->HasAuthority()
			       ? (bServerCombatHeld || PendingServerFire.IsSet() || PendingServerGrenade.IsSet())
			       : (bFireHeld || bPendingFire || bPendingGrenade);
	}

	void RequestReload();
	void RequestGrenade();
#if !UE_BUILD_SHIPPING
	double GetLastSprintFireRecovery() const { return LastSprintFireRecovery; }
#endif
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FBrServerAimTest;
	friend class FBrSprintIntegrationTest;
	friend class FBrSprintPendingCombatTest;
	friend class FBrSprintReplayRecoveryTest;
#endif
	UPROPERTY()
	TArray<FBrServerBullet> Bullets;
	FTimerHandle FireTimer;
	FTimerHandle ReloadTimer;
	FTimerHandle SprintFireTimer;
	FTimerHandle SprintGrenadeTimer;

	struct FPendingSprintAction
	{
		FVector Aim = FVector::ForwardVector;
		float MoveTimeStamp = 0;
		TWeakObjectPtr<UObject> Definition;
	};

	// Bounded first-request-wins slots: retransmission/bursts cannot create an
	// unbounded queue or bank a catch-up burst beyond the normal fire-rate gate.
	TOptional<FPendingSprintAction> PendingServerFire;
	TOptional<FPendingSprintAction> PendingServerGrenade;
	FTimerHandle PendingServerFireTimer;
	FTimerHandle PendingServerGrenadeTimer;
	void QueueSprintAction(bool bGrenade, const FVector& Aim, float MoveTimeStamp);
	void ServicePendingFire();
	void ServicePendingGrenade();
	void ClearServerPendingCombat();
	bool CombatRequestsAllowed() const;
	bool bFireHeld = false;
	bool bServerCombatHeld = false;
	bool bPendingFire = false;
	bool bPendingGrenade = false;
#if !UE_BUILD_SHIPPING
	double LastSprintFireRecovery = -1;
#endif
	void DeferredFire();
	void SendGrenade();
	void UpdateCombatIntent();
	UFUNCTION(Server, Reliable)
	void ServerCombatIntent(bool bHeld);
	UFUNCTION(Server, Reliable)
	void ServerCancelCombat();
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
