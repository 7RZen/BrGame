#include "BrInventoryComponent.h"
#include "BrSoundEventRules.h"
#include "BrPlayerController.h"
#include "BrAudioSubsystem.h"
#include "BrCharacter.h"
#include "BrCharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "BrCombatMath.h"
#include "BrDeveloperSettings.h"
#include "BrGrenade.h"
#include "BrHealthComponent.h"
#include "BrLog.h"
#include "BrLifeComponent.h"
#include "BrGameMode.h"
#include "BrGameState.h"
#include "BrPickupRules.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UBrInventoryComponent::UBrInventoryComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UBrInventoryComponent::BeginPlay()
{
	Super::BeginPlay();
	if (ABrCharacter* Character = Cast<ABrCharacter>(GetOwner()))
	{
		Character->GetHealth()->OnHealthDepleted.AddDynamic(this, &UBrInventoryComponent::HealthDepleted);
	}
	if (GetOwner()->HasAuthority() && !bRestoredState)
	{
		const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
		Throwable = Settings->HandGrenade.LoadSynchronous();
		if (!Throwable) UE_LOG(LogBr, Error, TEXT("Hand grenade definition missing: %s"), *Settings->HandGrenade.ToString());
		if (!GiveWeapon(Settings->StartingWeapon.LoadSynchronous()))
		{
			UE_LOG(LogBr, Error, TEXT("Starting weapon could not be equipped: %s"), *Settings->StartingWeapon.ToString());
		}
	}
}

void UBrInventoryComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	CancelCombat();
	CancelReload();
	Bullets.Empty();
	if (ABrCharacter* Character = Cast<ABrCharacter>(GetOwner()))
	{
		Character->GetHealth()->OnHealthDepleted.RemoveDynamic(this, &UBrInventoryComponent::HealthDepleted);
	}
	Super::EndPlay(Reason);
}

void UBrInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBrInventoryComponent, Weapon);
	DOREPLIFETIME_CONDITION(UBrInventoryComponent, Throwable, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UBrInventoryComponent, Loaded, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UBrInventoryComponent, Ammo, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UBrInventoryComponent, Grenades, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UBrInventoryComponent, bReloading, COND_OwnerOnly);
}

bool UBrInventoryComponent::IsAlive() const
{
	const ABrCharacter* Character = Cast<ABrCharacter>(GetOwner());
	return Character && Character->GetController() && Character->GetLife()->IsAlive() && Character->GetHealth()->Health > 0;
}

int32 UBrInventoryComponent::GetReserve() const
{
	return Weapon ? FBrAmmoAccounting::Reserve(Ammo, Weapon->AmmoType) : 0;
}

void UBrInventoryComponent::AddAmmo(UBrAmmoDefinition* Type, int32 Count)
{
	if (Type) FBrAmmoAccounting::Add(Ammo, Type, FBrPickupRules::AmmoGrant(Count,
		FBrAmmoAccounting::Reserve(Ammo, Type), Type->ReserveCap));
}

void UBrInventoryComponent::GiveAmmo(int32 Count)
{
	if (GetOwner()->HasAuthority() && Weapon) AddAmmo(Weapon->AmmoType, Count);
}

void UBrInventoryComponent::GiveGrenades(int32 Count)
{
	if (GetOwner()->HasAuthority() && Throwable && Count > 0)
	{
		Grenades = static_cast<int32>(FMath::Clamp<int64>(static_cast<int64>(Grenades) + Count, 0, FMath::Max(0, Throwable->MaxCarried)));
	}
}

bool UBrInventoryComponent::GiveWeapon(UBrWeaponDefinition* Definition)
{
	return EquipWeapon(Definition, Definition ? Definition->MagazineSize : 0,
		Definition ? FBrCombatMath::GrantReserve(Definition->MagazineSize, Definition->StartingMagazines) : 0);
}

