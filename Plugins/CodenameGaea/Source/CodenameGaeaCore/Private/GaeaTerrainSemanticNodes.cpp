#include "GaeaTerrainSemanticNodes.h"

#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor SemanticTerrainPort(FName Name, const TCHAR* DisplayName)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DisplayName = DisplayName;
		Port.DataType = TEXT("Terrain");
		return Port;
	}

	FGaeaTerrainPortDescriptor SemanticScalarPort(FName Name, const TCHAR* DisplayName)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DisplayName = DisplayName;
		Port.DataType = TEXT("ScalarField");
		return Port;
	}

	FGaeaTerrainParameterDescriptor SemanticNumber(FName Name, const TCHAR* DisplayName, double Default, double Minimum, double Maximum, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Group = Group;
		Parameter.Type = EGaeaTerrainParameterType::Number;
		Parameter.DefaultNumber = Default;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		return Parameter;
	}

	FGaeaScalarField MakeSemanticField(const FGaeaGridDomain& Domain, FName Name)
	{
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Field;
		Field.Initialize(Domain, Descriptor, 0.0f);
		return Field;
	}

	float Smooth01(float Value)
	{
		const float T = FMath::Clamp(Value, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	bool PrepareContextAndGeology(
		const FGaeaTerrainValue& Input,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainDataset& OutDataset,
		FString& Error)
	{
		OutDataset = Input.TerrainDataset;
		if (!FGaeaTerrainDerivedData::EnsureContext(OutDataset, Input.HeightScale, Context.PhysicalMetrics, &Error)) return false;
		if (!FGaeaTerrainDerivedData::EnsureGeology(OutDataset, Input.HeightScale, FGaeaTerrainDerivedDataSettings(), &Error)) return false;
		if (!FGaeaTerrainDerivedData::EnsureProcessMasks(OutDataset, Input.HeightScale, FGaeaTerrainDerivedDataSettings(), &Error)) return false;
		return true;
	}

	bool EvaluatePeaks(
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
			Error = TEXT("Peaks requires a valid terrain input 'Terrain'.");
			return false;
		}

		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		if (!FGaeaTerrainDerivedData::EnsureContext(Dataset, Input->HeightScale, Context.PhysicalMetrics, &Error)) return false;
		const FGaeaScalarField* Elevation = Dataset.FindScalarField(GaeaTerrainFieldNames::Elevation);
		const FGaeaScalarField* Convexity = Dataset.FindScalarField(GaeaTerrainFieldNames::Convexity);
		const FGaeaScalarField* Mountain = Dataset.FindScalarField(GaeaTerrainFieldNames::Mountain);
		const FGaeaScalarField* Slope = Dataset.FindScalarField(GaeaTerrainFieldNames::SlopeDegrees);
		if (!Elevation || !Convexity || !Mountain || !Slope)
		{
			Error = TEXT("Peaks could not resolve Elevation, Convexity, Mountain, and SlopeDegrees.");
			return false;
		}

		const float Threshold = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Threshold"), 0.58)), 0.0f, 1.0f);
		const float Falloff = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Falloff"), 0.18)), 0.001f, 1.0f);

		FGaeaScalarField Peaks = MakeSemanticField(Elevation->Domain, GaeaTerrainFieldNames::Peaks);
		for (int32 Y = 0; Y < Elevation->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Elevation->Domain.Dimensions.X; ++X)
			{
				const float E = FMath::Clamp(Elevation->AtInterior(X, Y), 0.0f, 1.0f);
				const float C = FMath::Clamp(Convexity->AtInterior(X, Y), 0.0f, 1.0f);
				const float M = FMath::Clamp(Mountain->AtInterior(X, Y), 0.0f, 1.0f);
				const float S = FMath::Clamp(Slope->AtInterior(X, Y) / 65.0f, 0.0f, 1.0f);
				const float Score = E * 0.42f + C * 0.30f + M * 0.20f + S * 0.08f;
				Peaks.AtInterior(X, Y) = Smooth01((Score - Threshold + Falloff) / Falloff);
			}
		}

		FGaeaScalarField Output = Peaks;
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(Peaks)))
		{
			Error = TEXT("Peaks could not publish its semantic field.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		Out.Outputs.Add(TEXT("Mask"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Output)));
		Error.Reset();
		return true;
	}

	bool EvaluateRockMap(
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
			Error = TEXT("RockMap requires a valid terrain input 'Terrain'.");
			return false;
		}

		FGaeaTerrainDataset Dataset;
		if (!PrepareContextAndGeology(*Input, Context, Dataset, Error)) return false;
		const FGaeaScalarField* Slope = Dataset.FindScalarField(GaeaTerrainFieldNames::SlopeDegrees);
		const FGaeaScalarField* Hardness = Dataset.FindScalarField(GaeaTerrainFieldNames::RockHardness);
		const FGaeaScalarField* Weathering = Dataset.FindScalarField(GaeaTerrainFieldNames::Weathering);
		const FGaeaScalarField* SoilDepth = Dataset.FindScalarField(GaeaTerrainFieldNames::SoilDepth);
		const FGaeaScalarField* Convexity = Dataset.FindScalarField(GaeaTerrainFieldNames::Convexity);
		if (!Slope || !Hardness || !Weathering || !SoilDepth || !Convexity)
		{
			Error = TEXT("RockMap could not resolve terrain geology/context fields.");
			return false;
		}

		const float Exposure = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Exposure"), 0.55)), 0.0f, 1.0f);
		const float Steepness = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Steepness"), 0.52)), 0.0f, 1.0f);

		FGaeaScalarField RockMap = MakeSemanticField(Slope->Domain, GaeaTerrainFieldNames::RockMap);
		for (int32 Y = 0; Y < Slope->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Slope->Domain.Dimensions.X; ++X)
			{
				const float S = FMath::Clamp(Slope->AtInterior(X, Y) / 70.0f, 0.0f, 1.0f);
				const float H = FMath::Clamp(Hardness->AtInterior(X, Y), 0.0f, 1.0f);
				const float W = FMath::Clamp(Weathering->AtInterior(X, Y), 0.0f, 1.0f);
				const float Soil = FMath::Clamp(SoilDepth->AtInterior(X, Y), 0.0f, 1.0f);
				const float Convex = FMath::Clamp(Convexity->AtInterior(X, Y), 0.0f, 1.0f);
				const float Structural = H * 0.46f + S * FMath::Lerp(0.25f, 0.52f, Steepness) + Convex * 0.12f;
				const float CoverSuppression = Soil * 0.52f + W * 0.22f;
				RockMap.AtInterior(X, Y) = FMath::Clamp((Structural - CoverSuppression + Exposure * 0.22f) * 1.25f, 0.0f, 1.0f);
			}
		}

		FGaeaScalarField Output = RockMap;
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(RockMap)))
		{
			Error = TEXT("RockMap could not publish its semantic field.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		Out.Outputs.Add(TEXT("Mask"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Output)));
		Error.Reset();
		return true;
	}

	bool EvaluateSoil(
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
			Error = TEXT("Soil requires a valid terrain input 'Terrain'.");
			return false;
		}

		FGaeaTerrainDataset Dataset;
		if (!PrepareContextAndGeology(*Input, Context, Dataset, Error)) return false;
		if (!FGaeaTerrainDerivedData::EnsureHydrology(Dataset, Input->HeightScale, Context.PhysicalMetrics, &Error)) return false;
		const FGaeaScalarField* SoilDepth = Dataset.FindScalarField(GaeaTerrainFieldNames::SoilDepth);
		const FGaeaScalarField* Concavity = Dataset.FindScalarField(GaeaTerrainFieldNames::Concavity);
		const FGaeaScalarField* Deposition = Dataset.FindScalarField(GaeaTerrainFieldNames::Deposition);
		const FGaeaScalarField* Rainfall = Dataset.FindScalarField(GaeaTerrainFieldNames::Rainfall);
		const FGaeaScalarField* Evaporation = Dataset.FindScalarField(GaeaTerrainFieldNames::Evaporation);
		const FGaeaScalarField* Slope = Dataset.FindScalarField(GaeaTerrainFieldNames::SlopeDegrees);
		const FGaeaScalarField* Catchment = Dataset.FindScalarField(GaeaTerrainFieldNames::CatchmentAreaKm2);
		if (!SoilDepth || !Concavity || !Deposition || !Rainfall || !Evaporation || !Slope || !Catchment)
		{
			Error = TEXT("Soil could not resolve terrain context, process, geology, and hydrology fields.");
			return false;
		}

		const float Coverage = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Coverage"), 0.70)), 0.0f, 1.0f);
		const float ValleyBias = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ValleyBias"), 0.58)), 0.0f, 1.0f);

		double MaxCatchment = UE_DOUBLE_SMALL_NUMBER;
		for (int32 Y = 0; Y < Catchment->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Catchment->Domain.Dimensions.X; ++X)
			{
				MaxCatchment = FMath::Max(MaxCatchment, static_cast<double>(Catchment->AtInterior(X, Y)));
			}
		}
		const double LogMaxCatchment = FMath::Loge(1.0 + MaxCatchment);

		const FGaeaScalarField* ExistingRock = Dataset.FindScalarField(GaeaTerrainFieldNames::RockMap);
		FGaeaScalarField Soil = MakeSemanticField(SoilDepth->Domain, GaeaTerrainFieldNames::Soil);
		for (int32 Y = 0; Y < SoilDepth->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < SoilDepth->Domain.Dimensions.X; ++X)
			{
				const float Base = FMath::Clamp(SoilDepth->AtInterior(X, Y), 0.0f, 1.0f);
				const float Valley = FMath::Clamp(Concavity->AtInterior(X, Y), 0.0f, 1.0f);
				const float Deposited = FMath::Clamp(Deposition->AtInterior(X, Y), 0.0f, 1.0f);
				const float Moisture = FMath::Clamp(Rainfall->AtInterior(X, Y) - Evaporation->AtInterior(X, Y) * 0.45f, 0.0f, 1.0f);
				const float SlopeStable = 1.0f - FMath::Clamp(Slope->AtInterior(X, Y) / 55.0f, 0.0f, 1.0f);
				const float Catchment01 = static_cast<float>(FMath::Clamp(FMath::Loge(1.0 + FMath::Max(static_cast<double>(Catchment->AtInterior(X, Y)), 0.0)) / LogMaxCatchment, 0.0, 1.0));
				const float Rock = ExistingRock ? FMath::Clamp(ExistingRock->AtInterior(X, Y), 0.0f, 1.0f) : 0.0f;
				const float Accumulation = Base * 0.36f + Valley * FMath::Lerp(0.12f, 0.30f, ValleyBias) + Deposited * 0.18f + Moisture * 0.10f + Catchment01 * 0.08f;
				Soil.AtInterior(X, Y) = FMath::Clamp(Accumulation * SlopeStable * Coverage * (1.0f - Rock * 0.72f), 0.0f, 1.0f);
			}
		}

		FGaeaScalarField Output = Soil;
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(Soil)))
		{
			Error = TEXT("Soil could not publish its semantic field.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		Out.Outputs.Add(TEXT("Mask"), FGaeaTerrainValue::MakeScalarField(MoveTemp(Output)));
		Error.Reset();
		return true;
	}
}

