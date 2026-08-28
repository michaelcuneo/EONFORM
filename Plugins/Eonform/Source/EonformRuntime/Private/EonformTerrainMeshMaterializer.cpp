#include "EonformTerrainMeshMaterializer.h"

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicMeshOverlay.h"
#include "DynamicMesh/MeshNormals.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainGenerationPlan.h"
#include "EonformTerrainRegionalSupport.h"

using UE::Geometry::FDynamicMesh3;
using UE::Geometry::FDynamicMeshColorOverlay;
using UE::Geometry::FIndex3i;
using UE::Geometry::FMeshNormals;

namespace
{
	thread_local bool bRegionalMaterializationActive = false;
	constexpr int32 RegionalMaterializationHalo = 16;

	bool ResolveHeightField(
		const FEonformTerrainDataset& Dataset,
		const FEonformTerrainMeshBuildOptions& Options,
		const FEonformScalarField*& OutHeight,
		FIntPoint& OutTargetResolution,
		FString* OutError)
	{
		auto Fail = [OutError](const TCHAR* Message)
		{
			if (OutError)
			{
				*OutError = Message;
			}
			return false;
		};

		OutHeight = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!OutHeight || !OutHeight->IsValid())
		{
			return Fail(TEXT("Terrain dataset has no valid Height field."));
		}
		if (Options.HeightScale <= UE_SMALL_NUMBER)
		{
			return Fail(TEXT("HeightScale must be greater than zero."));
		}
		if (Options.HorizontalScale <= UE_SMALL_NUMBER
			|| Options.HorizontalScaleXY.X <= UE_DOUBLE_SMALL_NUMBER
			|| Options.HorizontalScaleXY.Y <= UE_DOUBLE_SMALL_NUMBER
			|| Options.VerticalScale <= UE_SMALL_NUMBER)
		{
			return Fail(TEXT("Terrain materialization scales must be greater than zero."));
		}

		OutTargetResolution = FEonformTerrainMeshMaterializer::ResolveTargetResolution(Dataset, Options);
		if (OutTargetResolution.X < 2 || OutTargetResolution.Y < 2)
		{
			return Fail(TEXT("Target terrain resolution must be at least 2x2 samples."));
		}
		return true;
	}

	bool ResolveBaseColorFields(
		const FEonformTerrainDataset& Dataset,
		const FEonformScalarField*& OutR,
		const FEonformScalarField*& OutG,
		const FEonformScalarField*& OutB)
	{
		OutR = Dataset.FindScalarField(EonformTerrainFieldNames::BaseColorR);
		OutG = Dataset.FindScalarField(EonformTerrainFieldNames::BaseColorG);
		OutB = Dataset.FindScalarField(EonformTerrainFieldNames::BaseColorB);
		return OutR && OutG && OutB && OutR->IsValid() && OutG->IsValid() && OutB->IsValid();
	}

	bool TryBuildFromGenerationPlan(
		const FEonformTerrainMeshBuildOptions& Options,
		const FIntPoint& StartSample,
		const FIntPoint& EndSample,
		FDynamicMesh3& OutMesh,
		FString* OutError,
		bool& bOutHandled)
	{
		bOutHandled = false;
		if (bRegionalMaterializationActive) return false;
		if (Options.TargetResolution.X < 2 || Options.TargetResolution.Y < 2) return false;

		FEonformTerrainGenerationPlan Plan;
		if (!FEonformTerrainGenerationPlanRegistry::Get(Plan)) return false;

		const FEonformTerrainRegionalSupportReport Support = FEonformTerrainRegionalSupport::Analyze(Plan.Recipe);
		if (!Support.bSupported) return false;

		const FIntPoint FullResolution = Options.TargetResolution;
		if (StartSample.X < 0 || StartSample.Y < 0
			|| EndSample.X >= FullResolution.X || EndSample.Y >= FullResolution.Y
			|| EndSample.X <= StartSample.X || EndSample.Y <= StartSample.Y)
		{
			return false;
		}

		const FEonformGridDomain ReferenceDomain = Plan.Context.ResolveReferenceDomain(
			FullResolution,
			FVector2d(-50000.0, -50000.0),
			FVector2d(50000.0, 50000.0));
		if (!ReferenceDomain.IsValid()) return false;

		const FVector2d CellSize = ReferenceDomain.GetCellSize();
		FEonformTerrainEvaluationContext RegionContext = Plan.Context;
		RegionContext.TargetResolution = FIntPoint(
			EndSample.X - StartSample.X + 1,
			EndSample.Y - StartSample.Y + 1);
		RegionContext.ReferenceResolution = FullResolution;
		RegionContext.Region.WorldMinCm = ReferenceDomain.WorldMin + FVector2d(
			static_cast<double>(StartSample.X) * CellSize.X,
			static_cast<double>(StartSample.Y) * CellSize.Y);
		RegionContext.Region.WorldMaxCm = ReferenceDomain.WorldMin + FVector2d(
			static_cast<double>(EndSample.X) * CellSize.X,
			static_cast<double>(EndSample.Y) * CellSize.Y);
		RegionContext.Region.BorderSamples = RegionalMaterializationHalo;

		FEonformTerrainEvaluationResult RegionResult = FEonformTerrainEvaluator::Evaluate(Plan.Recipe, RegionContext);
		bOutHandled = true;
		if (!RegionResult.bSuccess)
		{
			if (OutError)
			{
				*OutError = FString::Printf(
					TEXT("Regional EONFORM evaluation failed for samples (%d,%d)-(%d,%d): %s"),
					StartSample.X,
					StartSample.Y,
					EndSample.X,
					EndSample.Y,
					*RegionResult.Error);
			}
			return false;
		}

		FEonformTerrainMeshBuildOptions LocalOptions = Options;
		LocalOptions.HeightScale = RegionResult.HeightScale;
		LocalOptions.TargetResolution = RegionContext.TargetResolution;

		TGuardValue<bool> Guard(bRegionalMaterializationActive, true);
		return FEonformTerrainMeshMaterializer::BuildDynamicMeshRegion(
			RegionResult.Dataset,
			LocalOptions,
			FIntPoint(0, 0),
			FIntPoint(LocalOptions.TargetResolution.X - 1, LocalOptions.TargetResolution.Y - 1),
			OutMesh,
			OutError);
	}
}

