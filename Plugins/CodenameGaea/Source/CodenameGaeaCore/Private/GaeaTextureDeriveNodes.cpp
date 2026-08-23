#include "GaeaTextureDeriveNodes.h"

#include "GaeaColorField.h"
#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor Port(FName Name, const TCHAR* DisplayName, FName DataType)
	{
		FGaeaTerrainPortDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = DisplayName;
		Result.DataType = DataType;
		return Result;
	}

	FGaeaTerrainParameterDescriptor Number(FName Name, const TCHAR* DisplayName, double Default, double Min, double Max, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = DisplayName;
		Result.Group = Group;
		Result.Type = EGaeaTerrainParameterType::Number;
		Result.DefaultNumber = Default;
		Result.bHasMinimum = true;
		Result.Minimum = Min;
		Result.bHasMaximum = true;
		Result.Maximum = Max;
		return Result;
	}

	FGaeaTerrainParameterDescriptor Integer(FName Name, const TCHAR* DisplayName, int64 Default, int64 Min, int64 Max, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = DisplayName;
		Result.Group = Group;
		Result.Type = EGaeaTerrainParameterType::Integer;
		Result.DefaultInteger = Default;
		Result.bHasMinimum = true;
		Result.Minimum = static_cast<double>(Min);
		Result.bHasMaximum = true;
		Result.Maximum = static_cast<double>(Max);
		return Result;
	}

	FGaeaTerrainParameterDescriptor Boolean(FName Name, const TCHAR* DisplayName, bool Default, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor Result;
		Result.Name = Name;
		Result.DisplayName = DisplayName;
		Result.Group = Group;
		Result.Type = EGaeaTerrainParameterType::Boolean;
		Result.DefaultBoolean = Default;
		return Result;
	}

	FGaeaTerrainParameterDescriptor Name(FName ParameterName, const TCHAR* DisplayName, FName Default, std::initializer_list<FName> Options, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor Result;
		Result.Name = ParameterName;
		Result.DisplayName = DisplayName;
		Result.Group = Group;
		Result.Type = EGaeaTerrainParameterType::Name;
		Result.DefaultName = Default;
		for (const FName Option : Options) Result.NameOptions.Add(Option);
		return Result;
	}

	FGaeaTerrainParameterDescriptor Color(FName ParameterName, const TCHAR* DisplayName, const FLinearColor& Default, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor Result;
		Result.Name = ParameterName;
		Result.DisplayName = DisplayName;
		Result.Group = Group;
		Result.Type = EGaeaTerrainParameterType::Color;
		Result.DefaultColor = Default;
		return Result;
	}

	FGaeaScalarField MakeField(const FGaeaGridDomain& Domain, FName FieldName)
	{
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = FieldName;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Field;
		Field.Initialize(Domain, Descriptor, 0.0f);
		return Field;
	}

	float Smooth01(float V)
	{
		V = FMath::Clamp(V, 0.0f, 1.0f);
		return V * V * (3.0f - 2.0f * V);
	}

	float HashNoise(int32 X, int32 Y, int32 Seed)
	{
		uint32 H = static_cast<uint32>(X) * 0x8da6b343u;
		H ^= static_cast<uint32>(Y) * 0xd8163841u;
		H ^= static_cast<uint32>(Seed) * 0xcb1ab31fu;
		H ^= H >> 13;
		H *= 0x85ebca6bu;
		H ^= H >> 16;
		return static_cast<float>(H & 0x00ffffffu) / static_cast<float>(0x00ffffffu);
	}

	float PatchNoise(int32 X, int32 Y, int32 Seed)
	{
		const int32 CX = X / 5;
		const int32 CY = Y / 5;
		const float A = HashNoise(CX, CY, Seed);
		const float B = HashNoise(CX + 1, CY, Seed);
		const float C = HashNoise(CX, CY + 1, Seed);
		const float D = HashNoise(CX + 1, CY + 1, Seed);
		const float TX = static_cast<float>(X % 5) / 5.0f;
		const float TY = static_cast<float>(Y % 5) / 5.0f;
		return FMath::Lerp(FMath::Lerp(A, B, TX), FMath::Lerp(C, D, TX), TY);
	}

	bool PrepareTerrain(const FGaeaTerrainValue& Input, const FGaeaTerrainEvaluationContext& Context, FGaeaTerrainDataset& Dataset, FString& Error)
	{
		Dataset = Input.TerrainDataset;
		FGaeaTerrainDerivedDataSettings Settings;
		if (!FGaeaTerrainDerivedData::EnsureContext(Dataset, Input.HeightScale, Context.PhysicalMetrics, &Error)) return false;
		if (!FGaeaTerrainDerivedData::EnsureGeology(Dataset, Input.HeightScale, Settings, &Error)) return false;
		if (!FGaeaTerrainDerivedData::EnsureProcessMasks(Dataset, Input.HeightScale, Settings, &Error)) return false;
		if (!FGaeaTerrainDerivedData::EnsureHydrology(Dataset, Input.HeightScale, Context.PhysicalMetrics, &Error)) return false;
		return true;
	}

	void AutoLevel(FGaeaScalarField& Field)
	{
		float MinValue = TNumericLimits<float>::Max();
		float MaxValue = TNumericLimits<float>::Lowest();
		for (const float V : Field.Values)
		{
			MinValue = FMath::Min(MinValue, V);
			MaxValue = FMath::Max(MaxValue, V);
		}
		const float Span = MaxValue - MinValue;
		if (Span <= UE_SMALL_NUMBER) return;
		for (float& V : Field.Values) V = FMath::Clamp((V - MinValue) / Span, 0.0f, 1.0f);
	}

	void Equalize(FGaeaScalarField& Field)
	{
		constexpr int32 BinCount = 256;
		int32 Histogram[BinCount] = {};
		for (const float V : Field.Values)
		{
			const int32 Bin = FMath::Clamp(FMath::RoundToInt(FMath::Clamp(V, 0.0f, 1.0f) * 255.0f), 0, 255);
			++Histogram[Bin];
		}
		int32 Cumulative[BinCount] = {};
		int32 Running = 0;
		for (int32 I = 0; I < BinCount; ++I)
		{
			Running += Histogram[I];
			Cumulative[I] = Running;
		}
		if (Running <= 0) return;
		for (float& V : Field.Values)
		{
			const int32 Bin = FMath::Clamp(FMath::RoundToInt(FMath::Clamp(V, 0.0f, 1.0f) * 255.0f), 0, 255);
			V = static_cast<float>(Cumulative[Bin]) / static_cast<float>(Running);
		}
	}

	bool EvaluateTextureBase(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("TextureBase requires a valid terrain input 'Terrain'.");
			return false;
		}

		FGaeaTerrainDataset Dataset;
		if (!PrepareTerrain(*Input, Context, Dataset, Error)) return false;
		const FGaeaScalarField* Slope = Dataset.FindScalarField(GaeaTerrainFieldNames::SlopeDegrees);
		const FGaeaScalarField* Flow = Dataset.FindScalarField(GaeaTerrainFieldNames::FlowAccumulation);
		const FGaeaScalarField* SoilDepth = Dataset.FindScalarField(GaeaTerrainFieldNames::SoilDepth);
		const FGaeaScalarField* Convexity = Dataset.FindScalarField(GaeaTerrainFieldNames::Convexity);
		const FGaeaScalarField* Elevation = Dataset.FindScalarField(GaeaTerrainFieldNames::Elevation);
		if (!Slope || !Flow || !SoilDepth || !Convexity || !Elevation)
		{
			Error = TEXT("TextureBase could not resolve required terrain semantics.");
			return false;
		}

		const float SlopeWeight = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Slope"), 0.55)), 0.0f, 1.0f);
		const float FlowWeight = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Flows"), 0.45)), 0.0f, 1.0f);
		const float SoilWeight = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Soil"), 0.45)), 0.0f, 1.0f);
		const float PatchesWeight = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Patches"), 0.35)), 0.0f, 1.0f);
		const float ChaosWeight = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Chaos"), 0.28)), 0.0f, 1.0f);
		const float PeaksWeight = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Peaks"), 0.35)), 0.0f, 1.0f);
		const FName Accentuate = Node.GetName(TEXT("Accentuate"), TEXT("New"));
		const FName Enhance = Node.GetName(TEXT("Enhance"), TEXT("None"));
		const bool bReverse = Node.GetBool(TEXT("Reverse"), false);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));

		float MaxFlow = 1.0f;
		for (const float V : Flow->Values) MaxFlow = FMath::Max(MaxFlow, V);
		const float InvLogFlow = 1.0f / FMath::Max(FMath::Loge(1.0f + MaxFlow), UE_SMALL_NUMBER);

		FGaeaScalarField Texture = MakeField(Slope->Domain, GaeaTerrainFieldNames::TextureBase);
		for (int32 Y = 0; Y < Slope->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Slope->Domain.Dimensions.X; ++X)
			{
				const float S = FMath::Clamp(Slope->AtInterior(X, Y) / 65.0f, 0.0f, 1.0f);
				const float F = FMath::Clamp(FMath::Loge(1.0f + FMath::Max(Flow->AtInterior(X, Y), 0.0f)) * InvLogFlow, 0.0f, 1.0f);
				const float Soil = FMath::Clamp(SoilDepth->AtInterior(X, Y), 0.0f, 1.0f);
				const float Peak = FMath::Clamp(Elevation->AtInterior(X, Y) * 0.55f + Convexity->AtInterior(X, Y) * 0.45f, 0.0f, 1.0f);
				const float Patch = PatchNoise(X, Y, Seed + 101);
				const float Chaos = HashNoise(X, Y, Seed + 313);

				const float WeightSum = FMath::Max(SlopeWeight + FlowWeight + SoilWeight + PatchesWeight + ChaosWeight + PeaksWeight, 0.001f);
				float V = (
					S * SlopeWeight
					+ F * FlowWeight
					+ Soil * SoilWeight
					+ Patch * PatchesWeight
					+ Chaos * ChaosWeight
					+ Peak * PeaksWeight) / WeightSum;

				if (Accentuate == TEXT("Old")) V = FMath::Clamp((V - 0.5f) * 1.35f + 0.5f, 0.0f, 1.0f);
				else if (Accentuate == TEXT("New")) V = Smooth01(V);
				if (bReverse) V = 1.0f - V;
				Texture.AtInterior(X, Y) = V;
			}
		}

		if (Enhance == TEXT("Autolevel")) AutoLevel(Texture);
		else if (Enhance == TEXT("Equalize")) Equalize(Texture);

		FGaeaScalarField Output = Texture;
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(Texture)))
		{
			Error = TEXT("TextureBase could not publish its texture semantic.");
			return false;
		}
		Out.Outputs.Add(TEXT("Terrain"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Output)));
		Error.Reset();
		return true;
	}

	struct FTextureProfile
	{
		float Slope;
		float Flow;
		float Soil;
		float Peaks;
		float Patches;
	};

	FTextureProfile ProfileFor(FName Style)
	{
		static const FTextureProfile Profiles[12] = {
			{0.70f,0.25f,0.20f,0.55f,0.25f}, {0.35f,0.70f,0.20f,0.30f,0.35f},
			{0.25f,0.25f,0.75f,0.15f,0.45f}, {0.55f,0.45f,0.30f,0.45f,0.55f},
			{0.20f,0.55f,0.55f,0.20f,0.70f}, {0.75f,0.20f,0.15f,0.65f,0.20f},
			{0.40f,0.65f,0.25f,0.25f,0.60f}, {0.30f,0.35f,0.70f,0.20f,0.50f},
			{0.60f,0.30f,0.35f,0.60f,0.35f}, {0.25f,0.75f,0.30f,0.15f,0.50f},
			{0.50f,0.40f,0.50f,0.35f,0.65f}, {0.45f,0.50f,0.40f,0.50f,0.45f}
		};
		const FString StyleString = Style.ToString();
		const TCHAR Letter = StyleString.IsEmpty() ? TEXT('A') : StyleString[0];
		const int32 Index = FMath::Clamp(static_cast<int32>(Letter - TEXT('A')), 0, 11);
		return Profiles[Index];
	}

	bool EvaluateTexturizer(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Texturizer requires a valid terrain input 'Terrain'.");
			return false;
		}

		FGaeaTerrainDataset Dataset;
		if (!PrepareTerrain(*Input, Context, Dataset, Error)) return false;
		const FGaeaScalarField* Slope = Dataset.FindScalarField(GaeaTerrainFieldNames::SlopeDegrees);
		const FGaeaScalarField* Flow = Dataset.FindScalarField(GaeaTerrainFieldNames::FlowAccumulation);
		const FGaeaScalarField* SoilDepth = Dataset.FindScalarField(GaeaTerrainFieldNames::SoilDepth);
		const FGaeaScalarField* Elevation = Dataset.FindScalarField(GaeaTerrainFieldNames::Elevation);
		const FGaeaScalarField* Convexity = Dataset.FindScalarField(GaeaTerrainFieldNames::Convexity);
		if (!Slope || !Flow || !SoilDepth || !Elevation || !Convexity)
		{
			Error = TEXT("Texturizer could not resolve required terrain semantics.");
			return false;
		}

		const FTextureProfile Profile = ProfileFor(Node.GetName(TEXT("Style"), TEXT("A")));
		const float Factor = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Factor"), 0.65)), 0.0f, 1.0f);
		const float Secondary = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Secondary"), 0.45)), 0.0f, 1.0f);
		const float FlowComponent = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Flows"), 0.50)), 0.0f, 1.0f);
		const float SlopeComponent = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Slope"), 0.50)), 0.0f, 1.0f);
		const float SoilComponent = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Soil"), 0.50)), 0.0f, 1.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));

		float MaxFlow = 1.0f;
		for (const float V : Flow->Values) MaxFlow = FMath::Max(MaxFlow, V);
		const float InvLogFlow = 1.0f / FMath::Max(FMath::Loge(1.0f + MaxFlow), UE_SMALL_NUMBER);

		FGaeaScalarField Texture = MakeField(Slope->Domain, GaeaTerrainFieldNames::Texturizer);
		for (int32 Y = 0; Y < Slope->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Slope->Domain.Dimensions.X; ++X)
			{
				const float S = FMath::Clamp(Slope->AtInterior(X, Y) / 65.0f, 0.0f, 1.0f);
				const float F = FMath::Clamp(FMath::Loge(1.0f + FMath::Max(Flow->AtInterior(X, Y), 0.0f)) * InvLogFlow, 0.0f, 1.0f);
				const float Soil = FMath::Clamp(SoilDepth->AtInterior(X, Y), 0.0f, 1.0f);
				const float Peak = FMath::Clamp(Elevation->AtInterior(X, Y) * 0.6f + Convexity->AtInterior(X, Y) * 0.4f, 0.0f, 1.0f);
				const float Patch = PatchNoise(X, Y, Seed + 71);
				const float Grain = HashNoise(X, Y, Seed + 919);

				const float Primary =
					S * Profile.Slope * SlopeComponent
					+ F * Profile.Flow * FlowComponent
					+ Soil * Profile.Soil * SoilComponent
					+ Peak * Profile.Peaks;
				const float PrimaryWeight = FMath::Max(
					Profile.Slope * SlopeComponent + Profile.Flow * FlowComponent + Profile.Soil * SoilComponent + Profile.Peaks,
					0.001f);
				const float Base = Primary / PrimaryWeight;
				const float Supporting = FMath::Lerp(Patch, 0.5f * Patch + 0.5f * Grain, Secondary);
				Texture.AtInterior(X, Y) = Smooth01(FMath::Clamp(FMath::Lerp(Base, Supporting, 0.40f * Secondary) * FMath::Lerp(0.55f, 1.25f, Factor), 0.0f, 1.0f));
			}
		}

		FGaeaScalarField Output = Texture;
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(Texture)))
		{
			Error = TEXT("Texturizer could not publish its texture semantic.");
			return false;
		}
		Out.Outputs.Add(TEXT("Terrain"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Output)));
		Error.Reset();
		return true;
	}

	bool EvaluateColorThreshold(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* ColorPtr = Inputs.Find(TEXT("Color"));
		const FGaeaTerrainValue* ColorInput = ColorPtr ? *ColorPtr : nullptr;
		if (!ColorInput || ColorInput->Type != EGaeaTerrainValueType::Color || !ColorInput->IsValid())
		{
			Error = TEXT("ColorThreshold requires a valid color input 'Color'.");
			return false;
		}

		const FLinearColor Start = Node.GetColor(TEXT("Start"), FLinearColor::White);
		const float Falloff = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Falloff"), 0.15)), 0.001f, 1.732f);
		FGaeaScalarField Mask = MakeField(ColorInput->ColorField.Domain, GaeaTerrainFieldNames::ColorThreshold);
		for (int32 Y = 0; Y < Mask.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Mask.Domain.Dimensions.X; ++X)
			{
				const FLinearColor Sample = ColorInput->ColorField.AtInterior(X, Y);
				const float DR = Sample.R - Start.R;
				const float DG = Sample.G - Start.G;
				const float DB = Sample.B - Start.B;
				const float Distance = FMath::Sqrt(DR * DR + DG * DG + DB * DB);
				Mask.AtInterior(X, Y) = 1.0f - Smooth01(FMath::Clamp(Distance / Falloff, 0.0f, 1.0f));
			}
		}

		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeScalarField(Mask));
		if (const FGaeaTerrainValue* const* TerrainPtr = Inputs.Find(TEXT("Terrain")))
		{
			const FGaeaTerrainValue* Terrain = *TerrainPtr;
			if (Terrain && Terrain->Type == EGaeaTerrainValueType::Terrain && Terrain->IsValid())
			{
				FGaeaTerrainDataset Dataset = Terrain->TerrainDataset;
				if (!Dataset.SetHeightDerivedScalarField(MoveTemp(Mask)))
				{
					Error = TEXT("ColorThreshold could not publish its mask semantic.");
					return false;
				}
				Out.Outputs.Add(TEXT("Terrain"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Terrain->HeightScale));
			}
		}
		Error.Reset();
		return true;
	}
}

