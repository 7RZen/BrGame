#pragma once
#include "CoreMinimal.h"
#include "BrGameMode.h"
#include "GameFramework/GameModeBase.h"
#include "BrFlowGameModes.generated.h"

UCLASS()
class BRGAME_API ABrLobbyGameMode : public ABrGameMode
{
	GENERATED_BODY()

public:
	ABrLobbyGameMode();
	virtual bool IsLobbyMode() const override { return true; }
};

UCLASS()
class BRGAME_API ABrMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ABrMenuGameMode();
	virtual void ChangeName(AController* Controller, const FString& NewName, bool bNameChange) override;

	virtual void HandleStartingNewPlayer_Implementation(APlayerController* Player) override
	{
	}
};
