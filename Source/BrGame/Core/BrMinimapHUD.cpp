#include "BrHUD.h"
#include "BrMinimapRules.h"
#include "BrFogComponent.h"
#include "BrVisibilitySubsystem.h"
#include "BrZoneBounds.h"
#include "BrSafeZone.h"
#include "BrCharacter.h"
#include "BrPlayerController.h"
#include "BrPlayerState.h"
#include "BrGameState.h"
#include "BrLifeComponent.h"
#include "BrDeveloperSettings.h"
#include "BrLog.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"

void ABrHUD::EndPlay(const EEndPlayReason::Type Reason)
{
	if (MapCapture) MapCapture->DestroyComponent();
	MapCapture = nullptr;
	MapMaterial = nullptr;
	MapImage = nullptr;
	Super::EndPlay(Reason);
}

bool ABrHUD::InitializeMap()
{
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	UMaterialInterface* Base = Settings->MinimapMaterial.LoadSynchronous();
	if (!Base)
	{
		UE_LOG(LogBr, Error, TEXT("Minimap material missing; run Scripts/Content/create_minimap_assets.py."));
		return false;
	}
	MapBounds = ABrZoneBounds::FindPlayableBounds(GetWorld());
	const double Width = FBrMinimapRules::FitRadius(MapBounds) * 2;
	const int32 Size = FMath::Clamp(Settings->MinimapTextureSize, 128, 2048);
	MapImage = UKismetRenderingLibrary::CreateRenderTarget2D(this, Size, Size, RTF_RGBA8);
	MapMaterial = UMaterialInstanceDynamic::Create(Base, this);
	MapCapture = NewObject<USceneCaptureComponent2D>(this);
	if (!MapImage || !MapMaterial || !MapCapture)
	{
		UE_LOG(LogBr, Error, TEXT("Minimap could not allocate capture resources."));
		return false;
	}
	MapCapture->bCaptureEveryFrame = false;
	MapCapture->bCaptureOnMovement = false;
	MapCapture->ProjectionType = ECameraProjectionMode::Orthographic;
	MapCapture->OrthoWidth = Width;
	MapCapture->bAutoCalculateOrthoPlanes = false;
	// With automatic planes disabled, UE's orthographic defaults span WORLD_MAX.
	MapCapture->CaptureSource = SCS_SceneColorHDRNoAlpha;
	MapCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	// Flat unlit cartography avoids map-specific exposure/lighting and local fog
	// volumes. Only static, nonreplicated world geometry is ever captured.
	MapCapture->ShowFlags.SetLighting(false);
	MapCapture->ShowFlags.SetPostProcessing(false);
	MapCapture->ShowFlags.SetAtmosphere(false);
	MapCapture->ShowFlags.SetFog(false);
	MapCapture->ShowFlags.SetDynamicShadows(false);
	int32 Count = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->GetIsReplicated() || It->IsA<APawn>() || It->IsActorBeingDestroyed() ||
			It->ActorHasTag(TEXT("BrMinimapExclude"))) continue;
		// Includes placed static meshes, Blueprint world geometry, landscape and
		// instanced static geometry without depending on editor/Landscape modules.
		TInlineComponentArray<UPrimitiveComponent*> Primitives(*It);
		for (UPrimitiveComponent* Mesh : Primitives)
			if (Mesh->Mobility == EComponentMobility::Static && Mesh->IsVisible() && !Mesh->bHiddenInGame &&
				Mesh->GetCollisionObjectType() == ECC_WorldStatic && !Mesh->ComponentHasTag(TEXT("BrMinimapExclude")) &&
				!It->IsHidden() && Mesh->Bounds.GetBox().IntersectXY(MapBounds))
			{
				MapCapture->ShowOnlyComponent(Mesh);
				++Count;
			}
	}
	if (!Count)
	{
		UE_LOG(LogBr, Error, TEXT("Minimap found no static world meshes inside playable bounds."));
		return false;
	}
	MapCapture->TextureTarget = MapImage;
	MapCapture->RegisterComponent();
	MapCapture->SetWorldLocationAndRotation(FVector(MapBounds.GetCenter().X, MapBounds.GetCenter().Y,
		MapBounds.Max.Z + Width + 5000), FRotator(-90, 0, 0));
	MapCapture->CaptureScene();
	MapMaterial->SetTextureParameterValue(TEXT("MapImage"), MapImage);
	MapMaterial->SetVectorParameterValue(TEXT("CaptureBounds"), FLinearColor(
		MapBounds.GetCenter().X, MapBounds.GetCenter().Y, Width, Width));
	UE_LOG(LogBr, Display, TEXT("Minimap static capture: World=%s Meshes=%d Size=%d Bounds=%s (one capture/map load)"),
		*GetWorld()->GetMapName(), Count, Size, *MapBounds.ToString());
	return true;
}

