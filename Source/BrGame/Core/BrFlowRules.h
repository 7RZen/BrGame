#pragma once
#include "CoreMinimal.h"
#include "BrMatchSettings.h"

/** Pure policies shared by authority, lobby preview and automation. */
struct FBrFlowRules
{
	static bool Admit(int32 Count, int32 Maximum) { return Maximum <= 0 || Count < Maximum; }
	static bool HasRoom(int32 Count, int32 Maximum, int32 Groups)
	{
		return Maximum <= 0 || Count < (Maximum / FMath::Max(2, Groups) + (Maximum % FMath::Max(2, Groups) != 0));
	}
	static int32 Smallest(const TArray<int32>& Sizes)
	{
		int32 Best = INDEX_NONE;
		for (int32 I = 0; I < Sizes.Num(); ++I)
			if (Best == INDEX_NONE || Sizes[I] < Sizes[Best]) Best = I;
		return Best;
	}
	static TArray<int32> Rebalance(TArray<int32> Teams, int32 Groups)
	{
		TArray<int32> Sizes;
		Sizes.Init(0, FMath::Max(2, Groups));
		for (int32 Team : Teams) if (Sizes.IsValidIndex(Team)) ++Sizes[Team];
		for (int32& Team : Teams) if (!Sizes.IsValidIndex(Team)) { Team = Smallest(Sizes); ++Sizes[Team]; }
		while (true)
		{
			const int32 Min = Smallest(Sizes);
			int32 Max = 0;
			for (int32 I = 1; I < Sizes.Num(); ++I) if (Sizes[I] > Sizes[Max]) Max = I;
			if (Sizes[Max] - Sizes[Min] <= 1) break;
			for (int32 I = Teams.Num() - 1; I >= 0; --I)
				if (Teams[I] == Max) { Teams[I] = Min; --Sizes[Max]; ++Sizes[Min]; break; }
		}
		return Teams;
	}
	static TArray<int32> AssignEven(int32 Players, int32 Groups)
	{
		TArray<int32> Teams;
		Teams.Init(INDEX_NONE, FMath::Max(0, Players));
		return Rebalance(MoveTemp(Teams), Groups);
	}
	static FLinearColor MilitaryGreen() { return FLinearColor(FColor::FromHex(TEXT("4B5320"))); }
	static FLinearColor Palette(int32 Index)
	{
		static const TCHAR* Colors[] = { TEXT("00BFFF"), TEXT("FF6030"), TEXT("FFD700"), TEXT("BA55D3"),
			TEXT("00E5CC"), TEXT("FF69B4"), TEXT("FFFFFF"), TEXT("5080FF"), TEXT("FFB070"), TEXT("A8E630") };
		if (Index < UE_ARRAY_COUNT(Colors)) return FLinearColor(FColor::FromHex(Colors[FMath::Max(0, Index)]));
		// Additional groups receive unique readable shades; the ten main swatches stay the chooser UI.
		return FLinearColor(FColor(static_cast<uint8>(64 + Index % 192),
			static_cast<uint8>(128 + (Index / 192) % 128), 224));
	}
	static bool CanChooseColor(bool bSameAdmittedController, bool bInGroup)
	{
		return bInGroup && bSameAdmittedController;
	}
	static FString SanitizeName(const FString& Input)
	{
		FString Name = Input.TrimStartAndEnd().Left(24);
		for (TCHAR& Ch : Name)
			if (!FChar::IsAlnum(Ch) && Ch != TEXT(' ') && Ch != TEXT('_') && Ch != TEXT('-')) Ch = TEXT('_');
		return Name.IsEmpty() ? TEXT("Player") : Name;
	}
	static bool CanRetrySpawn(bool bAdmittedInDestination, bool bLoadedDestination)
	{
		return bAdmittedInDestination && bLoadedDestination;
	}
	/** Empty groups use INDEX_NONE. Chooser is retained separately by the authority until empty. */
	static int32 AllocateColor(const TArray<int32>& Used, int32 Requested = INDEX_NONE)
	{
		if (Requested >= 0 && Requested < 10 && !Used.Contains(Requested)) return Requested;
		int32 Free = 0;
		while (Used.Contains(Free)) ++Free;
		return Free;
	}
	static void ReleaseEmpty(TArray<int32>& Colors, const TArray<int32>& Sizes)
	{
		for (int32 I = 0; I < Colors.Num(); ++I) if (!Sizes.IsValidIndex(I) || Sizes[I] == 0) Colors[I] = INDEX_NONE;
	}
	/** Entries are contestant IDs (player or group) of everyone not eliminated, including downed. */
	static bool Evaluate(const TArray<int32>& StillIn, int32 Participated, bool bAllowSolo, int32& Winner)
	{
		TSet<int32> Unique(StillIn);
		Winner = INDEX_NONE;
		if ((!bAllowSolo && Participated < 2) || Unique.Num() > 1) return false;
		for (int32 Id : Unique) Winner = Id;
		return true;
	}
};