FIntPoint FEonformTerrainMeshMaterializer::ResolveTargetResolution(
	const FEonformTerrainDataset& Dataset,
	const FEonformTerrainMeshBuildOptions& Options)
{
	const FEonformScalarField* Height = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
	if (!Height || !Height->IsValid())
	{
		return FIntPoint::ZeroValue;
	}

	const FIntPoint Source = Height->Domain.Dimensions;
	const int32 X = Options.TargetResolution.X > 1 ? Options.TargetResolution.X : Source.X;
	const int32 Y = Options.TargetResolution.Y > 1 ? Options.TargetResolution.Y : Source.Y;
	return FIntPoint(X, Y);
}

bool FEonformTerrainMeshMaterializer::BuildDynamicMesh(
	const FEonformTerrainDataset& Dataset,
	float HeightScale,
	FDynamicMesh3& OutMesh,
	FString* OutError)
{
	FEonformTerrainMeshBuildOptions Options;
	Options.HeightScale = HeightScale;
	return BuildDynamicMesh(Dataset, Options, OutMesh, OutError);
}

bool FEonformTerrainMeshMaterializer::BuildDynamicMesh(
	const FEonformTerrainDataset& Dataset,
	const FEonformTerrainMeshBuildOptions& Options,
	FDynamicMesh3& OutMesh,
	FString* OutError)
{
	const FIntPoint Resolution = ResolveTargetResolution(Dataset, Options);
	if (Resolution.X < 2 || Resolution.Y < 2)
	{
		if (OutError)
		{
			*OutError = TEXT("Target terrain resolution must be at least 2x2 samples.");
		}
		return false;
	}

	return BuildDynamicMeshRegion(
		Dataset,
		Options,
		FIntPoint(0, 0),
		FIntPoint(Resolution.X - 1, Resolution.Y - 1),
		OutMesh,
		OutError);
}