bool UBrInventoryComponent::IsValidWeaponDefinition(const UBrWeaponDefinition* Definition)
{
	return Definition && Definition->AmmoType && Definition->MagazineSize > 0 &&
		Definition->StartingMagazines > 0 && FMath::IsFinite(Definition->FireRate) &&
		Definition->FireRate > 0 && FMath::IsFinite(Definition->ReloadTime) && Definition->ReloadTime > 0 &&
		FMath::IsFinite(Definition->ProjectileSpeed) && Definition->ProjectileSpeed > 0 &&
		FMath::IsFinite(Definition->MaxRange) && Definition->MaxRange > 0;
}

bool UBrInventoryComponent::EquipWeapon(UBrWeaponDefinition* Definition, int32 LoadedRounds, int32 ReserveRounds)
{
	if (!GetOwner()->HasAuthority()) return false;
	if (!IsValidWeaponDefinition(Definition))
	{
		UE_LOG(LogBr, Error, TEXT("Invalid weapon definition: %s"), *GetNameSafe(Definition));
		return false;
	}
	CancelReload();
	CancelCombat();
	Weapon = Definition;
	// Shared reserve is kept. The caller decides whether this is a fresh grant or a dropped instance.
	Loaded = FMath::Clamp(LoadedRounds, 0, Definition->MagazineSize);
	AddAmmo(Definition->AmmoType, ReserveRounds);
	OnRep_Weapon();
	UE_LOG(LogBr, Display, TEXT("Equip: Player=%s Weapon=%s Loaded=%d Reserve=%d"),
		*GetNameSafe(GetOwner()), *Definition->DisplayName.ToString(), Loaded, GetReserve());
	return true;
}

FBrInventoryState UBrInventoryComponent::CaptureState() const
{
	FBrInventoryState State;
	State.Weapon = Weapon;
	State.Loaded = Loaded;
	State.Ammo = Ammo;
	State.Throwable = Throwable;
	State.Grenades = Grenades;
	return State;
}

void UBrInventoryComponent::RestoreState(const FBrInventoryState& State)
{
	if (!GetOwner()->HasAuthority()) return;
	bRestoredState = true;
	CancelReload();
	CancelCombat();
	Weapon = State.Weapon;
	Loaded = FMath::Clamp(State.Loaded, 0, Weapon ? FMath::Max(0, Weapon->MagazineSize) : 0);
	Ammo.Empty();
	for (const FBrAmmoPool& Pool : State.Ammo) AddAmmo(Pool.AmmoType, Pool.Count);
	Throwable = State.Throwable;
	Grenades = FMath::Clamp(State.Grenades, 0, Throwable ? FMath::Max(0, Throwable->MaxCarried) : 0);
	OnRep_Weapon();
}

void UBrInventoryComponent::OnRep_Weapon()
{
	CancelCombat();
	if (ABrCharacter* Character = Cast<ABrCharacter>(GetOwner()))
	{
		Character->UpdateWeaponVisual();
		Character->RequestAutoPickupRetry(); // Also covers inventory snapshot restoration.
	}
}

bool UBrInventoryComponent::ValidAim(const FVector& Aim)
{
	return !Aim.ContainsNaN() && FMath::Abs(Aim.Z) <= 0.01f &&
		FMath::IsNearlyEqual(Aim.SizeSquared(), 1.0, 0.02);
}

void UBrInventoryComponent::StartFire()
{
	ABrCharacter* Character = Cast<ABrCharacter>(GetOwner());
	if (!IsAlive() || !Character || !Character->IsLocallyControlled() || Character->IsGameplayInputBlocked()) return;
	bFireHeld = true;
	Character->GetBrMovement()->CancelSprintForCombat();
	UpdateCombatIntent();
	const float Delay = FMath::Max(0.f, static_cast<float>(Character->GetBrMovement()->GetSprintFireReadyTime() - GetWorld()->GetTimeSeconds()));
	if (Delay > 0)
	{
		if (!bPendingFire)
		{
			bPendingFire = true;
			GetWorld()->GetTimerManager().SetTimer(SprintFireTimer, this, &UBrInventoryComponent::DeferredFire, Delay, false);
		}
		return;
	}
	AttemptFire();
	if (Weapon && Weapon->bAutomatic)
	{
		GetWorld()->GetTimerManager().SetTimer(FireTimer, this, &UBrInventoryComponent::AttemptFire,
			1.0f / FMath::Max(0.1f, Weapon->FireRate), true);
	}
}

