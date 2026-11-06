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
#include "Engine/Canvas.h"
#include "Engine/GameViewportClient.h"
#include "CanvasTypes.h"
#include "UnrealClient.h"
#include "Misc/ScopeExit.h"
#include "SceneView.h"

void ABrHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !Canvas->Canvas || !PlayerOwner) return;
	const ABrPlayerController* PC = Cast<ABrPlayerController>(PlayerOwner);
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	if (!PC || PC->IsMenuOpen() || !GS || GS->bLobby || GS->bResults) return;
	const ABrCharacter* Character = Cast<ABrCharacter>(PlayerOwner->GetPawn());
	if (Character && Character->IsLocallyControlled() && !PC->IsMapOpen())
		Character->GetHitFeedback()->Draw(this, Canvas);

	const UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport();
	if (!ViewportClient || !ViewportClient->Viewport) return;
	const FIntPoint Size = ViewportClient->Viewport->GetSizeXY();
	if (Size.X <= 0 || Size.Y <= 0) return;
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	HudScale = FBrViewLayoutRules::Scale(Size.Y);
	ViewLayout = FBrViewLayoutRules::Make(Size, S->PlayAreaAspectRatio, S->MinSidePanelWidth, HudScale);
	// Status panels must survive camera handoff/no-target spectating, even if
	// the current view target supplies an unconstrained rectangle.
	// UE's FCanvas has a full render target, not a view-rect scissor. Scope only
	// its absolute transform and UCanvas dimensions; never change scene projection.
	FCanvas* DrawCanvas = Canvas->Canvas;
	const FIntPoint OldSize(Canvas->SizeX, Canvas->SizeY);
	const float OldOrgX = Canvas->OrgX, OldOrgY = Canvas->OrgY;
	const float OldClipX = Canvas->ClipX, OldClipY = Canvas->ClipY;
	Canvas->SizeX = Size.X; Canvas->SizeY = Size.Y;
	Canvas->OrgX = Canvas->OrgY = 0;
	Canvas->ClipX = Size.X; Canvas->ClipY = Size.Y;
	DrawCanvas->PushAbsoluteTransform(FMatrix::Identity);
	ON_SCOPE_EXIT
	{
		DrawCanvas->PopTransform();
		Canvas->SizeX = OldSize.X; Canvas->SizeY = OldSize.Y;
		Canvas->OrgX = OldOrgX; Canvas->OrgY = OldOrgY;
		Canvas->ClipX = OldClipX; Canvas->ClipY = OldClipY;
	};
	DrawRect(FLinearColor::Black, 0, 0, ViewLayout.Left.Width(), Size.Y);
	DrawRect(FLinearColor::Black, ViewLayout.Right.Min.X, 0, ViewLayout.Right.Width(), Size.Y);
	DrawCompass();
	DrawMinimap();
	if (!PC->IsMapOpen()) DrawPanels();
}

void ABrHUD::PanelText(const FString& Text, FLinearColor Color, bool bRight, float& Y, int32 MaxLines)
{
	const FIntRect& Panel = bRight ? ViewLayout.Right : ViewLayout.Left;
	const float Padding = 10 * HudScale;
	const float Width = Panel.Width() - 2 * Padding;
	if (Width < 1) return;
	const float LineHeight = 18 * HudScale;
	TArray<FString> Paragraphs;
	Text.ParseIntoArray(Paragraphs, TEXT("\n"), false);
	int32 Lines = 0;
	for (FString Paragraph : Paragraphs)
	{
		while (!Paragraph.IsEmpty())
		{
			if (Lines >= MaxLines || Y + LineHeight > Panel.Max.Y - Padding) return;
			int32 Count = Paragraph.Len();
			float W = 0, H = 0;
			GetTextSize(Paragraph, W, H, nullptr, HudScale);
			while (W > Width && Count > 0)
			{
				--Count;
				GetTextSize(Paragraph.Left(Count), W, H, nullptr, HudScale);
			}
			if (!Count) return;
			if (Count < Paragraph.Len())
				for (int32 I = Count - 1; I > 0; --I)
					if (FChar::IsWhitespace(Paragraph[I])) { Count = I; break; }
			DrawText(Paragraph.Left(Count), Color, Panel.Min.X + Padding, Y, nullptr, HudScale);
			Paragraph.RightChopInline(Count);
			Paragraph.TrimStartInline();
			Y += LineHeight;
			++Lines;
		}
	}
	Y += 6 * HudScale;
}

