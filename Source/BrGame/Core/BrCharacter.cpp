#include "BrCharacter.h"
#include "BrAimingRules.h"
#include "BrGameInstance.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "SceneView.h"
#include "BrAudioSubsystem.h"
#include "BrVisibilitySubsystem.h"
#include "BrFogComponent.h"
#include "BrZoneDamageType.h"
#include "BrDeveloperSettings.h"
#include "BrLog.h"
#include "BrCharacterMovementComponent.h"
#include "BrInputConfig.h"
#include "BrInventoryComponent.h"
#include "BrHealthComponent.h"
#include "BrCombatVisual.h"
#include "BrCombatMath.h"
#include "BrGameMode.h"
#include "BrPlayerState.h"
#include "BrPlayerController.h"
#include "BrFlowRules.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "BrLifeComponent.h"
#include "BrPickup.h"
#include "TimerManager.h"
#include "Components/TextRenderComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"

ABrCharacter::ABrCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UBrCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	bReplicates = true;
	SetReplicateMovement(true);
	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);
	// BrPickup object channel: overlap items without introducing physical/projectile blockers.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Overlap);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->bUseControllerDesiredRotation = false;

	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -96.0f));
	GetMesh()->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));

	Health = CreateDefaultSubobject<UBrHealthComponent>(TEXT("Health"));
	Inventory = CreateDefaultSubobject<UBrInventoryComponent>(TEXT("Inventory"));
	Life = CreateDefaultSubobject<UBrLifeComponent>(TEXT("Life"));
	HelpText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("HelpText"));
	HelpText->SetupAttachment(RootComponent);
	HelpText->SetAbsolute(false, true, false);
	HelpText->SetRelativeRotation(FRotator(90, 0, 0));
	HelpText->SetRelativeLocation(FVector(0, 0, 115));
	HelpText->SetHorizontalAlignment(EHTA_Center);
	HelpText->SetWorldSize(45);
	HelpText->SetTextRenderColor(FColor::Yellow);
	HelpText->SetCollisionProfileName(TEXT("NoCollision"));
	HelpText->SetVisibility(false);
	WeaponVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponVisual"));
	WeaponVisual->SetupAttachment(RootComponent);
	WeaponVisual->SetCollisionProfileName(TEXT("NoCollision"));
	WeaponVisual->SetRelativeLocation(FVector(40, 15, 0));
	WeaponVisual->SetRelativeScale3D(FVector(0.5f, 0.08f, 0.08f));

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
	CameraBoom->TargetArmLength = 1500.0f;
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bDoCollisionTest = false;

	TopDownCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));
	TopDownCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	TopDownCamera->bUsePawnControlRotation = false;
	TopDownCamera->bConstrainAspectRatio = false;
	TopDownCamera->SetProjectionMode(ECameraProjectionMode::Orthographic);
	TopDownCamera->SetOrthoWidth(OrthoWidth);
	TargetOrthoWidth = OrthoWidth;
	TopDownCamera->SetAutoActivate(false);
}

UBrCharacterMovementComponent* ABrCharacter::GetBrMovement() const
{
	return CastChecked<UBrCharacterMovementComponent>(GetCharacterMovement());
}

EBrPosture ABrCharacter::GetPosture() const
{
	return GetBrMovement()->GetPosture();
}

float ABrCharacter::GetCameraYaw() const
{
	if (IsLocallyControlled() && Controller) return Controller->GetControlRotation().Yaw;
	// CMC smooths the visual mesh on simulated proxies (including spectator targets).
	return (GetMesh()->GetComponentQuat() * GetBaseRotationOffset().Inverse()).Rotator().Yaw;
}

void ABrCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	Super::CalcCamera(DeltaTime, OutResult);
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	float Aspect = TopDownCamera->AspectRatio;
	FIntPoint ViewSize(1280, 720);
	if (const UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport())
	{
		if (ViewportClient->Viewport)
		{
			const FIntPoint Size = ViewportClient->Viewport->GetSizeXY();
			if (Size.X > 0 && Size.Y > 0)
			{
				ViewSize = Size;
				Aspect = static_cast<float>(Size.X) / Size.Y;
			}
		}
	}
	const float Pitch = FMath::Clamp(Settings->CameraPitchAngle, 30.f, 90.f);
	OutResult.Rotation = FRotator(-Pitch, GetCameraYaw(), 0);
	const FVector Forward = FRotator(0, GetCameraYaw(), 0).Vector();
	// Use the rendered ortho width on every camera evaluation, including spectator cameras.
	OutResult.Location = GetActorLocation() - OutResult.Rotation.Vector() * CameraBoom->TargetArmLength +
		Forward * FBrAimingRules::ForwardOffset(OutResult.OrthoWidth, Aspect, Pitch, Settings->PlayerScreenOffset);
	OutResult.AspectRatioAxisConstraint = AspectRatio_MaintainXFOV;
	// UE 5.8 auto planes copy CameraToViewTarget into projection data; near-plane correction then
	// translates the view sideways back onto the pawn. Calculate adaptive clipping here, around
	// the optical target instead, and prevent the later projection pass from recentring on the pawn.
	OutResult.SetCameraToViewTarget(OutResult.Location + OutResult.Rotation.Vector() * CameraBoom->TargetArmLength);
	FSceneViewProjectionData ClipData;
	ClipData.SetViewRectangle(FIntRect(0, 0, ViewSize.X, ViewSize.Y));
	OutResult.AutoCalculateOrthoPlanes(ClipData);
	OutResult.bAutoCalculateOrthoPlanes = false;
}

void ABrCharacter::OnRep_IsCrouched()
{
	// Both low postures use native crouch; use the replicated enum to avoid
	// capsule changes depending on component/actor replication order.
	GetBrMovement()->ApplyReplicatedPosture();
}

void ABrCharacter::OnStartCrouch(float HeightAdjust, float ScaledHeightAdjust)
{
	Super::OnStartCrouch(HeightAdjust, ScaledHeightAdjust);
	// Native callbacks restore CDO-based translation, including on late posture
	// replication/corrections while downed. Keep eye height and Blueprint events,
	// then restore both the life pose and the offsets used by network smoothing.
	ApplyLifeMeshPose();
}

void ABrCharacter::OnEndCrouch(float HeightAdjust, float ScaledHeightAdjust)
{
	Super::OnEndCrouch(HeightAdjust, ScaledHeightAdjust);
	ApplyLifeMeshPose();
}

void ABrCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	ServiceMeshPoseSmoke(DeltaSeconds);
#endif
	if (Life->State == EBrLifeState::Downed)
	{
		HelpText->SetText(FText::FromString(FString::Printf(TEXT("HELP! %.0fs"),
			FMath::Max(0.0f, Life->DownedEndTime - Life->ServerTime()))));
		HelpText->SetRelativeScale3D(FVector(1.0f + 0.15f * FMath::Sin(Life->ServerTime() * 5)));
	}
	GetMesh()->SetVisibility(Life->State != EBrLifeState::Eliminated &&
		(!Life->IsInvulnerable() || FMath::Fmod(Life->ServerTime(), 0.3f) < 0.2f));
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	// Applied to every character so spectators viewing through this camera get the same tilt.
	// Reuse the existing character tick solely to gate the cheap cadence timer.
	const bool bStepping = (HasAuthority() || IsLocallyControlled()) && Life->IsAlive() && GetPosture() != EBrPosture::Prone &&
		!GetCharacterMovement()->IsFalling() && GetVelocity().SizeSquared2D() >=
		FMath::Square(FMath::Max(1.f, Settings->FootstepMinimumSpeed));
	FTimerManager& Timers = GetWorld()->GetTimerManager();
	if (bStepping && !Timers.IsTimerActive(FootstepTimer))
	{
		LastFootstepService = GetWorld()->GetTimeSeconds();
		Timers.SetTimer(FootstepTimer, this, &ABrCharacter::ServiceFootsteps,
			FMath::Clamp(Settings->FootstepServiceInterval, 0.05f, 0.5f), true);
	}
	else if (!bStepping && Timers.IsTimerActive(FootstepTimer))
	{
		Timers.ClearTimer(FootstepTimer);
		FootstepProgress = 0;
	}
	CameraBoom->SetWorldRotation(FRotator(-FMath::Clamp(Settings->CameraPitchAngle, 30.0f, 90.0f), GetCameraYaw(), 0));
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	TargetOrthoWidth = ClampCameraWidth(TargetOrthoWidth);
	TopDownCamera->SetOrthoWidth(ClampCameraWidth(FMath::FInterpTo(
		TopDownCamera->OrthoWidth, TargetOrthoWidth, DeltaSeconds, FMath::Max(0.0f, Settings->ZoomInterpSpeed))));
}

void ABrCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	LoadedInputConfig = UBrDeveloperSettings::Get()->InputConfig.LoadSynchronous();
	if (!Input || !LoadedInputConfig || !LoadedInputConfig->MoveAction || !LoadedInputConfig->WalkAction ||
		!LoadedInputConfig->CrouchAction || !LoadedInputConfig->ProneAction)
	{
		UE_LOG(LogBr, Error, TEXT("Cannot bind Br movement input: Enhanced Input or movement actions are missing."));
		return;
	}
	Input->BindAction(LoadedInputConfig->MoveAction, ETriggerEvent::Triggered, this, &ABrCharacter::Move);
	if (LoadedInputConfig->LookAction)
		Input->BindAction(LoadedInputConfig->LookAction, ETriggerEvent::Triggered, this, &ABrCharacter::Look);
	else UE_LOG(LogBr, Error, TEXT("Br Look action missing; regenerate Br input assets."));
	Input->BindAction(LoadedInputConfig->WalkAction, ETriggerEvent::Started, this, &ABrCharacter::WalkStarted);
	Input->BindAction(LoadedInputConfig->WalkAction, ETriggerEvent::Completed, this, &ABrCharacter::WalkStopped);
	Input->BindAction(LoadedInputConfig->WalkAction, ETriggerEvent::Canceled, this, &ABrCharacter::WalkStopped);
	Input->BindAction(LoadedInputConfig->CrouchAction, ETriggerEvent::Started, this, &ABrCharacter::ToggleCrouch);
	Input->BindAction(LoadedInputConfig->ProneAction, ETriggerEvent::Started, this, &ABrCharacter::ToggleProne);
	if (LoadedInputConfig->ZoomAction)
	{
		Input->BindAction(LoadedInputConfig->ZoomAction, ETriggerEvent::Triggered, this, &ABrCharacter::Zoom);
	}
	else
	{
		UE_LOG(LogBr, Error, TEXT("Br zoom input action is missing; regenerate the Br input assets."));
	}
	if (!LoadedInputConfig->FireAction || !LoadedInputConfig->ReloadAction || !LoadedInputConfig->ThrowGrenadeAction)
	{
		UE_LOG(LogBr, Error, TEXT("Br combat input actions are missing."));
		return;
	}
	Input->BindAction(LoadedInputConfig->FireAction, ETriggerEvent::Started, this, &ABrCharacter::FireStarted);
	Input->BindAction(LoadedInputConfig->FireAction, ETriggerEvent::Completed, this, &ABrCharacter::FireStopped);
	Input->BindAction(LoadedInputConfig->FireAction, ETriggerEvent::Canceled, this, &ABrCharacter::FireStopped);
	Input->BindAction(LoadedInputConfig->ReloadAction, ETriggerEvent::Started, this, &ABrCharacter::Reload);
	Input->BindAction(LoadedInputConfig->ThrowGrenadeAction, ETriggerEvent::Started, this, &ABrCharacter::ThrowGrenade);
	if (!LoadedInputConfig->InteractAction || !LoadedInputConfig->RespawnHoldAction)
	{
		UE_LOG(LogBr, Error, TEXT("Br life input actions are missing."));
		return;
	}
	Input->BindAction(LoadedInputConfig->InteractAction, ETriggerEvent::Started, this, &ABrCharacter::InteractStarted);
	Input->BindAction(LoadedInputConfig->InteractAction, ETriggerEvent::Completed, this, &ABrCharacter::InteractStopped);
	Input->BindAction(LoadedInputConfig->InteractAction, ETriggerEvent::Canceled, this, &ABrCharacter::InteractStopped);
	Input->BindAction(LoadedInputConfig->RespawnHoldAction, ETriggerEvent::Started, this, &ABrCharacter::RespawnStarted);
	Input->BindAction(LoadedInputConfig->RespawnHoldAction, ETriggerEvent::Completed, this, &ABrCharacter::RespawnStopped);
	Input->BindAction(LoadedInputConfig->RespawnHoldAction, ETriggerEvent::Canceled, this, &ABrCharacter::RespawnStopped);
}