void UBrInventoryComponent::StopFire()
{
	GetWorld()->GetTimerManager().ClearTimer(FireTimer);
	bFireHeld = false;
	UpdateCombatIntent();
}

void UBrInventoryComponent::CancelCombat()
{
	bPendingFire = bPendingGrenade = false;
	GetWorld()->GetTimerManager().ClearTimer(SprintFireTimer);
	GetWorld()->GetTimerManager().ClearTimer(SprintGrenadeTimer);
	ClearServerPendingCombat();
	StopFire();
	// Remote menus/downing/gear changes must cancel the authority's queued action,
	// not only the already-expired local recovery timer. StopFire (tap release)
	// intentionally does NOT cancel an accepted pending single shot.
	const ABrCharacter* Character = Cast<ABrCharacter>(GetOwner());
	if (Character && Character->IsLocallyControlled()) ServerCancelCombat();
}

void UBrInventoryComponent::UpdateCombatIntent()
{
	const ABrCharacter* Character = Cast<ABrCharacter>(GetOwner());
	if (Character && Character->IsLocallyControlled())
		ServerCombatIntent(bFireHeld || bPendingFire || bPendingGrenade);
}

void UBrInventoryComponent::ServerCombatIntent_Implementation(bool bHeld)
{
	bServerCombatHeld = bHeld;
	if (bHeld)
		if (ABrCharacter* Character = Cast<ABrCharacter>(GetOwner())) Character->GetBrMovement()->CancelSprintForCombat();
}

void UBrInventoryComponent::ClearServerPendingCombat()
{
	PendingServerFire.Reset();
	PendingServerGrenade.Reset();
	GetWorld()->GetTimerManager().ClearTimer(PendingServerFireTimer);
	GetWorld()->GetTimerManager().ClearTimer(PendingServerGrenadeTimer);
	if (GetOwner()->HasAuthority()) bServerCombatHeld = false;
}

void UBrInventoryComponent::ServerCancelCombat_Implementation()
{
	ClearServerPendingCombat();
}

bool UBrInventoryComponent::CombatRequestsAllowed() const
{
	const ABrCharacter* Character = Cast<ABrCharacter>(GetOwner());
	const ABrGameState* State = GetWorld()->GetGameState<ABrGameState>();
	return IsAlive() && Character && !Character->IsGameplayInputBlocked() &&
		(!State || (!State->bLobby && !State->bResults));
}

void UBrInventoryComponent::QueueSprintAction(bool bGrenade, const FVector& Aim, float MoveTimeStamp)
{
	TOptional<FPendingSprintAction>& Pending = bGrenade ? PendingServerGrenade : PendingServerFire;
	if (Pending.IsSet()) return;
	FPendingSprintAction Action;
	Action.Aim = Aim;
	Action.MoveTimeStamp = MoveTimeStamp;
	Action.Definition = bGrenade ? static_cast<UObject*>(Throwable) : static_cast<UObject*>(Weapon);
	Pending = Action;
	const ABrCharacter* Character = CastChecked<ABrCharacter>(GetOwner());
	const float Delay = FMath::Max(UE_KINDA_SMALL_NUMBER, static_cast<float>(
		Character->GetBrMovement()->GetSprintFireReadyTime() - GetWorld()->GetTimeSeconds()));
	GetWorld()->GetTimerManager().SetTimer(bGrenade ? PendingServerGrenadeTimer : PendingServerFireTimer,
		this, bGrenade ? &UBrInventoryComponent::ServicePendingGrenade : &UBrInventoryComponent::ServicePendingFire, Delay, false);
}

