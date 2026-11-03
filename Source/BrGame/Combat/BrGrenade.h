#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BrDamageSource.h"
#include "BrGrenade.generated.h"

class UBrThrowableDefinition;
class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

UCLASS()
class BRGAME_API ABrGrenade : public AActor
{
	GENERATED_BODY()
public:
	ABrGrenade();
	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;
	bool Launch(UBrThrowableDefinition* InDefinition, const FVector& Velocity);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UPROPERTY()
	FBrDamageSource DamageSource;
	UPROPERTY()
	TObjectPtr<USphereComponent> Collision;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY()
	TObjectPtr<UProjectileMovementComponent> Movement;
	UPROPERTY(ReplicatedUsing=OnRep_Definition)
	TObjectPtr<UBrThrowableDefinition> Definition;
	UPROPERTY(ReplicatedUsing=OnRep_Exploded)
	bool bExploded = false;
	UPROPERTY(Replicated)
	FVector ExplosionCenter = FVector::ZeroVector;
	UFUNCTION()
	void Impact(const FHitResult& Hit);
	UFUNCTION()
	void OnRep_Definition();
	UFUNCTION()
	void OnRep_Exploded();
};
