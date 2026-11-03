#include "BrFogComponent.h"
#include "BrVisibilitySubsystem.h"
#include "BrFogRules.h"
#include "BrDeveloperSettings.h"
#include "BrPlayerController.h"
#include "BrPlayerState.h"
#include "BrCharacter.h"
#include "BrGrenade.h"
#include "BrCombatVisual.h"
#include "BrZoneBounds.h"
#include "BrLog.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TextureResource.h"
#include "UnrealClient.h"

UBrFogComponent::UBrFogComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}
void UBrFogComponent::BeginPlay()
{
	Super::BeginPlay();
	StartView();
}
void UBrFogComponent::StartView()
{
	RestorePresentation();
	GetWorld()->GetTimerManager().ClearTimer(Timer);
	if (IsValid(Volume)) Volume->Destroy();
	Volume = nullptr;
	Material = nullptr;
	VisibleMask = nullptr;
	ExploredMask = nullptr;
	ObstacleLastSeen.Reset();
	LastGroup = MIN_int64;
	PresentedGroup = MIN_int64;
	PresentedVisible.Reset();
	Sources.Reset();
	bActive = false;
	bRenderFailed = false;
	const ABrPlayerController* PC = Cast<ABrPlayerController>(GetOwner());
	if (PC && PC->IsLocalController())
		GetWorld()->GetTimerManager().SetTimer(Timer, this, &ThisClass::Update,
			1.f / FMath::Clamp(UBrDeveloperSettings::Get()->FogUpdateRate, 1.f, 60.f), true);
}
void UBrFogComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	RestorePresentation();
	GetWorld()->GetTimerManager().ClearTimer(Timer);
	if (IsValid(Volume)) Volume->Destroy();
	Super::EndPlay(Reason);
}
void UBrFogComponent::RestorePresentation()
{
	for (const auto& Pair : OriginalObstacles)
		if (UPrimitiveComponent* Primitive = Pair.Key.Get())
		{
			Primitive->SetRenderCustomDepth(Pair.Value.bCustomDepth);
			Primitive->SetCustomDepthStencilValue(Pair.Value.Stencil);
		}
	OriginalObstacles.Reset();
	ObstacleLastSeen.Reset();
	for (const auto& Pair : OriginalHidden)
		if (UPrimitiveComponent* Primitive = Pair.Key.Get())
		{
			Primitive->SetHiddenInGame(Pair.Value);
			if (Primitive->GetOwner()->IsA<ABrCharacter>())
			{
				Primitive->SetRenderCustomDepth(false);
				Primitive->SetCustomDepthStencilValue(0);
			}
		}
	OriginalHidden.Reset();
}
void UBrFogComponent::SeeObstacle(UPrimitiveComponent* Primitive, double Now)
{
	// Only placed, non-replicated static map meshes. Never opt gameplay actors,
	// pickups, pawns, grenades or their attached geometry into the fog exemption.
	if (!IsValid(Primitive) || !Primitive->IsA<UStaticMeshComponent>() ||
		Primitive->Mobility != EComponentMobility::Static ||
		!Primitive->GetOwner()->IsA<AStaticMeshActor>() || Primitive->GetOwner()->GetIsReplicated() ||
		Primitive->GetCollisionObjectType() != ECC_WorldStatic) return;
	static const FName GroundTag(TEXT("BrFogGround"));
	if (Primitive->ComponentHasTag(GroundTag) || Primitive->GetOwner()->ActorHasTag(GroundTag) ||
		FBrFogRules::GroundLike(Primitive->Bounds.BoxExtent)) return;
	if (!OriginalObstacles.Contains(Primitive))
	{
		// Respect authored custom-depth users rather than stealing their stencil.
		if (Primitive->bRenderCustomDepth || Primitive->CustomDepthStencilValue != 0) return;
		OriginalObstacles.Add(Primitive, {Primitive->bRenderCustomDepth != 0, Primitive->CustomDepthStencilValue});
	}
	ObstacleLastSeen.Add(Primitive, Now);
}
void UBrFogComponent::UpdateObstacles(bool bShow, double Now)
{
	for (auto It = OriginalObstacles.CreateIterator(); It; ++It)
	{
		UPrimitiveComponent* Primitive = It.Key().Get();
		if (!Primitive) { It.RemoveCurrent(); continue; }
		const double* Stamp = bShow ? ObstacleLastSeen.Find(It.Key()) : nullptr;
		const int32 Stencil = FBrFogRules::ObstacleStencil(Stamp != nullptr,
			Stamp ? Now - *Stamp : 0, UBrDeveloperSettings::Get()->FogDimDelay);
		Primitive->SetCustomDepthStencilValue(Stencil);
		Primitive->SetRenderCustomDepth(Stencil != 0);
	}
	for (auto It = ObstacleLastSeen.CreateIterator(); It; ++It)
		if (!It.Key().IsValid()) It.RemoveCurrent();
}
bool UBrFogComponent::LocallyVisible(const AActor* Actor)
{
	if (!Actor || !UBrVisibilitySubsystem::Active(Actor->GetWorld())) return true;
	for (FConstPlayerControllerIterator It = Actor->GetWorld()->GetPlayerControllerIterator(); It; ++It)
		if (const ABrPlayerController* PC = Cast<ABrPlayerController>(It->Get()))
			if (PC->IsLocalController())
				if (const UBrFogComponent* Fog = PC->FindComponentByClass<UBrFogComponent>()) return Fog->CanSee(Actor);
	return false;
}
bool UBrFogComponent::CanSee(const AActor* Actor) const
{
	if (!bActive || !Actor) return !UBrVisibilitySubsystem::Active(GetWorld());
	const ABrPlayerState* Viewer = UBrVisibilitySubsystem::ViewerState(Cast<ABrPlayerController>(GetOwner()));
	if (const ABrCharacter* Character = Cast<ABrCharacter>(Actor))
		if (UBrVisibilitySubsystem::Friendly(Viewer, Character->GetPlayerState<ABrPlayerState>())) return true;
	const double R2 = FMath::Square(UBrDeveloperSettings::Get()->VisionRadius);
	for (const TWeakObjectPtr<ABrCharacter>& Entry : Sources)
		if (const ABrCharacter* From = Entry.Get())
			if (FVector::DistSquared2D(From->GetActorLocation(), Actor->GetActorLocation()) <= R2 &&
				UBrVisibilitySubsystem::LOS(GetWorld(), From, Actor->GetActorLocation(), Actor)) return true;
	return false;
}
bool UBrFogComponent::MatchesActiveView() const
{
	const ABrPlayerState* Viewer = UBrVisibilitySubsystem::ViewerState(Cast<ABrPlayerController>(GetOwner()));
	return bActive && Viewer && LastGroup == UBrVisibilitySubsystem::Group(Viewer);
}
bool UBrFogComponent::IsPresentedVisible(const AActor* Actor) const
{
	if (!Actor) return false;
	if (!UBrVisibilitySubsystem::Active(GetWorld())) return true;
	const ABrPlayerState* Viewer = UBrVisibilitySubsystem::ViewerState(Cast<ABrPlayerController>(GetOwner()));
	return Viewer && PresentedGroup == UBrVisibilitySubsystem::Group(Viewer) && PresentedVisible.Contains(Actor);
}
bool UBrFogComponent::InitializeRendering()
{
	Bounds = ABrZoneBounds::FindPlayableBounds(GetWorld());
	if (!Bounds.IsValid || Bounds.GetSize().X < 1 || Bounds.GetSize().Y < 1)
	{
		UE_LOG(LogBr, Error, TEXT("Fog requires valid playable bounds."));
		return false;
	}
	UMaterialInterface* Base = UBrDeveloperSettings::Get()->FogMaterial.LoadSynchronous();
	if (!Base)
	{
		UE_LOG(LogBr, Error, TEXT("Fog material missing: run Scripts/Content/create_fog_assets.py."));
		return false;
	}
	const int32 Size = FMath::Clamp(UBrDeveloperSettings::Get()->FogTextureSize, 128, 4096);
	VisibleMask = UKismetRenderingLibrary::CreateRenderTarget2D(this, Size, Size, RTF_R8);
	// R needs full precision for long-round timestamps; G is bilinear coverage.
	// One RG32f target replaces all per-group RGBA32f targets.
	ExploredMask = UKismetRenderingLibrary::CreateRenderTarget2D(this, Size, Size,
		RTF_RG32f, FLinearColor::Transparent);
	Volume = GetWorld()->SpawnActor<APostProcessVolume>();
	if (!VisibleMask || !ExploredMask || !Volume)
	{
		UE_LOG(LogBr, Error, TEXT("Fog render resources could not be allocated."));
		return false;
	}
	VisibleMask->AddressX = TA_Clamp;
	VisibleMask->AddressY = TA_Clamp;
	VisibleMask->Filter = TF_Bilinear;
	VisibleMask->UpdateResource();
	ExploredMask->AddressX = TA_Clamp;
	ExploredMask->AddressY = TA_Clamp;
	ExploredMask->Filter = TF_Bilinear;
	ExploredMask->UpdateResource();
	Material = UMaterialInstanceDynamic::Create(Base, this);
	Material->SetTextureParameterValue(TEXT("VisibleMask"), VisibleMask);
	Material->SetTextureParameterValue(TEXT("ExploredMask"), ExploredMask);
	Material->SetVectorParameterValue(TEXT("FogBounds"), FLinearColor(Bounds.Min.X, Bounds.Min.Y,
		Bounds.GetSize().X, Bounds.GetSize().Y));
	Volume->bUnbound = true;
	Volume->Priority = 100;
	Volume->Settings.AddBlendable(Material, 1);
	UE_LOG(LogBr, Display, TEXT("Fog rendering ready: Bounds=%s Texture=%d Rays=%d"),
		*Bounds.ToString(), Size, UBrDeveloperSettings::Get()->FogRayCount);
	UE_LOG(LogBr, Display, TEXT("Fog RT budget: Visible=R8 Explored=RG32f ExplorationTargets=1 Payload=%.2fMiB"),
		double(Size) * Size * 9 / (1024 * 1024));
	return true;
}
void UBrFogComponent::DrawMask(UTextureRenderTarget2D* Target, const FLinearColor& Color)
{
	for (FCanvasUVTri& Tri : Triangles) Tri.V0_Color = Tri.V1_Color = Tri.V2_Color = Color;
	UCanvas* Canvas = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, Target, Canvas, Size, Context);
	if (Canvas) Canvas->K2_DrawTriangle(nullptr, Triangles);
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
}
void UBrFogComponent::ValidateSmokeMasks(int32 Stage)
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("BrFogSmoke")) || !FApp::CanEverRender()) return;
	if (!VisibleMask || !ExploredMask || Sources.IsEmpty() || !Sources[0].IsValid())
	{
		UE_LOG(LogBr, Error, TEXT("Fog GPU probe missing resources or vision source."));
		return;
	}
	const ABrPlayerState* Viewer = UBrVisibilitySubsystem::ViewerState(Cast<ABrPlayerController>(GetOwner()));
	const APawn* ViewerPawn = Viewer ? Viewer->GetPawn() : nullptr;
	if (!ViewerPawn)
	{
		UE_LOG(LogBr, Error, TEXT("Fog GPU probe missing viewer pawn."));
		return;
	}
	const FVector2D UV = FBrFogRules::UV(ViewerPawn->GetActorLocation(), Bounds);
	const int32 X = FMath::Clamp(FMath::FloorToInt(UV.X * VisibleMask->SizeX), 0, VisibleMask->SizeX - 1);
	const int32 Y = FMath::Clamp(FMath::FloorToInt(UV.Y * VisibleMask->SizeY), 0, VisibleMask->SizeY - 1);
	// Kismet's pixel helper only accepts RGBA formats. Use the engine readback
	// APIs for R8/RG32f; this synchronous path remains smoke-only.
	TArray<FColor> VisiblePixels;
	TArray<FLinearColor> ExploredPixels;
	const FIntRect PixelRect(X, Y, X + 1, Y + 1);
	FTextureRenderTargetResource* VisibleResource = VisibleMask->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* ExploredResource = ExploredMask->GameThread_GetRenderTargetResource();
	if (!VisibleResource || !ExploredResource ||
		!VisibleResource->ReadPixels(VisiblePixels, FReadSurfaceDataFlags(), PixelRect) ||
		!ExploredResource->ReadLinearColorPixels(ExploredPixels,
			FReadSurfaceDataFlags(RCM_MinMax), PixelRect) || VisiblePixels.Num() != 1 || ExploredPixels.Num() != 1)
	{
		UE_LOG(LogBr, Error, TEXT("Fog GPU probe reduced-format readback failed."));
		return;
	}
	const float Visible = VisiblePixels[0].R / 255.f;
	const float Stamp = ExploredPixels[0].R;
	const float Age = GetWorld()->GetTimeSeconds() + 1 - Stamp;
	if (Visible < 0.9f || Stamp <= 1 || Age < -0.5f || Age > 0.5f)
	{
		UE_LOG(LogBr, Error, TEXT("Fog GPU probe FAIL: Stage=%d Visible=%.3f Stamp=%.3f Age=%.3f"), Stage, Visible, Stamp, Age);
	}
	else
	{
		UE_LOG(LogBr, Display, TEXT("Fog GPU probe PASS: Stage=%d Visible=%.3f Stamp=%.3f Age=%.3f"), Stage, Visible, Stamp, Age);
	}
	// Smoke movements are centered in the map; the lower bounds corner is never
	// visited. Check both timestamp and green-channel exploration coverage.
	if (Stage == 5)
	{
		TArray<FLinearColor> UnknownPixels;
		if (!ExploredResource->ReadLinearColorPixels(UnknownPixels,
			FReadSurfaceDataFlags(RCM_MinMax), FIntRect(2, 2, 3, 3)) || UnknownPixels.Num() != 1)
		{
			UE_LOG(LogBr, Error, TEXT("Fog GPU unexplored probe readback failed."));
			return;
		}
		const FLinearColor Unknown = UnknownPixels[0];
		if (Unknown.R != 0 || Unknown.G != 0)
		{
			UE_LOG(LogBr, Error, TEXT("Fog GPU unexplored probe FAIL: Stamp=%.3f Coverage=%.3f"), Unknown.R, Unknown.G);
		}
		else
		{
			UE_LOG(LogBr, Display, TEXT("Fog GPU unexplored probe PASS: Stamp=0 Coverage=0"));
		}
	}
