#include "EonformCellular3DNode.h"

#include "EonformGridDomain.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor Cellular3DPort(FName Name, FName DataType, const TCHAR *DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = DataType;
		if (DisplayName)
			Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor Cellular3DNumberParameter(FName Name, const TCHAR *DisplayName, double DefaultValue, double Minimum, double Maximum)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Number;
		Parameter.DefaultNumber = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		return Parameter;
	}

	FEonformTerrainParameterDescriptor Cellular3DIntegerParameter(FName Name, const TCHAR *DisplayName, int64 DefaultValue, int64 Minimum, int64 Maximum)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Integer;
		Parameter.DefaultInteger = DefaultValue;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = static_cast<double>(Minimum);
		Parameter.bHasMaximum = true;
		Parameter.Maximum = static_cast<double>(Maximum);
		return Parameter;
	}

	FEonformTerrainParameterDescriptor Cellular3DBooleanParameter(FName Name, const TCHAR *DisplayName, bool DefaultValue)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
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

	uint32 Cellular3DCellHash(int32 X, int32 Y, int32 Layer, int32 Seed, uint32 Salt)
	{
		uint32 H = static_cast<uint32>(X) * 0x9e3779b9U;
		H ^= static_cast<uint32>(Y) * 0x85ebca6bU;
		H ^= static_cast<uint32>(Layer) * 0xc2b2ae35U;
		H ^= static_cast<uint32>(Seed) * 0x27d4eb2fU;
		H ^= Salt;
		return Cellular3DHash(H);
	}

	float Cellular3DHash01(int32 X, int32 Y, int32 Layer, int32 Seed, uint32 Salt)
	{
		return static_cast<float>(Cellular3DCellHash(X, Y, Layer, Seed, Salt) & 0x00ffffffU) / static_cast<float>(0x01000000U);
	}

	float Cellular3DFacetDistance(float DX, float DY, float ScaleZ, uint32 Hash)
	{
		// Blend several convex distance bases per cell. Each basis is planar, so
		// the resulting surface has the hard low-poly facets visible in Gaea's
		// Cellular3D reference rather than rounded radial Worley blobs.
		const float A = FMath::Lerp(0.65f, 1.35f, static_cast<float>((Hash >> 8) & 0xffU) / 255.0f);
		const float B = FMath::Lerp(0.65f, 1.35f, static_cast<float>((Hash >> 16) & 0xffU) / 255.0f);
		const float Manhattan = FMath::Abs(DX) * A + FMath::Abs(DY) * B;
		const float Chebyshev = FMath::Max(FMath::Abs(DX) * A, FMath::Abs(DY) * B);
		const float Mix = static_cast<float>((Hash >> 24) & 0xffU) / 255.0f;
		return FMath::Lerp(Manhattan, Chebyshev * 1.45f, Mix) / FMath::Max(ScaleZ, 0.001f);
	}

	bool EvaluateCellular3DNode(
			const FEonformTerrainNode &Node,
			const FEonformTerrainNodeInputs &Inputs,
			const FEonformTerrainEvaluationContext &,
			FEonformTerrainNodeEvaluation &Out,
			FString &Error)
	{
		const FEonformTerrainValue *Input = nullptr;
		if (const FEonformTerrainValue *const *InputPtr = Inputs.Find(TEXT("Input")))
			Input = *InputPtr;

		const FEonformScalarField *InputHeight = nullptr;
		float HeightScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HeightScale"), 8000.0)), 1.0f, 1000000.0f);
		FEonformGridDomain Domain;
		if (Input && Input->Type == EEonformTerrainValueType::Terrain && Input->IsValid())
		{
			InputHeight = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
			if (!InputHeight || !InputHeight->IsValid())
			{
				Error = TEXT("Cellular3D Input terrain has no valid Height field.");
				return false;
			}
			Domain = InputHeight->Domain;
			HeightScale = Input->HeightScale;
		}
		else
		{
			const int32 Resolution = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 257)), 2, 4097);
			const float WorldSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WorldSize"), 100000.0)), 1.0f, 10000000.0f);
			const double HalfWorldSize = static_cast<double>(WorldSize) * 0.5;
			Domain = FEonformGridDomain::Make(
					FIntPoint(Resolution, Resolution),
					FVector2d(-HalfWorldSize, -HalfWorldSize),
					FVector2d(HalfWorldSize, HalfWorldSize));
		}

		if (!Domain.IsValid())
		{
			Error = TEXT("Cellular3D produced an invalid grid domain.");
			return false;
		}

		const float WorldSize = static_cast<float>(FMath::Max(Domain.WorldMax.X - Domain.WorldMin.X, Domain.WorldMax.Y - Domain.WorldMin.Y));
		const float Size = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Size"), 0.5)), 0.001f, 10.0f);
		const float Gap = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Gap"), 0.0)), 0.0f, 1.0f);
		const float JitterX = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("JitterX"), 1.0)), 0.0f, 2.0f);
		const float JitterY = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("JitterY"), 1.0)), 0.0f, 2.0f);
		const float JitterZ = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("JitterZ"), 1.0)), 0.0f, 2.0f);
		const float ScaleX = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ScaleX"), 1.0)), 0.001f, 10.0f);
		const float ScaleY = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ScaleY"), 1.0)), 0.001f, 10.0f);
		const float ScaleZ = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ScaleZ"), 1.0)), 0.001f, 10.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		const bool bNormalizeInput = Node.GetBool(TEXT("NormalizeInput"), true);

		FEonformFieldDescriptor HeightDescriptor;
		HeightDescriptor.Name = EonformTerrainFieldNames::Height;
		HeightDescriptor.Unit = EEonformFieldUnit::Normalized;
		HeightDescriptor.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField HeightField;
		HeightField.Initialize(Domain, HeightDescriptor);

		FEonformFieldDescriptor HashDescriptor;
		HashDescriptor.Name = TEXT("Hashmap");
		HashDescriptor.Unit = EEonformFieldUnit::Normalized;
		HashDescriptor.Interpolation = EEonformInterpolation::Nearest;
		FEonformScalarField HashField;
		HashField.Initialize(Domain, HashDescriptor);

		float InputMin = 0.0f;
		float InputMax = 1.0f;
		if (InputHeight && bNormalizeInput)
		{
			InputMin = TNumericLimits<float>::Max();
			InputMax = TNumericLimits<float>::Lowest();
			for (const float Value : InputHeight->Values)
			{
				InputMin = FMath::Min(InputMin, Value);
				InputMax = FMath::Max(InputMax, Value);
			}
		}
		const float InputRange = FMath::Max(InputMax - InputMin, UE_SMALL_NUMBER);

		// Cellular3D has the opposite Size direction from Cellular: current Gaea
		// documents larger Size values as larger, more widely spaced 3D cells.
		const float CellWorldSize = FMath::Max(WorldSize * 0.055f * Size, WorldSize / 256.0f);
		constexpr int32 Layers = 3;

		for (int32 Y = 0; Y < Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Domain.Dimensions.X; ++X)
			{
				const FVector2d World = Domain.InteriorSampleToWorld(X, Y);
				const float PX = static_cast<float>(World.X) / (CellWorldSize * ScaleX);
				const float PY = static_cast<float>(World.Y) / (CellWorldSize * ScaleY);
				const int32 BaseX = FMath::FloorToInt(PX);
				const int32 BaseY = FMath::FloorToInt(PY);

				float BestSurface = -1.0f;
				float BestHashValue = 0.0f;
				float BestEdgeDistance = 0.0f;

				for (int32 Layer = 0; Layer < Layers; ++Layer)
				{
					for (int32 CY = BaseY - 2; CY <= BaseY + 2; ++CY)
					{
						for (int32 CX = BaseX - 2; CX <= BaseX + 2; ++CX)
						{
							const uint32 CellHash = Cellular3DCellHash(CX, CY, Layer, Seed, 0x3d77U);
							const float FeatureX = static_cast<float>(CX) + 0.5f + (Cellular3DHash01(CX, CY, Layer, Seed, 0x41a7U) - 0.5f) * JitterX;
							const float FeatureY = static_cast<float>(CY) + 0.5f + (Cellular3DHash01(CX, CY, Layer, Seed, 0x72b9U) - 0.5f) * JitterY;
							const float DX = PX - FeatureX;
							const float DY = PY - FeatureY;
							const float Radial = FMath::Sqrt(DX * DX + DY * DY);
							if (Radial > 1.75f)
								continue;

							const float BaseZ = (static_cast<float>(Layer) + 0.35f) / static_cast<float>(Layers);
							const float ZVariation = (Cellular3DHash01(CX, CY, Layer, Seed, 0xa3d1U) - 0.5f) * 0.8f * JitterZ;
							const float Peak = FMath::Clamp(BaseZ + ZVariation, 0.05f, 1.35f);
							const float FacetDistance = Cellular3DFacetDistance(DX, DY, ScaleZ, CellHash);
							const float Surface = Peak - FacetDistance * 0.65f;
							if (Surface > BestSurface)
							{
								BestSurface = Surface;
								BestHashValue = static_cast<float>(CellHash & 0x00ffffffU) / static_cast<float>(0x01000000U);
								BestEdgeDistance = FacetDistance;
							}
						}
					}
				}

				float InputWeight = 1.0f;
				if (InputHeight)
				{
					const float Source = InputHeight->AtInterior(X, Y);
					InputWeight = bNormalizeInput ? FMath::Clamp((Source - InputMin) / InputRange, 0.0f, 1.0f) : FMath::Clamp(Source, 0.0f, 1.0f);
				}

				const float GapCut = Gap * 0.32f;
				float Value = FMath::Clamp((BestSurface - GapCut) * InputWeight, 0.0f, 1.0f);
				if (Gap > UE_SMALL_NUMBER)
				{
					const float EdgeOpening = FMath::Clamp((BestEdgeDistance - Gap * 0.2f) / FMath::Max(1.0f - Gap * 0.2f, UE_SMALL_NUMBER), 0.0f, 1.0f);
					Value *= 1.0f - Gap * EdgeOpening;
				}

				HeightField.AtInterior(X, Y) = Value;
				HashField.AtInterior(X, Y) = BestHashValue;
			}
		}

		FEonformScalarField HashOutput = HashField;
		FEonformTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(HeightField)))
		{
			Error = TEXT("Cellular3D could not publish its Height field.");
			return false;
		}
		Dataset.SetScalarField(MoveTemp(HashField));

		FEonformTerrainValue Result = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Cellular3D produced an invalid terrain value.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		Out.Outputs.Add(TEXT("Hashmap"), FEonformTerrainValue::MakeScalarField(MoveTemp(HashOutput)));
		return true;
	}
}

