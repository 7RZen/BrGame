#include "BrHUD.h"
#include "BrHitFeedbackComponent.h"
#include "BrSafeZone.h"
#include "EngineUtils.h"
#include "BrCharacter.h"
#include "BrCharacterMovementComponent.h"
#include "BrInventoryComponent.h"
#include "BrHealthComponent.h"
#include "BrArmorComponent.h"
#include "BrLifeComponent.h"
#include "BrDeveloperSettings.h"
#include "BrPlayerState.h"
#include "BrPickup.h"
#include "BrPlayerController.h"
#include "BrGameState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

void ABrHUD::DrawHUD()
{
	Super::DrawHUD();
	DrawMinimap(); // Before the pawn-only HUD return, so spectators get their target's view.
	DrawCompass();
	if (Canvas && PlayerOwner)
	{
		if (const ABrPlayerController* PC = Cast<ABrPlayerController>(PlayerOwner))
		{
			if (PC->UnstuckEnd > 0)
				DrawText(FString::Printf(TEXT("UNSTUCK %.0f%% — stay still"),
					100 * FMath::Clamp((PC->MenuServerTime() - PC->UnstuckStart) /
						FMath::Max(0.1f, PC->UnstuckEnd - PC->UnstuckStart), 0.f, 1.f)), FLinearColor::Green, 420, 60, nullptr, 1.5f);
			else if (!PC->UnstuckMessage.IsEmpty()) DrawText(PC->UnstuckMessage, FLinearColor::Green, 420, 60);
		}
		const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
		const ABrPlayerState* PS = PlayerOwner->GetPlayerState<ABrPlayerState>();
		if (PS)
		{
			DrawText(FString::Printf(TEXT("Round score: %d"), PS->RoundScore), FLinearColor::White, 20, 430);
			// Group duplicate objectives for display only: every accepted offer
			// remains independent in the replicated journal and awards its own +2.
			TArray<const FBrQuestOffer*> Objectives;
			TArray<int32> Counts;
			int32 Completed = 0;
			for (const FBrQuestProgress& Quest : PS->Quests)
			{
				if (Quest.bComplete) { ++Completed; continue; }
				const int32 Index = Objectives.IndexOfByPredicate([&](const FBrQuestOffer* Offer)
				{
					return Offer->Objective == Quest.Offer.Objective && Offer->Weapon == Quest.Offer.Weapon;
				});
				if (Index != INDEX_NONE) ++Counts[Index];
				else { Objectives.Add(&Quest.Offer); Counts.Add(1); }
			}
			DrawText(FString::Printf(TEXT("Quests completed: %d (+2 each)"), Completed), FLinearColor::Green, 20, 455);
			for (int32 I = 0; I < Objectives.Num(); ++I)
				DrawText(FString::Printf(TEXT("[Quest x%d] %s"), Counts[I],
					*UBrQuestPickupDefinition::DescribeOffer(*Objectives[I]).ToString()),
					FLinearColor::Yellow, 20, 480 + I * 20);
		}
		if (GS && PS)
			DrawText(FString::Printf(TEXT("%s | Group %d"), GS->MatchSettings.Mode == EBrMode::Grouped ?
				TEXT("Grouped") : TEXT("All vs all"), PS->Team + 1), PS->TeamColor, 20, 370);
		if (const ABrPlayerController* PC = Cast<ABrPlayerController>(PlayerOwner))
			if (PC->IsFlowSpectating())
				DrawText(FString::Printf(TEXT("Spectating %s | LMB/RMB: next"), *GetNameSafe(PC->GetViewTarget())),
					FLinearColor::White, 20, 400);
	}
	if (Canvas && PlayerOwner && !PlayerOwner->GetPawn())
	{
		const ABrPlayerState* PS = PlayerOwner->GetPlayerState<ABrPlayerState>();
		if (PS && PS->bEliminated)
			DrawText(TEXT("Dead - eliminated for this round"), FLinearColor::Yellow, 20, 20);
	}
	const ABrCharacter* Character = PlayerOwner ? Cast<ABrCharacter>(PlayerOwner->GetPawn()) : nullptr;
	const ABrPlayerController* ArmorPC = Cast<ABrPlayerController>(PlayerOwner);
	if (Canvas && ArmorPC)
	{
		const ABrCharacter* Viewed = ArmorPC->IsFlowSpectating() ?
			Cast<ABrCharacter>(ArmorPC->GetViewTarget()) : Character;
		const TArray<float>* Plates = ArmorPC->IsFlowSpectating() ? ArmorPC->GetSpectatorArmor(Viewed) :
			Viewed && Viewed->IsLocallyControlled() ? &Viewed->GetArmor()->GetPlates() : nullptr;
		if (Viewed && ArmorPC->IsFlowSpectating())
			DrawText(FString::Printf(TEXT("Health: %.0f / %.0f"),
				Viewed->GetHealth()->Health, Viewed->GetHealth()->MaxHealth), FLinearColor::White, 20, 80);
		if (Plates)
		{
			const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
			const int32 Slots = FBrArmorRules::Capacity(S->MaxArmorPlates);
			DrawText(TEXT("Armor"), FLinearColor(0.3f, 0.7f, 1), 210, 60);
			const float Width = 150.f / FMath::Max(1, Slots);
			for (int32 I = 0; I < Slots; ++I)
			{
				const float Value = Plates->IsValidIndex(I) ? (*Plates)[I] : 0;
				const float X = 210 + I * Width;
				DrawRect(FLinearColor(0.1f, 0.1f, 0.1f, 0.8f), X, 80, Width - 2, 8);
				DrawRect(FLinearColor(0.3f, 0.7f, 1), X, 80, (Width - 2) *
					FMath::Clamp(Value / FMath::Max(0.01f, S->ArmorPlateValue), 0.f, 1.f), 8);
				if (Slots <= 6)
					DrawText(FString::Printf(TEXT("%.0f"), Value), FLinearColor::White, X, 92);
			}
		}
	}
	if (!Canvas || !Character || !Character->IsLocallyControlled())
	{
		return;
	}
	const UBrCharacterMovementComponent* Movement = Character->GetBrMovement();
	Character->GetHitFeedback()->Draw(this, Canvas);
	const ABrPlayerController* InputOwner = Cast<ABrPlayerController>(PlayerOwner);
	const auto Binding = [InputOwner](FName Name)
	{
		return InputOwner ? InputOwner->GetBindingLabel(Name) : FString(TEXT("[input unavailable]"));
	};
	const FString Posture = StaticEnum<EBrPosture>()->GetNameStringByValue(static_cast<int64>(Character->GetPosture()));
	const TCHAR* Gait = Movement->IsSliding() ? TEXT("Slide") : Movement->IsSprinting() ? TEXT("Sprint") : TEXT("Run");
	DrawText(FString::Printf(TEXT("%s | %s%s\nSpeed: %.0f cm/s | Max: %.0f cm/s"),
		*Posture, Gait, !Movement->IsSliding() && Character->GetPosture() != EBrPosture::Standing ? TEXT(" (modifier ignored)") : TEXT(""),
		Character->GetVelocity().Size2D(), Movement->GetMaxSpeed()), FLinearColor::White, 20.0f, 20.0f);
	const UBrInventoryComponent* Inventory = Character->GetInventory();
	if (Movement->GetSprintFraction() < 0.999f)
	{
		DrawRect(FLinearColor(0.1f, 0.1f, 0.1f, 0.8f), 20, 158, 150, 6);
		DrawRect(FLinearColor(0.2f, 0.8f, 0.65f, 1), 20, 158, 150 * Movement->GetSprintFraction(), 6);
	}
	const UBrHealthComponent* Health = Character->GetHealth();
	DrawText(FString::Printf(TEXT("Health: %.0f / %.0f\n%s | Mag: %d | Reserve: %d%s\nGrenades: %d"),
		Health->Health, Health->MaxHealth, Inventory->Weapon ? *Inventory->Weapon->DisplayName.ToString() : TEXT("Unarmed"),
		Inventory->Loaded, Inventory->GetReserve(), Inventory->bReloading ? TEXT(" | RELOADING") : TEXT(""),
		Inventory->Grenades), FLinearColor::White, 20.0f, 80.0f);
	const UBrLifeComponent* Life = Character->GetLife();
	const ABrSafeZone* Zone = nullptr;
	for (TActorIterator<ABrSafeZone> It(GetWorld()); It; ++It) { Zone = *It; break; }
	if (Zone)
	{
		const double ZoneNow = Zone->State.EffectiveTime(Zone->ServerTime());
		const int32 Seconds = FMath::Max(0, FMath::CeilToInt(Zone->State.WaitEnd - ZoneNow));
		FString ZoneText = FString::Printf(TEXT("Zone stage %d | %s%s"), Zone->State.StageIndex + 1,
			ZoneNow < Zone->State.WaitEnd ? *FString::Printf(TEXT("Zone shrinks in %02d:%02d"), Seconds / 60, Seconds % 60) :
				ZoneNow < Zone->State.ShrinkEnd ? TEXT("Zone shrinking") : TEXT("Zone collapsed"),
			Zone->State.bPaused ? TEXT(" | PAUSED") : TEXT(""));
		if (Zone->IsSuddenDeath()) ZoneText += TEXT("\nSUDDEN DEATH - No respawns; revive or be eliminated");
		if (!Zone->IsInside(Character->GetActorLocation()))
		{
			const FBrZoneCircle Circle = Zone->Circle();
			ZoneText += FString::Printf(TEXT("\nOutside zone! -%.0f HP/s | Safety: %.1f m"),
				Zone->State.DamagePerSecond, FMath::Max(0.0, FVector::Dist2D(Circle.Center, Character->GetActorLocation()) - Circle.Radius) / 100);
			// Screen-space direction accounts for the rotating camera.
			const FVector Delta = FRotator(0, -Character->GetCameraYaw(), 0).RotateVector(Circle.Center - Character->GetActorLocation()).GetSafeNormal2D();
			const FVector2D Origin(300, 455);
			const FVector2D Tip = Origin + FVector2D(Delta.Y, -Delta.X) * 35;
			DrawLine(Origin.X, Origin.Y, Tip.X, Tip.Y, FLinearColor::Red, 3);
			const FVector2D Back = FVector2D(Delta.Y, -Delta.X) * -10;
			const FVector2D Side(-Back.Y * 0.5, Back.X * 0.5);
			DrawLine(Tip.X, Tip.Y, Tip.X + Back.X + Side.X, Tip.Y + Back.Y + Side.Y, FLinearColor::Red, 3);
			DrawLine(Tip.X, Tip.Y, Tip.X + Back.X - Side.X, Tip.Y + Back.Y - Side.Y, FLinearColor::Red, 3);
		}
		DrawText(ZoneText, Zone->IsInside(Character->GetActorLocation()) ? FLinearColor::White : FLinearColor::Red, 360, 430);
	}
	const ABrPlayerState* PS = Character->GetPlayerState<ABrPlayerState>();
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	const float Now = Life->ServerTime();
	const TCHAR* StateText = Life->State == EBrLifeState::Downed ? TEXT("Dying") :
		Life->State == EBrLifeState::Eliminated ? TEXT("Dead") : TEXT("Alive");
	FString LifeText = FString::Printf(TEXT("%s | Deaths: %d / %d%s"),
		StateText, PS ? PS->DeathCount : 0, Settings->MaxDeaths,
		Life->IsInvulnerable() ? *FString::Printf(TEXT(" | PROTECTED %.1fs"), Life->InvulnerabilityEndTime - Now) : TEXT(""));
	if (Life->State == EBrLifeState::Downed)
	{
		LifeText += FString::Printf(TEXT("\nDowned: %.1fs"), FMath::Max(0.0f, Life->DownedEndTime - Now));
		if (PS && PS->DeathCount < Settings->MaxDeaths && (!Zone || !Zone->IsSuddenDeath()))
		{
			const float NextMax = FBrLifeRules::MaxHealth(Settings->BaseMaxHealth, PS->VoluntaryRespawnCount + 1,
				Settings->MaxHealthPenaltyPerRespawn, Settings->MinMaxHealthFraction);
			LifeText += FString::Printf(TEXT("\nHold %s to respawn (max HP -%.0f%% -> %.0f)"),
				*Binding(TEXT("Br.RespawnHold")), Settings->MaxHealthPenaltyPerRespawn * 100, NextMax);
			if (Life->RespawnHoldEndTime > 0)
				LifeText += FString::Printf(TEXT(" [%.0f%%]"), 100 * FMath::Clamp(1.0f -
					(Life->RespawnHoldEndTime - Now) / FMath::Max(0.1f, Settings->RespawnHoldTime), 0.0f, 1.0f));
		}
		else LifeText += TEXT("\nNo respawns left - wait for revive");
	}
	if (Life->ReviveTarget || Life->Reviver)
	{
		LifeText += FString::Printf(TEXT("\n%s %.0f%%"),
			Life->ReviveTarget ? TEXT("Reviving") : TEXT("Being revived"),
			100 * FMath::Clamp((Now - Life->ReviveStartTime) / FMath::Max(0.1f, Life->ReviveEndTime - Life->ReviveStartTime), 0.0f, 1.0f));
	}
	else if (ABrCharacter* Target = Life->FindReviveTarget())
	{
		const APlayerState* TargetPS = Target->GetPlayerState();
		LifeText += FString::Printf(TEXT("\nHold %s to revive %s"), *Binding(TEXT("Br.Interact")),
			TargetPS ? *TargetPS->GetPlayerName() : *Target->GetName());
	}
	else if (ABrPickup* Pickup = ABrPickup::FindInteractPickup(Character))
	{
		LifeText += FString::Printf(TEXT("\nPress %s to pick up %s"), *Binding(TEXT("Br.Interact")), *Pickup->GetPickupLabel().ToString());
	}
	DrawText(LifeText, FLinearColor::Yellow, 20.0f, 150.0f);
	if (const ABrPlayerController* PC = Cast<ABrPlayerController>(PlayerOwner))
	{
		if (GetWorld()->GetTimeSeconds() < PC->PickupFeedbackUntil)
			DrawText(PC->PickupFeedback, FLinearColor::Green, 20, 330);
	}
}
