#include "BrPenaltyComponent.h"
#include "BrPenaltyRules.h"
#include "BrCharacter.h"
#include "BrCharacterMovementComponent.h"
#include "BrDeveloperSettings.h"
#include "BrLifeComponent.h"
#include "BrGameMode.h"
#include "BrGameState.h"
#include "BrAudioSubsystem.h"
#include "BrLog.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

UBrPenaltyComponent::UBrPenaltyComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/BrGame/Pickups/M_Pickup.M_Pickup"));
	TubeMesh = Cylinder.Object;
	PuffMesh = Sphere.Object;
	PlaceholderMaterial = Material.Object;
}
ABrCharacter* UBrPenaltyComponent::Character() const { return CastChecked<ABrCharacter>(GetOwner()); }

void UBrPenaltyComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner()->HasAuthority())
	{
		TSet<FName> Ids;
		for (const auto& Ref : UBrDeveloperSettings::Get()->PenaltyDefinitions)
		{
			UBrPenaltyDefinition* Def = Ref.LoadSynchronous();
			if (!Def || Def->Id.IsNone() || Ids.Contains(Def->Id) ||
				!FMath::IsFinite(Def->Duration) || Def->Duration < 0.1f || Def->Duration > 300 ||
				!FMath::IsFinite(Def->SpeedMultiplier) || Def->SpeedMultiplier < 0.1f || Def->SpeedMultiplier > 4)
			{
				UE_LOG(LogBr, Error, TEXT("Invalid/duplicate penalty definition: %s"), *Ref.ToString());
				continue;
			}
			Ids.Add(Def->Id);
			Definitions.Add(Def);
		}
		if (Definitions.IsEmpty()) UE_LOG(LogBr, Error, TEXT("Plant disabled: no valid penalty definitions."));
	}
	LifeChanged();
	OnRep_Penalty();
}
void UBrPenaltyComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	CancelPlant();
	CancelIncomingPlants();
	if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(ServiceTimer);
	ClearCosmetics();
	Super::EndPlay(Reason);
}
void UBrPenaltyComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBrPenaltyComponent, Penalty);
	DOREPLIFETIME_CONDITION(UBrPenaltyComponent, PlantTarget, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UBrPenaltyComponent, PlantStartTime, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UBrPenaltyComponent, PlantEndTime, COND_OwnerOnly);
}
bool UBrPenaltyComponent::IsActive(float Time) const
{
	return FBrPenaltyRules::Active(Penalty.Definition != nullptr, Penalty.StartTime, Penalty.EndTime,
		Time < 0 ? Character()->GetLife()->ServerTime() : Time);
}
float UBrPenaltyComponent::SpeedMultiplier(float Time) const
{
	return IsActive(Time) ? Penalty.Definition->SpeedMultiplier : 1.f;
}
bool UBrPenaltyComponent::ForcesForward(float Time) const { return IsActive(Time) && Penalty.Definition->bForcedForward; }
bool UBrPenaltyComponent::DisablesSprint(float Time) const { return IsActive(Time) && Penalty.Definition->bDisablesSprint; }
bool UBrPenaltyComponent::ForcesStanding(float Time) const { return IsActive(Time) && Penalty.Definition->bForcesStanding; }