void ABrHUD::DrawMinimap()
{
	const ABrPlayerController* PC = Cast<ABrPlayerController>(PlayerOwner);
	const ABrGameState* GS = GetWorld()->GetGameState<ABrGameState>();
	if (!Canvas || !PC || !PC->IsLocalController() || PC->IsMenuOpen() || !GS || GS->bLobby || GS->bResults || !FApp::CanEverRender()) return;
	const ABrCharacter* Viewed = PC->IsFlowSpectating() ? PC->GetFlowSpectatorPawn() : Cast<ABrCharacter>(PC->GetPawn());
	const ABrPlayerState* Viewer = UBrVisibilitySubsystem::ViewerState(PC);
	const UBrFogComponent* Fog = PC->FindComponentByClass<UBrFogComponent>();
	if (!Viewed || !Viewer || !Fog) return; // fail closed during spectator handoff
	if (MapWorld.Get() != GetWorld())
	{
		if (MapCapture) MapCapture->DestroyComponent();
		MapCapture = nullptr;
		MapMaterial = nullptr;
		MapImage = nullptr;
		MapWorld = GetWorld();
		bMapFailed = false;
	}
	if (!MapMaterial && !bMapFailed) bMapFailed = !InitializeMap();
	if (bMapFailed) { DrawText(TEXT("Map unavailable (see log)"), FLinearColor::Red, Canvas->SizeX - 240, 30); return; }
	const bool bFog = UBrVisibilitySubsystem::Active(GetWorld());
	// Do not render a clear map during mask initialization/reset.
	if (bFog && (!Fog->GetVisibleMask() || !Fog->GetExploredMask())) return;
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	const bool bFull = PC->IsMapOpen();
	// Canvas uses physical pixels. Reference 720p, bounded on tiny/large views.
	const float Scale = FMath::Clamp(Canvas->SizeY / 720.f, 0.65f, 2.f);
	const float Size = bFull ? FMath::Min(Canvas->SizeX, Canvas->SizeY) *
		FMath::Clamp(Settings->FullMapViewportFraction, 0.3f, 0.9f) :
		FMath::Min(FMath::Clamp(Settings->MinimapSize, 100.f, 500.f) * Scale, Canvas->SizeY * 0.45f);
	const FVector2D Top = bFull ? FVector2D((Canvas->SizeX - Size) / 2, (Canvas->SizeY - Size) / 2) :
		FVector2D(Canvas->SizeX - Size - 16 * Scale, 16 * Scale);
	const FVector2D Center = Top + FVector2D(Size / 2, Size / 2);
	const double Radius = bFull ? FBrMinimapRules::FitRadius(MapBounds) : FMath::Max(100.f, Settings->MinimapRadius);
	const FVector Origin = bFull ? MapBounds.GetCenter() : Viewed->GetActorLocation();
	const double Yaw = bFull ? 0 : Viewed->GetCameraYaw();
	const FVector Forward = FRotator(0, Yaw, 0).Vector();
	MapMaterial->SetVectorParameterValue(TEXT("View"), FLinearColor(Origin.X, Origin.Y, Radius, bFull ? 0 : 1));
	MapMaterial->SetVectorParameterValue(TEXT("Basis"), FLinearColor(Forward.X, Forward.Y, Size, bFog ? 1 : 0));
	if (bFog)
	{
		const FBox& Bounds = Fog->GetMapBounds();
		MapMaterial->SetTextureParameterValue(TEXT("VisibleMask"), Fog->GetVisibleMask());
		MapMaterial->SetTextureParameterValue(TEXT("ExploredMask"), Fog->GetExploredMask());
		MapMaterial->SetVectorParameterValue(TEXT("FogBounds"), FLinearColor(Bounds.Min.X, Bounds.Min.Y,
			Bounds.GetSize().X, Bounds.GetSize().Y));
	}
	MapMaterial->SetScalarParameterValue(TEXT("DimOpacity"), Settings->DimOpacity);
	if (!bFog)
		MapMaterial->SetVectorParameterValue(TEXT("FogBounds"), FLinearColor(MapBounds.Min.X, MapBounds.Min.Y,
			MapBounds.GetSize().X, MapBounds.GetSize().Y));
	const ABrSafeZone* Zone = nullptr;
	for (TActorIterator<ABrSafeZone> It(GetWorld()); It; ++It) { Zone = *It; break; }
	const FBrZoneCircle Current = Zone ? Zone->Circle() : FBrZoneCircle();
	const FBrZoneCircle Target = Zone ? Zone->State.Target : FBrZoneCircle();
	MapMaterial->SetVectorParameterValue(TEXT("Zone"), FLinearColor(Current.Center.X, Current.Center.Y, Current.Radius, Zone ? 1 : 0));
	MapMaterial->SetVectorParameterValue(TEXT("TargetZone"), FLinearColor(Target.Center.X, Target.Center.Y, Target.Radius, 0));
	DrawMaterial(MapMaterial, Top.X, Top.Y, Size, Size, 0, 0, 1, 1);
	const float Marker = FMath::Clamp(Settings->MinimapMarkerSize, 2.f, 16.f) * Scale;
	auto Arrow = [&](FVector2D P, FVector2D Direction, FLinearColor Color)
	{
		Direction.Normalize();
		const FVector2D Tip = P + Direction * Marker * 1.8;
		const FVector2D Back = P - Direction * Marker;
		const FVector2D Side(-Direction.Y * Marker, Direction.X * Marker);
		DrawLine(Tip.X, Tip.Y, Back.X + Side.X, Back.Y + Side.Y, Color, 2 * Scale);
		DrawLine(Tip.X, Tip.Y, Back.X - Side.X, Back.Y - Side.Y, Color, 2 * Scale);
		DrawLine(Back.X + Side.X, Back.Y + Side.Y, Back.X - Side.X, Back.Y - Side.Y, Color, Scale);
	};
	for (TActorIterator<ABrCharacter> It(GetWorld()); It; ++It)
	{
		const ABrPlayerState* PS = It->GetPlayerState<ABrPlayerState>();
		if (!PS || !FBrMinimapRules::Marker(GS->MatchSettings.Mode == EBrMode::Grouped,
			Viewer->Team, Viewer->GetPlayerId(), PS->Team, PS->GetPlayerId(), Fog->IsPresentedVisible(*It),
			It->GetLife()->State != EBrLifeState::Eliminated)) continue;
		const FVector2D Offset = FBrMinimapRules::Offset(It->GetActorLocation(), Origin, Yaw, Radius);
		if (!FBrMinimapRules::Inside(Offset, !bFull)) continue;
		// Keep exact world alignment except icons touching the perimeter.
		const double Inset = 1 - Marker * 2 / (Size / 2);
		const FVector2D IconOffset = FBrMinimapRules::Inside(Offset / Inset, !bFull) ?
			Offset : FBrMinimapRules::Edge(Offset, !bFull) * Inset;
		const FVector2D P = Center + IconOffset * (Size / 2);
		const bool bFriend = UBrVisibilitySubsystem::Friendly(Viewer, PS);
		const FLinearColor Color = *It == Viewed ? FLinearColor::White : bFriend ? PS->TeamColor : FLinearColor::Red;
		if (It->GetLife()->State == EBrLifeState::Downed && bFriend)
		{
			const float Pulse = Marker * (1 + 0.25f * FMath::Sin(GetWorld()->GetTimeSeconds() * 5));
			DrawLine(P.X - Pulse, P.Y, P.X + Pulse, P.Y, FLinearColor::Red, 3 * Scale);
			DrawLine(P.X, P.Y - Pulse, P.X, P.Y + Pulse, FLinearColor::Red, 3 * Scale);
		}
		else if (*It == Viewed)
			Arrow(P, FBrMinimapRules::Offset(It->GetActorLocation() + It->GetActorForwardVector(), It->GetActorLocation(), Yaw, 1), Color);
		else DrawRect(Color, P.X - Marker / 2, P.Y - Marker / 2, Marker, Marker);
		if (bFull && *It == Viewed)
		{
			const FVector CameraForward = FRotator(0, Viewed->GetCameraYaw(), 0).Vector();
			const FVector2D Direction(CameraForward.Y, -CameraForward.X);
			DrawLine(P.X, P.Y, P.X + Direction.X * Marker * 5, P.Y + Direction.Y * Marker * 5, FLinearColor::Yellow, Scale);
		}
	}
	if (Zone && !bFull)
	{
		const FVector2D Offset = FBrMinimapRules::Offset(Current.Center, Origin, Yaw, Radius);
		if (!FBrMinimapRules::Inside(Offset, true))
			Arrow(Center + FBrMinimapRules::Edge(Offset, true) * (Size / 2 - Marker * 3), Offset, FLinearColor::Green);
	}
	DrawText(bFull ? TEXT("Full map | +X North | Yellow: camera | Map / Esc: close") : TEXT("Map"),
		FLinearColor::White, Top.X, Top.Y + Size + 4 * Scale, nullptr, Scale);
}
