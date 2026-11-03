#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BrHealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBrHealthDepleted);

UCLASS(ClassGroup=(BrGame), meta=(BlueprintSpawnableComponent))
class BRGAME_API UBrHealthComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UBrHealthComponent();
	UPROPERTY(ReplicatedUsing=OnRep_Health, BlueprintReadOnly, Category="Health")
	float Health = 100;
	UPROPERTY(ReplicatedUsing=OnRep_Health, BlueprintReadOnly, Category="Health")
	float MaxHealth = 100;
	UPROPERTY(BlueprintAssignable, Category="Health")
	FBrHealthDepleted OnHealthDepleted;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Health")
	void Heal(float Amount);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Health")
	void SetMaxHealth(float Amount);
	void SetHealth(float Amount);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UFUNCTION()
	void OnRep_Health();
	UFUNCTION()
	void ReceiveDamage(AActor* Actor, float Damage, const UDamageType* Type, AController* Instigator, AActor* Causer);
	bool bDepletedNotified = false;
	bool bMaxHealthInitialized = false;
};
