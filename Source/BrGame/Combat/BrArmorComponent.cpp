#include "BrArmorComponent.h"
#include "BrCharacter.h"
#include "BrDeveloperSettings.h"
#include "BrLifeComponent.h"
#include "BrPlayerState.h"
#include "BrPlayerController.h"
#include "BrAudioSubsystem.h"
#include "BrLog.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UBrArmorComponent::UBrArmorComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

void UBrArmorComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UBrArmorComponent, Plates, COND_OwnerOnly);
}

bool UBrArmorComponent::CanAddPlate() const
{
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	return FBrArmorRules::CanAddPlate(Plates, S->MaxArmorPlates, S->ArmorPlateValue);
}

bool UBrArmorComponent::AddPlate()
{
	ABrCharacter* C = CastChecked<ABrCharacter>(GetOwner());
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	if (!C->HasAuthority() || !C->GetLife()->IsAlive() ||
		!FBrArmorRules::AddPlate(Plates, S->MaxArmorPlates, S->ArmorPlateValue)) return false;
	Publish();
	return true;
}

FBrArmorRules::FAbsorption UBrArmorComponent::Absorb(float Damage, bool bBypass)
{
	ABrCharacter* C = CastChecked<ABrCharacter>(GetOwner());
	if (!C->HasAuthority() || !C->GetLife()->IsAlive()) return {};
	const auto Result = FBrArmorRules::Absorb(Plates, Damage, bBypass);
	if (Result.Absorbed > 0)
	{
		Publish();
		if (Result.Broken > 0)
		{
			PlayBreaks(Result.Broken);
		}
	}
	return Result;
}

void UBrArmorComponent::RestoreFromPlayerState()
{
	ABrCharacter* C = CastChecked<ABrCharacter>(GetOwner());
	if (!C->HasAuthority()) return;
	const ABrPlayerState* PS = C->GetPlayerState<ABrPlayerState>();
	if (!PS)
	{
		UE_LOG(LogBr, Error, TEXT("Armor restore failed: %s has no BrPlayerState."), *C->GetName());
		return;
	}
	Plates = PS->GetArmorPlates();
	Publish();
}

void UBrArmorComponent::Clear()
{
	if (!GetOwner()->HasAuthority()) return;
	Plates.Reset();
	Publish(); // Reset/elimination is not a damage break and plays no sound.
}

void UBrArmorComponent::Publish()
{
	ABrCharacter* C = CastChecked<ABrCharacter>(GetOwner());
	if (ABrPlayerState* PS = C->GetPlayerState<ABrPlayerState>()) PS->StoreArmorPlates(Plates);
	C->ForceNetUpdate();
	if (!GetWorld() || GetWorld()->bIsTearingDown) return;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		if (ABrPlayerController* PC = Cast<ABrPlayerController>(It->Get()))
			if (PC->GetFlowSpectatorPawn() == C) PC->RefreshSpectatorArmor();
}

void UBrArmorComponent::PlayBreaks(int32 Count)
{
	ABrCharacter* C = CastChecked<ABrCharacter>(GetOwner());
	// Existing hit feedback has no attacker sound route. Keep this private to victim/viewers.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ABrPlayerController* PC = Cast<ABrPlayerController>(It->Get());
		if (UBrAudioSubsystem::ListenerPawn(PC) != C) continue;
		for (int32 I = 0; I < Count; ++I)
			PC->ClientSound(EBrSoundKind::ArmorBreak, UBrDeveloperSettings::Get()->ArmorBreakSound, C->GetActorLocation());
	}
}
