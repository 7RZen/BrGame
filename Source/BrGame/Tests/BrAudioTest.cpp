#include "BrAudioRules.h"
#include "BrDeveloperSettings.h"
#include "BrGameInstance.h"
#include "BrAudioSubsystem.h"
#include "BrCombatDefinitions.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include <limits>
#include "Kismet/GameplayStatics.h"
#include "UObject/CoreNet.h"
#include "Sound/SoundBase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrAudioRangesTest, "BrGame.Audio.HearingRanges",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrAudioRangesTest::RunTest(const FString& Parameters)
{
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	TestEqual(TEXT("Run"), FBrAudioRules::StepRange(EBrPosture::Standing, false,
	                                                S->RunStepHearingRange, S->SprintStepHearingRange,
	                                                S->CrouchStepHearingRange), 1500.f);
	TestEqual(TEXT("Sprint"), FBrAudioRules::StepRange(EBrPosture::Standing, true, 1500, 2000, 300), 2000.f);
	TestEqual(TEXT("Crouch"), FBrAudioRules::StepRange(EBrPosture::Crouching, true, 1500, 600, 300), 300.f);
	TestEqual(TEXT("Prone silent"), FBrAudioRules::StepRange(EBrPosture::Prone, false, 1500, 600, 300), 0.f);
	for (float Range : {S->GunshotHearingRange, S->ExplosionHearingRange, S->DownedHearingRange, 1500.f, 600.f, 300.f})
	{
		TestTrue(TEXT("Inside hearing"), FBrAudioRules::InRange(FVector::ZeroVector, FVector(Range - 1, 0, 80), Range));
		TestFalse(TEXT("At/beyond zero-volume boundary"), FBrAudioRules::InRange(
			          FVector::ZeroVector, FVector(Range, 0, 0), Range));
	}
	TestFalse(TEXT("Disabled range"), FBrAudioRules::InRange(FVector::ZeroVector, FVector::ZeroVector, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrAudioTargetTest, "BrGame.Audio.ChannelIndependentRouting",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrAudioTargetTest::RunTest(const FString& Parameters)
{
	for (bool Ready : {false, true})
	{
		for (bool Range : {false, true})
		{
			for (bool Own : {false, true})
			{
				for (const TCHAR* State : {TEXT("Withheld"), TEXT("Opening"), TEXT("Open"), TEXT("Closing")})
				{
					TestEqual(FString::Printf(TEXT("%s: exactly one non-owner path, including host"), State),
					          FBrAudioRules::SendOther(Ready, Range, Own), Ready && Range && !Own);
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrAudioListenerTest, "BrGame.Audio.ListenerEligibility",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrAudioListenerTest::RunTest(const FString& Parameters)
{
	for (bool Ready : {false, true})
	{
		for (bool Admitted : {false, true})
		{
			for (bool Pawn : {false, true})
			{
				for (bool Spectating : {false, true})
				{
					for (bool Selected : {false, true})
					{
						TestEqual(TEXT("No controller/stale view-target fallback"),
						          FBrAudioRules::ListenerEligible(Ready, Admitted, Pawn, Spectating, Selected),
						          Ready && Admitted && (Spectating ? Selected : Pawn));
					}
				}
			}
		}
	}
	TestFalse(TEXT("Eliminated spectator without selection ignores stale pawn"),
	          FBrAudioRules::ListenerEligible(true, true, true, true, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrAudioCadenceTest, "BrGame.Audio.FootstepCadence",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrAudioCadenceTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Run 600cm/s"), FBrAudioRules::StepInterval(600, 180, 10), 0.3f);
	TestEqual(TEXT("Sprint faster"), FBrAudioRules::StepInterval(780, 180, 10), 180.f / 780.f);
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
	TestTrue(
		TEXT("Serialize audio profile without touching user's save"),
		UGameplayStatics::SaveGameToMemory(Profile, Bytes));
	const UBrAudioProfile* Loaded = Cast<UBrAudioProfile>(UGameplayStatics::LoadGameFromMemory(Bytes));
	TestNotNull(TEXT("Profile round trip"), Loaded);
	if (Loaded)
	{
		TestEqual(TEXT("Version"), Loaded->Version, 3);
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
	for (EBrSoundKind Kind : {
		     EBrSoundKind::Fire, EBrSoundKind::Explosion, EBrSoundKind::Downed, EBrSoundKind::RunStep,
		     EBrSoundKind::Pickup
	     })
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrAudioKindGainTest, "BrGame.Audio.PerKindVolume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrAudioKindGainTest::RunTest(const FString& Parameters)
{
	UBrDeveloperSettings* S = NewObject<UBrDeveloperSettings>();
	struct FMapping
	{
		EBrSoundKind Kind;
		float UBrDeveloperSettings::* Member;
	};
	const FMapping Mappings[] = {
		{EBrSoundKind::Fire, &UBrDeveloperSettings::FireVolume},
		{EBrSoundKind::Explosion, &UBrDeveloperSettings::ExplosionVolume},
		{EBrSoundKind::Downed, &UBrDeveloperSettings::DownedVolume},
		{EBrSoundKind::RunStep, &UBrDeveloperSettings::RunStepVolume},
		{EBrSoundKind::SprintStep, &UBrDeveloperSettings::SprintStepVolume},
		{EBrSoundKind::CrouchStep, &UBrDeveloperSettings::CrouchStepVolume},
		{EBrSoundKind::Pickup, &UBrDeveloperSettings::PickupVolume},
		{EBrSoundKind::Slide, &UBrDeveloperSettings::SlideVolume},
		{EBrSoundKind::ArmorBreak, &UBrDeveloperSettings::ArmorBreakVolume},
		{EBrSoundKind::Reload, &UBrDeveloperSettings::ReloadVolume},
		{EBrSoundKind::DryFire, &UBrDeveloperSettings::DryFireVolume},
		{EBrSoundKind::Pain, &UBrDeveloperSettings::PainVolume},
		{EBrSoundKind::PainArmor, &UBrDeveloperSettings::PainArmorVolume},
		{EBrSoundKind::VoiceLine, &UBrDeveloperSettings::VoiceLineVolume}
	};
	TestEqual(TEXT("Coverage includes every wire kind"), UE_ARRAY_COUNT(Mappings),
		static_cast<size_t>(EBrSoundKind::VoiceLine) + 1);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Mappings); ++Index)
		S->*(Mappings[Index].Member) = (Index + 1) * 0.11f;
	for (const FMapping& Mapping : Mappings)
	{
		const float Original = S->*(Mapping.Member);
		TestEqual(TEXT("Independent setting, not a shared category gain"),
			FBrAudioRules::KindVolume(*S, Mapping.Kind), Original);
		for (const auto& Probe : {TPair<float, float>(-0.5f, 0.f), TPair<float, float>(3.f, 2.f),
			TPair<float, float>(0.f, 0.f), TPair<float, float>(1.7f, 1.7f),
			TPair<float, float>(std::numeric_limits<float>::quiet_NaN(), 1.f)})
		{
			S->*(Mapping.Member) = Probe.Key;
			TestEqual(TEXT("Live-read clamped gain"), FBrAudioRules::KindVolume(*S, Mapping.Kind), Probe.Value);
			for (const FMapping& Other : Mappings)
				if (Other.Kind != Mapping.Kind)
					TestEqual(TEXT("Editing one kind does not change others"),
						FBrAudioRules::KindVolume(*S, Other.Kind), S->*(Other.Member));
		}
		S->*(Mapping.Member) = Original;
	}
	TestEqual(TEXT("Invalid kind neutral"), FBrAudioRules::KindVolume(*S, static_cast<EBrSoundKind>(255)), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrAudioWeaponGainTest, "BrGame.Audio.WeaponFireVolume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrAudioWeaponGainTest::RunTest(const FString& Parameters)
{
	UBrDeveloperSettings* Settings = GetMutableDefault<UBrDeveloperSettings>();
	const auto SavedRegistry = Settings->WeaponRegistry;
	ON_SCOPE_EXIT { Settings->WeaponRegistry = SavedRegistry; };
	Settings->WeaponRegistry.Empty();
	UBrWeaponDefinition* Weapon = NewObject<UBrWeaponDefinition>();
	Weapon->FireSound = TSoftObjectPtr<USoundBase>(
		FSoftObjectPath(TEXT("/Game/BrGame/Audio/SW_Fire_Pistol.SW_Fire_Pistol")));
	Weapon->FireVolume = 0.4f;
	Settings->WeaponRegistry.Add(TEXT("TestWeapon"), TSoftObjectPtr<UBrWeaponDefinition>(Weapon));
	UBrAudioSubsystem* Predicted = NewObject<UBrAudioSubsystem>();
	UBrAudioSubsystem* Remote = NewObject<UBrAudioSubsystem>();
	FBrSoundType Source;
	Source.Kind = EBrSoundKind::Fire;
	Source.Sound = Weapon->FireSound;
	FNetBitWriter Writer(nullptr, 4096);
	bool bSuccess = false;
	Source.NetSerialize(Writer, nullptr, bSuccess);
	TestTrue(TEXT("Unchanged RPC type serializes"), bSuccess);
	FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
	FBrSoundType Received;
	Received.NetSerialize(Reader, nullptr, bSuccess);
	TestTrue(TEXT("Remote type deserializes"), bSuccess);
	const FSoftObjectPath Path = Weapon->FireSound.ToSoftObjectPath();
	TestEqual(TEXT("Predicted path resolves weapon gain"), Predicted->WeaponFireVolume(Path), Weapon->FireVolume);
	TestEqual(TEXT("Remote RPC path resolves same gain"),
		Remote->WeaponFireVolume(Received.Sound.ToSoftObjectPath()), Predicted->WeaponFireVolume(Path));
	TestTrue(TEXT("Developer x weapon x randomized variation"),
		FMath::IsNearlyEqual(FBrAudioRules::FireVolume(0.75f, Remote->WeaponFireVolume(Path), -0.05f),
			0.75f * Weapon->FireVolume * 0.95f));
	Weapon->FireVolume = 1.6f;
	TestEqual(TEXT("Cached definition still reads live asset gain"), Predicted->WeaponFireVolume(Path), 1.6f);
	Weapon->FireVolume = 3.f;
	TestEqual(TEXT("Weapon gain upper clamp"), Remote->WeaponFireVolume(Path), 2.f);
	Weapon->FireVolume = -1.f;
	TestEqual(TEXT("Weapon gain lower clamp"), Remote->WeaponFireVolume(Path), 0.f);
	const FSoftObjectPath Unknown(TEXT("/Game/BrGame/Audio/Unknown.Unknown"));
	TestEqual(TEXT("Unknown sound neutral"), Remote->WeaponFireVolume(Unknown), 1.f);
	TestEqual(TEXT("Null sound neutral"), Remote->WeaponFireVolume(FSoftObjectPath()), 1.f);
	TestTrue(TEXT("Unknown sound preserves developer gain and variation"),
		FMath::IsNearlyEqual(FBrAudioRules::FireVolume(0.75f, Remote->WeaponFireVolume(Unknown), 0.05f), 0.75f * 1.05f));
	Weapon->FireSound = TSoftObjectPtr<USoundBase>(Unknown);
	Weapon->FireVolume = 0.8f;
	TestEqual(TEXT("Changed fire path picked up next shot"), Remote->WeaponFireVolume(Unknown), 0.8f);
	TestEqual(TEXT("Old fire path now unknown"), Remote->WeaponFireVolume(Path), 1.f);
	Settings->WeaponRegistry.Empty();
	TestEqual(TEXT("Removed registry entry not resolved from stale cache"), Predicted->WeaponFireVolume(Unknown), 1.f);
	TestTrue(TEXT("Clamp factors, not effective product"),
		FMath::IsNearlyEqual(FBrAudioRules::FireVolume(3.f, 3.f, 0.2f), 4.f * 1.2f));
	TestEqual(TEXT("Muted weapon stays muted"), FBrAudioRules::FireVolume(1.f, -1.f, 0.05f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBrAudioWireTest, "BrGame.Audio.BatchWireFormat",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBrAudioWireTest::RunTest(const FString& Parameters)
{
	FBrSoundType Type;
	Type.Sound = TSoftObjectPtr<USoundBase>(
		FSoftObjectPath(TEXT("/Game/BrGame/Audio/SW_Fire_MachineGun.SW_Fire_MachineGun")));
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