void RegisterEonformCellular3DNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::Cellular3D;
	Descriptor.DisplayName = TEXT("Cellular3D");
	Descriptor.Category = TEXT("Primitive");
	Descriptor.Description = TEXT("Generates volumetric-looking faceted cellular forms with independent axis jitter, scale, gap, and hash-map output.");
	Descriptor.Inputs.Add(Cellular3DPort(TEXT("Input"), TEXT("Terrain"), TEXT("Input")));
	Descriptor.Inputs.Add(Cellular3DPort(TEXT("Gap"), TEXT("ScalarField"), TEXT("Gap")));
	Descriptor.Outputs.Add(Cellular3DPort(TEXT("Out"), TEXT("Terrain"), TEXT("Out")));
	Descriptor.Outputs.Add(Cellular3DPort(TEXT("Hashmap"), TEXT("ScalarField"), TEXT("Hashmap")));
	Descriptor.Parameters.Add(Cellular3DNumberParameter(TEXT("Size"), TEXT("Size"), 0.5, 0.001, 10.0));
	Descriptor.Parameters.Add(Cellular3DNumberParameter(TEXT("Gap"), TEXT("Gap"), 0.0, 0.0, 1.0));
	Descriptor.Parameters.Add(Cellular3DNumberParameter(TEXT("JitterX"), TEXT("Jitter X"), 1.0, 0.0, 2.0));
	Descriptor.Parameters.Add(Cellular3DNumberParameter(TEXT("JitterY"), TEXT("Jitter Y"), 1.0, 0.0, 2.0));
	Descriptor.Parameters.Add(Cellular3DNumberParameter(TEXT("JitterZ"), TEXT("Jitter Z"), 1.0, 0.0, 2.0));
	Descriptor.Parameters.Add(Cellular3DNumberParameter(TEXT("ScaleX"), TEXT("Scale X"), 1.0, 0.001, 10.0));
	Descriptor.Parameters.Add(Cellular3DNumberParameter(TEXT("ScaleY"), TEXT("Scale Y"), 1.0, 0.001, 10.0));
	Descriptor.Parameters.Add(Cellular3DNumberParameter(TEXT("ScaleZ"), TEXT("Scale Z"), 1.0, 0.001, 10.0));
	Descriptor.Parameters.Add(Cellular3DIntegerParameter(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647));
	Descriptor.Parameters.Add(Cellular3DBooleanParameter(TEXT("NormalizeInput"), TEXT("Normalize Input"), true));

	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::Cellular3D, EvaluateCellular3DNode);
}
