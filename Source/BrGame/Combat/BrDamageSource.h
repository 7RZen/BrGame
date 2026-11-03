#pragma once
#include "CoreMinimal.h"
#include "Engine/DamageEvents.h"
#include "BrDamageSource.generated.h"

class ABrPlayerState;

/** Captured by the authority at launch, never accepted from a client RPC. No pawn ownership. */
USTRUCT()
struct FBrDamageSource
{
	GENERATED_BODY()
	UPROPERTY()
	TWeakObjectPtr<ABrPlayerState> PlayerState;
	int32 PlayerId = INDEX_NONE;
	int32 Team = INDEX_NONE;
	FString RoundKey;
	bool IsValid() const { return PlayerId != INDEX_NONE && !RoundKey.IsEmpty(); }
	bool IsSelf(int32 VictimId) const { return IsValid() && PlayerId == VictimId; }
	bool IsFriendly(int32 VictimTeam) const { return IsValid() && Team >= 0 && Team == VictimTeam; }
};

/** Native server-only damage event; the base type still broadcasts normal health damage delegates. */
struct FBrDamageEvent : public FDamageEvent
{
	static constexpr int32 ClassID = 1001;
	FBrDamageSource Source;
	FVector Origin = FVector::ZeroVector;
	FBrDamageEvent(const FBrDamageSource& InSource, TSubclassOf<UDamageType> Type, FVector InOrigin = FVector::ZeroVector)
		: Source(InSource), Origin(InOrigin) { DamageTypeClass = Type; }
	virtual int32 GetTypeID() const override { return ClassID; }
	virtual bool IsOfType(int32 InID) const override { return InID == ClassID || FDamageEvent::IsOfType(InID); }
};
