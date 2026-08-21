#include "GaeaAutoLevelNode.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor AutoLevelAnyPort(FName Name, const TCHAR* DisplayName = nullptr)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DataType = TEXT("Any");
		if (DisplayName) Port.DisplayName = DisplayName;
		return Port;
	}

	FGaeaTerrainParameterDescriptor AutoLevelNumberParameter(
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

	FGaeaTerrainParameterDescriptor AutoLevelBooleanParameter(
		FName Name,
		const TCHAR* DisplayName,
		bool DefaultValue)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Type = EGaeaTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = DefaultValue;
		return Parameter;
	}

	void AutoLevelFindRange(const FGaeaScalarField& Field, float& OutMin, float& OutMax)
	{
		OutMin = TNumericLimits<float>::Max();
		OutMax = TNumericLimits<float>::Lowest();
		for (int32 Y = 0; Y < Field.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Field.Domain.Dimensions.X; ++X)
			{
				const float Value = Field.AtInterior(X, Y);
				OutMin = FMath::Min(OutMin, Value);
				OutMax = FMath::Max(OutMax, Value);
			}
		}
	}

	float AutoLevelNormalize(float Value, float Minimum, float Maximum)
	{
		return FMath::Clamp((Value - Minimum) / FMath::Max(Maximum - Minimum, UE_SMALL_NUMBER), 0.0f, 1.0f);
	}

	float AutoLevelApplyLog(float Value, float Strength, bool bInverse)
	{
		const float Curve = FMath::Max(Strength, 0.001f);
		const float LogValue = FMath::Loge(1.0f + Value * Curve) / FMath::Loge(1.0f + Curve);
		return bInverse ? 1.0f - (FMath::Loge(1.0f + (1.0f - Value) * Curve) / FMath::Loge(1.0f + Curve)) : LogValue;
	}

	void AutoLevelBuildEqualizationLut(const FGaeaScalarField& Field, float Minimum, float Maximum, TArray<float>& OutLut)
	{
		constexpr int32 BinCount = 256;
		TArray<int32> Histogram;
		Histogram.Init(0, BinCount);
		int32 SampleCount = 0;

		for (int32 Y = 0; Y < Field.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Field.Domain.Dimensions.X; ++X)
			{
				const float N = AutoLevelNormalize(Field.AtInterior(X, Y), Minimum, Maximum);
				const int32 Bin = FMath::Clamp(FMath::FloorToInt(N * static_cast<float>(BinCount - 1)), 0, BinCount - 1);
				++Histogram[Bin];
				++SampleCount;
			}
		}

		OutLut.SetNumUninitialized(BinCount);
		int32 Cumulative = 0;
		for (int32 Bin = 0; Bin < BinCount; ++Bin)
		{
			Cumulative += Histogram[Bin];
			OutLut[Bin] = SampleCount > 0 ? static_cast<float>(Cumulative) / static_cast<float>(SampleCount) : 0.0f;
		}
	}

	float AutoLevelLookupEqualized(float Value, const TArray<float>& Lut)
	{
		if (Lut.IsEmpty()) return Value;
		const float Position = FMath::Clamp(Value, 0.0f, 1.0f) * static_cast<float>(Lut.Num() - 1);
		const int32 Lower = FMath::Clamp(FMath::FloorToInt(Position), 0, Lut.Num() - 1);
		const int32 Upper = FMath::Min(Lower + 1, Lut.Num() - 1);
		return FMath::Lerp(Lut[Lower], Lut[Upper], Position - static_cast<float>(Lower));
	}

	bool AutoLevelProcessField(
		const FGaeaTerrainNode& Node,
		const FGaeaScalarField& Source,
		bool bTerrain,
		FGaeaScalarField& OutField,
		FString& Error)
	{
		if (!Source.IsValid())
		{
			Error = TEXT("AutoLevel received an invalid scalar field.");
			return false;
		}

		float SourceMin = 0.0f;
		float SourceMax = 1.0f;
		AutoLevelFindRange(Source, SourceMin, SourceMax);
		const float SourceRange = SourceMax - SourceMin;
		if (SourceRange <= UE_SMALL_NUMBER)
		{
			OutField = Source;
			return true;
		}

		const bool bApplyAutoLevel = Node.GetBool(TEXT("ApplyAutoLevel"), true);
		const float AutoLevelStrength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("AutoLevelStrength"), 1.0)), 0.0f, 1.0f);
		const bool bApplyRaise = Node.GetBool(TEXT("ApplyRaise"), false);
		const float RaiseMultiplier = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RaiseMultiplier"), 1.0)), 0.0f, 8.0f);
		const bool bApplyLog = Node.GetBool(TEXT("ApplyLog"), false);
		const bool bInverseLog = Node.GetBool(TEXT("InverseLog"), false);
		const float LogStrength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Log"), 1.0)), 0.001f, 16.0f);
		const bool bApplyEqualize = Node.GetBool(TEXT("ApplyEqualize"), false);
		const float EqualizeStrength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Equalize"), 1.0)), 0.0f, 1.0f);
		const bool bApplyGamma = Node.GetBool(TEXT("ApplyGamma"), false);
		const bool bAutoGamma = Node.GetBool(TEXT("AutoGamma"), true);
		const float ManualGamma = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Gamma"), 1.0)), 0.05f, 8.0f);

		TArray<float> EqualizationLut;
		if (bApplyEqualize)
		{
			AutoLevelBuildEqualizationLut(Source, SourceMin, SourceMax, EqualizationLut);
		}

		float MeanNormalized = 0.0f;
		int32 SampleCount = 0;
		if (bApplyGamma && bAutoGamma)
		{
			for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
			{
				for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
				{
					MeanNormalized += AutoLevelNormalize(Source.AtInterior(X, Y), SourceMin, SourceMax);
					++SampleCount;
				}
			}
			if (SampleCount > 0) MeanNormalized /= static_cast<float>(SampleCount);
		}
		const float AutoGamma = FMath::Clamp(
			MeanNormalized > UE_SMALL_NUMBER && MeanNormalized < 1.0f - UE_SMALL_NUMBER
				? FMath::Loge(0.5f) / FMath::Loge(MeanNormalized)
				: 1.0f,
			0.05f,
			8.0f);

		OutField = Source;
		for (int32 Y = 0; Y < Source.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source.Domain.Dimensions.X; ++X)
			{
				const float Raw = Source.AtInterior(X, Y);
				const float OriginalNormalized = AutoLevelNormalize(Raw, SourceMin, SourceMax);
				float Value = bApplyAutoLevel
					? FMath::Lerp(bTerrain ? FMath::Clamp(Raw * 0.5f + 0.5f, 0.0f, 1.0f) : FMath::Clamp(Raw, 0.0f, 1.0f), OriginalNormalized, AutoLevelStrength)
					: (bTerrain ? FMath::Clamp(Raw * 0.5f + 0.5f, 0.0f, 1.0f) : FMath::Clamp(Raw, 0.0f, 1.0f));

				if (bApplyRaise) Value = FMath::Clamp(Value * RaiseMultiplier, 0.0f, 1.0f);
				if (bApplyLog) Value = FMath::Clamp(AutoLevelApplyLog(Value, LogStrength, bInverseLog), 0.0f, 1.0f);
				if (bApplyEqualize)
				{
					Value = FMath::Lerp(Value, AutoLevelLookupEqualized(OriginalNormalized, EqualizationLut), EqualizeStrength);
				}
				if (bApplyGamma)
				{
					const float Gamma = bAutoGamma ? AutoGamma : ManualGamma;
					Value = FMath::Pow(FMath::Clamp(Value, 0.0f, 1.0f), Gamma);
				}

				OutField.AtInterior(X, Y) = bTerrain ? Value * 2.0f - 1.0f : Value;
			}
		}
		return OutField.IsValid();
	}

	bool EvaluateAutoLevelNode(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* InputPtr = Inputs.Find(TEXT("Input"));
		const FGaeaTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || !Input->IsValid())
		{
			Error = TEXT("AutoLevel requires a valid Input.");
			return false;
		}

		if (Input->Type == EGaeaTerrainValueType::ScalarField)
		{
			FGaeaScalarField Result;
			if (!AutoLevelProcessField(Node, Input->ScalarField, false, Result, Error)) return false;
			Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Result)));
			return true;
		}

		if (Input->Type == EGaeaTerrainValueType::Terrain)
		{
			const FGaeaScalarField* Height = Input->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
			if (!Height || !Height->IsValid())
			{
				Error = TEXT("AutoLevel terrain input has no valid Height field.");
				return false;
			}

			FGaeaScalarField ResultHeight;
			if (!AutoLevelProcessField(Node, *Height, true, ResultHeight, Error)) return false;
			ResultHeight.Descriptor.Name = GaeaTerrainFieldNames::Height;

			FGaeaTerrainDataset Dataset = Input->TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(ResultHeight)))
			{
				Error = TEXT("AutoLevel could not publish its Height field.");
				return false;
			}

			FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale);
			if (!Result.IsValid())
			{
				Error = TEXT("AutoLevel produced an invalid terrain value.");
				return false;
			}
			Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
			return true;
		}

		Error = TEXT("AutoLevel received an unsupported input type.");
		return false;
	}
}