void UBrInventoryComponent::ServicePendingFire()
{
	if (!PendingServerFire.IsSet()) return;
	const FPendingSprintAction Action = PendingServerFire.GetValue();
	PendingServerFire.Reset();
	GetWorld()->GetTimerManager().ClearTimer(PendingServerFireTimer);
	if (CombatRequestsAllowed() && Action.Definition.IsValid() && Action.Definition.Get() == Weapon)
		ServerFire_Implementation(Action.Aim, Action.MoveTimeStamp);
}

void UBrInventoryComponent::ServicePendingGrenade()
{
	if (!PendingServerGrenade.IsSet()) return;
	const FPendingSprintAction Action = PendingServerGrenade.GetValue();
	PendingServerGrenade.Reset();
	GetWorld()->GetTimerManager().ClearTimer(PendingServerGrenadeTimer);
	if (CombatRequestsAllowed() && Action.Definition.IsValid() && Action.Definition.Get() == Throwable)
		ServerGrenade_Implementation(Action.Aim, Action.MoveTimeStamp);
}

void UBrInventoryComponent::DeferredFire()
{
	const ABrCharacter* Character = Cast<ABrCharacter>(GetOwner());
	if (!Character || !IsAlive() || Character->IsGameplayInputBlocked()) { CancelCombat(); return; }
	const float Remaining = static_cast<float>(Character->GetBrMovement()->GetSprintFireReadyTime() - GetWorld()->GetTimeSeconds());
	if (Remaining > UE_KINDA_SMALL_NUMBER)
	{
		// A genuine newer stop (not replay) may have established another recovery.
		// Keep a quick tap pending instead of clearing it before AttemptFire can run.
		GetWorld()->GetTimerManager().SetTimer(SprintFireTimer, this, &UBrInventoryComponent::DeferredFire, Remaining, false);
		return;
	}
	const bool bContinue = bFireHeld;
	bPendingFire = false;
	if (bContinue) StartFire();
	else AttemptFire(); // A quick tap still produces the deferred single shot.
	UpdateCombatIntent();
}

void UBrInventoryComponent::AttemptFire()
{
	ABrCharacter* Character = Cast<ABrCharacter>(GetOwner());
	if (!Character || !Character->IsLocallyControlled() || Character->IsGameplayInputBlocked() || !IsAlive() || !Weapon || bReloading) return;
	const double Now = GetWorld()->GetTimeSeconds();
	if (!Character->GetBrMovement()->CanSprintFire()) return;
	if (!FBrCombatMath::CanFire(Now, NextLocalFire)) return;
	NextLocalFire = FBrCombatMath::NextFireTime(Now, NextLocalFire, Weapon->FireRate);
	const FVector Aim = FRotator(0, Character->GetControlRotation().Yaw, 0).Vector();
	if (Loaded > 0 && GetNetMode() != NM_DedicatedServer)
	{
		Character->PlayTracer(Character->GetActorLocation(), Aim, Weapon->ProjectileSpeed, Weapon->MaxRange);
		if (UBrAudioSubsystem* Audio = GetWorld()->GetSubsystem<UBrAudioSubsystem>())
			Audio->Play(EBrSoundKind::Fire, Weapon->FireSound, Character->GetActorLocation());
	}
	ServerFire(Aim, Character->GetBrMovement()->GetCombatMoveTimeStamp());
}

