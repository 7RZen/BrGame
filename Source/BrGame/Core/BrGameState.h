#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "BrMatchSettings.h"
#include "BrGameState.generated.h"

UCLASS()
class BRGAME_API ABrGameState : public AGameState
{
	GENERATED_BODY()
public:
	UPROPERTY(Replicated)
	bool bFogDisabled = false;
	UPROPERTY(Replicated)
	bool bLobby = false;
	UPROPERTY(Replicated)
	bool bResults = false;
	UPROPERTY(Replicated)
	FString WinnerText;
	UPROPERTY(Replicated)
	FLinearColor WinnerColor = FLinearColor::White;
	UPROPERTY(Replicated)
	FString ResultsTable;
	UPROPERTY(Replicated)
	float ResultsEndTime = 0;
	UPROPERTY(Replicated)
	TArray<int32> GroupColors;
	UPROPERTY(ReplicatedUsing=OnRep_MatchSettings, BlueprintReadOnly, Category="Match")
	FBrMatchSettings MatchSettings;
	void SetMatchSettings(const FBrMatchSettings& Settings);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
	UFUNCTION()
	void OnRep_MatchSettings();
};
