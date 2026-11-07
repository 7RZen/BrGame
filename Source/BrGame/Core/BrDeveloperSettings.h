#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "BrCombatMath.h"
#include "BrLifeRules.h"
#include "BrZoneRules.h"
#include "BrAudioRules.h"
#include "BrMapEntry.h"
#include "BrDeveloperSettings.generated.h"

class UAnimInstance;
class UAnimationAsset;
class UBlendSpace;
class USkeletalMesh;
class UBrInputConfig;
class UBrWeaponDefinition;
class UBrThrowableDefinition;
class UBrPickupDefinition;
class UMaterialInterface;
class USoundBase;
class USoundClass;
class USoundMix;
class UBrVoiceSet;
class UBrPenaltyDefinition;

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="BrGame"))
class BRGAME_API UBrDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category="Life|Penalties", meta=(ClampMin="0.1", ClampMax="120", Units="s"))
	float PlantDuration = 10;
	UPROPERTY(Config, EditAnywhere, Category="Life|Penalties", meta=(ClampMin="0", ClampMax="5000", Units="cm"))
	float PenaltyHearingRange = 1200;
	UPROPERTY(Config, EditAnywhere, Category="Life|Penalties")
	TArray<TSoftObjectPtr<UBrPenaltyDefinition>> PenaltyDefinitions;
	UPROPERTY(Config, EditAnywhere, Category="Movement|Slide", meta=(ClampMin="1", ClampMax="2"))
	float SlideSpeedMultiplier = 1.2f;
	UPROPERTY(Config, EditAnywhere, Category="Movement|Slide", meta=(ClampMin="0.05", ClampMax="2", Units="s"))
	float SlideDuration = 0.8f;
	/** Cooldown starts on every slide exit, including early cancellation. */
	UPROPERTY(Config, EditAnywhere, Category="Movement|Slide", meta=(ClampMin="0", Units="s"))
	float SlideCooldown = 1;
	UPROPERTY(Config, EditAnywhere, Category="Movement|Slide", meta=(ClampMin="0", ClampMax="1"))
	float SlideWallSpeedFraction = 0.5f;
	UPROPERTY(Config, EditAnywhere, Category="Character|Slide", meta=(ClampMin="0", ClampMax="1", Units="s"))
	float SlidePoseBlendTime = 0.08f;
	UPROPERTY(Config, EditAnywhere, Category="Character|Slide", meta=(ClampMin="0", ClampMax="70", Units="deg"))
	float SlideBackLean = 40;
	UPROPERTY(Config, EditAnywhere, Category="Character|Slide", meta=(ClampMin="15", ClampMax="60", Units="cm"))
	float SlidePelvisHeight = 32;
	UPROPERTY(Config, EditAnywhere, Category="Character|Slide", meta=(ClampMin="20", ClampMax="100", Units="cm"))
	float SlideLegReach = 72;
	UPROPERTY(Config, EditAnywhere, Category="Character|Slide", meta=(ClampMin="0", Units="cm"))
	float SlideFootSpread = 15;
	UPROPERTY(Config, EditAnywhere, Category="Character|Slide", meta=(ClampMin="0", Units="cm"))
	float SlideFootHeight = 10;
	UPROPERTY(Config, EditAnywhere, Category="Character|Slide", meta=(Units="cm"))
	FVector SlideLeftHandTarget = FVector(35, -15, 8);
	UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="0", Units="cm"))
	float SlideHearingRange = 1000;
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	TSoftObjectPtr<USoundBase> SlideSound = TSoftObjectPtr<USoundBase>(
		FSoftObjectPath(TEXT("/Game/BrGame/Audio/SW_Slide.SW_Slide")));
	/** Full-body, in-place, non-additive sequence or blendspace, on the character skeleton.
	 * Blendspace convention: direction (degrees) X, speed (cm/s) Y. Empty slots use procedural poses. */
	UPROPERTY(Config, EditAnywhere, Category="Character|Posture")
	TSoftObjectPtr<UAnimationAsset> CrouchIdle;
	UPROPERTY(Config, EditAnywhere, Category="Character|Posture")
	TSoftObjectPtr<UAnimationAsset> CrouchMove;
	UPROPERTY(Config, EditAnywhere, Category="Character|Posture")
	TSoftObjectPtr<UAnimationAsset> ProneIdle;
	UPROPERTY(Config, EditAnywhere, Category="Character|Posture")
	TSoftObjectPtr<UAnimationAsset> ProneMove;
	UPROPERTY(Config, EditAnywhere, Category="Character|Posture", meta=(ClampMin="0", ClampMax="2", Units="s"))
	float CrouchPoseBlendTime = 0.25f;
	UPROPERTY(Config, EditAnywhere, Category="Character|Posture", meta=(ClampMin="0", ClampMax="2", Units="s"))
	float PronePoseBlendTime = 0.4f;
	/** Pelvis drop at the default 60 cm crouch capsule half-height; scales with capsule reduction. */
	UPROPERTY(Config, EditAnywhere, Category="Character|Posture", meta=(ClampMin="0", ClampMax="75", Units="cm"))
	float CrouchPelvisDrop = 65;
	UPROPERTY(Config, EditAnywhere, Category="Character|Posture", meta=(ClampMin="0", ClampMax="35", Units="cm"))
	float CrouchPelvisForward = 22;
	UPROPERTY(Config, EditAnywhere, Category="Character|Posture", meta=(ClampMin="0", ClampMax="25", Units="deg"))
	float CrouchHipLean = 12;
	/** Additional forward spine lean on top of the hip lean. */
	UPROPERTY(Config, EditAnywhere, Category="Character|Posture", meta=(ClampMin="0", ClampMax="55", Units="deg"))
	float CrouchTorsoLean = 38;
	/** Outward/forward ratio of each knee's IK bend direction. */
	UPROPERTY(Config, EditAnywhere, Category="Character|Posture", meta=(ClampMin="0", ClampMax="2"))
	float CrouchKneeOutward = 1.2f;
	/** Extra lateral foot spacing per side; sampled foot height/cycle stays unchanged. */
	UPROPERTY(Config, EditAnywhere, Category="Character|Posture", meta=(ClampMin="0", ClampMax="20", Units="cm"))
	float CrouchFootSpread = 8;
	UPROPERTY(Config, EditAnywhere, Category="Character|Posture")
	TSoftObjectPtr<UBlendSpace> PostureLocomotion = TSoftObjectPtr<UBlendSpace>(
		FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run.BS_Idle_Walk_Run")));
	UPROPERTY(Config, EditAnywhere, Category="HitFeedback", meta=(ClampMin="0", Units="deg"))
	float BulletHitAngle = 3;
	UPROPERTY(Config, EditAnywhere, Category="HitFeedback", meta=(ClampMin="0", Units="deg"))
	float GrenadeHitAngle = 12;
	UPROPERTY(Config, EditAnywhere, Category="HitFeedback", meta=(ClampMin="0", Units="s"))
	float BulletHitDuration = 0.15f;
	UPROPERTY(Config, EditAnywhere, Category="HitFeedback", meta=(ClampMin="0", Units="s"))
	float GrenadeHitDuration = 0.4f;
	UPROPERTY(Config, EditAnywhere, Category="HitFeedback", meta=(ClampMin="0", Units="cm"))
	float GrenadeHitJolt = 18;
	UPROPERTY(Config, EditAnywhere, Category="HitFeedback", meta=(ClampMin="0", Units="s"))
	float HitIndicatorDuration = 1;
	UPROPERTY(Config, EditAnywhere, Category="HitFeedback", meta=(ClampMin="1", ClampMax="32"))
	int32 HitIndicatorCap = 8;
	UPROPERTY(Config, EditAnywhere, Category="HitFeedback", meta=(ClampMin="0", Units="s"))
	float HitFlashDuration = 0.3f;
	UPROPERTY(Config, EditAnywhere, Category="HitFeedback", meta=(ClampMin="0", ClampMax="1"))
	float HitFlashOpacity = 0.45f;
	UPROPERTY(Config, EditAnywhere, Category="HitFeedback", meta=(ClampMin="1"))
	float HitFlashFullDamage = 30;
	UPROPERTY(Config, EditAnywhere, Category="HitFeedback", meta=(ClampMin="0"))
	float HitEdgeWidth = 45;
	UPROPERTY(Config, EditAnywhere, Category="HitFeedback", meta=(ClampMin="0", Units="s"))
	float HitReactionDuration = 0.18f;
	UPROPERTY(Config, EditAnywhere, Category="HitFeedback", meta=(ClampMin="0", Units="cm"))
	float HitReactionDistance = 10;
	UPROPERTY(Config, EditAnywhere, Category="HitFeedback")
	FLinearColor HitMeshColor = FLinearColor(2, 0, 0, 1);
	UPROPERTY(Config, EditAnywhere, Category="HitFeedback", meta=(ClampMin="0", ClampMax="1"))
	float HitMeshOpacity = 0.65f;
	UPROPERTY(Config, EditAnywhere, Category="HitFeedback")
	TSoftObjectPtr<UMaterialInterface> HitOverlay = TSoftObjectPtr<UMaterialInterface>(
		FSoftObjectPath(TEXT("/Game/BrGame/Feedback/M_HitOverlay.M_HitOverlay")));
	UPROPERTY(Config, EditAnywhere, Category="Menu", meta=(ClampMin="0.1", ClampMax="10", Units="s"))
	float ForfeitAckTimeout = 2;
	UPROPERTY(Config, EditAnywhere, Category="Menu", meta=(ClampMin="0.1", ClampMax="2", Units="s"))
	float ForfeitRetryInterval = 0.5f;
	UPROPERTY(Config, EditAnywhere, Category="Unstuck", meta=(ClampMin="0.1", Units="s"))
	float UnstuckHoldTime = 5;
	UPROPERTY(Config, EditAnywhere, Category="Unstuck", meta=(ClampMin="0", Units="s"))
	float UnstuckCooldown = 60;
	UPROPERTY(Config, EditAnywhere, Category="Unstuck", meta=(ClampMin="100", ClampMax="2000", Units="cm"))
	float UnstuckSearchRadius = 1000;
	UPROPERTY(Config, EditAnywhere, Category="Unstuck", meta=(ClampMin="1", ClampMax="50", Units="cm"))
	float UnstuckMovementTolerance = 10;
	UPROPERTY(Config, EditAnywhere, Category="Unstuck", meta=(ClampMin="50", ClampMax="250", Units="cm"))
	float UnstuckSearchStep = 100;
	UPROPERTY(Config, EditAnywhere, Category="Unstuck", meta=(ClampMin="0.01", ClampMax="0.25", Units="s"))
	float UnstuckServiceInterval = 0.05f;
	UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="0.05", ClampMax="0.25", Units="s"))
	float AudioBatchInterval = 0.1f;
	/** Regenerate audio assets after changing limits/rules/priorities. */
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	FBrAudioVoicePolicy FireVoice;
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	FBrAudioVoicePolicy ExplosionVoice;
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	FBrAudioVoicePolicy DownedVoice;
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	FBrAudioVoicePolicy FootstepVoice;
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	FBrAudioVoicePolicy PickupVoice;
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	FBrAudioVoicePolicy ReloadVoice;
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	FBrAudioVoicePolicy DryFireVoice;
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	FBrAudioVoicePolicy PainVoice;
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	FBrAudioVoicePolicy VoiceLineVoice;
	UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="0", Units="cm"))
	float ReloadHearingRange = 1500;
	UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="0", Units="cm"))
	float DryFireHearingRange = 1500;
	UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="0", Units="cm"))
	float PainHearingRange = 1000;
	UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="0", Units="cm"))
	float VoiceLineHearingRange = 2000;
	UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="0", Units="s"))
	float PainSoundMinInterval = 0.5f;
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	TSoftObjectPtr<USoundBase> DryFireSound = TSoftObjectPtr<USoundBase>(
		FSoftObjectPath(TEXT("/Game/BrGame/Audio/SW_DryFire.SW_DryFire")));
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	TSoftObjectPtr<USoundBase> PainSound = TSoftObjectPtr<USoundBase>(
		FSoftObjectPath(TEXT("/Game/BrGame/Audio/SW_Pain.SW_Pain")));
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	TSoftObjectPtr<USoundBase> PainArmorSound = TSoftObjectPtr<USoundBase>(
		FSoftObjectPath(TEXT("/Game/BrGame/Audio/SW_PainArmor.SW_PainArmor")));
	UPROPERTY(Config, EditAnywhere, Category="Audio|Voice")
	TArray<TSoftObjectPtr<UBrVoiceSet>> VoiceSets;
	UPROPERTY(Config, EditAnywhere, Category="Audio|Voice")
	TSoftObjectPtr<USoundClass> VoiceSoundClass = TSoftObjectPtr<USoundClass>(
		FSoftObjectPath(TEXT("/Game/BrGame/Audio/SC_Voice.SC_Voice")));
	const FBrAudioVoicePolicy& VoicePolicy(EBrSoundKind Kind) const;
	UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="0", Units="cm"))
	float GunshotHearingRange = 5000;
	UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="0", Units="cm"))
	float ExplosionHearingRange = 8000;
	UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="0", Units="cm"))
	float DownedHearingRange = 4000;
	UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="0", Units="cm"))
	float RunStepHearingRange = 1500;
	UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="0", Units="cm"))
	float SprintStepHearingRange = 2000;
	UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="0", Units="cm"))
	float CrouchStepHearingRange = 300;
	/** Fraction of hearing radius with full volume, followed by linear falloff. */
	UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="0", ClampMax="0.9"))
	float AudioInnerRadiusFraction = 0.1f;
	/** Per-event developer gains, read on each playback (including PIE tuning). */
	UPROPERTY(Config, EditAnywhere, Category="Audio|Volume", meta=(ClampMin="0", ClampMax="2"))
	float FireVolume = 1.f;
	UPROPERTY(Config, EditAnywhere, Category="Audio|Volume", meta=(ClampMin="0", ClampMax="2"))
	float ExplosionVolume = 1.f;
	UPROPERTY(Config, EditAnywhere, Category="Audio|Volume", meta=(ClampMin="0", ClampMax="2"))
	float DownedVolume = 0.9f;
	UPROPERTY(Config, EditAnywhere, Category="Audio|Volume", meta=(ClampMin="0", ClampMax="2"))
	float VoiceLineVolume = 1.f;
	UPROPERTY(Config, EditAnywhere, Category="Audio|Volume", meta=(ClampMin="0", ClampMax="2"))
	float RunStepVolume = 0.35f;
	UPROPERTY(Config, EditAnywhere, Category="Audio|Volume", meta=(ClampMin="0", ClampMax="2"))
	float SprintStepVolume = 0.5f;
	UPROPERTY(Config, EditAnywhere, Category="Audio|Volume", meta=(ClampMin="0", ClampMax="2"))
	float CrouchStepVolume = 0.1f;
	UPROPERTY(Config, EditAnywhere, Category="Audio|Volume", meta=(ClampMin="0", ClampMax="2"))
	float SlideVolume = 0.5f;
	UPROPERTY(Config, EditAnywhere, Category="Audio|Volume", meta=(ClampMin="0", ClampMax="2"))
	float ReloadVolume = 0.6f;
	UPROPERTY(Config, EditAnywhere, Category="Audio|Volume", meta=(ClampMin="0", ClampMax="2"))
	float DryFireVolume = 0.5f;
	UPROPERTY(Config, EditAnywhere, Category="Audio|Volume", meta=(ClampMin="0", ClampMax="2"))
	float PainVolume = 0.7f;
	UPROPERTY(Config, EditAnywhere, Category="Audio|Volume", meta=(ClampMin="0", ClampMax="2"))
	float PainArmorVolume = 0.6f;
	UPROPERTY(Config, EditAnywhere, Category="Audio|Volume", meta=(ClampMin="0", ClampMax="2"))
	float PickupVolume = 0.6f;
	UPROPERTY(Config, EditAnywhere, Category="Audio|Volume", meta=(ClampMin="0", ClampMax="2"))
	float ArmorBreakVolume = 0.8f;
	UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="0", ClampMax="0.2"))
	float FirePitchVariation = 0.04f;
	UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="0", ClampMax="0.2"))
	float FireVolumeVariation = 0.05f;
	UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="1", Units="cm"))
	float FootstepStride = 180;
	UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="1", Units="cm/s"))
	float FootstepMinimumSpeed = 10;
	UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin="0.05", ClampMax="0.5", Units="s"))
	float FootstepServiceInterval = 0.1f;
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	TArray<TSoftObjectPtr<USoundBase>> RunFootsteps;
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	TArray<TSoftObjectPtr<USoundBase>> SoftFootsteps;
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	TSoftObjectPtr<USoundBase> DownedScream;
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	TSoftObjectPtr<USoundBase> DefaultPickupSound;
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	TSoftObjectPtr<USoundBase> ArmorBreakSound = TSoftObjectPtr<USoundBase>(
		FSoftObjectPath(TEXT("/Game/BrGame/Audio/SW_ArmorBreak.SW_ArmorBreak")));
	UPROPERTY(Config, EditAnywhere, Category="Combat|Armor", meta=(ClampMin="0.01", ClampMax="1000"))
	float ArmorPlateValue = 20;
	UPROPERTY(Config, EditAnywhere, Category="Combat|Armor", meta=(ClampMin="0", ClampMax="100"))
	int32 MaxArmorPlates = 2;
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	TSoftObjectPtr<USoundClass> MasterSoundClass;
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	TSoftObjectPtr<USoundClass> EffectsSoundClass;
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	TSoftObjectPtr<USoundMix> VolumeSoundMix;
	UPROPERTY(Config, EditAnywhere, Category="Minimap", meta=(ClampMin="100", ClampMax="500", DisplayName="Minimap Size (reference pixels)"))
	float MinimapSize = 220;
	UPROPERTY(Config, EditAnywhere, Category="Minimap", meta=(ClampMin="100", Units="cm"))
	float MinimapRadius = 6000;
	UPROPERTY(Config, EditAnywhere, Category="Minimap", meta=(ClampMin="128", ClampMax="2048", DisplayName="Map Capture Size (pixels)"))
	int32 MinimapTextureSize = 1024;
	UPROPERTY(Config, EditAnywhere, Category="Minimap", meta=(ClampMin="0.3", ClampMax="0.9"))
	float FullMapViewportFraction = 0.8f;
	UPROPERTY(Config, EditAnywhere, Category="Minimap", meta=(ClampMin="2", ClampMax="16", DisplayName="Marker Size (reference pixels)"))
	float MinimapMarkerSize = 5;
	UPROPERTY(Config, EditAnywhere, Category="Minimap")
	TSoftObjectPtr<UMaterialInterface> MinimapMaterial = TSoftObjectPtr<UMaterialInterface>(
		FSoftObjectPath(TEXT("/Game/BrGame/Minimap/M_Minimap.M_Minimap")));
	UPROPERTY(Config, EditAnywhere, Category="Fog", meta=(ClampMin="100", Units="cm"))
	float VisionRadius = 1500;
	UPROPERTY(Config, EditAnywhere, Category="Fog", meta=(ClampMin="1", ClampMax="60", Units="Hz"))
	float VisibilityUpdateRate = 10;
	UPROPERTY(Config, EditAnywhere, Category="Fog", meta=(ClampMin="1", ClampMax="60", Units="Hz"))
	float FogUpdateRate = 25;
	UPROPERTY(Config, EditAnywhere, Category="Fog", meta=(ClampMin="32", ClampMax="2048"))
	int32 FogRayCount = 720;
	UPROPERTY(Config, EditAnywhere, Category="Fog", meta=(ClampMin="128", ClampMax="4096"))
	int32 FogTextureSize = 2048;
	/** Inward-only mask filtering: never extends LOS past a blocking wall. */
	UPROPERTY(Config, EditAnywhere, Category="Fog", meta=(ClampMin="0", ClampMax="150", Units="cm"))
	float FogEdgeFeather = 60;
	/** Main-view explored content only; shares the dimming timestamp/delay. */
	UPROPERTY(Config, EditAnywhere, Category="Fog")
	bool bFogDimBlur = true;
	/** Maximum 40 cm: bounded by the three intermediate screen-depth probes. */
	UPROPERTY(Config, EditAnywhere, Category="Fog", meta=(ClampMin="0", ClampMax="40", Units="cm", EditCondition="bFogDimBlur"))
	float FogDimBlurRadius = 25;
	UPROPERTY(Config, EditAnywhere, Category="Fog", meta=(ClampMin="0", ClampMax="1"))
	float DimOpacity = 0.6f;
	UPROPERTY(Config, EditAnywhere, Category="Fog", meta=(ClampMin="0.01", Units="s"))
	float FogDimDelay = 2;
	UPROPERTY(Config, EditAnywhere, Category="Fog", meta=(ClampMin="0", ClampMax="1", Units="s"))
	float VisionGrace = 0.25f;
	UPROPERTY(Config, EditAnywhere, Category="Fog|Assets")
	TSoftObjectPtr<UMaterialInterface> FogMaterial = TSoftObjectPtr<UMaterialInterface>(
		FSoftObjectPath(TEXT("/Game/BrGame/Fog/M_Fog.M_Fog")));
	UPROPERTY(Config, EditAnywhere, Category="Zone")
	TArray<FBrZoneStage> ZoneStages;
	UPROPERTY(Config, EditAnywhere, Category="Zone", meta=(ClampMin="0.01", ClampMax="1000"))
	float ZoneTimeScale = 1;
	UPROPERTY(Config, EditAnywhere, Category="Zone")
	bool bDisableRespawnDuringFinalStage = true;
	UPROPERTY(Config, EditAnywhere, Category="Zone|Assets")
	TSoftObjectPtr<UMaterialInterface> ZoneRingMaterial;
	UPROPERTY(Config, EditAnywhere, Category="Maps", meta=(TitleProperty="DisplayName"))
	TArray<FBrMapEntry> Maps;
	UPROPERTY(Config, EditAnywhere, Category="Maps")
	FName DefaultMapId = TEXT("Town");
	/** Filters invalid/duplicate IDs, non-world assets and missing packages without loading levels. */
	TArray<FBrMapEntry> ValidMaps() const;
	const FBrMapEntry* FindMap(FName Id) const;
	bool IsMapSelectionValid(FName Id) const;
	FName InitialMapId() const;
	bool ResolveMap(FName Selection, FRandomStream& Random, FBrMapEntry& Out) const;
	FText MapName(FName Id) const;
	UPROPERTY(Config, EditAnywhere, Category="Flow", meta=(ClampMin="1", Units="s"))
	float ResultsDuration = 10;
	/** Allow a single contestant to finish; false keeps solo practice running. */
	UPROPERTY(Config, EditAnywhere, Category="Flow")
	bool bAllowSoloTesting = false;
	/** Server timeline: disconnected or uninitialized travelling contestants forfeit after this grace. */
	UPROPERTY(Config, EditAnywhere, Category="Flow", meta=(ClampMin="0.1", Units="s"))
	float ReconnectGracePeriod = 60;
	UPROPERTY(Config, EditAnywhere, Category="Flow", meta=(ClampMin="100", Units="cm"))
	float TeamSpawnRadius = 400;
	UPROPERTY(Config, EditAnywhere, Category="Character")
	FName TeamColorParameter = TEXT("Paint Tint");
	UBrDeveloperSettings();

	static const UBrDeveloperSettings* Get();

	UPROPERTY(Config, EditAnywhere, Category="Pickups")
	TMap<FName, TSoftObjectPtr<UBrPickupDefinition>> PickupRegistry;
	/** Weapon pickup template; payload supplies the actual weapon and loaded rounds. */
	UPROPERTY(Config, EditAnywhere, Category="Pickups")
	TSoftObjectPtr<UBrPickupDefinition> DroppedWeaponPickup;
	/** Required interactive quest token; missing asset cancels crate landings. */
	UPROPERTY(Config, EditAnywhere, Category="Crates|Loot")
	TSoftObjectPtr<UBrPickupDefinition> QuestPickup;
	/** Ordered: Pistol, AssaultRifle, MachineGun, Ammo, Grenade, Health20, Health50, Health100, Armor. */
	UPROPERTY(Config, EditAnywhere, Category="Crates|Loot")
	TArray<TSoftObjectPtr<UBrPickupDefinition>> CrateDefinitions;
	UPROPERTY(Config, EditAnywhere, Category="Crates|Loot", meta=(ClampMin="1", ClampMax="2"))
	int32 CrateARMin = 1;
	UPROPERTY(Config, EditAnywhere, Category="Crates|Loot", meta=(ClampMin="1", ClampMax="2"))
	int32 CrateARMax = 2;
	UPROPERTY(Config, EditAnywhere, Category="Crates|Loot", meta=(ClampMin="0", ClampMax="1"))
	float CrateMGChance = 0.5f;
	UPROPERTY(Config, EditAnywhere, Category="Crates|Loot", meta=(ClampMin="0", ClampMax="100"))
	int32 CrateHealthMin = 3;
	UPROPERTY(Config, EditAnywhere, Category="Crates|Loot", meta=(ClampMin="0", ClampMax="100"))
	int32 CrateHealthMax = 8;
	UPROPERTY(Config, EditAnywhere, Category="Crates|Loot")
	TArray<float> CrateHealthWeights = {60, 30, 10};
	UPROPERTY(Config, EditAnywhere, Category="Crates|Loot", meta=(ClampMin="0", ClampMax="100"))
	int32 CrateAmmoMin = 3;
	UPROPERTY(Config, EditAnywhere, Category="Crates|Loot", meta=(ClampMin="0", ClampMax="100"))
	int32 CrateAmmoMax = 8;
	UPROPERTY(Config, EditAnywhere, Category="Crates|Loot", meta=(ClampMin="0", ClampMax="100"))
	int32 CrateGrenadeMin = 2;
	UPROPERTY(Config, EditAnywhere, Category="Crates|Loot", meta=(ClampMin="0", ClampMax="100"))
	int32 CrateGrenadeMax = 4;
	UPROPERTY(Config, EditAnywhere, Category="Crates|Loot", meta=(ClampMin="0", ClampMax="100"))
	int32 CrateArmorMin = 2;
	UPROPERTY(Config, EditAnywhere, Category="Crates|Loot", meta=(ClampMin="0", ClampMax="100"))
	int32 CrateArmorMax = 8;
	UPROPERTY(Config, EditAnywhere, Category="Crates", meta=(ClampMin="1", ClampMax="256"))
	int32 CratePlacementAttempts = 64;
	UPROPERTY(Config, EditAnywhere, Category="Crates", meta=(ClampMin="300", Units="cm"))
	float CratePawnClearance = 300;
	UPROPERTY(Config, EditAnywhere, Category="Crates", meta=(ClampMin="100", Units="cm"))
	float CrateDropHeight = 3000;
	UPROPERTY(Config, EditAnywhere, Category="Crates", meta=(ClampMin="0.1", Units="s"))
	float CrateFallDuration = 4;
	UPROPERTY(Config, EditAnywhere, Category="Crates", meta=(ClampMin="0.1", Units="s"))
	float CrateCoverLifetime = 20;
	UPROPERTY(Config, EditAnywhere, Category="Crates", meta=(ClampMin="1", Units="s"))
	float CrateItemLifetime = 120;
	UPROPERTY(Config, EditAnywhere, Category="Crates", meta=(ClampMin="0", Units="cm"))
	float CrateScatterMin = 250;
	UPROPERTY(Config, EditAnywhere, Category="Crates", meta=(ClampMin="0", Units="cm"))
	float CrateScatterMax = 500;
	/** Minimum XY distance between same-wave crate landings; raised to twice scatter max if smaller. */
	UPROPERTY(Config, EditAnywhere, Category="Crates", meta=(ClampMin="170", Units="cm"))
	float CrateWaveSpacing = 2000;
	UPROPERTY(Config, EditAnywhere, Category="Crates|Assets")
	TSoftObjectPtr<USoundBase> CrateLandingSound;
	UPROPERTY(Config, EditAnywhere, Category="Pickups", meta=(ClampMin="1", Units="cm"))
	float InteractRadius = 150;
	/** Uncollected dropped weapon lifetime; zero persists until round cleanup. */
	UPROPERTY(Config, EditAnywhere, Category="Pickups", meta=(ClampMin="0", Units="s"))
	float DroppedWeaponLifetime = 120;

	UPROPERTY(Config, EditAnywhere, Category="Life", meta=(ClampMin="0.1", Units="s"))
	float DownedDuration = 30;
	UPROPERTY(Config, EditAnywhere, Category="Life", meta=(ClampMin="1", Units="cm"))
	float ReviveRadius = 150;
	UPROPERTY(Config, EditAnywhere, Category="Life", meta=(ClampMin="0.1", Units="s"))
	float ReviveDuration = 5;
	UPROPERTY(Config, EditAnywhere, Category="Life")
	EBrReviveRule ReviveRule = EBrReviveRule::Anyone;
	UPROPERTY(Config, EditAnywhere, Category="Life")
	bool bCancelReviveOnDamage = true;
	UPROPERTY(Config, EditAnywhere, Category="Life", meta=(ClampMin="0.01", ClampMax="1"))
	float RevivedHealthFraction = 0.30f;
	UPROPERTY(Config, EditAnywhere, Category="Life", meta=(ClampMin="1", ClampMax="100"))
	float BaseMaxHealth = 100;
	UPROPERTY(Config, EditAnywhere, Category="Life", meta=(ClampMin="0"))
	int32 MaxDeaths = 10;
	UPROPERTY(Config, EditAnywhere, Category="Life")
	bool bTimeoutRespawnCountsAsDeath = true;
	UPROPERTY(Config, EditAnywhere, Category="Life", meta=(ClampMin="0.1", Units="s"))
	float RespawnHoldTime = 1;
	UPROPERTY(Config, EditAnywhere, Category="Life", meta=(ClampMin="0", ClampMax="1"))
	float MaxHealthPenaltyPerRespawn = 0.10f;
	UPROPERTY(Config, EditAnywhere, Category="Life", meta=(ClampMin="0.01", ClampMax="1"))
	float MinMaxHealthFraction = 0.1f;
	UPROPERTY(Config, EditAnywhere, Category="Life", meta=(ClampMin="1", ClampMax="256"))
	int32 SpawnCandidates = 24;
	UPROPERTY(Config, EditAnywhere, Category="Life", meta=(ClampMin="1", Units="cm"))
	float SpawnSearchRadius = 10000;
	UPROPERTY(Config, EditAnywhere, Category="Life", meta=(ClampMin="0", Units="cm"))
	float MinSpawnDistanceFromEnemies = 2000;
	UPROPERTY(Config, EditAnywhere, Category="Life", meta=(ClampMin="0", Units="s"))
	float RespawnInvulnerabilityDuration = 3;
	UPROPERTY(Config, EditAnywhere, Category="Life")
	bool bCancelInvulnerabilityOnFire = true;

	UPROPERTY(Config, EditAnywhere, Category="Combat|Assets")
	TSoftObjectPtr<UBrWeaponDefinition> StartingWeapon;
	/** Stable identifiers used by cheats and future pickups. */
	UPROPERTY(Config, EditAnywhere, Category="Combat|Assets")
	TMap<FName, TSoftObjectPtr<UBrWeaponDefinition>> WeaponRegistry;
	UPROPERTY(Config, EditAnywhere, Category="Combat|Assets")
	TSoftObjectPtr<UBrThrowableDefinition> HandGrenade;
	UPROPERTY(Config, EditAnywhere, Category="Combat|Hit Chance")
	FBrHitChanceRow StandingHitChance;
	UPROPERTY(Config, EditAnywhere, Category="Combat|Hit Chance")
	FBrHitChanceRow CrouchingHitChance;
	UPROPERTY(Config, EditAnywhere, Category="Combat|Hit Chance")
	FBrHitChanceRow ProneHitChance;
	UPROPERTY(Config, EditAnywhere, Category="Combat|Damage", meta=(ClampMin="0", ClampMax="1"))
	float StandingDamageMultiplier = 1;
	UPROPERTY(Config, EditAnywhere, Category="Combat|Damage", meta=(ClampMin="0", ClampMax="1"))
	float CrouchingDamageMultiplier = 0.75f;
	UPROPERTY(Config, EditAnywhere, Category="Combat|Damage", meta=(ClampMin="0", ClampMax="1"))
	float ProneDamageMultiplier = 0.5f;
	UPROPERTY(Config, EditAnywhere, Category="Combat|Damage")
	bool bGrenadePostureDamage = true;
	UPROPERTY(Config, EditAnywhere, Category="Combat|Damage")
	bool bGrenadeSelfDamage = true;

	UPROPERTY(Config, EditAnywhere, Category="Character")
	TSoftObjectPtr<USkeletalMesh> DefaultCharacterMesh;

	UPROPERTY(Config, EditAnywhere, Category="Character")
	TSoftClassPtr<UAnimInstance> DefaultCharacterAnimClass;

	UPROPERTY(Config, EditAnywhere, Category="Input")
	TSoftObjectPtr<UBrInputConfig> InputConfig;

	UPROPERTY(Config, EditAnywhere, Category="Movement", meta=(ClampMin="0.0", Units="cm/s"))
	float BaseRunSpeed = 600.0f;

	UPROPERTY(Config, EditAnywhere, Category="Movement|Sprint", meta=(ClampMin="1.0", ClampMax="3.0"))
	float SprintSpeedMultiplier = 1.3f;

	UPROPERTY(Config, EditAnywhere, Category="Movement|Sprint", meta=(ClampMin="0", ClampMax="89", Units="deg"))
	float SprintForwardHalfAngle = 50;
	UPROPERTY(Config, EditAnywhere, Category="Movement|Sprint", meta=(ClampMin="0.1", Units="s"))
	float SprintStaminaDuration = 5;
	UPROPERTY(Config, EditAnywhere, Category="Movement|Sprint", meta=(ClampMin="0", Units="s"))
	float SprintRegenDelay = 1;
	UPROPERTY(Config, EditAnywhere, Category="Movement|Sprint", meta=(ClampMin="0.1", Units="s"))
	float SprintRegenDuration = 5;
	UPROPERTY(Config, EditAnywhere, Category="Movement|Sprint", meta=(ClampMin="0.01", Units="s"))
	float SprintMinimumStart = 0.5f;
	UPROPERTY(Config, EditAnywhere, Category="Movement|Sprint", meta=(ClampMin="0", Units="s"))
	float SprintToFireDelay = 0.2f;
	/** Bounded arrival-time tolerance; never permits firing during an active sprint. */
	UPROPERTY(Config, EditAnywhere, Category="Movement|Sprint", meta=(ClampMin="0", ClampMax="0.05", Units="s"))
	float SprintFireGrace = 0.03f;

	UPROPERTY(Config, EditAnywhere, Category="Movement", meta=(ClampMin="0.0", ClampMax="1.0"))
	float CrouchSpeedMultiplier = 0.2f;

	UPROPERTY(Config, EditAnywhere, Category="Movement", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ProneSpeedMultiplier = 0.2f;

	UPROPERTY(Config, EditAnywhere, Category="Movement", meta=(ClampMin="42.0", ClampMax="96.0", Units="cm"))
	float CrouchCapsuleHalfHeight = 60.0f;

	UPROPERTY(Config, EditAnywhere, Category="Movement", meta=(ClampMin="42.0", ClampMax="96.0", Units="cm"))
	float ProneCapsuleHalfHeight = 42.0f;

	/** Initial local comfort setting, in degrees per raw mouse X unit. Saved overrides are machine-local. */
	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="0.02", ClampMax="2.0"))
	float DefaultMouseSensitivity = 0.2f;

	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="0.75", ClampMax="2.5"))
	float PlayAreaAspectRatio = 4.f / 3.f;

	/** 160 reference pixels at 720p = 240 physical pixels at 1080p. */
	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="80", ClampMax="600", DisplayName="Minimum Side Panel Width (720p reference pixels)"))
	float MinSidePanelWidth = 160.f;

	UPROPERTY(Config, EditAnywhere, Category="HUD")
	bool bShowMovementDebugHud = false;

	/** Fraction of play-area height below centre occupied by the pawn origin, independent of zoom/pitch. */
	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="0.0", ClampMax="0.4"))
	float PlayerScreenOffset = 0.25f;

	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="1.0", Units="cm"))
	float MinOrthoWidth = 1200.0f;

	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="1.0", Units="cm"))
	float MaxOrthoWidth = 4000.0f;

	/** Camera angle below the horizon; 90 looks straight down, lower values tilt toward the facing direction. */
	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="30.0", ClampMax="90.0", Units="deg"))
	float CameraPitchAngle = 65.0f;

	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="0.0", Units="cm"))
	float ZoomStep = 250.0f;

	UPROPERTY(Config, EditAnywhere, Category="Camera", meta=(ClampMin="0.0"))
	float ZoomInterpSpeed = 10.0f;
};