bool FEonformTerrainMeshMaterializer::BuildDynamicMeshRegion(
	const FEonformTerrainDataset& Dataset,
	const FEonformTerrainMeshBuildOptions& Options,
	const FIntPoint& StartSample,
	const FIntPoint& EndSample,
	FDynamicMesh3& OutMesh,
	FString* OutError)
{
	bool bRegionalHandled = false;
	if (TryBuildFromGenerationPlan(Options, StartSample, EndSample, OutMesh, OutError, bRegionalHandled))
	{
		return true;
	}
	if (bRegionalHandled)
	{
		return false;
	}

	const FEonformScalarField* Height = nullptr;
	FIntPoint TargetResolution = FIntPoint::ZeroValue;
	if (!ResolveHeightField(Dataset, Options, Height, TargetResolution, OutError))
	{
		return false;
	}

	if (StartSample.X < 0 || StartSample.Y < 0 ||
		EndSample.X >= TargetResolution.X || EndSample.Y >= TargetResolution.Y ||
		EndSample.X <= StartSample.X || EndSample.Y <= StartSample.Y)
	{
		if (OutError)
		{
			*OutError = TEXT("Requested terrain mesh region is outside the target sample grid.");
		}
		return false;
	}

	const FEonformScalarField* BaseColorR = nullptr;
	const FEonformScalarField* BaseColorG = nullptr;
	const FEonformScalarField* BaseColorB = nullptr;
	const bool bHasBaseColor = ResolveBaseColorFields(Dataset, BaseColorR, BaseColorG, BaseColorB);

	const FVector2d MinWorld = Height->Domain.InteriorSampleToWorld(0, 0);
	const FVector2d MaxWorld = Height->Domain.InteriorSampleToWorld(
		Height->Domain.Dimensions.X - 1,
		Height->Domain.Dimensions.Y - 1);
	const FVector2d WorldCenter = (MinWorld + MaxWorld) * 0.5;

	const int32 LocalWidth = EndSample.X - StartSample.X + 1;
	const int32 LocalHeight = EndSample.Y - StartSample.Y + 1;
	OutMesh = FDynamicMesh3(true, bHasBaseColor, false, false);

	// UDynamicMeshComponent and Mesh conversion render paths consume the primary
	// FVector4f color overlay, not FDynamicMesh3's legacy per-vertex color array.
	// Keep the legacy color array populated as well for callers that inspect it,
	// but make the primary overlay the authoritative render representation.
	FDynamicMeshColorOverlay* ColorOverlay = nullptr;
	if (bHasBaseColor)
	{
		OutMesh.EnableAttributes();
		OutMesh.Attributes()->EnablePrimaryColors();
		ColorOverlay = OutMesh.Attributes()->PrimaryColors();
	}

	TArray<int32> VertexIds;
	VertexIds.SetNumUninitialized(LocalWidth * LocalHeight);
	TArray<int32> ColorElementIds;
	if (ColorOverlay)
	{
		ColorElementIds.SetNumUninitialized(LocalWidth * LocalHeight);
	}

	for (int32 LocalY = 0; LocalY < LocalHeight; ++LocalY)
	{
		const int32 GlobalY = StartSample.Y + LocalY;
		const double V = TargetResolution.Y > 1
			? static_cast<double>(GlobalY) / static_cast<double>(TargetResolution.Y - 1)
			: 0.0;

		for (int32 LocalX = 0; LocalX < LocalWidth; ++LocalX)
		{
			const int32 GlobalX = StartSample.X + LocalX;
			const double U = TargetResolution.X > 1
				? static_cast<double>(GlobalX) / static_cast<double>(TargetResolution.X - 1)
				: 0.0;

			const FVector2d SourceWorld(
				FMath::Lerp(MinWorld.X, MaxWorld.X, U),
				FMath::Lerp(MinWorld.Y, MaxWorld.Y, V));
			const float HeightValue = Height->SampleWorld(SourceWorld, true);
			const FVector2d FromCenter = SourceWorld - WorldCenter;
			const FVector3d Position(
				WorldCenter.X + FromCenter.X * Options.HorizontalScale * Options.HorizontalScaleXY.X,
				WorldCenter.Y + FromCenter.Y * Options.HorizontalScale * Options.HorizontalScaleXY.Y,
				static_cast<double>(HeightValue) * static_cast<double>(Options.HeightScale) * Options.VerticalScale);

			const int32 GridIndex = LocalY * LocalWidth + LocalX;
			const int32 VertexId = OutMesh.AppendVertex(Position);
			VertexIds[GridIndex] = VertexId;
			if (ColorOverlay)
			{
				const FVector3f VertexColor(
					FMath::Clamp(BaseColorR->SampleWorld(SourceWorld, true), 0.0f, 1.0f),
					FMath::Clamp(BaseColorG->SampleWorld(SourceWorld, true), 0.0f, 1.0f),
					FMath::Clamp(BaseColorB->SampleWorld(SourceWorld, true), 0.0f, 1.0f));
				OutMesh.SetVertexColor(VertexId, VertexColor);

				const int32 ColorElementId = ColorOverlay->AppendElement(FVector4f(VertexColor.X, VertexColor.Y, VertexColor.Z, 1.0f));
				ColorOverlay->SetParentVertex(ColorElementId, VertexId);
				ColorElementIds[GridIndex] = ColorElementId;
			}
		}
	}

	for (int32 Y = 0; Y < LocalHeight - 1; ++Y)
	{
		for (int32 X = 0; X < LocalWidth - 1; ++X)
		{
			const int32 I00 = Y * LocalWidth + X;
			const int32 I10 = Y * LocalWidth + X + 1;
			const int32 I01 = (Y + 1) * LocalWidth + X;
			const int32 I11 = (Y + 1) * LocalWidth + X + 1;
			const int32 V00 = VertexIds[I00];
			const int32 V10 = VertexIds[I10];
			const int32 V01 = VertexIds[I01];
			const int32 V11 = VertexIds[I11];

			const int32 Triangle0 = OutMesh.AppendTriangle(V00, V11, V10, 0);
			const int32 Triangle1 = OutMesh.AppendTriangle(V00, V01, V11, 0);
			if (Triangle0 < 0 || Triangle1 < 0)
			{
				if (OutError)
				{
					*OutError = TEXT("Failed to append terrain mesh triangles.");
				}
				return false;
			}

			if (ColorOverlay)
			{
				ColorOverlay->SetTriangle(
					Triangle0,
					FIndex3i(ColorElementIds[I00], ColorElementIds[I11], ColorElementIds[I10]));
				ColorOverlay->SetTriangle(
					Triangle1,
					FIndex3i(ColorElementIds[I00], ColorElementIds[I01], ColorElementIds[I11]));
			}
		}
	}

	FMeshNormals::QuickComputeVertexNormals(OutMesh, false);
	if (OutError)
	{
		OutError->Reset();
	}
	return true;
}