void ABrCharacter::FireStarted() { if (!IsGameplayInputBlocked()) Inventory->StartFire(); }
void ABrCharacter::FireStopped() { Inventory->StopFire(); }
void ABrCharacter::Reload() { if (!IsGameplayInputBlocked()) Inventory->RequestReload(); }
void ABrCharacter::ThrowGrenade() { if (!IsGameplayInputBlocked()) Inventory->RequestGrenade(); }
void ABrCharacter::InteractStarted() { if (!IsGameplayInputBlocked()) Life->StartInteract(); }
void ABrCharacter::InteractStopped() { Life->StopInteract(); }
void ABrCharacter::RespawnStarted() { if (!IsGameplayInputBlocked()) Life->StartRespawnHold(); }
void ABrCharacter::RespawnStopped() { Life->StopRespawnHold(); }

bool ABrCharacter::CanMove() const { return !Life || Life->IsAlive(); }
bool ABrCharacter::IsGameplayInputBlocked() const
{
	const ABrPlayerController* PC = Cast<ABrPlayerController>(GetController());
	return PC && IsLocallyControlled() && PC->IsGameplayInputBlocked();
}
bool ABrCharacter::CanChangePosture() const { return CanMove(); }

void ABrCharacter::ApplyMeshPose(const FVector& Location, const FRotator& Rotation)
{
	CacheInitialMeshOffset(Location, Rotation);
	GetMesh()->SetRelativeLocationAndRotation(Location, Rotation);
}

void ABrCharacter::ApplyLifeMeshPose()
{
	const bool bAlive = !Life || Life->IsAlive();
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	ApplyMeshPose(FVector(0, 0, -HalfHeight + (bAlive ? 0 : 12)),
		FRotator(bAlive ? 0 : 90, -90, 0));
}

void ABrCharacter::ApplyLifePresentation()
{
	const bool bAlive = Life->IsAlive();
	if (bAlive) RequestAutoPickupRetry(); // BeginPlay, revive, respawn and restored Alive state.
	Inventory->StopFire();
	ConsumeMovementInputVector();
	GetCharacterMovement()->StopMovementImmediately();
	GetBrMovement()->SetWalking(false);
	bUseControllerRotationYaw = Life->State != EBrLifeState::Eliminated;
	HelpText->SetVisibility(Life->State == EBrLifeState::Downed);
	WeaponVisual->SetVisibility(bAlive);
	// Dead/dying bodies must not stop grenade movement or pawn movement.
	GetCapsuleComponent()->SetCollisionEnabled(bAlive ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (!bAlive)
	{
		GetCharacterMovement()->DisableMovement();
		GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		GetMesh()->SetAnimation(nullptr);
		ApplyLifeMeshPose();
	}
	else
	{
		if (GetCharacterMovement()->MovementMode == MOVE_None) GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		// Native crouch callbacks keep the mesh grounded for both low postures.
		ApplyLifeMeshPose();
		UClass* AnimClass = UBrDeveloperSettings::Get()->DefaultCharacterAnimClass.LoadSynchronous();
		if (AnimClass) GetMesh()->SetAnimInstanceClass(AnimClass);
		else UE_LOG(LogBr, Warning, TEXT("Life presentation: unarmed animation class is missing."));
	}
	GetMesh()->SetVisibility(Life->State != EBrLifeState::Eliminated);
}

float ABrCharacter::GetWeaponRunSpeedMultiplier() const
{
	return Inventory && Inventory->Weapon ? FMath::Clamp(Inventory->Weapon->RunSpeedMultiplier, 0.1f, 1.0f) : 1.0f;
}

void ABrCharacter::UpdateWeaponVisual()
{
	WeaponVisual->SetStaticMesh(Inventory->Weapon ? Inventory->Weapon->Mesh.Get() : nullptr);
	if (Inventory->Weapon && !Inventory->Weapon->Mesh)
	{
		UE_LOG(LogBr, Warning, TEXT("Weapon has no cosmetic mesh: %s"), *GetNameSafe(Inventory->Weapon));
	}
}

void ABrCharacter::PlayTracer(const FVector& Origin, const FVector& Direction, float Speed, float Range)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	if (!UBrFogComponent::LocallyVisible(this)) return;
	if (ABrCombatVisual* Visual = GetWorld()->SpawnActor<ABrCombatVisual>(Origin, Direction.Rotation()))
	{
		Visual->InitTracer(Direction, Speed, Range);
	}
}

