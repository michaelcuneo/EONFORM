#include "GaeaCellular3DNode.h"

#include "GaeaGridDomain.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor Cellular3DTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor Cellular3DNumberParameter(
		FName Name,
		const TCHAR* DisplayName,
		double DefaultValue,
		double Minimum,
		double Maximum)
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

	FGaeaTerrainParameterDescriptor Cellular3DIntegerParameter(
		FName Name,
		const TCHAR* DisplayName,
		int64 DefaultValue,
		int64 Minimum,
		int64 Maximum)
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

	uint32 Cellular3DHash(uint32 Value)
	{
		Value ^= Value >> 16;
		Value *= 0x7feb352dU;
		Value ^= Value >> 15;
		Value *= 0x846ca68bU;
		Value ^= Value >> 16;
		return Value;
	}

	float Cellular3DHash01(int32 X, int32 Y, int32 Z, int32 Seed, uint32 Salt)
	{
		uint32 H = static_cast<uint32>(X) * 0x9e3779b9U;
		H ^= static_cast<uint32>(Y) * 0x85ebca6bU;
		H ^= static_cast<uint32>(Z) * 0xc2b2ae35U;
		H ^= static_cast<uint32>(Seed) * 0x27d4eb2fU;
		H ^= Salt;
		return static_cast<float>(Cellular3DHash(H) & 0x00ffffffU) / static_cast<float>(0x01000000U);
	}

	float Cellular3DSample(
		float X,
		float Y,
		float Z,
		float JitterX,
		float JitterY,
		float JitterZ,
		int32 Seed)
	{
		const int32 BaseX = FMath::FloorToInt(X);
		const int32 BaseY = FMath::FloorToInt(Y);
		const int32 BaseZ = FMath::FloorToInt(Z);
		float Nearest = TNumericLimits<float>::Max();

		for (int32 CZ = BaseZ - 1; CZ <= BaseZ + 1; ++CZ)
		{
			for (int32 CY = BaseY - 1; CY <= BaseY + 1; ++CY)
			{
				for (int32 CX = BaseX - 1; CX <= BaseX + 1; ++CX)
				{
					const float FeatureX = static_cast<float>(CX) + 0.5f
						+ (Cellular3DHash01(CX, CY, CZ, Seed, 0x41a7U) - 0.5f) * JitterX;
					const float FeatureY = static_cast<float>(CY) + 0.5f
						+ (Cellular3DHash01(CX, CY, CZ, Seed, 0x72b9U) - 0.5f) * JitterY;
					const float FeatureZ = static_cast<float>(CZ) + 0.5f
						+ (Cellular3DHash01(CX, CY, CZ, Seed, 0xa3d1U) - 0.5f) * JitterZ;

					const float DX = X - FeatureX;
					const float DY = Y - FeatureY;
					const float DZ = Z - FeatureZ;
					Nearest = FMath::Min(Nearest, FMath::Sqrt(DX * DX + DY * DY + DZ * DZ));
				}
			}
		}

		return Nearest;
	}

	bool EvaluateCellular3DNode(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs&,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		// Resolution/world scale are integration details and remain hidden from
		// the public Gaea-facing descriptor, just as they are for Perlin/Cellular.
		const int32 Resolution = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 257)), 2, 1025);
		const float WorldSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WorldSize"), 100000.0)), 1.0f, 10000000.0f);
		const float HeightScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HeightScale"), 8000.0)), 1.0f, 1000000.0f);

		const float Size = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Size"), 0.5)), 0.001f, 10.0f);
		const float Gap = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Gap"), 0.0)), 0.0f, 1.0f);
		const float JitterX = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("JitterX"), 1.0)), 0.0f, 2.0f);
		const float JitterY = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("JitterY"), 1.0)), 0.0f, 2.0f);
		const float JitterZ = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("JitterZ"), 1.0)), 0.0f, 2.0f);
		const float ScaleX = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ScaleX"), 1.0)), 0.001f, 10.0f);
		const float ScaleY = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ScaleY"), 1.0)), 0.001f, 10.0f);
		const float ScaleZ = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ScaleZ"), 1.0)), 0.001f, 10.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));

		const double HalfWorldSize = static_cast<double>(WorldSize) * 0.5;
		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(
			FIntPoint(Resolution, Resolution),
			FVector2d(-HalfWorldSize, -HalfWorldSize),
			FVector2d(HalfWorldSize, HalfWorldSize));
		if (!Domain.IsValid())
		{
			Error = TEXT("Cellular3D produced an invalid grid domain.");
			return false;
		}

		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = GaeaTerrainFieldNames::Height;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;

		FGaeaScalarField HeightField;
		HeightField.Initialize(Domain, Descriptor);

		const float CellWorldSize = FMath::Max(WorldSize * 0.08f * Size, 1.0f);
		const float ZSlice = static_cast<float>((Cellular3DHash(static_cast<uint32>(Seed)) & 0xffffU)) / 65535.0f * 8.0f;
		const float GapThreshold = FMath::Lerp(0.95f, 0.25f, Gap);

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const FVector2d World = Domain.InteriorSampleToWorld(X, Y);
				const float NX = static_cast<float>(World.X) / (CellWorldSize * ScaleX);
				const float NY = static_cast<float>(World.Y) / (CellWorldSize * ScaleY);
				const float NZ = ZSlice / ScaleZ;
				const float Distance = Cellular3DSample(NX, NY, NZ, JitterX, JitterY, JitterZ, Seed);

				// Convert nearest-cell distance into the exposed heightfield. Gap opens
				// space between cells while preserving a continuous terrain signal.
				const float Cell = 1.0f - FMath::Clamp(Distance / FMath::Max(GapThreshold, UE_SMALL_NUMBER), 0.0f, 1.0f);
				HeightField.AtInterior(X, Y) = Cell * 2.0f - 1.0f;
			}
		}

		FGaeaTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(HeightField)))
		{
			Error = TEXT("Cellular3D could not publish its Height field.");
			return false;
		}

		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Cellular3D produced an invalid terrain value.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterGaeaCellular3DNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Cellular3D;
	Descriptor.DisplayName = TEXT("Cellular3D");
	Descriptor.Category = TEXT("Primitive");
	Descriptor.Description = TEXT("Generates a 3D cellular-noise slice with independent axis jitter and scale controls.");
	Descriptor.Outputs.Add(Cellular3DTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(Cellular3DNumberParameter(TEXT("Size"), TEXT("Size"), 0.5, 0.001, 10.0));
	Descriptor.Parameters.Add(Cellular3DNumberParameter(TEXT("Gap"), TEXT("Gap"), 0.0, 0.0, 1.0));
	Descriptor.Parameters.Add(Cellular3DNumberParameter(TEXT("JitterX"), TEXT("Jitter X"), 1.0, 0.0, 2.0));
	Descriptor.Parameters.Add(Cellular3DNumberParameter(TEXT("JitterY"), TEXT("Jitter Y"), 1.0, 0.0, 2.0));
	Descriptor.Parameters.Add(Cellular3DNumberParameter(TEXT("JitterZ"), TEXT("Jitter Z"), 1.0, 0.0, 2.0));
	Descriptor.Parameters.Add(Cellular3DNumberParameter(TEXT("ScaleX"), TEXT("Scale X"), 1.0, 0.001, 10.0));
	Descriptor.Parameters.Add(Cellular3DNumberParameter(TEXT("ScaleY"), TEXT("Scale Y"), 1.0, 0.001, 10.0));
	Descriptor.Parameters.Add(Cellular3DNumberParameter(TEXT("ScaleZ"), TEXT("Scale Z"), 1.0, 0.001, 10.0));
	Descriptor.Parameters.Add(Cellular3DIntegerParameter(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647));

	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::Cellular3D, EvaluateCellular3DNode);
}
