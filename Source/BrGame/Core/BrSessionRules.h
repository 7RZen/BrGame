#pragma once

#include "CoreMinimal.h"
#include "BrMatchSettings.h"
#include "OnlineSessionSettings.h"
#include "Misc/SecureHash.h"

// Increment Protocol whenever admission/gameplay wire compatibility changes.
struct FBrSessionRules
{
	static bool IdentityMatches(bool bSteam, bool bSteamClaim, uint64 Claimed, uint64 TransportPeer)
	{
		// Null IDs remain explicitly unauthenticated development identities.
		return !bSteam || (bSteamClaim && TransportPeer != 0 && Claimed == TransportPeer);
	}
	enum class EAdmission { Accept, Wait, Reject };
	static EAdmission LobbyAdmission(bool bSteam, EBrSessionVisibility Visibility, bool bVerified, bool bMember, double WaitSeconds)
	{
		if (!bSteam) return EAdmission::Accept;
		if (!bVerified) return EAdmission::Reject;
		if (Visibility == EBrSessionVisibility::Public || bMember) return EAdmission::Accept;
		return WaitSeconds < 5.0 ? EAdmission::Wait : EAdmission::Reject;
	}
	static constexpr int32 Protocol = 9;
	static constexpr const TCHAR* GameId = TEXT("BrGame");
	static bool Compatible(const FOnlineSessionSettings& Settings)
	{
		FString Id;
		int32 Version = 0;
		return Settings.Get(TEXT("BRGAME"), Id) && Id == GameId &&
			Settings.Get(TEXT("BRPROTO"), Version) && Version == Protocol;
	}
	static int32 Connections(int32 Maximum, bool bSteam)
	{
		// Steam lobbies have a backend limit of 250 members. This is not a gameplay cap.
		return Maximum > 0 ? Maximum : bSteam ? 250 : 1000000;
	}
	static void Visibility(FOnlineSessionSettings& Settings, EBrSessionVisibility Value)
	{
		Settings.bShouldAdvertise = true;
		Settings.bAllowInvites = true;
		Settings.bAllowJoinViaPresence = Value != EBrSessionVisibility::InviteOnly;
		Settings.bAllowJoinViaPresenceFriendsOnly = Value == EBrSessionVisibility::FriendsOnly;
	}
	static bool ValidPassword(const FString& Password) { return Password.Len() <= 64; }
	// Never put the literal password in travel URLs (engine travel logging includes URLs).
	// This bearer proof must still be treated as a credential in local diagnostic logs.
	static FString PasswordProof(const FString& Password)
	{
		if (Password.IsEmpty()) return FString();
		FTCHARToUTF8 Data(*(TEXT("BrGame/9/password:") + Password));
		return FSHA1::HashBuffer(Data.Get(), Data.Length()).ToString();
	}
	static bool ApprovePassword(const FString& Password, const FString& Proof)
	{
		if (Password.IsEmpty()) return true;
		const FString Expected = PasswordProof(Password);
		if (Expected.IsEmpty() || Proof.Len() != Expected.Len()) return false;
		uint32 Difference = 0;
		for (int32 I = 0; I < Expected.Len(); ++I) Difference |= Expected[I] ^ Proof[I];
		return Difference == 0;
	}
};

// State shared by runtime publication and deterministic failure/backoff tests.
struct FBrAdvertisementState
{
	FString Attempted, Published;
	int32 Failures = 0;
	double RetryAt = 0;
	// A failed backend update may be partial. Even a rollback to Published must
	// be reconciled, rather than assuming the remote service still has that state.
	bool CanAttempt(const FString& Desired, double Now) const { return (Desired != Published || Failures > 0) && Now >= RetryAt; }
	void Complete(bool bSuccess, double Now)
	{
		if (bSuccess) { Published = Attempted; Failures = 0; RetryAt = 0; }
		else
		{
			Failures = FMath::Min(Failures + 1, 6);
			RetryAt = Now + FMath::Min(30.0, static_cast<double>(1 << (Failures - 1)));
		}
	}
};
