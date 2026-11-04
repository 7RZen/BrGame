#include "BrPlayableGround.h"
#include "BrLog.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

bool FBrPlayableGround::Find(UWorld* World, const FVector& Candidate, FVector& Floor,
	const FBrGroundQuery& Settings, const AActor* Ignore)
{
	if (!World || Candidate.ContainsNaN()) return false;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(BrPlayableGround), false, Ignore);
	FHitResult Hit;
	const FVector Start = Candidate + FVector(0, 0, Settings.TraceUp);
	const FVector End = Candidate - FVector(0, 0, Settings.TraceDown);
	// First blocking surface only: never trace through a roof to bless the floor below it.
	// Match the zone's blocking channel, not only object type. TestArena's
	// immobile Floor uses BlockAllDynamic but still blocks WorldStatic traces.
	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Query);
	if (!bHit || !Hit.bBlockingHit || Hit.bStartPenetrating) return false;
	const UPrimitiveComponent* Component = Hit.GetComponent();
	const bool bStatic = Component && FBrPlayableGroundRules::StableSupport(
		Component->GetCollisionObjectType() == ECC_WorldStatic || Component->GetCollisionObjectType() == ECC_WorldDynamic,
		Component->IsSimulatingPhysics(), Component->GetComponentVelocity().SizeSquared());
	const bool bTagged = (Component && Component->ComponentHasTag(TEXT("BrFogGround"))) ||
		(Hit.GetActor() && Hit.GetActor()->ActorHasTag(TEXT("BrFogGround")));
	if (!FBrPlayableGroundRules::AcceptSurface(Hit.ImpactNormal.Z, bStatic, bTagged,
		Settings.bRequireTaggedStaticFloor))
	{
		UE_LOG(LogBr, VeryVerbose, TEXT("Ground surface rejected: Component=%s NormalZ=%.2f Static=%d Tagged=%d"),
			*GetNameSafe(Component), Hit.ImpactNormal.Z, bStatic, bTagged);
		return false;
	}
	if (Settings.MinFloorHalfWidth > 0)
	{
		if (!Component) return false;
		// Check both world-horizontal bounds and scaled mesh-local axes. The
		// latter prevents yaw rotation from inflating a thin prop's AABB.
		const FVector WorldExtent = Component->Bounds.BoxExtent;
		const FVector LocalExtent = Component->CalcBounds(FTransform(FQuat::Identity,
			FVector::ZeroVector, Component->GetComponentScale())).BoxExtent;
		const FVector LocalNormal = Component->GetComponentTransform().InverseTransformVectorNoScale(Hit.ImpactNormal);
		if (!FBrPlayableGroundRules::SupportsSurfaceFootprint(WorldExtent, LocalExtent, LocalNormal, Settings.MinFloorHalfWidth))
		{
			UE_LOG(LogBr, VeryVerbose, TEXT("Ground footprint rejected: Component=%s World=%s Local=%s Required=%.0f"),
				*Component->GetPathName(), *WorldExtent.ToString(), *LocalExtent.ToString(), Settings.MinFloorHalfWidth);
			return false;
		}
	}
	const FVector CapsuleCenter = Hit.ImpactPoint + FVector(0, 0, Settings.HalfHeight + 4);
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(Settings.Radius, Settings.HalfHeight);
	const bool bBlocked = Settings.bUsePawnProfile ?
		World->OverlapBlockingTestByProfile(CapsuleCenter, FQuat::Identity, TEXT("Pawn"), Capsule, Query) :
		World->OverlapBlockingTestByChannel(CapsuleCenter, FQuat::Identity, ECC_WorldStatic, Capsule, Query);
	if (bBlocked)
	{
		UE_LOG(LogBr, VeryVerbose, TEXT("Ground capsule blocked: Component=%s Floor=%s"),
			*GetNameSafe(Component), *Hit.ImpactPoint.ToString());
		return false;
	}
	Floor = Hit.ImpactPoint;
	return true;
}
