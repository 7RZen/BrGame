#include "BrHUD.h"
#include "BrCompassRules.h"
#include "BrCharacter.h"
#include "BrDeveloperSettings.h"
#include "BrGameState.h"
#include "BrPlayerController.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "Misc/App.h"

void ABrHUD::DrawCompass()
{
	const ABrPlayerController* PC = Cast<ABrPlayerController>(PlayerOwner);
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	if (!Canvas || !PC || !PC->IsLocalController() || PC->IsMenuOpen() ||
		!GS || GS->bLobby || GS->bResults || !FApp::CanEverRender()) return;

	const ABrCharacter* Viewed = PC->IsFlowSpectating() ?
		PC->GetFlowSpectatorPawn() : Cast<ABrCharacter>(PC->GetPawn());
	if (!IsValid(Viewed) || Viewed->IsActorBeingDestroyed()) return;
	const FVector Position = Viewed->GetActorLocation();
	const double Yaw = Viewed->GetCameraYaw();
	if (Position.ContainsNaN() || !FMath::IsFinite(Yaw)) return;

	const float Scale = HudScale;
	const FString Coordinates = FString::Printf(TEXT("X: %s\nY: %s"),
		*FBrCompassRules::FormatCoordinate(Position.X), *FBrCompassRules::FormatCoordinate(Position.Y));
	float CoordinateY = 64 * Scale;
	PanelText(Coordinates, FLinearColor::White, true, CoordinateY, 2);
	float TextWidth = 0, TextHeight = 0;
	const float CenterX = ViewLayout.Right.Min.X + ViewLayout.Right.Width() * 0.5f;
	const float HalfWidth = (ViewLayout.Right.Width() - 20 * Scale) / 2;
	if (HalfWidth < 10 * Scale) return;

	const float Top = 8 * Scale;
	const float Baseline = Top + 40 * Scale;
	DrawRect(FLinearColor(0, 0, 0, 0.65f), CenterX - HalfWidth, Top, HalfWidth * 2, 48 * Scale);
	DrawLine(CenterX - HalfWidth, Baseline, CenterX + HalfWidth, Baseline, FLinearColor::White, Scale);
	// Fixed 120-degree window; ticks move underneath the fixed center chevron.
	for (int32 Bearing = 0; Bearing < 360; Bearing += 15)
	{
		const double Offset = FBrCompassRules::HeadingOffset(Bearing, Yaw);
		if (FMath::Abs(Offset) > 60.0) continue;
		const float X = CenterX + Offset / 60.0 * HalfWidth;
		const bool bCardinal = Bearing % 90 == 0;
		DrawLine(X, Baseline - (bCardinal ? 10 : 5) * Scale, X, Baseline, FLinearColor::White, Scale);
		if (!bCardinal) continue;
		const FString Label = FBrCompassRules::CardinalLabel(Bearing);
		GetTextSize(Label, TextWidth, TextHeight, nullptr, Scale);
		// Do not let labels at the strip edge spill into adjacent HUD regions.
		if (X - TextWidth / 2 < CenterX - HalfWidth || X + TextWidth / 2 > CenterX + HalfWidth) continue;
		DrawText(Label, FLinearColor::White, X - TextWidth / 2, Top + 12 * Scale, nullptr, Scale);
	}
	const FLinearColor Marker = FLinearColor::Yellow;
	DrawLine(CenterX - 5 * Scale, Top + 3 * Scale, CenterX, Top + 9 * Scale, Marker, 2 * Scale);
	DrawLine(CenterX + 5 * Scale, Top + 3 * Scale, CenterX, Top + 9 * Scale, Marker, 2 * Scale);
}
