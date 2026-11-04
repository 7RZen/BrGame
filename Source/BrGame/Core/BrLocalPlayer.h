#pragma once
#include "CoreMinimal.h"
#include "Engine/LocalPlayer.h"
#include "BrLocalPlayer.generated.h"

/** Engine's OSS nickname normally overwrites URL ?Name=. Keep the explicit local profile name. */
UCLASS()
class BRGAME_API UBrLocalPlayer : public ULocalPlayer
{
	GENERATED_BODY()

public:
	virtual FString GetNickname() const override { return FString(); }
};
