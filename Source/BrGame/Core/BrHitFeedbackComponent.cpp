#include "BrHitFeedbackComponent.h"
#include "BrHitFeedbackRules.h"
#include "BrCharacter.h"
#include "BrDeveloperSettings.h"
#include "BrFogComponent.h"
#include "BrLifeComponent.h"
#include "BrLog.h"
#include "Camera/CameraTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInstanceDynamic.h"

UBrHitFeedbackComponent::UBrHitFeedbackComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}
ABrCharacter* UBrHitFeedbackComponent::Character() const { return CastChecked<ABrCharacter>(GetOwner()); }
float UBrHitFeedbackComponent::Now() const { return GetWorld()->GetTimeSeconds(); }
bool UBrHitFeedbackComponent::LocalVictim() const
{
	const ABrCharacter* C = Character();
	const APlayerController* PC = Cast<APlayerController>(C->GetController());
	return PC && PC->IsLocalController() && PC->GetPawn() == C && PC->GetViewTarget() == C && C->GetLife()->IsAlive();
}
void UBrHitFeedbackComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() != NM_DedicatedServer)
	{
		const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
		UMaterialInterface* Base = S->HitOverlay.LoadSynchronous();
		if (!Base)
		{
			UE_LOG(LogBr, Error, TEXT("Hit feedback overlay missing; run Scripts/Content/create_hit_feedback_assets.py"));
		}
		else
		{
			UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, this);
			Material->SetVectorParameterValue(TEXT("HitColor"), S->HitMeshColor);
			Material->SetScalarParameterValue(TEXT("HitOpacity"), FMath::Clamp(S->HitMeshOpacity, 0.f, 1.f));
			Overlay = Material;
		}
	}
}
void UBrHitFeedbackComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (bReactionApplied) Character()->GetMesh()->SetOverlayMaterial(PreviousOverlay);
	Indicators.Reset();
	Super::EndPlay(Reason);
}
void UBrHitFeedbackComponent::ServerDamage(float LostHealth, const FVector& Origin, bool bExplosion)
{
	if (!GetOwner()->HasAuthority() || Origin.ContainsNaN() ||
		!FBrHitFeedbackRules::DamageEligible(LostHealth, Character()->GetLife()->IsAlive(), Character()->GetLife()->IsInvulnerable())) return;
	ClientDamage(Origin, bExplosion, static_cast<uint8>(FMath::Clamp(FMath::CeilToInt(LostHealth), 1, 255)));
	MulticastReaction((Character()->GetActorLocation() - Origin).GetSafeNormal2D());
}
void UBrHitFeedbackComponent::ClientDamage_Implementation(FVector_NetQuantize Origin, bool bExplosion, uint8 Damage)
{
	if (!LocalVictim()) return;
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	Indicators.RemoveAll([this, S](const FBrHitIndicator& I) { return Now() - I.Time >= S->HitIndicatorDuration; });
	const int32 Cap = FMath::Clamp(S->HitIndicatorCap, 1, 32);
	while (Indicators.Num() >= Cap) Indicators.RemoveAt(0);
	Indicators.Add({Origin, Now()});
	++OwnerEvents;
	const float Duration = bExplosion ? S->GrenadeHitDuration : S->BulletHitDuration;
	const float Amplitude = bExplosion ? S->GrenadeHitAngle : S->BulletHitAngle;
	// Refresh the strongest active profile, never sum impulses.
	const bool bActive = Now() - ShakeTime < ShakeDuration;
	ShakeDuration = FMath::Max(0.f, bActive ? FMath::Max(ShakeDuration, Duration) : Duration);
	ShakeAmplitude = FMath::Clamp(bActive ? FMath::Max(ShakeAmplitude, Amplitude) : Amplitude, 0.f, FMath::Max(S->BulletHitAngle, S->GrenadeHitAngle));
	JoltAmplitude = bExplosion ? FMath::Max(0.f, S->GrenadeHitJolt) : (bActive ? JoltAmplitude : 0);
	ShakeTime = FlashTime = Now();
	ShakeSeed = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f));
	FlashStrength = FMath::Clamp(Damage / FMath::Max(1.f, S->HitFlashFullDamage), 0.f, 1.f);
}
void UBrHitFeedbackComponent::MulticastReaction_Implementation(FVector_NetQuantizeNormal Away)
{
	if (GetNetMode() == NM_DedicatedServer ||
		!FBrHitFeedbackRules::RemoteEligible(UBrFogComponent::LocallyVisible(Character()), Character()->IsLocallyControlled(), Character()->GetLife()->IsAlive())) return;
	ReactionAway = Away;
	ReactionTime = Now();
	++RemoteEvents;
}
void UBrHitFeedbackComponent::Camera(FMinimalViewInfo& View) const
{
	if (!LocalVictim()) return;
	const float Age = Now() - ShakeTime;
	// One camera-local view-axis twist. World yaw and local roll would compound at
	// steep pitches (doubling the screen-plane rotation at 90 degrees).
	View.Rotation.Roll += FBrHitFeedbackRules::Shake(Age, ShakeDuration, ShakeAmplitude, ShakeSeed.X);
	View.Location += FVector(ShakeSeed.Y, ShakeSeed.Z, ShakeSeed.X).GetSafeNormal() *
		FBrHitFeedbackRules::Shake(Age, ShakeDuration, JoltAmplitude, 1);
}
FVector UBrHitFeedbackComponent::MeshOffset() const
{
	const float Envelope = FBrHitFeedbackRules::Envelope(Now() - ReactionTime, UBrDeveloperSettings::Get()->HitReactionDuration);
	return Character()->GetActorQuat().UnrotateVector(ReactionAway) * FMath::Max(0.f, UBrDeveloperSettings::Get()->HitReactionDistance) * Envelope;
}
bool UBrHitFeedbackComponent::UpdateReaction()
{
	const bool bActive = FBrHitFeedbackRules::RemoteEligible(UBrFogComponent::LocallyVisible(Character()),
		Character()->IsLocallyControlled(), Character()->GetLife()->IsAlive()) &&
		FBrHitFeedbackRules::Envelope(Now() - ReactionTime, UBrDeveloperSettings::Get()->HitReactionDuration) > 0;
	if (!bActive) ReactionTime = -1000; // Hidden hits never replay on visibility entry.
	const bool bChanged = bActive || bReactionApplied;
	if (bActive && !bReactionApplied)
	{
		PreviousOverlay = Character()->GetMesh()->GetOverlayMaterial();
		Character()->GetMesh()->SetOverlayMaterial(Overlay);
	}
	else if (!bActive && bReactionApplied)
	{
		Character()->GetMesh()->SetOverlayMaterial(PreviousOverlay);
		PreviousOverlay = nullptr;
	}
	bReactionApplied = bActive;
	return bChanged;
}
void UBrHitFeedbackComponent::Draw(AHUD* HUD, UCanvas* Canvas) const
{
	if (!HUD || !Canvas || !LocalVictim()) return;
	const UBrDeveloperSettings* S = UBrDeveloperSettings::Get();
	const FVector2D Size(Canvas->SizeX, Canvas->SizeY);
	const float Scale = Size.Y / 720.f;
	const float Flash = FBrHitFeedbackRules::Envelope(Now() - FlashTime, S->HitFlashDuration) *
		FlashStrength * FMath::Clamp(S->HitFlashOpacity, 0.f, 1.f);
	// Layered translucent strips form a cheap Canvas vignette.
	for (int32 I = 0; I < 12; ++I)
	{
		const float W = FMath::Max(0.f, S->HitEdgeWidth) * Scale / 12;
		const FLinearColor Color(1, 0, 0, Flash * (1.f - I / 12.f));
		HUD->DrawRect(Color, I * W, 0, W, Size.Y);
		HUD->DrawRect(Color, Size.X - (I + 1) * W, 0, W, Size.Y);
		HUD->DrawRect(Color, 0, I * W, Size.X, W);
		HUD->DrawRect(Color, 0, Size.Y - (I + 1) * W, Size.X, W);
	}
	APlayerController* PC = Cast<APlayerController>(Character()->GetController());
	FVector2D PawnScreen;
	if (!PC->ProjectWorldLocationToScreen(Character()->GetActorLocation(), PawnScreen)) return;
	const FRotator CameraRotation = PC->PlayerCameraManager->GetCameraRotation();
	for (const FBrHitIndicator& I : Indicators)
	{
		const float Alpha = FBrHitFeedbackRules::Envelope(Now() - I.Time, S->HitIndicatorDuration);
		if (Alpha <= 0) continue;
		const FVector2D Direction = FBrHitFeedbackRules::ScreenDirection(I.Origin - Character()->GetActorLocation(), CameraRotation);
		const FVector2D Edge = FBrHitFeedbackRules::EdgePoint(PawnScreen, Direction, Size, 22 * Scale);
		// Arc centred on the ray/edge intersection, curving back toward the victim.
		const FVector2D Tangent(-Direction.Y, Direction.X);
		FVector2D Last = Edge;
		for (int32 Segment = 0; Segment <= 16; ++Segment)
		{
			const float Angle = FMath::Lerp(-0.65f, 0.65f, Segment / 16.f);
			const FVector2D P = Edge + Tangent * FMath::Sin(Angle) * 55 * Scale -
				Direction * (1.f - FMath::Cos(Angle)) * 55 * Scale;
			if (Segment) HUD->DrawLine(Last.X, Last.Y, P.X, P.Y, FLinearColor(1, 0, 0, Alpha), 5 * Scale);
			Last = P;
		}
	}
}