void ABrHUD::PanelMeter(float Fraction, FLinearColor Color, bool bRight, float& Y)
{
	const FIntRect& Panel = bRight ? ViewLayout.Right : ViewLayout.Left;
	const float Width = Panel.Width() - 20 * HudScale;
	if (Width <= 0 || Y + 12 * HudScale > Panel.Max.Y) return;
	const float X = Panel.Min.X + 10 * HudScale;
	DrawRect(FLinearColor(0.15f, 0.15f, 0.15f), X, Y, Width, 6 * HudScale);
	DrawRect(Color, X, Y, Width * FMath::Clamp(Fraction, 0.f, 1.f), 6 * HudScale);
	Y += 14 * HudScale;
}

void ABrHUD::DrawPanels()
{
	const ABrPlayerController* PC = CastChecked<ABrPlayerController>(PlayerOwner);
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	const ABrPlayerState* PS = PlayerOwner->GetPlayerState<ABrPlayerState>();
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	const ABrCharacter* Character = Cast<ABrCharacter>(PC->GetPawn());
	const ABrCharacter* Viewed = PC->IsFlowSpectating() ? PC->GetFlowSpectatorPawn() : Character;
	float LeftY = 14 * HudScale;
	float RightY = RightStatusY;
	// Match information is independent of possession (including elimination).
	if (GS && PS)
		PanelText(FString::Printf(TEXT("%s | Group %d"), GS->MatchSettings.Mode == EBrMode::Grouped ?
			TEXT("Grouped") : TEXT("All vs all"), PS->Team + 1),
			FBrViewLayoutRules::PanelTextColor(PS->TeamColor), true, RightY);
	if (PC->IsFlowSpectating())
		PanelText(FString::Printf(TEXT("Spectating %s | LMB/RMB: next"), *GetNameSafe(PC->GetViewTarget())),
			FLinearColor::White, true, RightY, 3);
	const ABrSafeZone* Zone = nullptr;
	for (TActorIterator<ABrSafeZone> It(GetWorld()); It; ++It) { Zone = *It; break; }
	if (Zone)
	{
		const double ZoneNow = Zone->State.EffectiveTime(Zone->ServerTime());
		const int32 Seconds = FMath::Max(0, FMath::CeilToInt(Zone->State.WaitEnd - ZoneNow));
		FString ZoneText = FString::Printf(TEXT("Zone stage %d\n%s%s"), Zone->State.StageIndex + 1,
			ZoneNow < Zone->State.WaitEnd ? *FString::Printf(TEXT("Shrinks in %02d:%02d"), Seconds / 60, Seconds % 60) :
			ZoneNow < Zone->State.ShrinkEnd ? TEXT("Shrinking") : TEXT("Collapsed"),
			Zone->State.bPaused ? TEXT(" | PAUSED") : TEXT(""));
		if (Zone->IsSuddenDeath()) ZoneText += TEXT("\nSUDDEN DEATH - No respawns");
		const bool bOutside = Viewed && !Zone->IsInside(Viewed->GetActorLocation());
		if (bOutside)
			ZoneText += FString::Printf(TEXT("\nOutside zone! -%.0f HP/s\nSafety: %.1f m"), Zone->State.DamagePerSecond,
				FMath::Max(0.0, FVector::Dist2D(Zone->Circle().Center, Viewed->GetActorLocation()) - Zone->Circle().Radius) / 100);
		PanelText(ZoneText, bOutside ? FLinearColor::Red : FLinearColor::White, true, RightY);
		if (bOutside && RightY + 55 * HudScale < Canvas->SizeY)
		{
			const FVector Delta = FRotator(0, -Viewed->GetCameraYaw(), 0).RotateVector(
				Zone->Circle().Center - Viewed->GetActorLocation()).GetSafeNormal2D();
			const FVector2D Origin(ViewLayout.Right.Min.X + ViewLayout.Right.Width() / 2.f, RightY + 22 * HudScale);
			const FVector2D Direction(Delta.Y, -Delta.X);
			const FVector2D Tip = Origin + Direction * 20 * HudScale;
			const FVector2D Back = Direction * -8 * HudScale;
			const FVector2D Side(-Back.Y * 0.5, Back.X * 0.5);
			DrawLine(Origin.X, Origin.Y, Tip.X, Tip.Y, FLinearColor::Red, 2 * HudScale);
			DrawLine(Tip.X, Tip.Y, Tip.X + Back.X + Side.X, Tip.Y + Back.Y + Side.Y, FLinearColor::Red, 2 * HudScale);
			DrawLine(Tip.X, Tip.Y, Tip.X + Back.X - Side.X, Tip.Y + Back.Y - Side.Y, FLinearColor::Red, 2 * HudScale);
			RightY += 55 * HudScale;
		}
	}
	if (PS)
	{
		PanelText(FString::Printf(TEXT("Round score: %d"), PS->RoundScore), FLinearColor::White, true, RightY);
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
		PanelText(FString::Printf(TEXT("Quests completed: %d (+2 each)"), Completed), FLinearColor::Green, true, RightY);
		for (int32 I = 0; I < Objectives.Num(); ++I)
			PanelText(FString::Printf(TEXT("[Quest x%d] %s"), Counts[I],
				*UBrQuestPickupDefinition::DescribeOffer(*Objectives[I]).ToString()), FLinearColor::Yellow, true, RightY, 3);
	}
	if (PS && PS->bEliminated)
		PanelText(TEXT("Dead - eliminated for this round"), FLinearColor::Yellow, false, LeftY);
	if (Viewed)
	{
		const UBrHealthComponent* Health = Viewed->GetHealth();
		PanelText(FString::Printf(TEXT("Health: %.0f / %.0f"), Health->Health, Health->MaxHealth),
			FLinearColor::White, false, LeftY);
		PanelMeter(Health->Health / FMath::Max(1.f, Health->MaxHealth), FLinearColor::Green, false, LeftY);
		const TArray<float>* Plates = PC->IsFlowSpectating() ? PC->GetSpectatorArmor(Viewed) :
			Viewed->IsLocallyControlled() ? &Viewed->GetArmor()->GetPlates() : nullptr;
		if (Plates)
		{
			const int32 Slots = FBrArmorRules::Capacity(Settings->MaxArmorPlates);
			for (int32 I = 0; I < Slots; ++I)
			{
				const float Value = Plates->IsValidIndex(I) ? (*Plates)[I] : 0;
				PanelText(FString::Printf(TEXT("Armor %d: %.0f"), I + 1, Value), FLinearColor(0.3f, 0.7f, 1), false, LeftY, 1);
				PanelMeter(Value / FMath::Max(0.01f, Settings->ArmorPlateValue), FLinearColor(0.3f, 0.7f, 1), false, LeftY);
			}
		}
	}
	if (PC->UnstuckEnd > 0)
	{
		const float Progress = FMath::Clamp((PC->MenuServerTime() - PC->UnstuckStart) /
			FMath::Max(0.1f, PC->UnstuckEnd - PC->UnstuckStart), 0.f, 1.f);
		PanelText(FString::Printf(TEXT("UNSTUCK %.0f%% - stay still"), 100 * Progress), FLinearColor::Green, false, LeftY);
		PanelMeter(Progress, FLinearColor::Green, false, LeftY);
	}
	else if (!PC->UnstuckMessage.IsEmpty()) PanelText(PC->UnstuckMessage, FLinearColor::Green, false, LeftY);
	if (!Character || !Character->IsLocallyControlled()) return;
	const auto Binding = [PC](FName Name) { return PC->GetBindingLabel(Name); };
	const UBrCharacterMovementComponent* Movement = Character->GetBrMovement();
	const UBrInventoryComponent* Inventory = Character->GetInventory();
	PanelText(FString::Printf(TEXT("%s\nMag: %d | Reserve: %d%s\nGrenades: %d"),
		Inventory->Weapon ? *Inventory->Weapon->DisplayName.ToString() : TEXT("Unarmed"),
		Inventory->Loaded, Inventory->GetReserve(), Inventory->bReloading ? TEXT("\nRELOADING") : TEXT(""),
		Inventory->Grenades), FLinearColor::White, false, LeftY);
	if (Movement->GetSprintFraction() < 0.999f)
	{
		PanelText(TEXT("Sprint stamina"), FLinearColor::White, false, LeftY, 1);
		PanelMeter(Movement->GetSprintFraction(), FLinearColor(0.2f, 0.8f, 0.65f), false, LeftY);
	}
	const UBrLifeComponent* Life = Character->GetLife();
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
	PanelText(LifeText, FLinearColor::Yellow, false, LeftY);
	if (GetWorld()->GetTimeSeconds() < PC->PickupFeedbackUntil)
		PanelText(PC->PickupFeedback, FLinearColor::Green, false, LeftY, 3);
#if !UE_BUILD_SHIPPING
	if (Settings->bShowMovementDebugHud)
	{
		float DebugY = FMath::Max(LeftY, Canvas->SizeY - 100 * HudScale);
		const FString Posture = StaticEnum<EBrPosture>()->GetNameStringByValue(static_cast<int64>(Character->GetPosture()));
		const TCHAR* Gait = Movement->IsSliding() ? TEXT("Slide") : Movement->IsSprinting() ? TEXT("Sprint") : TEXT("Run");
		PanelText(FString::Printf(TEXT("%s | %s\nSpeed: %.0f cm/s\nMax: %.0f cm/s"), *Posture, Gait,
			Character->GetVelocity().Size2D(), Movement->GetMaxSpeed()), FLinearColor::White, false, DebugY, 4);
	}
#endif
}