void UBrInventoryComponent::ServerFire_Implementation(FVector_NetQuantizeNormal Aim, float MoveTimeStamp)
{
	if (const ABrCharacter* OwnerCharacter = Cast<ABrCharacter>(GetOwner()))
		if (ABrPlayerController* PC = Cast<ABrPlayerController>(OwnerCharacter->GetController()))
			PC->CancelUnstuck(TEXT("Unstuck cancelled: firing."));
	if (!CombatRequestsAllowed() || !Weapon || bReloading || !ValidAim(Aim)) return;
	ABrCharacter* Character = CastChecked<ABrCharacter>(GetOwner());
	if (!Character->GetBrMovement()->IsValidCombatMoveTimeStamp(MoveTimeStamp)) return;
	if (!Character->GetBrMovement()->CanSprintFire())
	{
		// Active sprint without a stop/intent is still rejected. Once authority
		// has stopped, a valid action survives coalesced reliable delivery.
		if (!Character->GetBrMovement()->IsSprinting() && Loaded > 0 &&
			FBrCombatMath::CanFire(GetWorld()->GetTimeSeconds(), NextAllowedFire))
			QueueSprintAction(false, Aim, MoveTimeStamp);
		return;
	}
	if (PendingServerFire.IsSet()) return;
	const double Now = GetWorld()->GetTimeSeconds();
	if (!FBrCombatMath::CanFire(Now, NextAllowedFire)) return;
	if (Loaded <= 0)
	{
		if (FBrSoundEventRules::DryFire(Loaded, GetReserve()))
		{
			NextAllowedFire = FBrCombatMath::NextFireTime(Now, NextAllowedFire, Weapon->FireRate);
			if (UBrAudioSubsystem* Audio = GetWorld()->GetSubsystem<UBrAudioSubsystem>())
				Audio->Emit(Character, EBrSoundKind::DryFire, UBrDeveloperSettings::Get()->DryFireSound,
					Character->GetActorLocation(), nullptr, true);
		}
		else ServerReload_Implementation();
		return;
	}
	NextAllowedFire = FBrCombatMath::NextFireTime(Now, NextAllowedFire, Weapon->FireRate);
	--Loaded;
#if !UE_BUILD_SHIPPING
	LastSprintFireRecovery = Now - Character->GetBrMovement()->GetSprintFireReadyTime() +
		UBrDeveloperSettings::Get()->SprintToFireDelay;
	if (Character->GetBrMovement()->IsSliding() && FParse::Param(FCommandLine::Get(), TEXT("BrMatchSmoke")))
		UE_LOG(LogBr, Display, TEXT("E2E SLIDE FIRE PASS: accepted during authoritative slide Loaded=%d"), Loaded);
#endif
	Character->GetBrMovement()->ApplyAcceptedCombatAim(Aim, MoveTimeStamp);
	Character->GetLife()->Fired();
	Character->RequestAutoPickupRetry();
	FBrServerBullet& Bullet = Bullets.AddDefaulted_GetRef();
	// Center origin prevents a client/muzzle offset from spawning a shot beyond a wall.
	Bullet.Position = Character->GetActorLocation();
	Bullet.Origin = Bullet.Position;
	Bullet.Direction = FVector(Aim.X, Aim.Y, 0).GetSafeNormal();
	Bullet.Remaining = Weapon->MaxRange;
	Bullet.ShooterPosture = Character->GetPosture();
	Bullet.Definition = Weapon;
	if (const ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>())
		Bullet.Source = Mode->CaptureDamageSource(Character->GetController());
	Bullet.Ignored.Add(Character);
	SetComponentTickEnabled(true);
	if (UBrAudioSubsystem* Audio = GetWorld()->GetSubsystem<UBrAudioSubsystem>())
		Audio->Emit(Character, EBrSoundKind::Fire, Weapon->FireSound, Bullet.Position);
	Character->MulticastTracer(Bullet.Position, Bullet.Direction, Weapon->ProjectileSpeed, Weapon->MaxRange);
}

void UBrInventoryComponent::RequestReload() { ServerReload(); }

void UBrInventoryComponent::ServerReload_Implementation()
{
	if (!GetOwner()->HasAuthority() || !CombatRequestsAllowed() || !IsAlive() || !Weapon || bReloading ||
		FBrCombatMath::ReloadRounds(Loaded, Weapon->MagazineSize, GetReserve()) == 0) return;
	bReloading = true;
	if (UBrAudioSubsystem* Audio = GetWorld()->GetSubsystem<UBrAudioSubsystem>())
		ReloadSoundId = Audio->StartReload(GetOwner(), Weapon->ReloadSound);
	GetWorld()->GetTimerManager().SetTimer(ReloadTimer, this, &UBrInventoryComponent::FinishReload, Weapon->ReloadTime, false);
}

