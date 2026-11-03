#include "BrPickupDefinition.h"
#include "BrPickup.h"
#include "BrPickupRules.h"
#include "BrCharacter.h"
#include "BrDeveloperSettings.h"
#include "BrHealthComponent.h"
#include "BrInventoryComponent.h"
#include "BrLifeComponent.h"
#include "BrLog.h"
#include "Components/CapsuleComponent.h"

bool UBrPickupDefinition::CanApply(const ABrCharacter* Character, const FBrPickupPayload& Payload) const
{
	return IsValid(Character) && Character->GetController() && Character->GetLife()->IsAlive() &&
		Character->GetHealth()->Health > 0;
}

FText UBrPickupDefinition::GetLabel(const FBrPickupPayload& Payload) const { return Label.IsEmpty() ? DisplayName : Label; }

bool UBrAmmoPickupDefinition::CanApply(const ABrCharacter* Character, const FBrPickupPayload& Payload) const
{
	if (!Super::CanApply(Character, Payload)) return false;
	const UBrInventoryComponent* Inventory = Character->GetInventory();
	return Inventory->Weapon && Inventory->Weapon->AmmoType && FBrPickupRules::AmmoGrant(
		Inventory->Weapon->MagazineSize, Inventory->GetReserve(), Inventory->Weapon->AmmoType->ReserveCap) > 0;
}

bool UBrAmmoPickupDefinition::Apply(ABrCharacter* Character, FBrPickupPayload& Payload) const
{
	if (!IsValid(Character) || !Character->HasAuthority() || !CanApply(Character, Payload)) return false;
	UBrInventoryComponent* Inventory = Character->GetInventory();
	const int32 Before = Inventory->GetReserve();
	Inventory->GiveAmmo(Inventory->Weapon->MagazineSize);
	Payload.Feedback = FString::Printf(TEXT("+%d ammo"), Inventory->GetReserve() - Before);
	return Inventory->GetReserve() > Before;
}

bool UBrHealthPickupDefinition::CanApply(const ABrCharacter* Character, const FBrPickupPayload& Payload) const
{
	return Super::CanApply(Character, Payload) && FBrPickupRules::HealthGrant(
		Character->GetHealth()->Health, Character->GetHealth()->MaxHealth, HealthFraction) > UE_KINDA_SMALL_NUMBER;
}

bool UBrHealthPickupDefinition::Apply(ABrCharacter* Character, FBrPickupPayload& Payload) const
{
	if (!IsValid(Character) || !Character->HasAuthority() || !CanApply(Character, Payload)) return false;
	UBrHealthComponent* Health = Character->GetHealth();
	const float Before = Health->Health;
	Health->Heal(FBrPickupRules::HealthGrant(Before, Health->MaxHealth, HealthFraction));
	Payload.Feedback = FString::Printf(TEXT("+%.0f HP"), Health->Health - Before);
	return Health->Health > Before;
}

bool UBrGrenadePickupDefinition::CanApply(const ABrCharacter* Character, const FBrPickupPayload& Payload) const
{
	if (!Super::CanApply(Character, Payload)) return false;
	const UBrInventoryComponent* Inventory = Character->GetInventory();
	return Inventory->Throwable && FBrPickupRules::GrenadeGrant(Inventory->Grenades, Inventory->Throwable->MaxCarried) > 0;
}

bool UBrGrenadePickupDefinition::Apply(ABrCharacter* Character, FBrPickupPayload& Payload) const
{
	if (!IsValid(Character) || !Character->HasAuthority() || !CanApply(Character, Payload)) return false;
	Character->GetInventory()->GiveGrenades(1);
	Payload.Feedback = TEXT("+1 grenade");
	return true;
}

UBrWeaponPickupDefinition::UBrWeaponPickupDefinition() { bRequiresInteract = true; }

UBrWeaponDefinition* UBrWeaponPickupDefinition::GetWeapon(const FBrPickupPayload& Payload) const
{
	return Payload.bDropped ? Payload.DroppedWeapon.Get() : Weapon.Get();
}