#endif
}
void UBrFogComponent::Update()
{
	const ABrPlayerController* PC = Cast<ABrPlayerController>(GetOwner());
	ABrPlayerState* Viewer = UBrVisibilitySubsystem::ViewerState(PC);
	bActive = UBrVisibilitySubsystem::Active(GetWorld());
	Sources.Reset();
	PresentedVisible.Reset();
	PresentedGroup = Viewer ? UBrVisibilitySubsystem::Group(Viewer) : MIN_int64;
	if (bActive && Viewer)
		for (TActorIterator<ABrCharacter> It(GetWorld()); It; ++It)
			if (UBrVisibilitySubsystem::Source(*It) && UBrVisibilitySubsystem::Friendly(Viewer, It->GetPlayerState<ABrPlayerState>()))
				Sources.Add(*It);
	// Primitive visibility is local-only. SetActorHiddenInGame would alter authority
	// relevancy on a listen server; never use it for this.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		if (It->IsA<ABrCharacter>() || It->IsA<ABrGrenade>() || It->IsA<ABrCombatVisual>())
		{
			const bool bShow = !bActive || CanSee(*It);
			if (bShow) PresentedVisible.Add(*It);
			const ABrCharacter* Character = Cast<ABrCharacter>(*It);
			const bool bFriendly = bActive && Character && UBrVisibilitySubsystem::Friendly(
				Viewer, Character->GetPlayerState<ABrPlayerState>());
			// bHiddenInGame is independent of gameplay's SetVisibility (life/invulnerability).
			TInlineComponentArray<UPrimitiveComponent*> Primitives(*It);
			for (UPrimitiveComponent* Primitive : Primitives)
			{
				const bool bOriginallyHidden = OriginalHidden.FindOrAdd(Primitive, Primitive->bHiddenInGame);
				Primitive->SetHiddenInGame(bOriginallyHidden || !bShow);
				if (Character)
				{
					// Reserve stencil 1 for friends. This preserves own/team meshes
					// even at authored map bounds and polygon rasterization edges.
					Primitive->SetRenderCustomDepth(bFriendly);
					Primitive->SetCustomDepthStencilValue(bFriendly ? 1 : 0);
				}
			}
		}
	for (auto HiddenIt = OriginalHidden.CreateIterator(); HiddenIt; ++HiddenIt)
		if (!HiddenIt.Key().IsValid()) HiddenIt.RemoveCurrent();
	if (Volume) Volume->bEnabled = bActive;
	if (!bActive) UpdateObstacles(false, GetWorld()->GetTimeSeconds());
	if (!bActive || GetWorld()->GetNetMode() == NM_DedicatedServer || !FApp::CanEverRender()) return;
	if (!Material && !bRenderFailed) bRenderFailed = !InitializeRendering();
	if (!Material) return;
	if (!Viewer) return;
	const int64 Group = UBrVisibilitySubsystem::Group(Viewer);
	if (LastGroup != Group)
	{
		// Reuse the same allocation for every target. No GPU or CPU group copies,
		// readback stalls, or transient allocations when cycling survivors.
		UKismetRenderingLibrary::ClearRenderTarget2D(this, ExploredMask, FLinearColor::Transparent);
		ObstacleLastSeen.Reset();
		UpdateObstacles(false, GetWorld()->GetTimeSeconds());
		LastGroup = Group;
		UE_LOG(LogBr, Verbose, TEXT("Fog group reset: Group=%lld Exploration=%s ResidentTargets=1"), Group, *ExploredMask->GetName());
	}
	const UBrDeveloperSettings* Settings = UBrDeveloperSettings::Get();
	const int32 Rays = FMath::Clamp(Settings->FogRayCount, 32, 2048);
	const double SeenTime = GetWorld()->GetTimeSeconds();
	const float Size = VisibleMask->SizeX;
	Triangles.Reset();
	Polygon.SetNumUninitialized(Rays, EAllowShrinking::No);
	for (const TWeakObjectPtr<ABrCharacter>& Entry : Sources)
	{
		ABrCharacter* From = Entry.Get();
		if (!From) continue;
		const FVector Origin = From->GetActorLocation() + FVector(0, 0, 20);
		const FVector2D Center = FBrFogRules::UV(Origin, Bounds) * Size;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(BrFogPolygon), false, From);
		for (int32 I = 0; I < Rays; ++I)
		{
			const double Angle = 2 * UE_PI * I / Rays;
			const FVector End = Origin + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Settings->VisionRadius;
			FHitResult Hit;
			const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Origin, End, UBrVisibilitySubsystem::VisionChannel, Query);
			if (bHit) SeeObstacle(Hit.GetComponent(), SeenTime);
			Polygon[I] = FBrFogRules::UV(bHit ? Hit.ImpactPoint : End, Bounds) * Size;
		}
		for (int32 I = 0; I < Rays; ++I)
		{
			FCanvasUVTri& Tri = Triangles.AddDefaulted_GetRef();
			Tri.V0_Pos = Center;
			Tri.V1_Pos = Polygon[I];
			Tri.V2_Pos = Polygon[(I + 1) % Rays];
		}
	}
	UpdateObstacles(true, SeenTime);
	UKismetRenderingLibrary::ClearRenderTarget2D(this, VisibleMask);
	DrawMask(VisibleMask, FLinearColor::White);
	// Persistent HDR target stores last-seen timestamps; no feedback sampling/ping-pong.
	const float Now = GetWorld()->GetTimeSeconds() + 1;
	DrawMask(ExploredMask, FLinearColor(Now, 1, 0, 1));
	Material->SetScalarParameterValue(TEXT("FogTime"), Now);
	Material->SetScalarParameterValue(TEXT("DimOpacity"), FMath::Clamp(Settings->DimOpacity, 0.f, 1.f));
	Material->SetScalarParameterValue(TEXT("FogDimDelay"), FMath::Max(0.01f, Settings->FogDimDelay));
	Material->SetScalarParameterValue(TEXT("EdgeFeather"), FMath::Clamp(Settings->FogEdgeFeather, 0.f, 150.f));
}
