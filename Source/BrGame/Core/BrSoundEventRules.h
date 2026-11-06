#pragma once
#include "CoreMinimal.h"

enum class EBrPainSound : uint8 { None, Pain, Armor };

/** Authority policy without UObject or asset dependencies. */
struct FBrSoundEventRules
{
	static bool DryFire(int32 Loaded, int32 Reserve) { return Loaded <= 0 && Reserve <= 0; }
	static EBrPainSound Pain(bool bZone, bool bDowned, float Applied, float HealthLost)
	{
		return bZone || bDowned || Applied <= 0 ? EBrPainSound::None :
			HealthLost > 0 ? EBrPainSound::Pain : EBrPainSound::Armor;
	}
	static bool PainReady(double Now, double Last, float Interval)
	{
		return FMath::IsFinite(Now) && Now - Last >= FMath::Max(0.f, Interval);
	}
	static int32 VoiceIndex(int32 Index, int32 Count)
	{
		return Index >= 0 && Index < Count ? Index : 0;
	}
	/** RandomSlot is drawn uniformly from [0, ChoiceCount); skip the last line. */
	static int32 Line(int32 Count, int32 Last, int32 RandomSlot)
	{
		if (Count <= 0) return INDEX_NONE;
		const bool bSkip = Count > 1 && Last >= 0 && Last < Count;
		const int32 Slot = FMath::Clamp(RandomSlot, 0, Count - (bSkip ? 2 : 1));
		return bSkip && Slot >= Last ? Slot + 1 : Slot;
	}
	static int32 ChoiceCount(int32 Count, int32 Last)
	{
		return FMath::Max(0, Count - (Count > 1 && Last >= 0 && Last < Count ? 1 : 0));
	}
};