FText UBrWeaponPickupDefinition::GetLabel(const FBrPickupPayload& Payload) const
{
	const UBrWeaponDefinition* Definition = GetWeapon(Payload);
	if (!Definition) return Super::GetLabel(Payload);
	return Payload.bDropped ? FText::FromString(FString::Printf(TEXT("%s (%d loaded)"),
		*Definition->DisplayName.ToString(), Payload.LoadedRounds)) : Definition->DisplayName;
}

bool UBrWeaponPickupDefinition::CanApply(const ABrCharacter* Character, const FBrPickupPayload& Payload) const
{
	if (!Super::CanApply(Character, Payload)) return false;
	UBrWeaponDefinition* Definition = GetWeapon(Payload);
	if (!UBrInventoryComponent::IsValidWeaponDefinition(Definition)) return false;
	const UBrInventoryComponent* Inventory = Character->GetInventory();
	const FBrWeaponPickupPlan Plan = FBrPickupRules::WeaponPlan(Inventory->Weapon != nullptr, Inventory->Weapon == Definition,
		Inventory->Loaded, Definition->MagazineSize, Definition->StartingMagazines, Payload.bDropped, Payload.LoadedRounds);
	return !Plan.bSameWeapon || FBrPickupRules::AmmoGrant(Plan.ReserveGrant, Inventory->GetReserve(), Definition->AmmoType->ReserveCap) > 0;
}

bool UBrWeaponPickupDefinition::Apply(ABrCharacter* Character, FBrPickupPayload& Payload) const
{
	if (!IsValid(Character) || !Character->HasAuthority() || !CanApply(Character, Payload)) return false;
	UBrInventoryComponent* Inventory = Character->GetInventory();
	UBrWeaponDefinition* Definition = GetWeapon(Payload);
	const FBrWeaponPickupPlan Plan = FBrPickupRules::WeaponPlan(Inventory->Weapon != nullptr, Inventory->Weapon == Definition,
		Inventory->Loaded, Definition->MagazineSize, Definition->StartingMagazines, Payload.bDropped, Payload.LoadedRounds);
	if (Plan.bSameWeapon)
	{
		const int32 Before = Inventory->GetReserve();
		Inventory->GiveAmmo(Plan.ReserveGrant); // Consume the pickup once, don't replace or refill the carried magazine.
		Payload.Feedback = FString::Printf(TEXT("+%d ammo"), Inventory->GetReserve() - Before);
		return Inventory->GetReserve() > Before;
	}
	ABrPickup* Drop = nullptr;
	if (Plan.bDropOldWeapon)
	{
		UBrPickupDefinition* Template = UBrDeveloperSettings::Get()->DroppedWeaponPickup.LoadSynchronous();
		if (!Cast<UBrWeaponPickupDefinition>(Template))
		{
			UE_LOG(LogBr, Error, TEXT("Weapon swap refused: missing/invalid dropped weapon pickup template."));
			return false;
		}
		FBrPickupPayload Dropped;
		Dropped.bDropped = true;
		Dropped.DroppedWeapon = Inventory->Weapon;
		Dropped.LoadedRounds = Plan.DropLoaded;
		const FVector Feet = Character->GetActorLocation() -
			FVector(0, 0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 35);
		Drop = ABrPickup::Spawn(Character->GetWorld(), FTransform(Feet), Template, Dropped);
		if (!Drop) return false; // No gear mutation until the old magazine has somewhere to go.
	}
	if (!Inventory->EquipWeapon(Definition, Plan.LoadedAfter, Plan.ReserveGrant))
	{
		if (Drop) Drop->Destroy();
		return false;
	}
	Payload.Feedback = FString::Printf(TEXT("Equipped %s"), *Definition->DisplayName.ToString());
	return true;
}

UBrPickupDefinition* UBrPickupTable::Roll(FRandomStream& Stream) const
{
	TArray<float> Weights;
	Weights.Reserve(Entries.Num());
	for (const FBrPickupTableEntry& Entry : Entries) Weights.Add(Entry.Definition ? Entry.Weight : 0);
	const int32 Index = FBrPickupRules::WeightedIndex(Weights, Stream);
	return Entries.IsValidIndex(Index) ? Entries[Index].Definition.Get() : nullptr;
}
