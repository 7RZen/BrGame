#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "BrRoundRecord.h"
#include "BrPlayerState.generated.h"

UCLASS()
class BRGAME_API ABrPlayerState : public APlayerState
{
	GENERATED_BODY()
public:
	UPROPERTY(ReplicatedUsing=OnRep_Team, BlueprintReadOnly, Category="Team")
	int32 Team = INDEX_NONE;
	UPROPERTY(ReplicatedUsing=OnRep_Team, BlueprintReadOnly, Category="Team")
	FLinearColor TeamColor = FLinearColor::White;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Team")
	bool bHost = false;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Team")
	bool bCanChooseColor = false;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Match")
	int32 DownsInflicted = 0;
	void SetTeam(int32 NewTeam, FLinearColor Color);
	void ResetRound();
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Life")
	int32 DeathCount = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Life")
	int32 VoluntaryRespawnCount = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Life")
	bool bEliminated = false;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void CopyProperties(APlayerState* PlayerState) override;
	virtual void OverrideWith(APlayerState* PlayerState) override;
	FBrLifeBookkeeping GetLifeBookkeeping() const;
	void RestoreLifeBookkeeping(const FBrLifeBookkeeping& Life);
private:
	UFUNCTION()
	void OnRep_Team();
};