bool UBrPenaltyComponent::CanPlant(const ABrCharacter* Target, bool bResolveDeadline) const
{
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	if (GetOwner()->HasAuthority())
	{
		const ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>();
		if (!Mode || !Mode->IsAdmitted(Character()->GetController())) return false;
	}
	return IsValid(Target) && Character()->GetController() && (!GS || (!GS->bResults && !GS->bLobby)) &&
		FBrPenaltyRules::CanPlant(Character()->GetLife()->State, Target->GetLife()->State,
			FVector::DistSquared(GetOwner()->GetActorLocation(), Target->GetActorLocation()),
			UBrDeveloperSettings::Get()->ReviveRadius, Target == GetOwner(),
			Target->GetPenalty()->Penalty.bPlantedThisDown) &&
		(bResolveDeadline || Character()->GetLife()->ServerTime() < Target->GetLife()->DownedEndTime);
}
ABrCharacter* UBrPenaltyComponent::FindPlantTarget() const
{
	ABrCharacter* Best = nullptr;
	double Distance = TNumericLimits<double>::Max();
	for (TActorIterator<ABrCharacter> It(GetWorld()); It; ++It)
	{
		const double D = FVector::DistSquared(GetOwner()->GetActorLocation(), It->GetActorLocation());
		if (D < Distance && CanPlant(*It)) { Best = *It; Distance = D; }
	}
	return Best;
}
void UBrPenaltyComponent::StartPlant() { ServerPlant(true); }
void UBrPenaltyComponent::StopPlant() { ServerPlant(false); }
void UBrPenaltyComponent::ServerPlant_Implementation(bool bHeld)
{
	if (!bHeld) { CancelPlant(); return; }
	if (PlantTarget || Definitions.IsEmpty()) return;
	PlantTarget = FindPlantTarget(); // Intent only: no client target, item or progress accepted.
	if (!PlantTarget) return;
	Character()->GetBrMovement()->SetSprinting(false);
	PlantStartTime = Character()->GetLife()->ServerTime();
	PlantEndTime = PlantStartTime + FMath::Clamp(UBrDeveloperSettings::Get()->PlantDuration, 0.1f, 120.f);
	PlantTarget->GetPenalty()->Planters.AddUnique(this);
	EnsureService();
	GetOwner()->ForceNetUpdate();
}
void UBrPenaltyComponent::CancelPlant()
{
	if (!GetOwner()->HasAuthority()) return;
	if (IsValid(PlantTarget)) PlantTarget->GetPenalty()->Planters.Remove(this);
	PlantTarget = nullptr;
	PlantStartTime = PlantEndTime = 0;
	if (GetWorld()) GetOwner()->ForceNetUpdate();
}
void UBrPenaltyComponent::NewDown()
{
	if (!GetOwner()->HasAuthority()) return;
	CancelPlant();
	Penalty.bPlantedThisDown = false;
	Changed(); // An already-running effect keeps its original deadline.
}
void UBrPenaltyComponent::LifeChanged()
{
	if (!GetOwner()->HasAuthority()) return;
	if (!Character()->GetLife()->IsAlive()) CancelPlant();
	if (Character()->GetLife()->State == EBrLifeState::Eliminated)
	{
		Penalty = FBrPenaltyState();
	}
	else if (Penalty.Definition && Penalty.StartTime <= 0 && Character()->GetLife()->IsAlive())
	{
		Penalty.StartTime = FBrPenaltyRules::StartDormant(0, true, Character()->GetLife()->ServerTime());
		Penalty.EndTime = Penalty.StartTime + Penalty.Definition->Duration;
	}
	if (Character()->GetLife()->State != EBrLifeState::Downed) CancelIncomingPlants();
	Changed();
}
void UBrPenaltyComponent::Restore(const FBrPenaltyState& State)
{
	if (!GetOwner()->HasAuthority()) return;
	Penalty = State;
	if (Penalty.Definition && Penalty.StartTime > 0 && Character()->GetLife()->ServerTime() >= Penalty.EndTime)
		Penalty.Definition = nullptr;
	if (HasBegunPlay()) LifeChanged();
}
void UBrPenaltyComponent::ResetRound()
{
	if (!GetOwner()->HasAuthority()) return;
	CancelPlant();
	CancelIncomingPlants();
	Penalty = FBrPenaltyState();
	Changed();
}
void UBrPenaltyComponent::ResolvePlants(float Now, const FBrDownedExit& Exit)
{
	if (!GetOwner()->HasAuthority() || Character()->GetLife()->State != EBrLifeState::Downed ||
		Penalty.bPlantedThisDown) return;
	UBrPenaltyComponent* Winner = nullptr;
	for (const auto& Entry : Planters)
	{
		UBrPenaltyComponent* Other = Entry.Get();
		if (Other && Other->PlantTarget == Character() && Other->CanPlant(Character(), true) &&
			FBrPenaltyRules::Wins(Other->PlantEndTime, Now, Exit) &&
			(!Winner || Other->PlantEndTime < Winner->PlantEndTime))
			Winner = Other;
	}
	if (!Winner || Winner->Definitions.IsEmpty()) return;
	Penalty.Definition = Winner->Definitions[FMath::RandRange(0, Winner->Definitions.Num() - 1)];
	Penalty.StartTime = Penalty.EndTime = 0; // Replacement is dormant, even if the old effect was active.
	Penalty.bPlantedThisDown = true;
	if (ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>())
		Mode->AwardPlantScore(Winner->Character()->GetController());
	else UE_LOG(LogBr, Error, TEXT("Plant completed without BrGameMode; cannot award score."));
	UE_LOG(LogBr, Display, TEXT("Penalty planted: %s -> %s Item=%s"), *Winner->GetOwner()->GetName(),
		*GetOwner()->GetName(), *Penalty.Definition->Id.ToString());
	CancelIncomingPlants();
	Changed();
}
void UBrPenaltyComponent::CancelIncomingPlants()
{
	if (!GetOwner()->HasAuthority()) return;
	const auto Pending = Planters;
	for (const auto& Entry : Pending) if (auto* Other = Entry.Get()) Other->CancelPlant();
	Planters.Reset();
}
void UBrPenaltyComponent::EnsureService()
{
	if (!GetWorld()->GetTimerManager().IsTimerActive(ServiceTimer))
		GetWorld()->GetTimerManager().SetTimer(ServiceTimer, this, &UBrPenaltyComponent::Service, 0.05f, true);
}
void UBrPenaltyComponent::Changed()
{
	GetOwner()->ForceNetUpdate();
	if (HasBegunPlay()) OnRep_Penalty();
	if (HasBegunPlay() && GetOwner()->HasAuthority() && !GetWorld()->bIsTearingDown)
		if (ABrGameMode* Mode = GetWorld()->GetAuthGameMode<ABrGameMode>())
			Mode->RecordPlayer(Character()->GetController());
}
void UBrPenaltyComponent::OnRep_Penalty()
{
	if (Penalty.Definition || PlantTarget) EnsureService();
	Cosmetics();
}
void UBrPenaltyComponent::Service()
{
	if (GetOwner()->HasAuthority())
	{
		const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
		if (GS && GS->bResults)
		{
			CancelPlant();
			Penalty = FBrPenaltyState();
			Changed();
		}
		if (PlantTarget)
		{
			// The target's life service owns both completion and exit ordering. This timer only cancels.
			if (!CanPlant(PlantTarget, true)) CancelPlant();
		}
		if (Penalty.Definition && Penalty.StartTime > 0 && Character()->GetLife()->ServerTime() >= Penalty.EndTime)
		{
			Penalty.Definition = nullptr;
			Penalty.StartTime = Penalty.EndTime = 0;
			Changed(); // Do not clear one-per-down when an active effect expires during another down.
		}
	}
	Cosmetics();
	if (!Penalty.Definition && !PlantTarget) GetWorld()->GetTimerManager().ClearTimer(ServiceTimer);
}

