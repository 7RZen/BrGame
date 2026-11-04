#pragma once

#include "CoreMinimal.h"
#include "Engine/SkeletalMesh.h"

/** Reference-pose mannequin: mesh +Y is the face, +Z is feet-to-head.
 * Map these to actor +Z and +X respectively (not a roll about the body's forward).
 * Shared by life presentation and dormant corpses; alive/prone animation is independent.
 */
namespace BrSupinePose
{
	inline FRotator Rotation() { return FRotator(0, 90, -90); }

	inline float GroundLift(const USkeletalMesh* Mesh)
	{
		// In this axis permutation mesh Y becomes world Z. Imported reference bounds
		// include skin thickness, unlike bone positions. Exclude designer bounds extensions.
		// No asset yet during construction: BeginPlay reapplies after loading the real mesh.
		if (!Mesh) return 0.f;
		const FBoxSphereBounds Bounds = Mesh->GetImportedBounds();
		return static_cast<float>(Bounds.BoxExtent.Y - Bounds.Origin.Y) + 0.5f;
	}
}