void ABrCharacter::MulticastTracer_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction, float Speed, float Range)
{
	// Owning client already predicted the cosmetic; no gameplay is predicted.
	if (!IsLocallyControlled())
	{
		PlayTracer(Origin, Direction, Speed, Range);
	}
}

void ABrCharacter::ServiceFootsteps()
{
	const double Now = GetWorld()->GetTimeSeconds();
	const float Delta = FMath::Min(static_cast<float>(Now - LastFootstepService), 0.5f);
	LastFootstepService = Now;
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	const float Speed = GetVelocity().Size2D();
	const float Interval = FBrAudioRules::StepInterval(Speed, S->FootstepStride, S->FootstepMinimumSpeed);
	if (!Life->IsAlive() || GetPosture() == EBrPosture::Prone || Interval <= 0 ||
		GetCharacterMovement()->IsFalling())
	{
		FootstepProgress = 0;
		return;
	}
	FootstepProgress += Delta / Interval;
	if (FootstepProgress < 1) return;
	FootstepProgress = FMath::Fmod(FootstepProgress, 1.f);
	if (UBrAudioSubsystem* Audio = GetWorld()->GetSubsystem<UBrAudioSubsystem>())
	{
		const EBrSoundKind Kind = FBrAudioRules::StepKind(GetPosture(), GetBrMovement()->IsWalking());
		const TSoftObjectPtr<USoundBase> Sound = Audio->StepSound(Kind);
		if (HasAuthority()) Audio->Emit(this, Kind, Sound, GetActorLocation());
		// Only owners predict footsteps. Non-owners (including the listen host)
		// receive the server's positional batch, never a replicated-pawn cadence.
		if (GetNetMode() != NM_DedicatedServer && IsLocallyControlled())
			Audio->Play(Kind, Sound, GetActorLocation());
	}
}

float ABrCharacter::TakeDamage(float Damage, const FDamageEvent& Event, AController* InstigatorController, AActor* Causer)
{
	if (!HasAuthority() || !FMath::IsFinite(Damage) || Damage <= 0 || Health->Health <= 0) return 0;
	ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
	if (!Mode)
	{
		UE_LOG(LogBr, Error, TEXT("Br damage rejected: authoritative BrGameMode is missing."));
		return 0;
	}
	const FBrDamageSource Source = Event.IsOfType(FBrDamageEvent::ClassID) ?
		static_cast<const FBrDamageEvent&>(Event).Source : Mode->CaptureDamageSource(InstigatorController);
	if (Event.IsOfType(FBrDamageEvent::ClassID) && !Source.IsValid()) return 0;
	const bool bZone = Event.DamageTypeClass && Event.DamageTypeClass->IsChildOf(UBrZoneDamageType::StaticClass());
	if (!Mode->CanDamageSource(Source, this, bZone)) return 0;
	const bool bBullet = Event.DamageTypeClass && Event.DamageTypeClass->IsChildOf(UBrBulletDamageType::StaticClass());
	const bool bExplosion = Event.DamageTypeClass && Event.DamageTypeClass->IsChildOf(UBrExplosionDamageType::StaticClass());
	const ABrPlayerState* VictimPS = GetPlayerState<ABrPlayerState>();
	const bool bSelf = VictimPS && Source.IsSelf(VictimPS->GetPlayerId());
	if (bBullet && (bSelf || Causer == this)) return 0;
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	if (bExplosion && bSelf && !Settings->bGrenadeSelfDamage) return 0;
	const float Adjusted = bZone ? Damage : FBrCombatMath::Damage(Damage, GetPosture(), bExplosion, Settings->bGrenadePostureDamage,
		Settings->StandingDamageMultiplier, Settings->CrouchingDamageMultiplier, Settings->ProneDamageMultiplier);
	const float Applied = Super::TakeDamage(Adjusted, Event, InstigatorController, Causer);
	if (Applied > 0 && Life->State == EBrLifeState::Downed && !bSelf && !bZone) Mode->CreditDown(Source);
	if (Applied > 0) Life->DamageReceived();
	return Applied;
}

