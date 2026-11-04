#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BrCombatDefinitions.h"
#include "BrPickupDefinition.generated.h"

class ABrCharacter;
class UMaterialInterface;
class UStaticMesh;

USTRUCT(BlueprintType)
struct FBrPickupPayload
{
	GENERATED_BODY()
	UPROPERTY()
	bool bDropped = false;
	UPROPERTY()
	int32 LoadedRounds = 0;
	/** A generic drop template can represent any future weapon definition. */
	UPROPERTY()
	TObjectPtr<UBrWeaponDefinition> DroppedWeapon;
	// Apply's transient feedback output; not replicated with the payload.
	FString Feedback;
};

UCLASS(Abstract, BlueprintType)
class BRGAME_API UBrPickupDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="Pickup")
	FText DisplayName;
	UPROPERTY(EditAnywhere, Category="Audio")
	TSoftObjectPtr<USoundBase> PickupSound;
	UPROPERTY(EditAnywhere, Category="Pickup")
	bool bRequiresInteract = false;
	UPROPERTY(EditAnywhere, Category="Visual")
	TObjectPtr<UStaticMesh> Mesh;
	UPROPERTY(EditAnywhere, Category="Visual")
	TObjectPtr<UMaterialInterface> Material;
	UPROPERTY(EditAnywhere, Category="Visual")
	FLinearColor Color = FLinearColor::White;
	UPROPERTY(EditAnywhere, Category="Visual")
	FText Label;
	UPROPERTY(EditAnywhere, Category="Visual", meta=(ClampMin="0.01", ClampMax="2"))
	FVector VisualScale = FVector(0.5f);
	virtual bool CanApply(const ABrCharacter* Character, const FBrPickupPayload& Payload) const;
	virtual bool Apply(ABrCharacter* Character, FBrPickupPayload& Payload) const PURE_VIRTUAL(UBrPickupDefinition::Apply, return false;);
	virtual FText GetLabel(const FBrPickupPayload& Payload) const;
};

UCLASS(BlueprintType)
class BRGAME_API UBrAmmoPickupDefinition : public UBrPickupDefinition
{
	GENERATED_BODY()
public:
	virtual bool CanApply(const ABrCharacter* Character, const FBrPickupPayload& Payload) const override;
	virtual bool Apply(ABrCharacter* Character, FBrPickupPayload& Payload) const override;
};

UCLASS(BlueprintType)
class BRGAME_API UBrHealthPickupDefinition : public UBrPickupDefinition
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="Pickup", meta=(ClampMin="0", ClampMax="1"))
	float HealthFraction = 0.2f;
	virtual bool CanApply(const ABrCharacter* Character, const FBrPickupPayload& Payload) const override;
	virtual bool Apply(ABrCharacter* Character, FBrPickupPayload& Payload) const override;
};

UCLASS(BlueprintType)
class BRGAME_API UBrGrenadePickupDefinition : public UBrPickupDefinition
{
	GENERATED_BODY()
public:
	virtual bool CanApply(const ABrCharacter* Character, const FBrPickupPayload& Payload) const override;
	virtual bool Apply(ABrCharacter* Character, FBrPickupPayload& Payload) const override;
};

UCLASS(BlueprintType)
class BRGAME_API UBrWeaponPickupDefinition : public UBrPickupDefinition
{
	GENERATED_BODY()
public:
	UBrWeaponPickupDefinition();
	UPROPERTY(EditAnywhere, Category="Pickup")
	TObjectPtr<UBrWeaponDefinition> Weapon;
	UBrWeaponDefinition* GetWeapon(const FBrPickupPayload& Payload) const;
	virtual FText GetLabel(const FBrPickupPayload& Payload) const override;
	virtual bool CanApply(const ABrCharacter* Character, const FBrPickupPayload& Payload) const override;
	virtual bool Apply(ABrCharacter* Character, FBrPickupPayload& Payload) const override;
};

USTRUCT(BlueprintType)
struct FBrPickupTableEntry
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Pickup")
	TObjectPtr<UBrPickupDefinition> Definition;
	UPROPERTY(EditAnywhere, Category="Pickup", meta=(ClampMin="0"))
	float Weight = 1;
};

/** Legacy asset schema retained so DA_BrPickupTable still loads; crates do not use this table. */
UCLASS(BlueprintType)
class BRGAME_API UBrPickupTable : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="Pickup")
	TArray<FBrPickupTableEntry> Entries;
	UBrPickupDefinition* Roll(FRandomStream& Stream) const;
};