void RegisterGaeaAutoLevelNode()
{
	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::AutoLevel;
	Descriptor.DisplayName = TEXT("AutoLevel");
	Descriptor.Category = TEXT("Adjustments");
	Descriptor.Description = TEXT("Levels terrain or masks using AutoLevel, Raise, Logarithmic, Equalize, and Gamma stages.");
	Descriptor.Inputs.Add(AutoLevelAnyPort(TEXT("Input"), TEXT("Input")));
	Descriptor.Outputs.Add(AutoLevelAnyPort(TEXT("Out"), TEXT("Out")));

	Descriptor.Parameters.Add(AutoLevelBooleanParameter(TEXT("ApplyAutoLevel"), TEXT("Apply Autolevel"), true));
	Descriptor.Parameters.Add(AutoLevelNumberParameter(TEXT("AutoLevelStrength"), TEXT("Strength"), 1.0, 0.0, 1.0));
	Descriptor.Parameters.Add(AutoLevelBooleanParameter(TEXT("ApplyRaise"), TEXT("Apply Raise"), false));
	Descriptor.Parameters.Add(AutoLevelNumberParameter(TEXT("RaiseMultiplier"), TEXT("Multiplier"), 1.0, 0.0, 8.0));
	Descriptor.Parameters.Add(AutoLevelBooleanParameter(TEXT("ApplyLog"), TEXT("Apply Log"), false));
	Descriptor.Parameters.Add(AutoLevelBooleanParameter(TEXT("InverseLog"), TEXT("Inverse"), false));
	Descriptor.Parameters.Add(AutoLevelNumberParameter(TEXT("Log"), TEXT("Log"), 1.0, 0.001, 16.0));
	Descriptor.Parameters.Add(AutoLevelBooleanParameter(TEXT("ApplyEqualize"), TEXT("Apply Equalize"), false));
	Descriptor.Parameters.Add(AutoLevelNumberParameter(TEXT("Equalize"), TEXT("Equalize"), 1.0, 0.0, 1.0));
	Descriptor.Parameters.Add(AutoLevelBooleanParameter(TEXT("ApplyGamma"), TEXT("Apply Gamma"), false));
	Descriptor.Parameters.Add(AutoLevelBooleanParameter(TEXT("AutoGamma"), TEXT("Auto-Gamma"), true));
	Descriptor.Parameters.Add(AutoLevelNumberParameter(TEXT("Gamma"), TEXT("Gamma"), 1.0, 0.05, 8.0));

	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::AutoLevel, EvaluateAutoLevelNode);
}
