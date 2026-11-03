#include "BrAudioRules.h"
#include "BrDeveloperSettings.h"
#include "BrGameInstance.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/CoreNet.h"
#include "Sound/SoundBase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrAudioRangesTest, "BrGame.Audio.HearingRanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrAudioRangesTest::RunTest(const FString& Parameters)
{
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	TestEqual(TEXT("Run"), FBrAudioRules::StepRange(EBrPosture::Standing, false,
		S->RunStepHearingRange, S->WalkStepHearingRange, S->CrouchStepHearingRange), 1500.f);
	TestEqual(TEXT("Walk"), FBrAudioRules::StepRange(EBrPosture::Standing, true, 1500, 600, 300), 600.f);
	TestEqual(TEXT("Crouch"), FBrAudioRules::StepRange(EBrPosture::Crouching, true, 1500, 600, 300), 300.f);
	TestEqual(TEXT("Prone silent"), FBrAudioRules::StepRange(EBrPosture::Prone, false, 1500, 600, 300), 0.f);
	for (float Range : {S->GunshotHearingRange, S->ExplosionHearingRange, S->DownedHearingRange, 1500.f, 600.f, 300.f})
	{
		TestTrue(TEXT("Inside hearing"), FBrAudioRules::InRange(FVector::ZeroVector, FVector(Range - 1, 0, 80), Range));
		TestFalse(TEXT("At/beyond zero-volume boundary"), FBrAudioRules::InRange(FVector::ZeroVector, FVector(Range, 0, 0), Range));
	}
	TestFalse(TEXT("Disabled range"), FBrAudioRules::InRange(FVector::ZeroVector, FVector::ZeroVector, 0));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrAudioTargetTest, "BrGame.Audio.ChannelIndependentRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrAudioTargetTest::RunTest(const FString& Parameters)
{
	for (bool Ready : {false, true})
		for (bool Range : {false, true})
			for (bool Own : {false, true})
				for (const TCHAR* State : {TEXT("Withheld"), TEXT("Opening"), TEXT("Open"), TEXT("Closing")})
					TestEqual(FString::Printf(TEXT("%s: exactly one non-owner path, including host"), State),
						FBrAudioRules::SendOther(Ready, Range, Own), Ready && Range && !Own);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrAudioListenerTest, "BrGame.Audio.ListenerEligibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrAudioListenerTest::RunTest(const FString& Parameters)
{
	for (bool Ready : {false, true})
		for (bool Admitted : {false, true})
			for (bool Pawn : {false, true})
				for (bool Spectating : {false, true})
					for (bool Selected : {false, true})
						TestEqual(TEXT("No controller/stale view-target fallback"),
							FBrAudioRules::ListenerEligible(Ready, Admitted, Pawn, Spectating, Selected),
							Ready && Admitted && (Spectating ? Selected : Pawn));
	TestFalse(TEXT("Eliminated spectator without selection ignores stale pawn"),
		FBrAudioRules::ListenerEligible(true, true, true, true, false));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrAudioCadenceTest, "BrGame.Audio.FootstepCadence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrAudioCadenceTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Run 600cm/s"), FBrAudioRules::StepInterval(600, 180, 10), 0.3f);
	TestEqual(TEXT("Walk slower"), FBrAudioRules::StepInterval(300, 180, 10), 0.6f);
	TestEqual(TEXT("Crouch slower"), FBrAudioRules::StepInterval(120, 180, 10), 1.5f);
	TestEqual(TEXT("Stationary"), FBrAudioRules::StepInterval(0, 180, 10), 0.f);
	TestEqual(TEXT("Jitter"), FBrAudioRules::StepInterval(9, 180, 10), 0.f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrAudioVolumeTest, "BrGame.Audio.VolumePersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrAudioVolumeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Lower clamp"), FBrAudioRules::Volume(-1), 0.f);
	TestEqual(TEXT("Upper clamp"), FBrAudioRules::Volume(2), 1.f);
	UBrAudioProfile* Profile = NewObject<UBrAudioProfile>();
	Profile->Master = FBrAudioRules::Volume(0.37f);
	Profile->Effects = FBrAudioRules::Volume(0.62f);
	TArray<uint8> Bytes;
	TestTrue(TEXT("Serialize audio profile without touching user's save"), UGameplayStatics::SaveGameToMemory(Profile, Bytes));
	const UBrAudioProfile* Loaded = Cast<UBrAudioProfile>(UGameplayStatics::LoadGameFromMemory(Bytes));
	TestNotNull(TEXT("Profile round trip"), Loaded);
	if (Loaded)
	{
		TestEqual(TEXT("Version"), Loaded->Version, 2);
		TestEqual(TEXT("Master persisted"), Loaded->Master, 0.37f);
		TestEqual(TEXT("Effects persisted"), Loaded->Effects, 0.62f);
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrAudioVoiceTest, "BrGame.Audio.VoiceProtection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrAudioVoiceTest::RunTest(const FString& Parameters)
{
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	TestTrue(TEXT("Screams outrank sustained fire"), S->DownedVoice.Priority > S->FireVoice.Priority);
	TestTrue(TEXT("Footsteps outrank sustained fire"), S->FootstepVoice.Priority > S->FireVoice.Priority);
	for (EBrSoundKind Kind : {EBrSoundKind::Fire, EBrSoundKind::Explosion, EBrSoundKind::Downed, EBrSoundKind::RunStep, EBrSoundKind::Pickup})
	{
		const FBrAudioVoicePolicy& Policy = S->VoicePolicy(Kind);
		USoundConcurrency* Asset = Policy.Concurrency.LoadSynchronous();
		TestNotNull(TEXT("Generated category concurrency"), Asset);
		if (Asset)
		{
			TestEqual(TEXT("Configured voice cap"), Asset->Concurrency.MaxCount, Policy.MaxCount);
			TestEqual(TEXT("Configured resolution"), Asset->Concurrency.ResolutionRule, Policy.ResolutionRule);
			TestFalse(TEXT("Category budget shared across sources"), !!Asset->Concurrency.bLimitToOwner);
		}
	}
	for (const TSoftObjectPtr<USoundBase>& Sound : S->RunFootsteps)
	{
		USoundBase* Wave = Sound.LoadSynchronous();
		TestNotNull(TEXT("Footstep sound"), Wave);
		if (Wave)
		{
			TestEqual(TEXT("Footstep priority"), Wave->Priority, S->FootstepVoice.Priority);
			TestTrue(TEXT("Quiet footsteps retain priority"), !!Wave->bBypassVolumeScaleForPriority);
		}
	}
	if (USoundBase* Scream = S->DownedScream.LoadSynchronous())
	{
		TestEqual(TEXT("Scream priority"), Scream->Priority, S->DownedVoice.Priority);
		TestTrue(TEXT("Scream retains priority at range"), !!Scream->bBypassVolumeScaleForPriority);
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrAudioWireTest, "BrGame.Audio.BatchWireFormat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrAudioWireTest::RunTest(const FString& Parameters)
{
	FBrSoundType Type;
	Type.Sound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/BrGame/Audio/SW_Fire_MachineGun.SW_Fire_MachineGun")));
	FNetBitWriter TypeWriter(nullptr, 4096);
	bool bSuccess = false;
	Type.NetSerialize(TypeWriter, nullptr, bSuccess);
	TestTrue(TEXT("Type serialization"), bSuccess);
	TestTrue(TEXT("Current asset type dictionary entry <=100 bytes"), TypeWriter.GetNumBits() <= 800);
	FBrSoundEvent Event;
	Event.TypeIndex = 5;
	Event.Location = FVector(-10000, 10000, 98);
	FNetBitWriter Writer(nullptr, 4096);
	Event.NetSerialize(Writer, nullptr, bSuccess);
	TestTrue(TEXT("Event serialization"), bSuccess);
	TestTrue(TEXT("Town-position event <=10 bytes"), Writer.GetNumBits() <= 80);
	FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
	FBrSoundEvent Loaded;
	Loaded.NetSerialize(Reader, nullptr, bSuccess);
	TestTrue(TEXT("Event deserialization"), bSuccess);
	TestEqual(TEXT("Batch-local type index"), Loaded.TypeIndex, Event.TypeIndex);
	TestTrue(TEXT("Quantized location round trip"), FVector(Loaded.Location).Equals(Event.Location, 1));
	AddInfo(FString::Printf(TEXT("Audio wire payload: Event=%lld bits Type=%lld bits (excludes RPC/array/UDP headers)"),
		Writer.GetNumBits(), TypeWriter.GetNumBits()));
	return true;
}
#endif
