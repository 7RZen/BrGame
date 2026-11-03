#include "BrSessionRules.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrSessionBindingTest, "BrGame.Session.IdentityBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrSessionBindingTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Own Steam ID bound to transport"), FBrSessionRules::IdentityMatches(true, true, 123, 123));
	TestFalse(TEXT("Authenticated attacker cannot claim victim ledger"), FBrSessionRules::IdentityMatches(true, true, 456, 123));
	TestFalse(TEXT("Cannot claim new ID to dodge penalties"), FBrSessionRules::IdentityMatches(true, true, 789, 123));
	TestFalse(TEXT("Missing transport identity fails closed"), FBrSessionRules::IdentityMatches(true, true, 123, 0));
	TestFalse(TEXT("Wrong ID provider fails closed"), FBrSessionRules::IdentityMatches(true, false, 123, 123));
	TestFalse(TEXT("Zero identities fail closed"), FBrSessionRules::IdentityMatches(true, true, 0, 0));
	TestTrue(TEXT("Null remains unauthenticated development policy"), FBrSessionRules::IdentityMatches(false, false, 456, 0));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrSessionAdmissionTest, "BrGame.Session.PrivateAdmission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrSessionAdmissionTest::RunTest(const FString& Parameters)
{
	using EAdmission = FBrSessionRules::EAdmission;
	TestTrue(TEXT("Public verified direct connection allowed"), FBrSessionRules::LobbyAdmission(true, EBrSessionVisibility::Public, true, false, 6) == EAdmission::Accept);
	TestTrue(TEXT("Public cannot bypass identity"), FBrSessionRules::LobbyAdmission(true, EBrSessionVisibility::Public, false, true, 0) == EAdmission::Reject);
	for (EBrSessionVisibility Visibility : { EBrSessionVisibility::FriendsOnly, EBrSessionVisibility::InviteOnly })
	{
		TestTrue(TEXT("Verified member admitted"), FBrSessionRules::LobbyAdmission(true, Visibility, true, true, 0) == EAdmission::Accept);
		TestTrue(TEXT("Unverified lobby member rejected"), FBrSessionRules::LobbyAdmission(true, Visibility, false, true, 0) == EAdmission::Reject);
		TestTrue(TEXT("Membership propagation allowed time"), FBrSessionRules::LobbyAdmission(true, Visibility, true, false, 4.9) == EAdmission::Wait);
		TestTrue(TEXT("Direct bypass rejected at bound"), FBrSessionRules::LobbyAdmission(true, Visibility, true, false, 5) == EAdmission::Reject);
		TestTrue(TEXT("Propagated membership admitted"), FBrSessionRules::LobbyAdmission(true, Visibility, true, true, 4.9) == EAdmission::Accept);
	}
	TestTrue(TEXT("Null unchanged"), FBrSessionRules::LobbyAdmission(false, EBrSessionVisibility::InviteOnly, false, false, 100) == EAdmission::Accept);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrSessionPublicationTest, "BrGame.Session.PublicationRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrSessionPublicationTest::RunTest(const FString& Parameters)
{
	FBrAdvertisementState State;
	State.Attempted = TEXT("public");
	State.Complete(true, 0);
	State.Attempted = TEXT("private");
	State.Complete(false, 10);
	TestEqual(TEXT("Failure cannot commit attempted settings"), State.Published, FString(TEXT("public")));
	TestFalse(TEXT("Backoff prevents immediate retry"), State.CanAttempt(TEXT("private"), 10.9));
	TestTrue(TEXT("Same failed settings retried"), State.CanAttempt(TEXT("private"), 11));
	TestTrue(TEXT("Rollback also repairs potentially partial failed update"), State.CanAttempt(TEXT("public"), 11));
	for (int32 I = 0; I < 20; ++I) State.Complete(false, 20);
	TestEqual(TEXT("Backoff capped at thirty seconds"), State.RetryAt, 50.0);
	State.Complete(true, 51);
	TestEqual(TEXT("Success commits actual attempted snapshot"), State.Published, FString(TEXT("private")));
	TestFalse(TEXT("Published settings not republished"), State.CanAttempt(TEXT("private"), 100));
	TestTrue(TEXT("Change during flight remains dirty"), State.CanAttempt(TEXT("new desired"), 51));
	TestEqual(TEXT("Success resets failures"), State.Failures, 0);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrSessionPasswordTest, "BrGame.Session.Password",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrSessionPasswordTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Unprotected IP login"), FBrSessionRules::ApprovePassword(TEXT(""), TEXT("")));
	TestFalse(TEXT("Missing password denied"), FBrSessionRules::ApprovePassword(TEXT("secret"), TEXT("")));
	TestFalse(TEXT("Wrong password denied"), FBrSessionRules::ApprovePassword(TEXT("secret"), FBrSessionRules::PasswordProof(TEXT("wrong"))));
	TestTrue(TEXT("Correct password admitted"), FBrSessionRules::ApprovePassword(TEXT("secret"), FBrSessionRules::PasswordProof(TEXT("secret"))));
	TestFalse(TEXT("Plaintext is not accepted"), FBrSessionRules::ApprovePassword(TEXT("secret"), TEXT("secret")));
	TestFalse(TEXT("Host password length bounded"), FBrSessionRules::ValidPassword(FString::ChrN(65, 'x')));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrSessionFilterTest, "BrGame.Session.Compatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrSessionFilterTest::RunTest(const FString& Parameters)
{
	FOnlineSessionSettings Value;
	TestFalse(TEXT("Other Spacewar games hidden"), FBrSessionRules::Compatible(Value));
	Value.Set(TEXT("BRGAME"), FString(FBrSessionRules::GameId));
	Value.Set(TEXT("BRPROTO"), FBrSessionRules::Protocol - 1);
	TestFalse(TEXT("Old protocol hidden, including invites"), FBrSessionRules::Compatible(Value));
	Value.Set(TEXT("BRPROTO"), FBrSessionRules::Protocol);
	TestTrue(TEXT("Matching protocol accepted"), FBrSessionRules::Compatible(Value));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrSessionMappingTest, "BrGame.Session.SettingsMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrSessionMappingTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Finite cap preserved"), FBrSessionRules::Connections(42, true), 42);
	TestEqual(TEXT("Null unlimited advertisement"), FBrSessionRules::Connections(0, false), 1000000);
	TestEqual(TEXT("Steam unlimited uses backend member limit"), FBrSessionRules::Connections(0, true), 250);
	FOnlineSessionSettings Value;
	FBrSessionRules::Visibility(Value, EBrSessionVisibility::Public);
	TestTrue(TEXT("Public searchable/joinable"), Value.bShouldAdvertise && Value.bAllowJoinViaPresence && !Value.bAllowJoinViaPresenceFriendsOnly);
	FBrSessionRules::Visibility(Value, EBrSessionVisibility::FriendsOnly);
	TestTrue(TEXT("Friends only maps to Steam FriendsOnly lobby"), Value.bAllowJoinViaPresenceFriendsOnly);
	FBrSessionRules::Visibility(Value, EBrSessionVisibility::InviteOnly);
	TestTrue(TEXT("Private lobby invites without presence join"), Value.bAllowInvites && !Value.bAllowJoinViaPresence && !Value.bAllowJoinViaPresenceFriendsOnly);
	return true;
}
#endif
