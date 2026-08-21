#include "GaeaCellularNode.h"

#include "GaeaGridDomain.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor CellularTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor CellularNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Number;
		Parameter.DefaultNumber = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor CellularIntegerParameter(FName Name, const TCHAR* DisplayName, int64 DefaultValue, int64 Minimum, int64 Maximum)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Integer;
		Parameter.DefaultInteger = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = static_cast<double>(Minimum);
		Parameter.bHasMaximum = true;
		Parameter.Maximum = static_cast<double>(Maximum);
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor CellularNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue, std::initializer_list<FName> Options)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		for (const FName Option : Options) Parameter.NameOptions.Add(Option);
		return Parameter;
	}

	uint32 CellularHash(uint32 X)
	{
		X ^= X >> 16;
		X *= 0x7feb352dU;
		X ^= X >> 15;
		X *= 0x846ca68bU;
		X ^= X >> 16;
		return X;
	}

	float CellularHash01(int32 X, int32 Y, int32 Seed, uint32 Salt)
	{
		uint32 H = static_cast<uint32>(X) * 0x9e3779b9U;
		H ^= static_cast<uint32>(Y) * 0x85ebca6bU;
		H ^= static_cast<uint32>(Seed) * 0xc2b2ae35U;
		H ^= Salt;
		return static_cast<float>(CellularHash(H) & 0x00ffffffU) / static_cast<float>(0x01000000U);
	}

	float CellularDistance(float DX, float DY, FName Metric, float Morph, float Anisotropy)
	{
		DX *= FMath::Lerp(1.0f, 2.0f, Anisotropy);
		const float Euclidean = FMath::Sqrt(DX * DX + DY * DY);
		const float Manhattan = FMath::Abs(DX) + FMath::Abs(DY);
		const float Chebyshev = FMath::Max(FMath::Abs(DX), FMath::Abs(DY));
		if (Metric == TEXT("Euclidian_Manhattan")) return FMath::Lerp(Euclidean, Manhattan, Morph);
		if (Metric == TEXT("Euclidian_Chebyshev")) return FMath::Lerp(Euclidean, Chebyshev, Morph);
		if (Metric == TEXT("Minkowski"))
		{
			const float P = FMath::Lerp(1.0f, 4.0f, Morph);
			return FMath::Pow(FMath::Pow(FMath::Abs(DX), P) + FMath::Pow(FMath::Abs(DY), P), 1.0f / P);
		}
		return Euclidean;
	}

	bool EvaluateCellularNode(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const int32 Resolution = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 257)), 2, 1025);
		const float WorldSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WorldSize"), 100000.0)), 1.0f, 10000000.0f);
		const float HeightScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HeightScale"), 8000.0)), 1.0f, 1000000.0f);
		const float Size = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Size"), 0.5)), 0.001f, 10.0f);
		const float Density = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Density"), 1.0)), 0.05f, 8.0f);
		const float Jitter = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Jitter"), 1.0)), 0.0f, 2.0f);
		const float MetricMorph = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("MetricMorph"), 0.0)), 0.0f, 1.0f);
		const FName DistanceMetric = Node.GetName(TEXT("DistanceMetric"), TEXT("Euclidian_Manhattan"));
		const float AxesJitter = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("CoordinateAxesJitter"), 0.0)), 0.0f, 1.0f);
		const float Clustering = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("CAJClustering"), 0.0)), 0.0f, 1.0f);
		const float Anisotropy = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Anisotropy"), 0.0)), 0.0f, 1.0f);
		const FName DistanceFunction = Node.GetName(TEXT("DistanceFunction"), TEXT("DistanceInv"));
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));

		const double HalfWorldSize = static_cast<double>(WorldSize) * 0.5;
		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(FIntPoint(Resolution, Resolution), FVector2d(-HalfWorldSize, -HalfWorldSize), FVector2d(HalfWorldSize, HalfWorldSize));
		if (!Domain.IsValid())
		{
			Error = TEXT("Cellular produced an invalid grid domain.");
			return false;
		}

		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = GaeaTerrainFieldNames::Height;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField HeightField;
		HeightField.Initialize(Domain, Descriptor);

		const float CellWorldSize = FMath::Max(WorldSize * 0.08f * Size / Density, 1.0f);
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const FVector2d World = Domain.InteriorSampleToWorld(X, Y);
				float PX = static_cast<float>(World.X) / CellWorldSize;
				float PY = static_cast<float>(World.Y) / CellWorldSize;
				PX += (CellularHash01(X, Y, Seed, 0x1234U) - 0.5f) * AxesJitter * 0.25f;
				PY += (CellularHash01(X, Y, Seed, 0x5678U) - 0.5f) * AxesJitter * 0.25f;
				const int32 BaseX = FMath::FloorToInt(PX);
				const int32 BaseY = FMath::FloorToInt(PY);
				float F1 = TNumericLimits<float>::Max();
				float F2 = TNumericLimits<float>::Max();
				uint32 NearestHash = 0;

				for (int32 CY = BaseY - 2; CY <= BaseY + 2; ++CY)
				{
					for (int32 CX = BaseX - 2; CX <= BaseX + 2; ++CX)
					{
						const float ClusterX = (CellularHash01(CX, CY, Seed, 0xa11cU) - 0.5f) * Clustering;
						const float ClusterY = (CellularHash01(CX, CY, Seed, 0xb22dU) - 0.5f) * Clustering;
						const float FeatureX = static_cast<float>(CX) + 0.5f + (CellularHash01(CX, CY, Seed, 0xc33eU) - 0.5f) * Jitter + ClusterX;
						const float FeatureY = static_cast<float>(CY) + 0.5f + (CellularHash01(CX, CY, Seed, 0xd44fU) - 0.5f) * Jitter + ClusterY;
						const float Distance = CellularDistance(PX - FeatureX, PY - FeatureY, DistanceMetric, MetricMorph, Anisotropy);
						if (Distance < F1)
						{
							F2 = F1;
							F1 = Distance;
							NearestHash = CellularHash(static_cast<uint32>(CX) ^ (static_cast<uint32>(CY) * 0x9e3779b9U) ^ static_cast<uint32>(Seed));
						}
						else if (Distance < F2)
						{
							F2 = Distance;
						}
					}
				}

				float V = 0.0f;
				if (DistanceFunction == TEXT("Hash")) V = static_cast<float>(NearestHash & 0xffffU) / 65535.0f;
				else if (DistanceFunction == TEXT("DistanceInv")) V = 1.0f - FMath::Clamp(F1, 0.0f, 1.0f);
				else if (DistanceFunction == TEXT("Distance2Inv")) V = 1.0f - FMath::Clamp(F2, 0.0f, 1.0f);
				else if (DistanceFunction == TEXT("DistanceDiff")) V = FMath::Clamp(F2 - F1, 0.0f, 1.0f);
				else if (DistanceFunction == TEXT("DistanceToEdge")) V = FMath::Clamp((F2 - F1) * 2.0f, 0.0f, 1.0f);
				else
				{
					Error = TEXT("Cellular Distance Function is invalid.");
					return false;
				}
				HeightField.AtInterior(X, Y) = V * 2.0f - 1.0f;
			}
		}

		FGaeaTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(HeightField)))
		{
			Error = TEXT("Cellular could not publish its Height field.");
			return false;
		}
		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Cellular produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterGaeaCellularNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Cellular;
	Descriptor.DisplayName = TEXT("Cellular");
	Descriptor.Category = TEXT("Primitive");
	Descriptor.Description = TEXT("Generates Gaea-style cellular distance noise as a terrain primitive.");
	Descriptor.Outputs.Add(CellularTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(CellularNumberParameter(TEXT("Size"), TEXT("Size"), 0.5, 0.001, 10.0));
	Descriptor.Parameters.Add(CellularNumberParameter(TEXT("Density"), TEXT("Density"), 1.0, 0.05, 8.0));
	Descriptor.Parameters.Add(CellularNumberParameter(TEXT("Jitter"), TEXT("Jitter"), 1.0, 0.0, 2.0));
	Descriptor.Parameters.Add(CellularNumberParameter(TEXT("MetricMorph"), TEXT("Metric Morph"), 0.0, 0.0, 1.0));
	Descriptor.Parameters.Add(CellularNameParameter(TEXT("DistanceMetric"), TEXT("Distance Metric"), TEXT("Euclidian_Manhattan"), { TEXT("Euclidian_Manhattan"), TEXT("Euclidian_Chebyshev"), TEXT("Minkowski") }));
	Descriptor.Parameters.Add(CellularNumberParameter(TEXT("CoordinateAxesJitter"), TEXT("Coordinate Axes Jitter"), 0.0, 0.0, 1.0));
	Descriptor.Parameters.Add(CellularNumberParameter(TEXT("CAJClustering"), TEXT("CAJ Clustering"), 0.0, 0.0, 1.0));
	Descriptor.Parameters.Add(CellularNumberParameter(TEXT("Anisotropy"), TEXT("Anisotropy"), 0.0, 0.0, 1.0));
	Descriptor.Parameters.Add(CellularNameParameter(TEXT("DistanceFunction"), TEXT("Distance Function"), TEXT("DistanceInv"), { TEXT("Hash"), TEXT("DistanceInv"), TEXT("Distance2Inv"), TEXT("DistanceDiff"), TEXT("DistanceToEdge") }));
	Descriptor.Parameters.Add(CellularIntegerParameter(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Cellular, EvaluateCellularNode);
}