void UBrPenaltyComponent::ClearCosmetics()
{
	if (Audio) Audio->DestroyComponent();
	if (Niagara) Niagara->DestroyComponent();
	if (Tube) Tube->DestroyComponent();
	for (UStaticMeshComponent* Puff : Puffs) if (Puff) Puff->DestroyComponent();
	Audio = nullptr; Niagara = nullptr; Tube = nullptr;
	Puffs.Reset(); PuffBirths.Reset(); PresentedDefinition = nullptr;
}
void UBrPenaltyComponent::Cosmetics()
{
	if (GetNetMode() == NM_DedicatedServer) return;
	UBrPenaltyDefinition* Def = Penalty.Definition;
	if (PresentedDefinition != Def)
	{
		ClearCosmetics();
		PresentedDefinition = Def;
		if (Def)
		{
			if (!TubeMesh || !PuffMesh || !PlaceholderMaterial)
			{
				UE_LOG(LogBr, Error, TEXT("Penalty placeholder mesh/material missing."));
				return;
			}
			auto Mesh = [&](UStaticMesh* Shape, FLinearColor Color)
			{
				UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Character());
				C->SetStaticMesh(Shape);
				C->SetCollisionProfileName(TEXT("NoCollision"));
				C->SetCastShadow(false);
				C->SetCanEverAffectNavigation(false);
				C->SetMaterial(0, PlaceholderMaterial);
				C->RegisterComponent();
				if (auto* MID = C->CreateDynamicMaterialInstance(0)) MID->SetVectorParameterValue(TEXT("PickupColor"), Color);
				return C;
			};
			Tube = Mesh(TubeMesh, Def->TubeColor);
			Tube->AttachToComponent(Character()->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("pelvis"));
			Tube->SetRelativeLocation(FVector(0, -18, 0));
			Tube->SetRelativeRotation(FRotator(0, 0, 90));
			Tube->SetRelativeScale3D(FVector(0.065, 0.065, 0.30));
			if (!Def->VFX.IsNull())
			{
				if (UNiagaraSystem* System = Def->VFX.LoadSynchronous())
					Niagara = UNiagaraFunctionLibrary::SpawnSystemAttached(System, Tube, NAME_None, FVector::ZeroVector,
						FRotator::ZeroRotator, EAttachLocation::SnapToTarget, false, false);
				else UE_LOG(LogBr, Error, TEXT("Penalty VFX missing: %s"), *Def->VFX.ToString());
			}
			else if (Def->Trail != EBrPenaltyTrail::None)
			{
				for (int32 I = 0; I < 16; ++I)
				{
					auto* Puff = Mesh(PuffMesh, Def->Trail == EBrPenaltyTrail::Flame ?
						FLinearColor(1, 0.25f, 0) : FLinearColor(0.35f, 0.35f, 0.35f));
					Puff->SetVisibility(false);
					Puffs.Add(Puff); PuffBirths.Add(-1);
				}
			}
			if (!Def->LoopSound.IsNull())
			{
				if (USoundBase* Sound = Def->LoopSound.LoadSynchronous())
				{
					Audio = NewObject<UAudioComponent>(Character());
					Audio->SetupAttachment(Character()->GetRootComponent());
					Audio->bAutoActivate = false;
					Audio->bAutoDestroy = false;
					Audio->SetSound(Sound);
					Audio->bOverrideAttenuation = true;
					Audio->AttenuationOverrides.bAttenuate = true;
					Audio->AttenuationOverrides.bSpatialize = true;
					Audio->AttenuationOverrides.AttenuationShapeExtents = FVector(150);
					Audio->AttenuationOverrides.FalloffDistance = UBrDeveloperSettings::Get()->PenaltyHearingRange;
					Audio->RegisterComponent();
					// Asset uses SC_Effects, honoring the existing master/effects volume mix.
				}
				else UE_LOG(LogBr, Error, TEXT("Penalty loop sound missing: %s"), *Def->LoopSound.ToString());
			}
			NextPuff = 0;
		}
	}
	if (!Def || !Tube) return;
	// Relevancy alone has a channel-close delay. Local fog presentation also gates all emissions,
	// existing world-space puffs and audio, including on the listen host. Never use Audio::Emit.
	const bool bVisible = !Character()->GetMesh()->bHiddenInGame && Character()->GetMesh()->IsVisible() && !Character()->IsHidden();
	const bool bRunning = IsActive() && bVisible;
	Tube->SetVisibility(bVisible && (Penalty.StartTime <= 0 || IsActive()));
	if (Audio)
	{
		if (bRunning && !Audio->IsPlaying()) Audio->Play();
		if (!bRunning) Audio->Stop();
	}
	if (Niagara)
	{
		Niagara->SetVisibility(bRunning);
		if (bRunning && !Niagara->IsActive()) Niagara->Activate(true);
		if (!bRunning) Niagara->DeactivateImmediate();
	}
	const float Now = GetWorld()->GetTimeSeconds();
	const float Lifetime = Def->Trail == EBrPenaltyTrail::Smoke ? 2.4f : 0.4f;
	if (bRunning && !Puffs.IsEmpty() &&
		(PuffBirths[(NextPuff + 15) % 16] < 0 || Now - PuffBirths[(NextPuff + 15) % 16] >= Lifetime / 16.f))
	{
		Puffs[NextPuff]->SetWorldLocation(Tube->GetComponentLocation() + FVector(0, 0, 4));
		PuffBirths[NextPuff] = Now;
		NextPuff = (NextPuff + 1) % 16;
	}
	for (int32 I = 0; I < Puffs.Num(); ++I)
	{
		const float Age = Now - PuffBirths[I];
		const bool bShow = bRunning && PuffBirths[I] >= 0 && Age < Lifetime;
		Puffs[I]->SetVisibility(bShow);
		if (!bVisible) PuffBirths[I] = -1; // No stale trail on fog re-entry.
		if (bShow) Puffs[I]->SetWorldScale3D(FVector((Def->Trail == EBrPenaltyTrail::Smoke ? 0.12f : 0.06f) + Age * 0.18f));
	}
}
