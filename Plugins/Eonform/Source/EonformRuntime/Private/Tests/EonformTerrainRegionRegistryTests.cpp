#if WITH_DEV_AUTOMATION_TESTS

#include "EonformTerrainRegionRegistry.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEonformTerrainRegionRegistryLifecycleTest,
	"Eonform.Runtime.TerrainRegionRegistry.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEonformTerrainRegionRegistryLifecycleTest::RunTest(const FString& Parameters)
{
	FEonformTerrainRegionRegistry::Reset();

	const FEonformTerrainRegionId RegionId{ TEXT("EonformGraph"), FIntPoint(1, 2) };
	const FIntPoint GridDimensions(4, 3);
	const FIntRect SampleBounds(64, 128, 129, 193);
	const FIntPoint InitialResolution(65, 65);
	const FIntPoint PromotedResolution(129, 129);

	TestTrue(TEXT("Region request is accepted"), FEonformTerrainRegionRegistry::RequestRegion(
		RegionId,
		GridDimensions,
		SampleBounds,
		InitialResolution,
		10));

	FEonformTerrainRegionSnapshot Snapshot;
	TestTrue(TEXT("Requested region can be read"), FEonformTerrainRegionRegistry::Get(RegionId, Snapshot));
	TestTrue(TEXT("Requested region snapshot is valid"), Snapshot.IsValid());
	TestEqual(TEXT("Requested region remains unloaded until committed"), Snapshot.Residency, EEonformTerrainRegionResidency::Unloaded);
	TestEqual(TEXT("Requested region is queued"), Snapshot.Stage, EEonformTerrainRegionStage::Queued);
	TestFalse(TEXT("Requested region has no resident resolution"), Snapshot.bHasResidentResolution);
	TestEqual(TEXT("Requested region target resolution"), Snapshot.TargetResolution, InitialResolution);
	TestEqual(TEXT("Requested revision"), Snapshot.RequestedRevision, static_cast<uint64>(10));
	TestFalse(TEXT("Progress is indeterminate until measured"), Snapshot.HasMeasuredProgress());

	TestTrue(TEXT("Meshing stage can be recorded"), FEonformTerrainRegionRegistry::SetStage(
		RegionId,
		EEonformTerrainRegionStage::Meshing,
		TEXT("MeshTerrain")));
	TestTrue(TEXT("Measured progress can be recorded"), FEonformTerrainRegionRegistry::SetMeasuredProgress(RegionId, 25, 100));
	TestTrue(TEXT("Measured region can be read"), FEonformTerrainRegionRegistry::Get(RegionId, Snapshot));
	TestTrue(TEXT("Progress is measured"), Snapshot.HasMeasuredProgress());
	TestEqual(TEXT("Measured progress fraction"), Snapshot.Progress.GetFraction(), 0.25);

	TestTrue(TEXT("Initial region can be committed"), FEonformTerrainRegionRegistry::CommitRegion(
		RegionId,
		10,
		InitialResolution,
		4225,
		8192));
	TestTrue(TEXT("Committed region can be read"), FEonformTerrainRegionRegistry::Get(RegionId, Snapshot));
	TestEqual(TEXT("Committed region is loaded"), Snapshot.Residency, EEonformTerrainRegionResidency::Loaded);
	TestEqual(TEXT("Committed region is resident"), Snapshot.Stage, EEonformTerrainRegionStage::Resident);
	TestTrue(TEXT("Committed region has resident resolution"), Snapshot.bHasResidentResolution);
	TestEqual(TEXT("Committed region resident resolution"), Snapshot.ResidentResolution, InitialResolution);
	TestTrue(TEXT("Committed region satisfies current request"), Snapshot.IsResidentCurrent());
	TestEqual(TEXT("Committed vertex count"), Snapshot.VertexCount, 4225);
	TestEqual(TEXT("Committed triangle count"), Snapshot.TriangleCount, 8192);

	TestTrue(TEXT("Higher-resolution revision can be requested"), FEonformTerrainRegionRegistry::RequestRegion(
		RegionId,
		GridDimensions,
		FIntRect(64, 128, 193, 257),
		PromotedResolution,
		11));
	TestTrue(TEXT("Promoted request can be read"), FEonformTerrainRegionRegistry::Get(RegionId, Snapshot));
	TestEqual(TEXT("Previous region remains loaded during promotion"), Snapshot.Residency, EEonformTerrainRegionResidency::Loaded);
	TestTrue(TEXT("Previous resident resolution survives promotion request"), Snapshot.bHasResidentResolution);
	TestEqual(TEXT("Previous resident resolution is preserved"), Snapshot.ResidentResolution, InitialResolution);
	TestEqual(TEXT("Previous resident revision is preserved"), Snapshot.ResidentRevision, static_cast<uint64>(10));
	TestEqual(TEXT("Promotion target resolution"), Snapshot.TargetResolution, PromotedResolution);
	TestEqual(TEXT("Promotion requested revision"), Snapshot.RequestedRevision, static_cast<uint64>(11));
	TestTrue(TEXT("Previous resident becomes dirty while promotion is pending"), Snapshot.bDirty);
	TestFalse(TEXT("Previous resident no longer satisfies current request"), Snapshot.IsResidentCurrent());

	TestTrue(TEXT("Promotion generation stage can be recorded"), FEonformTerrainRegionRegistry::SetStage(
		RegionId,
		EEonformTerrainRegionStage::Generating,
		TEXT("Mountain")));
	TestTrue(TEXT("Promotion failure can be recorded"), FEonformTerrainRegionRegistry::FailRegion(RegionId, TEXT("Synthetic failure")));
	TestTrue(TEXT("Failed promotion can be read"), FEonformTerrainRegionRegistry::Get(RegionId, Snapshot));
	TestEqual(TEXT("Failed promotion keeps previous region loaded"), Snapshot.Residency, EEonformTerrainRegionResidency::Loaded);
	TestEqual(TEXT("Failed promotion reports failed stage"), Snapshot.Stage, EEonformTerrainRegionStage::Failed);
	TestEqual(TEXT("Failed promotion preserves resident revision"), Snapshot.ResidentRevision, static_cast<uint64>(10));
	TestEqual(TEXT("Failed promotion preserves resident resolution"), Snapshot.ResidentResolution, InitialResolution);
	TestEqual(TEXT("Failed promotion reports error"), Snapshot.Error, FString(TEXT("Synthetic failure")));

	TestTrue(TEXT("Promotion can be requested again"), FEonformTerrainRegionRegistry::RequestRegion(
		RegionId,
		GridDimensions,
		FIntRect(64, 128, 193, 257),
		PromotedResolution,
		11));
	TestTrue(TEXT("Promoted region can be committed"), FEonformTerrainRegionRegistry::CommitRegion(
		RegionId,
		11,
		PromotedResolution,
		16641,
		32768));
	TestTrue(TEXT("Promoted region can be read"), FEonformTerrainRegionRegistry::Get(RegionId, Snapshot));
	TestTrue(TEXT("Promoted region satisfies current request"), Snapshot.IsResidentCurrent());
	TestFalse(TEXT("Promoted region is clean"), Snapshot.bDirty);

	const FEonformTerrainRegionId EarlierRegionId{ TEXT("EonformGraph"), FIntPoint(0, 0) };
	TestTrue(TEXT("Second region can be requested"), FEonformTerrainRegionRegistry::RequestRegion(
		EarlierRegionId,
		GridDimensions,
		FIntRect(0, 0, 65, 65),
		InitialResolution,
		11));
	TArray<FEonformTerrainRegionSnapshot> SourceRegions;
	FEonformTerrainRegionRegistry::GetSourceRegions(TEXT("EonformGraph"), SourceRegions);
	TestEqual(TEXT("Source returns both regions"), SourceRegions.Num(), 2);
	if (SourceRegions.Num() == 2)
	{
		TestEqual(TEXT("Source regions are row-major sorted"), SourceRegions[0].Id.Coordinate, FIntPoint(0, 0));
		TestEqual(TEXT("Later source region follows"), SourceRegions[1].Id.Coordinate, FIntPoint(1, 2));
	}

	TestTrue(TEXT("Region can begin eviction"), FEonformTerrainRegionRegistry::BeginEviction(RegionId));
	TestTrue(TEXT("Evicting region can be read"), FEonformTerrainRegionRegistry::Get(RegionId, Snapshot));
	TestEqual(TEXT("Region is evicting"), Snapshot.Residency, EEonformTerrainRegionResidency::Evicting);
	TestTrue(TEXT("Region can be marked unloaded"), FEonformTerrainRegionRegistry::MarkUnloaded(RegionId));
	TestTrue(TEXT("Unloaded region can be read"), FEonformTerrainRegionRegistry::Get(RegionId, Snapshot));
	TestEqual(TEXT("Region is unloaded"), Snapshot.Residency, EEonformTerrainRegionResidency::Unloaded);
	TestFalse(TEXT("Unloaded region has no resident resolution"), Snapshot.bHasResidentResolution);
	TestEqual(TEXT("Unloaded region retains requested revision"), Snapshot.RequestedRevision, static_cast<uint64>(11));
	TestTrue(TEXT("Unloaded requested region remains dirty"), Snapshot.bDirty);

	TestTrue(TEXT("Source regions can be removed"), FEonformTerrainRegionRegistry::RemoveSource(TEXT("EonformGraph")));
	FEonformTerrainRegionRegistry::GetSourceRegions(TEXT("EonformGraph"), SourceRegions);
	TestEqual(TEXT("Source removal clears region snapshots"), SourceRegions.Num(), 0);

	FEonformTerrainRegionRegistry::Reset();
	return true;
}

#endif
