#include "Misc/AutomationTest.h"
#include "BrSoundEventRules.h"
#include "BrAudioRules.h"
#include "BrGameInstance.h"
#include "BrSessionRules.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/NetSerialization.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrPainSoundTest, "BrGame.Sounds.PainPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrPainSoundTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Zone silent"), FBrSoundEventRules::Pain(true, false, 20, 20), EBrPainSound::None);
	TestEqual(TEXT("Down uses existing scream"), FBrSoundEventRules::Pain(false, true, 20, 20), EBrPainSound::None);
	TestEqual(TEXT("Rejected damage silent"), FBrSoundEventRules::Pain(false, false, 0, 0), EBrPainSound::None);
	TestEqual(TEXT("Full absorption muted"), FBrSoundEventRules::Pain(false, false, 20, 0), EBrPainSound::Armor);
	TestEqual(TEXT("Partial absorption pain"), FBrSoundEventRules::Pain(false, false, 20, 5), EBrPainSound::Pain);
	TestEqual(TEXT("Unarmored pain"), FBrSoundEventRules::Pain(false, false, 20, 20), EBrPainSound::Pain);
	TestTrue(TEXT("First hit ready"), FBrSoundEventRules::PainReady(0, -DBL_MAX, 0.5f));
	TestFalse(TEXT("Shared pain/armor throttle"), FBrSoundEventRules::PainReady(1.49, 1, 0.5f));
	TestTrue(TEXT("Boundary ready"), FBrSoundEventRules::PainReady(1.5, 1, 0.5f));
	TestFalse(TEXT("Time reset cannot spam"), FBrSoundEventRules::PainReady(0, 1, 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrDryFireSoundTest, "BrGame.Sounds.DryFirePolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrDryFireSoundTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Empty and no reserve clicks"), FBrSoundEventRules::DryFire(0, 0));
	TestFalse(TEXT("Empty with reserve auto reloads"), FBrSoundEventRules::DryFire(0, 1));
	TestFalse(TEXT("Loaded shoots"), FBrSoundEventRules::DryFire(1, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrDownLineSoundTest, "BrGame.Sounds.VoicePolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrDownLineSoundTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("No lines"), FBrSoundEventRules::Line(0, 0, 0), INDEX_NONE);
	TestEqual(TEXT("Single line may repeat"), FBrSoundEventRules::Line(1, 0, 0), 0);
	for (int32 Count = 2; Count <= 8; ++Count)
		for (int32 Last = 0; Last < Count; ++Last)
		{
			TSet<int32> Chosen;
			for (int32 Slot = 0; Slot < FBrSoundEventRules::ChoiceCount(Count, Last); ++Slot)
			{
				const int32 Line = FBrSoundEventRules::Line(Count, Last, Slot);
				TestTrue(TEXT("Valid nonrepeat"), Line >= 0 && Line < Count && Line != Last);
				Chosen.Add(Line);
			}
			TestEqual(TEXT("All other lines reachable"), Chosen.Num(), Count - 1);
		}
	TestEqual(TEXT("Unknown last keeps all choices"), FBrSoundEventRules::ChoiceCount(3, 9), 3);
	TestEqual(TEXT("Negative voice falls back"), FBrSoundEventRules::VoiceIndex(-1, 3), 0);
	TestEqual(TEXT("Large voice falls back"), FBrSoundEventRules::VoiceIndex(MAX_int32, 3), 0);
	TestEqual(TEXT("Empty library fallback"), FBrSoundEventRules::VoiceIndex(1, 0), 0);
	TestEqual(TEXT("Valid voice retained"), FBrSoundEventRules::VoiceIndex(2, 3), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrSoundWireTest, "BrGame.Sounds.WireAndProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBrSoundWireTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Protocol"), FBrSessionRules::Protocol, 16);
	for (uint8 Kind = 0; Kind <= 14; ++Kind)
	{
		FBrSoundType Source;
		Source.Kind = static_cast<EBrSoundKind>(Kind);
		bool bSuccess = false;
		FNetBitWriter Writer(nullptr, 4096);
		Source.NetSerialize(Writer, nullptr, bSuccess);
		TestEqual(TEXT("Wire bound on write"), bSuccess, Kind <= static_cast<uint8>(EBrSoundKind::VoiceLine));
		FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
		FBrSoundType Dest;
		Dest.NetSerialize(Reader, nullptr, bSuccess);
		TestEqual(TEXT("Wire bound on read"), bSuccess, Kind <= static_cast<uint8>(EBrSoundKind::VoiceLine));
		TestEqual(TEXT("Kind round trip"), Dest.Kind, Source.Kind);
	}
	UBrAudioProfile* Profile = NewObject<UBrAudioProfile>();
	Profile->Voice = 0.42f;
	Profile->VoiceIndex = 2;
	TArray<uint8> Bytes;
	TestTrue(TEXT("Save to memory, not local user's slot"), UGameplayStatics::SaveGameToMemory(Profile, Bytes));
	const UBrAudioProfile* Loaded = Cast<UBrAudioProfile>(UGameplayStatics::LoadGameFromMemory(Bytes));
	TestNotNull(TEXT("Load profile"), Loaded);
	if (Loaded)
	{
		TestEqual(TEXT("Version 3"), Loaded->Version, 3);
		TestEqual(TEXT("Voice volume persists"), Loaded->Voice, 0.42f);
		TestEqual(TEXT("Voice choice persists"), Loaded->VoiceIndex, 2);
	}
	return true;
}
#endif
