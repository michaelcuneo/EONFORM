#include "GaeaTerrainMeshMaterializer.h"

#include "DynamicMesh/MeshNormals.h"
#include "GaeaTerrainFieldNames.h"

using UE::Geometry::FDynamicMesh3;
using UE::Geometry::FMeshNormals;

bool FGaeaTerrainMeshMaterializer::BuildDynamicMesh(
	const FGaeaTerrainDataset& Dataset,
	float HeightScale,
	FDynamicMesh3& OutMesh,
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

	const FGaeaScalarField* Height = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
	if (!Height || !Height->IsValid())
	{
		return Fail(TEXT("Terrain dataset has no valid Height field."));
	}
	if (HeightScale <= UE_SMALL_NUMBER)
	{
		return Fail(TEXT("HeightScale must be greater than zero."));
	}

	const FIntPoint Dimensions = Height->Domain.Dimensions;
	if (Dimensions.X < 2 || Dimensions.Y < 2)
	{
		return Fail(TEXT("Height field must be at least 2x2 samples."));
	}

	OutMesh = FDynamicMesh3(true, false, false, false);

	TArray<int32> VertexIds;
	VertexIds.SetNumUninitialized(Dimensions.X * Dimensions.Y);

	for (int32 Y = 0; Y < Dimensions.Y; ++Y)
	{
		for (int32 X = 0; X < Dimensions.X; ++X)
		{
			const FVector2d WorldPosition = Height->Domain.InteriorSampleToWorld(X, Y);
			const double Z = static_cast<double>(Height->AtInterior(X, Y)) * static_cast<double>(HeightScale);
			VertexIds[Y * Dimensions.X + X] = OutMesh.AppendVertex(FVector3d(WorldPosition.X, WorldPosition.Y, Z));
		}
	}

	for (int32 Y = 0; Y < Dimensions.Y - 1; ++Y)
	{
		for (int32 X = 0; X < Dimensions.X - 1; ++X)
		{
			const int32 V00 = VertexIds[Y * Dimensions.X + X];
			const int32 V10 = VertexIds[Y * Dimensions.X + X + 1];
			const int32 V01 = VertexIds[(Y + 1) * Dimensions.X + X];
			const int32 V11 = VertexIds[(Y + 1) * Dimensions.X + X + 1];

			if (OutMesh.AppendTriangle(V00, V10, V01, -1) < 0 ||
				OutMesh.AppendTriangle(V10, V11, V01, -1) < 0)
			{
				return Fail(TEXT("Failed to append terrain mesh triangles."));
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