void UBrInventoryComponent::FinishReload()
{
	if (IsAlive() && Weapon)
	{
		const int32 Rounds = FBrCombatMath::ReloadRounds(Loaded, Weapon->MagazineSize, GetReserve());
		for (FBrAmmoPool& Pool : Ammo)
		{
			if (Pool.AmmoType == Weapon->AmmoType)
			{
				Pool.Count -= Rounds;
				Loaded += Rounds;
				if (Rounds > 0)
				{
					if (ABrCharacter* Character = Cast<ABrCharacter>(GetOwner())) Character->RequestAutoPickupRetry();
				}
				break;
			}
		}
	}
	bReloading = false;
	if (UBrAudioSubsystem* Audio = GetWorld()->GetSubsystem<UBrAudioSubsystem>()) Audio->ForgetReload(ReloadSoundId);
	ReloadSoundId = 0;
}

void UBrInventoryComponent::CancelReload()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReloadTimer);
		if (ReloadSoundId != 0)
			if (UBrAudioSubsystem* Audio = World->GetSubsystem<UBrAudioSubsystem>()) Audio->StopReload(GetOwner(), ReloadSoundId);
	}
	ReloadSoundId = 0;
	bReloading = false;
}

void UBrInventoryComponent::HealthDepleted()
{
	CancelCombat();
	if (GetOwner()->HasAuthority()) CancelReload();
}

void UBrInventoryComponent::RequestGrenade()
{
	ABrCharacter* Character = Cast<ABrCharacter>(GetOwner());
	if (!Character || !Character->IsLocallyControlled() || Character->IsGameplayInputBlocked() || !IsAlive() || bPendingGrenade) return;
	Character->GetBrMovement()->CancelSprintForCombat();
	bPendingGrenade = true;
	UpdateCombatIntent();
	const float Delay = FMath::Max(0.f, static_cast<float>(Character->GetBrMovement()->GetSprintFireReadyTime() - GetWorld()->GetTimeSeconds()));
	if (Delay > 0)
		GetWorld()->GetTimerManager().SetTimer(SprintGrenadeTimer, this, &UBrInventoryComponent::SendGrenade, Delay, false);
	else SendGrenade();
}

void UBrInventoryComponent::SendGrenade()
{
	const ABrCharacter* Character = Cast<ABrCharacter>(GetOwner());
	if (Character && !Character->IsGameplayInputBlocked() && IsAlive())
		ServerGrenade(FRotator(0, Character->GetControlRotation().Yaw, 0).Vector(),
			Character->GetBrMovement()->GetCombatMoveTimeStamp());
	bPendingGrenade = false;
	UpdateCombatIntent();
}

void UBrInventoryComponent::ServerGrenade_Implementation(FVector_NetQuantizeNormal Aim, float MoveTimeStamp)
{
	if (const ABrCharacter* OwnerCharacter = Cast<ABrCharacter>(GetOwner()))
		if (ABrPlayerController* PC = Cast<ABrPlayerController>(OwnerCharacter->GetController()))
			PC->CancelUnstuck(TEXT("Unstuck cancelled: throwing."));
	if (!CombatRequestsAllowed() || !Throwable || Grenades <= 0 || !ValidAim(Aim) ||
		GetWorld()->GetTimeSeconds() < NextGrenade) return;
	ABrCharacter* Character = CastChecked<ABrCharacter>(GetOwner());
	if (!Character->GetBrMovement()->IsValidCombatMoveTimeStamp(MoveTimeStamp)) return;
	if (!Character->GetBrMovement()->CanSprintFire())
	{
		if (!Character->GetBrMovement()->IsSprinting()) QueueSprintAction(true, Aim, MoveTimeStamp);
		return;
	}
	if (PendingServerGrenade.IsSet()) return;
	FActorSpawnParameters Params;
	Params.Owner = Character;
	Params.Instigator = Character;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ABrGrenade* Grenade = GetWorld()->SpawnActor<ABrGrenade>(ABrGrenade::StaticClass(),
		Character->GetActorLocation(), FRotator::ZeroRotator, Params);
	if (!Grenade)
	{
		UE_LOG(LogBr, Error, TEXT("Failed to spawn hand grenade for %s"), *Character->GetName());
		return;
	}
	if (!Grenade->Launch(Throwable, (FVector(Aim.X, Aim.Y, 0) + FVector(0, 0, 0.25f)).GetSafeNormal() * Throwable->ThrowSpeed))
	{
		Grenade->Destroy();
		return;
	}
	Character->GetBrMovement()->ApplyAcceptedCombatAim(Aim, MoveTimeStamp);
	Character->GetLife()->Fired();
	--Grenades;
	Character->RequestAutoPickupRetry();
	NextGrenade = GetWorld()->GetTimeSeconds() + 0.25;
}

void UBrInventoryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!GetOwner()->HasAuthority()) { SetComponentTickEnabled(false); return; }
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	ABrCharacter* Shooter = CastChecked<ABrCharacter>(GetOwner());
	for (int32 Index = Bullets.Num() - 1; Index >= 0; --Index)
	{
		FBrServerBullet& Bullet = Bullets[Index];
		const float Travel = FMath::Min(Bullet.Remaining, Bullet.Definition->ProjectileSpeed * DeltaTime);
		const FVector End = Bullet.Position + Bullet.Direction * Travel;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(BrBullet), false);
		for (const TWeakObjectPtr<AActor>& Ignored : Bullet.Ignored)
		{
			if (Ignored.IsValid()) Query.AddIgnoredActor(Ignored.Get());
		}
		bool bStopped = false;
		FHitResult Hit;
		// Re-query the entire segment after every miss, so thin/overlapping targets don't hide a wall or another player.
		while (GetWorld()->SweepSingleByObjectType(Hit, Bullet.Position, End, FQuat::Identity,
			Objects, FCollisionShape::MakeSphere(2), Query))
		{
			// Check at impact, not at fire time: the shooter can throw a grenade after this bullet starts.
			// Other players' grenades still block bullets; world-dynamic geometry remains blocking.
			ABrGrenade* Grenade = Cast<ABrGrenade>(Hit.GetActor());
			if (Grenade && (Grenade->GetOwner() == Shooter || Grenade->GetInstigator() == Shooter))
			{
				Bullet.Ignored.Add(Grenade);
				Query.AddIgnoredActor(Grenade);
				continue;
			}
			ABrCharacter* Target = Cast<ABrCharacter>(Hit.GetActor());
			if (Target)
			{
				const ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
				// Non-targets (downed, eliminated, protected, future friendly-fire policy) never consume a bullet.
				if (Mode && !Mode->CanDamageSource(Bullet.Source, Target))
				{
					Query.AddIgnoredActor(Target);
					// Re-evaluate eligibility next segment; spawn protection can expire in flight.
					continue;
				}
				const float Chance = FBrCombatMath::HitChance(Bullet.ShooterPosture, Target->GetPosture(),
					Settings->StandingHitChance, Settings->CrouchingHitChance, Settings->ProneHitChance);
				if (FMath::FRand() * 100.0f >= Chance)
				{
					Bullet.Ignored.Add(Target);
					Query.AddIgnoredActor(Target);
					continue;
				}
				const FBrDamageEvent Event(Bullet.Source, UBrBulletDamageType::StaticClass(), Bullet.Origin);
				Target->TakeDamage(Bullet.Definition->Damage, Event, Shooter->GetController(), Shooter);
			}
			bStopped = true;
			break;
		}
		Bullet.Position = End;
		Bullet.Remaining -= Travel;
		if (bStopped || Bullet.Remaining <= 0) Bullets.RemoveAtSwap(Index);
	}
	if (Bullets.IsEmpty()) SetComponentTickEnabled(false);
}