void ABrCharacter::Move(const FInputActionValue& Value)
{
	if (!CanMove() || IsGameplayInputBlocked())
	{
		return;
	}
	const FVector Direction = FBrAimingRules::Movement(GetCameraYaw(), Value.Get<FVector2D>());
	AddMovementInput(Direction, Direction.Size());
}

void ABrCharacter::Look(const FInputActionValue& Value)
{
	if (!IsLocallyControlled() || !Controller || IsGameplayInputBlocked() ||
		Life->State == EBrLifeState::Eliminated) return;
	const UBrGameInstance* GI = GetGameInstance<UBrGameInstance>();
	const float Sensitivity = GI ? GI->GetMouseSensitivity() : UBrDeveloperSettings::Get()->DefaultMouseSensitivity;
	const FRotator Rotation(0, FBrAimingRules::TurnYaw(Controller->GetControlRotation().Yaw, Value.Get<float>(), Sensitivity), 0);
	Controller->SetControlRotation(Rotation);
	FaceRotation(Rotation); // Same-frame facing; CMC carries control yaw to authority, also in MOVE_None.
}

void ABrCharacter::WalkStarted() { if (CanMove() && !IsGameplayInputBlocked()) GetBrMovement()->SetWalking(true); }
void ABrCharacter::WalkStopped() { GetBrMovement()->SetWalking(false); }

void ABrCharacter::ToggleCrouch()
{
	if (CanChangePosture() && !IsGameplayInputBlocked())
	{
		GetBrMovement()->RequestPosture(GetPosture() == EBrPosture::Crouching ? EBrPosture::Standing : EBrPosture::Crouching);
	}
}

void ABrCharacter::ToggleProne()
{
	if (CanChangePosture() && !IsGameplayInputBlocked())
	{
		GetBrMovement()->RequestPosture(GetPosture() == EBrPosture::Prone ? EBrPosture::Standing : EBrPosture::Prone);
	}
}

void ABrCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	InitializeCameraZoom();
}

float ABrCharacter::ClampCameraWidth(float Width) const
{
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	const float Minimum = FMath::Max(1.0f, Settings->MinOrthoWidth);
	const float Maximum = FMath::Max(Minimum, Settings->MaxOrthoWidth);
	return FMath::Clamp(Width, Minimum, Maximum);
}

void ABrCharacter::InitializeCameraZoom()
{
	TargetOrthoWidth = ClampCameraWidth(OrthoWidth);
	TopDownCamera->SetOrthoWidth(TargetOrthoWidth);
}

void ABrCharacter::Zoom(const FInputActionValue& Value)
{
	const float WheelAxis = Value.Get<float>();
	if (IsLocallyControlled() && !IsGameplayInputBlocked() && FMath::IsFinite(WheelAxis))
	{
		TargetOrthoWidth = ClampCameraWidth(TargetOrthoWidth -
			WheelAxis * FMath::Max(0.0f, UBrDeveloperSettings::Get()->ZoomStep));
	}
}

void ABrCharacter::BeginPlay()
{
	Super::BeginPlay();
	LastFootstepService = GetWorld()->GetTimeSeconds();
	UE_LOG(LogBr, Display, TEXT("BrCharacter BeginPlay: Name=%s Authority=%s Controller=%s"),
		*GetName(), HasAuthority() ? TEXT("true") : TEXT("false"), *GetNameSafe(GetController()));

	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	if (Settings->MinOrthoWidth < 1.0f || Settings->MaxOrthoWidth < Settings->MinOrthoWidth)
	{
		UE_LOG(LogBr, Warning, TEXT("Invalid camera zoom limits; using a positive, ordered width range."));
	}
	USkeletalMesh* CharacterMesh = Settings->DefaultCharacterMesh.LoadSynchronous();
	if (CharacterMesh)
	{
		GetMesh()->SetSkeletalMesh(CharacterMesh);

		UClass* AnimClass = Settings->DefaultCharacterAnimClass.LoadSynchronous();
		if (AnimClass)
		{
			GetMesh()->SetAnimInstanceClass(AnimClass);
		}
		else
		{
			UE_LOG(LogBr, Warning, TEXT("Character animation is unset or failed to load: %s. Using reference pose."),
				*Settings->DefaultCharacterAnimClass.ToString());
		}
	}
	else
	{
		UE_LOG(LogBr, Warning, TEXT("Character mesh is unset or failed to load: %s. Character will have no visual mesh."),
			*Settings->DefaultCharacterMesh.ToString());
	}

	InitializeCameraZoom();
	UpdateCameraActivation();
	ApplyLifePresentation();
	ApplyTeamColor();
}

