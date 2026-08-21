#include "GaeaFractalTerracesNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor FractalTerracesTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor FractalTerracesNumberParameter(
		FName Name,
		const TCHAR* DisplayName,
		double DefaultValue,
		double Minimum,
		double Maximum,
		const TCHAR* Group = nullptr)
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
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor FractalTerracesIntegerParameter(
		FName Name,
		const TCHAR* DisplayName,
		int64 DefaultValue,
		int64 Minimum,
		int64 Maximum,
		const TCHAR* Group = nullptr)
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
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	FGaeaTerrainParameterDescriptor FractalTerracesNameParameter(
		FName Name,
		const TCHAR* DisplayName,
		FName DefaultValue,
		const TCHAR* Group = nullptr)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Name;
		Parameter.DefaultName = DefaultValue;
		Parameter.NameOptions.Add(TEXT("Classic"));
		Parameter.NameOptions.Add(TEXT("Improved"));
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	float FractalTerracesHash01(int32 X, int32 Y, int32 Seed)
	{
		uint32 H = static_cast<uint32>(X) * 374761393u;
		H += static_cast<uint32>(Y) * 668265263u;
		H += static_cast<uint32>(Seed) * 2246822519u;
		H = (H ^ (H >> 13u)) * 1274126177u;
		H ^= H >> 16u;
		return static_cast<float>(H & 0x00ffffffu) / static_cast<float>(0x00ffffffu);
	}

	float FractalTerracesSmoothStep(float T)
	{
		T = FMath::Clamp(T, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	float FractalTerracesApplyBand(float Value, float Steps, float Shape, float Character, float Phase)
	{
		const float Scaled = FMath::Clamp(Value + Phase, 0.0f, 1.0f) * FMath::Max(Steps, 1.0f);
		const float Base = FMath::FloorToFloat(Scaled);
		const float Fraction = FMath::Frac(Scaled);
		const float ShapePower = FMath::Lerp(0.25f, 4.0f, FMath::Clamp(Shape, 0.0f, 1.0f));
		float Edge = FMath::Pow(Fraction, ShapePower);
		Edge = FMath::Lerp(Edge, FractalTerracesSmoothStep(Edge), FMath::Clamp(Character, 0.0f, 1.0f));
		return FMath::Clamp((Base + Edge) / FMath::Max(Steps, 1.0f), 0.0f, 1.0f);
	}

	bool FractalTerracesProcessHeight(
		const FGaeaTerrainNode& Node,
		const FGaeaScalarField& Source,
		FGaeaScalarField& OutField,
		FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("FractalTerraces received an invalid Height field.");
			return false;
		}

		const FName Mode = Node.GetName(TEXT("Mode"), TEXT("Improved"));
		if (Mode != TEXT("Classic") && Mode != TEXT("Improved"))
		{
			Error = TEXT("FractalTerraces Mode must be Classic or Improved.");
			return false;
		}

		const float Spacing = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Spacing"), 0.15)), 0.01f, 1.0f);
		const int32 Octaves = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Octaves"), 4)), 1, 12);
		const float Intensity = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Intensity"), 0.65)), 0.0f, 1.0f);
		const float Shape = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Shape"), 0.5)), 0.0f, 1.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));

		const float ShapesSeparation = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ShapesSeparation"), 0.5)), 0.0f, 1.0f);
		const int32 MacroOctaves = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("MacroOctaves"), 2)), 1, 8);
		const float MicroShape = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("MicroShape"), 0.5)), 0.0f, 1.0f);
		const float Character = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Character"), 0.5)), 0.0f, 1.0f);
		const float ThicknessUniformity = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ThicknessUniformity"), 0.5)), 0.0f, 1.0f);
		const float HardnessUniformity = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HardnessUniformity"), 0.5)), 0.0f, 1.0f);
		const float StrataDetails = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("StrataDetails"), 0.5)), 0.0f, 1.0f);

		OutField = Source;
		const int32 Width = Source.Domain.Dimensions.X;
		const int32 Height = Source.Domain.Dimensions.Y;
		const float BaseSteps = FMath::Max(2.0f, 1.0f / Spacing);

		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const float OriginalSigned = FMath::Clamp(Source.AtInterior(X, Y), -1.0f, 1.0f);
				const float Original = OriginalSigned * 0.5f + 0.5f;

				float Accumulated = 0.0f;
				float WeightSum = 0.0f;
				float Weight = 1.0f;
				for (int32 Octave = 0; Octave < Octaves; ++Octave)
				{
					const float Frequency = FMath::Pow(2.0f, static_cast<float>(Octave));
					const float Noise = FractalTerracesHash01(X >> FMath::Min(Octave, 8), Y >> FMath::Min(Octave, 8), Seed + Octave * 7919) - 0.5f;
					const float Uniformity = FMath::Lerp(1.0f + Noise * 0.8f, 1.0f, ThicknessUniformity);
					const float Phase = Noise * ShapesSeparation * 0.08f;
					const float Steps = BaseSteps * Frequency * FMath::Max(0.25f, Uniformity);
					const float OctaveShape = FMath::Clamp(FMath::Lerp(Shape, MicroShape, static_cast<float>(Octave) / FMath::Max(1.0f, static_cast<float>(Octaves - 1))), 0.0f, 1.0f);
					const float Terraced = FractalTerracesApplyBand(Original, Steps, OctaveShape, Character, Phase);
					const float HardnessNoise = FractalTerracesHash01(X, Y, Seed ^ (Octave * 104729)) - 0.5f;
					const float Hardness = FMath::Clamp(FMath::Lerp(1.0f + HardnessNoise * 0.5f, 1.0f, HardnessUniformity), 0.5f, 1.5f);
					Accumulated += FMath::Lerp(Original, Terraced, FMath::Clamp(Intensity * Hardness, 0.0f, 1.0f)) * Weight;
					WeightSum += Weight;
					Weight *= FMath::Lerp(0.35f, 0.7f, StrataDetails);
				}

				float Result = WeightSum > UE_SMALL_NUMBER ? Accumulated / WeightSum : Original;
				if (Mode == TEXT("Improved"))
				{
					float Macro = 0.0f;
					float MacroWeightSum = 0.0f;
					float MacroWeight = 1.0f;
					for (int32 MacroOctave = 0; MacroOctave < MacroOctaves; ++MacroOctave)
					{
						const float MacroSteps = FMath::Max(2.0f, BaseSteps / FMath::Pow(2.0f, static_cast<float>(MacroOctave + 1)));
						Macro += FractalTerracesApplyBand(Original, MacroSteps, Shape, Character, 0.0f) * MacroWeight;
						MacroWeightSum += MacroWeight;
						MacroWeight *= 0.5f;
					}
					if (MacroWeightSum > UE_SMALL_NUMBER)
					{
						Result = FMath::Lerp(Result, Macro / MacroWeightSum, ShapesSeparation * 0.35f);
					}
				}

				OutField.AtInterior(X, Y) = FMath::Clamp(Result * 2.0f - 1.0f, -1.0f, 1.0f);
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateFractalTerracesNode(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("FractalTerraces requires a valid terrain input 'Terrain'.");
			return false;
		}

		const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("FractalTerraces terrain input has no valid Height field.");
			return false;
		}

		FGaeaScalarField ResultHeight;
		if (!FractalTerracesProcessHeight(Node, *Height, ResultHeight, Error)) return false;
		ResultHeight.Descriptor.Name = GaeaTerrainFieldNames::Height;

		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
		{
			Error = TEXT("FractalTerraces could not publish its Height field.");
			return false;
		}

		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("FractalTerraces produced an invalid terrain value.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterGaeaFractalTerracesNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::FractalTerraces;
	Descriptor.DisplayName = TEXT("FractalTerraces");
	Descriptor.Category = TEXT("Profile");
	Descriptor.Description = TEXT("Creates detailed fractal terraces and terrain stratification across multiple octaves.");
	Descriptor.Inputs.Add(FractalTerracesTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(FractalTerracesTerrainPort(TEXT("Out"), TEXT("Out")));

	Descriptor.Parameters.Add(FractalTerracesNameParameter(TEXT("Mode"), TEXT("Mode"), TEXT("Improved"), TEXT("Terracing")));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("Spacing"), TEXT("Spacing"), 0.15, 0.01, 1.0, TEXT("Terracing")));
	Descriptor.Parameters.Add(FractalTerracesIntegerParameter(TEXT("Octaves"), TEXT("Octaves"), 4, 1, 12, TEXT("Terracing")));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("Intensity"), TEXT("Intensity"), 0.65, 0.0, 1.0, TEXT("Terracing")));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("Shape"), TEXT("Shape"), 0.5, 0.0, 1.0, TEXT("Terracing")));
	Descriptor.Parameters.Add(FractalTerracesIntegerParameter(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Terracing")));

	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("ShapesSeparation"), TEXT("Shapes Separation"), 0.5, 0.0, 1.0, TEXT("Advanced")));
	Descriptor.Parameters.Add(FractalTerracesIntegerParameter(TEXT("MacroOctaves"), TEXT("Macro Octaves"), 2, 1, 8, TEXT("Advanced")));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("MicroShape"), TEXT("Micro Shape"), 0.5, 0.0, 1.0, TEXT("Advanced")));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("Character"), TEXT("Character"), 0.5, 0.0, 1.0, TEXT("Advanced")));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("ThicknessUniformity"), TEXT("Thickness Uniformity"), 0.5, 0.0, 1.0, TEXT("Advanced")));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("HardnessUniformity"), TEXT("Hardness Uniformity"), 0.5, 0.0, 1.0, TEXT("Advanced")));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("StrataDetails"), TEXT("Strata Details"), 0.5, 0.0, 1.0, TEXT("Advanced")));

	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::FractalTerraces, EvaluateFractalTerracesNode);
}
