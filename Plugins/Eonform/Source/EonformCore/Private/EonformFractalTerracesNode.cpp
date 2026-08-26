#include "EonformFractalTerracesNode.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor FractalTerracesTerrainPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Terrain");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FEonformTerrainParameterDescriptor FractalTerracesNumberParameter(FName Name, const TCHAR* DisplayName, double DefaultValue, double Minimum, double Maximum, const TCHAR* Group = nullptr)
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
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	FEonformTerrainParameterDescriptor FractalTerracesIntegerParameter(FName Name, const TCHAR* DisplayName, int64 DefaultValue, int64 Minimum, int64 Maximum, const TCHAR* Group = nullptr)
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
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	FEonformTerrainParameterDescriptor FractalTerracesBooleanParameter(FName Name, const TCHAR* DisplayName, bool DefaultValue, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	FEonformTerrainParameterDescriptor FractalTerracesRangeParameter(FName Name, const TCHAR* DisplayName, double DefaultMin, double DefaultMax, double Minimum, double Maximum, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EEonformTerrainParameterType::Range;
		Parameter.DefaultRangeMin = DefaultMin;
		Parameter.DefaultRangeMax = DefaultMax;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		if (Group) Parameter.Group = Group;
		return Parameter;
	}

	FEonformTerrainParameterDescriptor FractalTerracesWarpStyleParameter()
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = TEXT("WarpStyle");
		Parameter.DisplayName = TEXT("Warp Style");
		Parameter.Type = EEonformTerrainParameterType::Name;
		Parameter.DefaultName = TEXT("A");
		Parameter.NameOptions.Add(TEXT("A"));
		Parameter.NameOptions.Add(TEXT("B"));
		Parameter.NameOptions.Add(TEXT("C"));
		Parameter.NameOptions.Add(TEXT("D"));
		Parameter.Group = TEXT("Tilt");
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

	bool FractalTerracesProcessHeight(const FEonformTerrainNode& Node, const FEonformScalarField& Source, FEonformScalarField& OutField, FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("FractalTerraces received an invalid Height field.");
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
		const float ProtectMin = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ProtectRangeMin"), 0.0)), 0.0f, 1.0f);
		const float ProtectMax = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ProtectRangeMax"), 0.0)), ProtectMin, 1.0f);
		const bool bApplyTilt = Node.GetBool(TEXT("ApplyTilt"), false);
		const float TiltAmount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("TiltAmount"), 0.0)), -1.0f, 1.0f);
		const float Direction = FMath::DegreesToRadians(static_cast<float>(Node.GetNumber(TEXT("Direction"), 0.0)));
		const int32 TiltSeed = static_cast<int32>(Node.GetInteger(TEXT("TiltSeed"), Seed));
		const float WarpAmount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WarpAmount"), 0.0)), 0.0f, 1.0f);
		const float WarpSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WarpSize"), 0.5)), 0.01f, 1.0f);
		const FName WarpStyle = Node.GetName(TEXT("WarpStyle"), TEXT("A"));
		const float WarpStyleScale = WarpStyle == TEXT("B") ? 1.35f : WarpStyle == TEXT("C") ? 1.7f : WarpStyle == TEXT("D") ? 2.1f : 1.0f;

		OutField = Source;
		const int32 Width = Source.Domain.Dimensions.X;
		const int32 Height = Source.Domain.Dimensions.Y;
		const float BaseSteps = FMath::Max(2.0f, 1.0f / Spacing);
		const float DirX = FMath::Cos(Direction);
		const float DirY = FMath::Sin(Direction);

		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const float OriginalSigned = FMath::Clamp(Source.AtInterior(X, Y), -1.0f, 1.0f);
				const float Original = OriginalSigned * 0.5f + 0.5f;
				if (ProtectMax > ProtectMin && Original >= ProtectMin && Original <= ProtectMax)
				{
					OutField.AtInterior(X, Y) = OriginalSigned;
					continue;
				}

				float ProfileValue = Original;
				if (bApplyTilt)
				{
					const float NX = Width > 1 ? static_cast<float>(X) / static_cast<float>(Width - 1) - 0.5f : 0.0f;
					const float NY = Height > 1 ? static_cast<float>(Y) / static_cast<float>(Height - 1) - 0.5f : 0.0f;
					const float WarpNoise = (FractalTerracesHash01(
						FMath::FloorToInt(static_cast<float>(X) * WarpSize * 0.1f),
						FMath::FloorToInt(static_cast<float>(Y) * WarpSize * 0.1f),
						TiltSeed) - 0.5f) * 2.0f;
					ProfileValue = FMath::Clamp(ProfileValue + TiltAmount * (NX * DirX + NY * DirY) + WarpNoise * WarpAmount * 0.1f * WarpStyleScale, 0.0f, 1.0f);
				}

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
					const float Terraced = FractalTerracesApplyBand(ProfileValue, Steps, OctaveShape, Character, Phase);
					const float HardnessNoise = FractalTerracesHash01(X, Y, Seed ^ (Octave * 104729)) - 0.5f;
					const float Hardness = FMath::Clamp(FMath::Lerp(1.0f + HardnessNoise * 0.5f, 1.0f, HardnessUniformity), 0.5f, 1.5f);
					Accumulated += FMath::Lerp(ProfileValue, Terraced, FMath::Clamp(Intensity * Hardness, 0.0f, 1.0f)) * Weight;
					WeightSum += Weight;
					Weight *= FMath::Lerp(0.35f, 0.7f, StrataDetails);
				}

				float Result = WeightSum > UE_SMALL_NUMBER ? Accumulated / WeightSum : ProfileValue;
				float Macro = 0.0f;
				float MacroWeightSum = 0.0f;
				float MacroWeight = 1.0f;
				for (int32 MacroOctave = 0; MacroOctave < MacroOctaves; ++MacroOctave)
				{
					const float MacroSteps = FMath::Max(2.0f, BaseSteps / FMath::Pow(2.0f, static_cast<float>(MacroOctave + 1)));
					Macro += FractalTerracesApplyBand(ProfileValue, MacroSteps, Shape, Character, 0.0f) * MacroWeight;
					MacroWeightSum += MacroWeight;
					MacroWeight *= 0.5f;
				}
				if (MacroWeightSum > UE_SMALL_NUMBER) Result = FMath::Lerp(Result, Macro / MacroWeightSum, ShapesSeparation * 0.35f);
				OutField.AtInterior(X, Y) = FMath::Clamp(Result * 2.0f - 1.0f, -1.0f, 1.0f);
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateFractalTerracesNode(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("FractalTerraces requires a valid terrain input 'Terrain'.");
			return false;
		}
		const FEonformScalarField* Height = Input->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("FractalTerraces terrain input has no valid Height field.");
			return false;
		}
		FEonformScalarField ResultHeight;
		if (!FractalTerracesProcessHeight(Node, *Height, ResultHeight, Error)) return false;
		ResultHeight.Descriptor.Name = EonformTerrainFieldNames::Height;
		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
		{
			Error = TEXT("FractalTerraces could not publish its Height field.");
			return false;
		}
		FEonformTerrainValue Result = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("FractalTerraces produced an invalid terrain value.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterEonformFractalTerracesNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::FractalTerraces;
	Descriptor.DisplayName = TEXT("FractalTerraces");
	Descriptor.Category = TEXT("Surface");
	Descriptor.Description = TEXT("Creates multi-scale stratified terraces with optional protected elevations, tilt, and warp.");
	Descriptor.Inputs.Add(FractalTerracesTerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(FractalTerracesTerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("Spacing"), TEXT("Spacing"), 0.15, 0.01, 1.0));
	Descriptor.Parameters.Add(FractalTerracesIntegerParameter(TEXT("Octaves"), TEXT("Octaves"), 4, 1, 12));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("Intensity"), TEXT("Intensity"), 0.65, 0.0, 1.0));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("Shape"), TEXT("Shape"), 0.5, 0.0, 1.0));
	Descriptor.Parameters.Add(FractalTerracesIntegerParameter(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("ShapesSeparation"), TEXT("Shapes Separation"), 0.5, 0.0, 1.0, TEXT("Advanced")));
	Descriptor.Parameters.Add(FractalTerracesIntegerParameter(TEXT("MacroOctaves"), TEXT("Macro Octaves"), 2, 1, 8, TEXT("Advanced")));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("MicroShape"), TEXT("Micro Shape"), 0.5, 0.0, 1.0, TEXT("Advanced")));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("Character"), TEXT("Character"), 0.5, 0.0, 1.0, TEXT("Advanced")));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("ThicknessUniformity"), TEXT("Thickness Uniformity"), 0.5, 0.0, 1.0, TEXT("Advanced")));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("HardnessUniformity"), TEXT("Hardness Uniformity"), 0.5, 0.0, 1.0, TEXT("Advanced")));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("StrataDetails"), TEXT("Strata Details"), 0.5, 0.0, 1.0, TEXT("Advanced")));
	Descriptor.Parameters.Add(FractalTerracesRangeParameter(TEXT("ProtectRange"), TEXT("Protect Range"), 0.0, 0.0, 0.0, 1.0, TEXT("Advanced")));
	Descriptor.Parameters.Add(FractalTerracesBooleanParameter(TEXT("ApplyTilt"), TEXT("Apply Tilt"), false, TEXT("Tilt")));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("TiltAmount"), TEXT("Tilt Amount"), 0.0, -1.0, 1.0, TEXT("Tilt")));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("Direction"), TEXT("Direction"), 0.0, -360.0, 360.0, TEXT("Tilt")));
	Descriptor.Parameters.Add(FractalTerracesIntegerParameter(TEXT("TiltSeed"), TEXT("Tilt Seed"), 1337, -2147483647, 2147483647, TEXT("Tilt")));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("WarpAmount"), TEXT("Warp Amount"), 0.0, 0.0, 1.0, TEXT("Tilt")));
	Descriptor.Parameters.Add(FractalTerracesNumberParameter(TEXT("WarpSize"), TEXT("Warp Size"), 0.5, 0.01, 1.0, TEXT("Tilt")));
	Descriptor.Parameters.Add(FractalTerracesWarpStyleParameter());

	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::FractalTerraces, EvaluateFractalTerracesNode);
}