void RegisterGaeaTextureDeriveNodes()
{
	{
		FGaeaTerrainNodeDescriptor Descriptor;
		Descriptor.Type = GaeaTerrainNodeTypes::TextureBase;
		Descriptor.DisplayName = TEXT("TextureBase");
		Descriptor.Category = TEXT("Derive");
		Descriptor.Description = TEXT("Builds a natural material-distribution mask from terrain structure, flow, soil, patchiness, chaos, and peak semantics.");
		Descriptor.Inputs.Add(Port(TEXT("Terrain"), TEXT("Input"), TEXT("Terrain")));
		Descriptor.Outputs.Add(Port(TEXT("Terrain"), TEXT("Terrain"), TEXT("Terrain")));
		Descriptor.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("ScalarField")));
		Descriptor.Parameters.Add(Number(TEXT("Slope"), TEXT("Slope"), 0.55, 0.0, 1.0, TEXT("Structure")));
		Descriptor.Parameters.Add(Number(TEXT("Flows"), TEXT("Flows"), 0.45, 0.0, 1.0, TEXT("Structure")));
		Descriptor.Parameters.Add(Number(TEXT("Soil"), TEXT("Soil"), 0.45, 0.0, 1.0, TEXT("Structure")));
		Descriptor.Parameters.Add(Number(TEXT("Patches"), TEXT("Patches"), 0.35, 0.0, 1.0, TEXT("Structure")));
		Descriptor.Parameters.Add(Number(TEXT("Chaos"), TEXT("Chaos"), 0.28, 0.0, 1.0, TEXT("Structure")));
		Descriptor.Parameters.Add(Number(TEXT("Peaks"), TEXT("Peaks"), 0.35, 0.0, 1.0, TEXT("Structure")));
		Descriptor.Parameters.Add(Name(TEXT("Accentuate"), TEXT("Accentuate"), TEXT("New"), { TEXT("Off"), TEXT("Old"), TEXT("New") }, TEXT("Processing")));
		Descriptor.Parameters.Add(Name(TEXT("Enhance"), TEXT("Enhance"), TEXT("None"), { TEXT("None"), TEXT("Autolevel"), TEXT("Equalize") }, TEXT("Processing")));
		Descriptor.Parameters.Add(Boolean(TEXT("Reverse"), TEXT("Reverse"), false, TEXT("Processing")));
		Descriptor.Parameters.Add(Integer(TEXT("Seed"), TEXT("Seed"), 1337, MIN_int32, MAX_int32, TEXT("Processing")));
		FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
		FGaeaTerrainNodeRegistry::Register(Descriptor.Type, EvaluateTextureBase);
	}

	{
		FGaeaTerrainNodeDescriptor Descriptor;
		Descriptor.Type = GaeaTerrainNodeTypes::Texturizer;
		Descriptor.DisplayName = TEXT("Texturizer");
		Descriptor.Category = TEXT("Derive");
		Descriptor.Description = TEXT("Generates curated natural-looking texture masks from twelve terrain-aware style profiles.");
		Descriptor.Inputs.Add(Port(TEXT("Terrain"), TEXT("Input"), TEXT("Terrain")));
		Descriptor.Outputs.Add(Port(TEXT("Terrain"), TEXT("Terrain"), TEXT("Terrain")));
		Descriptor.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("ScalarField")));
		Descriptor.Parameters.Add(Name(TEXT("Style"), TEXT("Style"), TEXT("A"), { TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D"), TEXT("E"), TEXT("F"), TEXT("G"), TEXT("H"), TEXT("I"), TEXT("J"), TEXT("K"), TEXT("L") }, TEXT("Style")));
		Descriptor.Parameters.Add(Number(TEXT("Factor"), TEXT("Factor"), 0.65, 0.0, 1.0, TEXT("Style")));
		Descriptor.Parameters.Add(Number(TEXT("Secondary"), TEXT("Secondary"), 0.45, 0.0, 1.0, TEXT("Style")));
		Descriptor.Parameters.Add(Integer(TEXT("Seed"), TEXT("Seed"), 1337, MIN_int32, MAX_int32, TEXT("Style")));
		Descriptor.Parameters.Add(Number(TEXT("Flows"), TEXT("Flows"), 0.50, 0.0, 1.0, TEXT("Components")));
		Descriptor.Parameters.Add(Number(TEXT("Slope"), TEXT("Slope"), 0.50, 0.0, 1.0, TEXT("Components")));
		Descriptor.Parameters.Add(Number(TEXT("Soil"), TEXT("Soil"), 0.50, 0.0, 1.0, TEXT("Components")));
		FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
		FGaeaTerrainNodeRegistry::Register(Descriptor.Type, EvaluateTexturizer);
	}

	{
		FGaeaTerrainNodeDescriptor Descriptor;
		Descriptor.Type = GaeaTerrainNodeTypes::ColorThreshold;
		Descriptor.DisplayName = TEXT("ColorThreshold");
		Descriptor.Category = TEXT("Derive");
		Descriptor.Description = TEXT("Creates a greyscale selection mask from colors close to a target color.");
		Descriptor.Inputs.Add(Port(TEXT("Color"), TEXT("Color"), TEXT("Color")));
		Descriptor.Inputs.Add(Port(TEXT("Terrain"), TEXT("Terrain Context"), TEXT("Terrain")));
		Descriptor.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("ScalarField")));
		Descriptor.Outputs.Add(Port(TEXT("Terrain"), TEXT("Terrain"), TEXT("Terrain")));
		Descriptor.Parameters.Add(Color(TEXT("Start"), TEXT("Start"), FLinearColor::White, TEXT("Selection")));
		Descriptor.Parameters.Add(Number(TEXT("Falloff"), TEXT("Falloff"), 0.15, 0.001, 1.732, TEXT("Selection")));
		FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
		FGaeaTerrainNodeRegistry::Register(Descriptor.Type, EvaluateColorThreshold);
	}
}
