#include "GaeaDotNoiseNode.h"

#include "GaeaGridDomain.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor DotNoiseTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor DotNoiseNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum)
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

	FGaeaTerrainParameterDescriptor DotNoiseIntegerParameter(FName Name, const TCHAR* DisplayName, int64 DefaultValue, int64 Minimum, int64 Maximum)
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

	FGaeaTerrainParameterDescriptor DotNoiseNameParameter(FName Name, const TCHAR* DisplayName, FName DefaultValue, std::initializer_list<FName> Options)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		for (const FName Option : Options) Parameter.NameOptions.Add(Option);
		return Parameter;
	}

	uint32 DotNoiseHash(uint32 Value)
	{
		Value ^= Value >> 16;
		Value *= 0x7feb352dU;
		Value ^= Value >> 15;
		Value *= 0x846ca68bU;
		Value ^= Value >> 16;
		return Value;
	}

	float DotNoiseHash01(int32 X, int32 Y, int32 Seed, uint32 Salt)
	{
		uint32 H = static_cast<uint32>(X) * 0x9e3779b9U;
		H ^= static_cast<uint32>(Y) * 0x85ebca6bU;
		H ^= static_cast<uint32>(Seed) * 0xc2b2ae35U;
		H ^= Salt;
		return static_cast<float>(DotNoiseHash(H) & 0x00ffffffU) / static_cast<float>(0x01000000U);
	}

	float DotNoiseBlend(float A, float B, FName Mode)
	{
		if (Mode == TEXT("Blend")) return FMath::Lerp(A, B, 0.5f);
		if (Mode == TEXT("Add")) return A + B;
		if (Mode == TEXT("Subtract")) return A - B;
		if (Mode == TEXT("Difference")) return FMath::Abs(A - B);
		if (Mode == TEXT("Multiply")) return A * B;
		if (Mode == TEXT("Screen")) return 1.0f - (1.0f - A) * (1.0f - B);
		if (Mode == TEXT("Max")) return FMath::Max(A, B);
		if (Mode == TEXT("Min")) return FMath::Min(A, B);
		return B;
	}

	bool EvaluateDotNoiseNode(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const int32 Resolution = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 257)), 2, 1025);
		const float WorldSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WorldSize"), 100000.0)), 1.0f, 10000000.0f);
		const float HeightScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HeightScale"), 8000.0)), 1.0f, 1000000.0f);
		const FName Style = Node.GetName(TEXT("Style"), TEXT("Dot"));
		const int32 Iterations = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Iterations"), 3)), 1, 16);
		const float Multiplier = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Multiplier"), 1.0)), 0.1f, 16.0f);
		const float Size = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Size"), 0.08)), 0.001f, 1.0f);
		const float HeightValue = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 1.0)), -4.0f, 4.0f);
		const FName BlendMode = Node.GetName(TEXT("BlendMode"), TEXT("Max"));
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));

		const double HalfWorldSize = static_cast<double>(WorldSize) * 0.5;
		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(FIntPoint(Resolution, Resolution), FVector2d(-HalfWorldSize, -HalfWorldSize), FVector2d(HalfWorldSize, HalfWorldSize));
		if (!Domain.IsValid())
		{
			Error = TEXT("DotNoise produced an invalid grid domain.");
			return false;
		}

		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = GaeaTerrainFieldNames::Height;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Height;
		Height.Initialize(Domain, Descriptor);

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const float U = static_cast<float>(X) / static_cast<float>(FMath::Max(Resolution - 1, 1));
				const float V = static_cast<float>(Y) / static_cast<float>(FMath::Max(Resolution - 1, 1));
				float Value = 0.0f;

				for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
				{
					const int32 Grid = FMath::Clamp(FMath::RoundToInt((6.0f + Iteration * 4.0f) * Multiplier), 1, 128);
					const float GX = U * static_cast<float>(Grid);
					const float GY = V * static_cast<float>(Grid);
					const int32 CellX = FMath::FloorToInt(GX);
					const int32 CellY = FMath::FloorToInt(GY);
					float Pass = 0.0f;

					for (int32 CY = CellY - 1; CY <= CellY + 1; ++CY)
					{
						for (int32 CX = CellX - 1; CX <= CellX + 1; ++CX)
						{
							const float CenterX = static_cast<float>(CX) + 0.15f + DotNoiseHash01(CX, CY, Seed + Iteration * 101, 0x31a7U) * 0.7f;
							const float CenterY = static_cast<float>(CY) + 0.15f + DotNoiseHash01(CX, CY, Seed + Iteration * 101, 0x72b9U) * 0.7f;
							const float Radius = FMath::Max(Size * static_cast<float>(Grid) * FMath::Lerp(0.65f, 1.35f, DotNoiseHash01(CX, CY, Seed + Iteration * 101, 0xa3d1U)), 0.01f);
							const float DX = GX - CenterX;
							const float DY = GY - CenterY;
							float Stamp = 0.0f;
							if (Style == TEXT("Square"))
							{
								const float D = FMath::Max(FMath::Abs(DX), FMath::Abs(DY));
								Stamp = 1.0f - FMath::SmoothStep(Radius * 0.65f, Radius, D);
							}
							else
							{
								const float D = FMath::Sqrt(DX * DX + DY * DY);
								Stamp = 1.0f - FMath::SmoothStep(Radius * 0.65f, Radius, D);
							}
							Pass = FMath::Max(Pass, Stamp);
						}
					}

					Value = DotNoiseBlend(Value, Pass * HeightValue, BlendMode);
				}

				Height.AtInterior(X, Y) = FMath::Clamp(Value, -1.0f, 1.0f);
			}
		}

		FGaeaTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Height)))
		{
			Error = TEXT("DotNoise could not publish its Height field.");
			return false;
		}
		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("DotNoise produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterGaeaDotNoiseNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::DotNoise;
	Descriptor.DisplayName = TEXT("DotNoise");
	Descriptor.Category = TEXT("Primitive");
	Descriptor.Description = TEXT("Generates layered dot or square stamp noise with configurable blending.");
	Descriptor.Outputs.Add(DotNoiseTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(DotNoiseNameParameter(TEXT("Style"), TEXT("Style"), TEXT("Dot"), { TEXT("Dot"), TEXT("Square") }));
	Descriptor.Parameters.Add(DotNoiseIntegerParameter(TEXT("Iterations"), TEXT("Iterations"), 3, 1, 16));
	Descriptor.Parameters.Add(DotNoiseNumberParameter(TEXT("Multiplier"), TEXT("Multiplier"), 1.0, 0.1, 16.0));
	Descriptor.Parameters.Add(DotNoiseNumberParameter(TEXT("Size"), TEXT("Size"), 0.08, 0.001, 1.0));
	Descriptor.Parameters.Add(DotNoiseNumberParameter(TEXT("Height"), TEXT("Height"), 1.0, -4.0, 4.0));
	Descriptor.Parameters.Add(DotNoiseNameParameter(TEXT("BlendMode"), TEXT("Blend Mode"), TEXT("Max"), { TEXT("None"), TEXT("Blend"), TEXT("Add"), TEXT("Subtract"), TEXT("Difference"), TEXT("Multiply"), TEXT("Screen"), TEXT("Max"), TEXT("Min") }));
	Descriptor.Parameters.Add(DotNoiseIntegerParameter(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::DotNoise, EvaluateDotNoiseNode);
}
