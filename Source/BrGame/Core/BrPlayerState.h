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
	int32 GetVoiceIndex() const { return VoiceIndex; }
	void SetVoiceIndex(int32 Index);
	/** Authority-only history follows the player, never a pawn or a public replication stream. */
	FSoftObjectPath LastDownLine;
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
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Match")
	int32 RoundScore = 0;
	UPROPERTY(Replicated)
	TArray<FBrQuestProgress> Quests;
	void RestoreQuests(const FBrQuestJournal& Journal);
	void SetTeam(int32 NewTeam, FLinearColor Color);
	void ResetRound();
	/** Server-only persistent pawn-independent armor, never replicated on this public actor. */
	const TArray<float>& GetArmorPlates() const { return ArmorPlates; }
	void StoreArmorPlates(const TArray<float>& Values);
	void ResetArmor();
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
	int32 VoiceIndex = 0;
	UPROPERTY(Transient)
	TArray<float> ArmorPlates;
	UFUNCTION()
	void OnRep_Team();
};
