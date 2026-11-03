#include "BrInventoryComponent.h"
#include "BrPlayerController.h"
#include "BrAudioSubsystem.h"
#include "BrCharacter.h"
#include "BrCharacterMovementComponent.h"
#include "BrCombatMath.h"
#include "BrDeveloperSettings.h"
#include "BrGrenade.h"
#include "BrHealthComponent.h"
#include "BrLog.h"
#include "BrLifeComponent.h"
#include "BrGameMode.h"
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
	StopFire();
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
	StopFire();
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
	StopFire();
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
	StopFire();
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
	if (!IsAlive()) return;
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
}

void UBrInventoryComponent::AttemptFire()
{
	ABrCharacter* Character = Cast<ABrCharacter>(GetOwner());
	if (!Character || !Character->IsLocallyControlled() || Character->IsGameplayInputBlocked() || !IsAlive() || !Weapon || bReloading) return;
	const double Now = GetWorld()->GetTimeSeconds();
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
	if (!IsAlive() || !Weapon || bReloading || !ValidAim(Aim)) return;
	ABrCharacter* Character = CastChecked<ABrCharacter>(GetOwner());
	if (!Character->GetBrMovement()->IsValidCombatMoveTimeStamp(MoveTimeStamp)) return;
	const double Now = GetWorld()->GetTimeSeconds();
	if (!FBrCombatMath::CanFire(Now, NextAllowedFire)) return;
	if (Loaded <= 0)
	{
		ServerReload_Implementation();
		return;
	}
	NextAllowedFire = FBrCombatMath::NextFireTime(Now, NextAllowedFire, Weapon->FireRate);
	--Loaded;
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
	if (!IsAlive() || !Weapon || bReloading ||
		FBrCombatMath::ReloadRounds(Loaded, Weapon->MagazineSize, GetReserve()) == 0) return;
	bReloading = true;
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
}

void UBrInventoryComponent::CancelReload()
{
	GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);
	bReloading = false;
}

void UBrInventoryComponent::HealthDepleted()
{
	StopFire();
	if (GetOwner()->HasAuthority()) CancelReload();
}

void UBrInventoryComponent::RequestGrenade()
{
	const ABrCharacter* Character = Cast<ABrCharacter>(GetOwner());
	if (Character) ServerGrenade(FRotator(0, Character->GetControlRotation().Yaw, 0).Vector(),
		Character->GetBrMovement()->GetCombatMoveTimeStamp());
}

void UBrInventoryComponent::ServerGrenade_Implementation(FVector_NetQuantizeNormal Aim, float MoveTimeStamp)
{
	if (const ABrCharacter* OwnerCharacter = Cast<ABrCharacter>(GetOwner()))
		if (ABrPlayerController* PC = Cast<ABrPlayerController>(OwnerCharacter->GetController()))
			PC->CancelUnstuck(TEXT("Unstuck cancelled: throwing."));
	if (!IsAlive() || !Throwable || Grenades <= 0 || !ValidAim(Aim) ||
		GetWorld()->GetTimeSeconds() < NextGrenade) return;
	ABrCharacter* Character = CastChecked<ABrCharacter>(GetOwner());
	if (!Character->GetBrMovement()->IsValidCombatMoveTimeStamp(MoveTimeStamp)) return;
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