void ABrCharacter::Restart()
{
	Super::Restart();
	UpdateCameraActivation();
}

void ABrCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	UpdateCameraActivation();
}

void ABrCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	ApplyTeamColor();
	RequestAutoPickupRetry(); // Initial overlaps can happen before possession.
}

void ABrCharacter::RequestAutoPickupRetry()
{
	if (!HasAuthority() || !GetWorld() || GetWorld()->bIsTearingDown ||
		IsActorBeingDestroyed() || bAutoPickupRetryQueued) return;
	bAutoPickupRetryQueued = true;
	AutoPickupRetryTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ABrCharacter::RetryAutoPickups);
}

void ABrCharacter::RetryAutoPickups()
{
	bAutoPickupRetryQueued = false;
	if (!HasAuthority() || !HasActorBegunPlay() || IsActorBeingDestroyed() ||
		!GetController() || !Life->IsAlive() || Health->Health <= 0) return;
	TArray<AActor*> OverlappingPickups;
	GetCapsuleComponent()->GetOverlappingActors(OverlappingPickups, ABrPickup::StaticClass());
	for (AActor* Actor : OverlappingPickups)
	{
		ABrPickup* Pickup = Cast<ABrPickup>(Actor);
		if (IsValid(Pickup)) Pickup->TryAutoCollect(this); // Preserve availability/application/re-entrancy guards.
	}
}

void ABrCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorld()->GetTimerManager().ClearTimer(FootstepTimer);
	GetWorld()->GetTimerManager().ClearTimer(AutoPickupRetryTimer);
	bAutoPickupRetryQueued = false;
	Super::EndPlay(EndPlayReason);
}

void ABrCharacter::UpdateCameraActivation()
{
	TopDownCamera->SetActive(IsLocallyControlled());
}

void ABrCharacter::TopDownCameraActivate() { TopDownCamera->SetActive(true); }
bool ABrCharacter::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const
{
	if (const UBrVisibilitySubsystem* Visibility = GetWorld()->GetSubsystem<UBrVisibilitySubsystem>())
		if (UBrVisibilitySubsystem::Active(GetWorld())) return Visibility->Relevant(this, RealViewer);
	const ABrPlayerController* Spectator = Cast<ABrPlayerController>(RealViewer);
	if (Spectator && Spectator->IsFlowSpectating() &&
		(Spectator->GetFlowSpectatorPawn() == this || Spectator->GetSpectatorTarget() == GetPlayerState<ABrPlayerState>()))
	{
		return true;
	}
	return Super::IsNetRelevantFor(RealViewer, ViewTarget, SrcLocation);
}
void ABrCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	UE_LOG(LogBr, Verbose, TEXT("Fog pawn binding: Pawn=%s PS=%s Player=%s"),
		*GetName(), *GetNameSafe(GetPlayerState()), GetPlayerState() ? *GetPlayerState()->GetPlayerName() : TEXT("None"));
	ApplyTeamColor();
}
void ABrCharacter::ApplyTeamColor()
{
	if (!HasActorBegunPlay() || !GetMesh()->GetSkeletalMeshAsset()) return;
	const ABrPlayerState* PS = GetPlayerState<ABrPlayerState>();
	const FLinearColor Color = PS ? PS->TeamColor : FBrFlowRules::MilitaryGreen();
	if (TeamMaterials.IsEmpty())
	{
		for (int32 I = 0; I < GetMesh()->GetNumMaterials(); ++I)
			if (UMaterialInstanceDynamic* Material = GetMesh()->CreateAndSetMaterialInstanceDynamic(I)) TeamMaterials.Add(Material);
		if (TeamMaterials.IsEmpty()) UE_LOG(LogBr, Error, TEXT("No mannequin materials available for team color"));
	}
	for (UMaterialInstanceDynamic* Material : TeamMaterials)
		if (Material) Material->SetVectorParameterValue(UBrDeveloperSettings::Get()->TeamColorParameter, Color);
}