void RegisterGaeaTerrainSemanticNodes()
{
	{
		FGaeaTerrainNodeDescriptor Descriptor;
		Descriptor.Type = GaeaTerrainNodeTypes::Peaks;
		Descriptor.DisplayName = TEXT("Peaks");
		Descriptor.Category = TEXT("Derive");
		Descriptor.Description = TEXT("Derives summit and high-ridge prominence from elevation, convexity, mountain context, and slope.");
		Descriptor.Inputs.Add(SemanticTerrainPort(TEXT("Terrain"), TEXT("Input")));
		Descriptor.Outputs.Add(SemanticTerrainPort(TEXT("Out"), TEXT("Out")));
		Descriptor.Outputs.Add(SemanticScalarPort(TEXT("Mask"), TEXT("Peaks")));
		Descriptor.Parameters.Add(SemanticNumber(TEXT("Threshold"), TEXT("Threshold"), 0.58, 0.0, 1.0, TEXT("Selection")));
		Descriptor.Parameters.Add(SemanticNumber(TEXT("Falloff"), TEXT("Falloff"), 0.18, 0.001, 1.0, TEXT("Selection")));
		FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
		FGaeaTerrainNodeRegistry::Register(Descriptor.Type, EvaluatePeaks);
	}

	{
		FGaeaTerrainNodeDescriptor Descriptor;
		Descriptor.Type = GaeaTerrainNodeTypes::RockMap;
		Descriptor.DisplayName = TEXT("RockMap");
		Descriptor.Category = TEXT("Derive");
		Descriptor.Description = TEXT("Derives exposed-rock suitability from slope, lithologic hardness, weathering, convexity, and soil cover.");
		Descriptor.Inputs.Add(SemanticTerrainPort(TEXT("Terrain"), TEXT("Input")));
		Descriptor.Outputs.Add(SemanticTerrainPort(TEXT("Out"), TEXT("Out")));
		Descriptor.Outputs.Add(SemanticScalarPort(TEXT("Mask"), TEXT("Rock Map")));
		Descriptor.Parameters.Add(SemanticNumber(TEXT("Exposure"), TEXT("Exposure"), 0.55, 0.0, 1.0, TEXT("Rock")));
		Descriptor.Parameters.Add(SemanticNumber(TEXT("Steepness"), TEXT("Steepness"), 0.52, 0.0, 1.0, TEXT("Rock")));
		FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
		FGaeaTerrainNodeRegistry::Register(Descriptor.Type, EvaluateRockMap);
	}

	{
		FGaeaTerrainNodeDescriptor Descriptor;
		Descriptor.Type = GaeaTerrainNodeTypes::Soil;
		Descriptor.DisplayName = TEXT("Soil");
		Descriptor.Category = TEXT("Derive");
		Descriptor.Description = TEXT("Derives stable soil-cover suitability from geology, deposition, terrain shelter, hydrologic accumulation, moisture, and slope.");
		Descriptor.Inputs.Add(SemanticTerrainPort(TEXT("Terrain"), TEXT("Input")));
		Descriptor.Outputs.Add(SemanticTerrainPort(TEXT("Out"), TEXT("Out")));
		Descriptor.Outputs.Add(SemanticScalarPort(TEXT("Mask"), TEXT("Soil")));
		Descriptor.Parameters.Add(SemanticNumber(TEXT("Coverage"), TEXT("Coverage"), 0.70, 0.0, 1.0, TEXT("Soil")));
		Descriptor.Parameters.Add(SemanticNumber(TEXT("ValleyBias"), TEXT("Valley Bias"), 0.58, 0.0, 1.0, TEXT("Soil")));
		FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
		FGaeaTerrainNodeRegistry::Register(Descriptor.Type, EvaluateSoil);
	}
}
