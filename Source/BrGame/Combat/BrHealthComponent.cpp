#include "BrHealthComponent.h"
#include "BrLog.h"
#include "BrDeveloperSettings.h"
#include "BrCharacter.h"
#include "BrLifeComponent.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UBrHealthComponent::UBrHealthComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

void UBrHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner()->HasAuthority())
	{
		if (!bMaxHealthInitialized)
		{
			SetMaxHealth(UBrDeveloperSettings::Get()->BaseMaxHealth);
			SetHealth(MaxHealth);
		}
		GetOwner()->OnTakeAnyDamage.AddDynamic(this, &UBrHealthComponent::ReceiveDamage);
	}
}

void UBrHealthComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	GetOwner()->OnTakeAnyDamage.RemoveDynamic(this, &UBrHealthComponent::ReceiveDamage);
	Super::EndPlay(Reason);
}

void UBrHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBrHealthComponent, Health);
	DOREPLIFETIME(UBrHealthComponent, MaxHealth);
}

void UBrHealthComponent::ReceiveDamage(AActor* Actor, float Damage, const UDamageType* Type, AController* Instigator, AActor* Causer)
{
	if (FMath::IsFinite(Damage) && Damage > 0)
	{
		SetHealth(Health - Damage);
	}
}

void UBrHealthComponent::OnRep_Health()
{
	if (Health <= 0 && !bDepletedNotified)
	{
		bDepletedNotified = true;
		UE_LOG(LogBr, Display, TEXT("Health depleted: %s"), *GetNameSafe(GetOwner()));
		OnHealthDepleted.Broadcast();
	}
	else if (Health > 0)
	{
		bDepletedNotified = false;
	}
}

void UBrHealthComponent::SetHealth(float Amount)
{
	if (GetOwner()->HasAuthority() && FMath::IsFinite(Amount))
	{
		const float PreviousHealth = Health;
		Health = FMath::Clamp(Amount, 0.0f, MaxHealth);
		OnRep_Health();
		if (Health < PreviousHealth)
		{
			if (ABrCharacter* BrCharacter = Cast<ABrCharacter>(GetOwner())) BrCharacter->RequestAutoPickupRetry();
		}
	}
}

void UBrHealthComponent::Heal(float Amount)
{
	if (const ABrCharacter* BrCharacter = Cast<ABrCharacter>(GetOwner()))
	{
		if (!BrCharacter->GetLife()->IsAlive()) return; // Ordinary pickups are not revives.
	}
	if (FMath::IsFinite(Amount) && Amount > 0) SetHealth(Health + Amount);
}

void UBrHealthComponent::SetMaxHealth(float Amount)
{
	if (GetOwner()->HasAuthority() && FMath::IsFinite(Amount))
	{
		bMaxHealthInitialized = true;
		MaxHealth = FMath::Clamp(Amount, 0.0f, 100.0f);
		SetHealth(Health);
	}
}
