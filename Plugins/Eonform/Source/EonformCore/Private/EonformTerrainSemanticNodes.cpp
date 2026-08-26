#include "EonformTerrainSemanticNodes.h"

#include "EonformTerrainDerivedData.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor SemanticTerrainPort(FName Name, const TCHAR* DisplayName)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DisplayName = DisplayName;
		Port.DataType = TEXT("Terrain");
		return Port;
	}

	FEonformTerrainPortDescriptor SemanticScalarPort(FName Name, const TCHAR* DisplayName)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DisplayName = DisplayName;
		Port.DataType = TEXT("ScalarField");
		return Port;
	}

	FEonformTerrainParameterDescriptor SemanticNumber(FName Name, const TCHAR* DisplayName, double Default, double Minimum, double Maximum, const TCHAR* Group)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Group = Group;
		Parameter.Type = EEonformTerrainParameterType::Number;
		Parameter.DefaultNumber = Default;
		Parameter.bHasMinimum = true;
		Parameter.Minimum = Minimum;
		Parameter.bHasMaximum = true;
		Parameter.Maximum = Maximum;
		return Parameter;
	}

	FEonformTerrainParameterDescriptor SemanticBoolean(FName Name, const TCHAR* DisplayName, bool Default, const TCHAR* Group)
	{
		FEonformTerrainParameterDescriptor Parameter;
		Parameter.Name = Name;
		Parameter.DisplayName = DisplayName;
		Parameter.Group = Group;
		Parameter.Type = EEonformTerrainParameterType::Boolean;
		Parameter.DefaultBoolean = Default;
		return Parameter;
	}

	FEonformScalarField MakeSemanticField(const FEonformGridDomain& Domain, FName Name)
	{
		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = EEonformFieldUnit::Normalized;
		Descriptor.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Field;
		Field.Initialize(Domain, Descriptor, 0.0f);
		return Field;
	}

	float Smooth01(float Value)
	{
		const float T = FMath::Clamp(Value, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	bool PrepareContextAndGeology(
		const FEonformTerrainValue& Input,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainDataset& OutDataset,
		FString& Error)
	{
		OutDataset = Input.TerrainDataset;
		if (!FEonformTerrainDerivedData::EnsureContext(OutDataset, Input.HeightScale, Context.PhysicalMetrics, &Error)) return false;
		if (!FEonformTerrainDerivedData::EnsureGeology(OutDataset, Input.HeightScale, FEonformTerrainDerivedDataSettings(), &Error)) return false;
		if (!FEonformTerrainDerivedData::EnsureProcessMasks(OutDataset, Input.HeightScale, FEonformTerrainDerivedDataSettings(), &Error)) return false;
		return true;
	}

	bool EvaluatePeaks(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Peaks requires a valid terrain input 'Terrain'.");
			return false;
		}

		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		if (!FEonformTerrainDerivedData::EnsureContext(Dataset, Input->HeightScale, Context.PhysicalMetrics, &Error)) return false;
		const FEonformScalarField* Elevation = Dataset.FindScalarField(EonformTerrainFieldNames::Elevation);
		const FEonformScalarField* Convexity = Dataset.FindScalarField(EonformTerrainFieldNames::Convexity);
		const FEonformScalarField* Mountain = Dataset.FindScalarField(EonformTerrainFieldNames::Mountain);
		const FEonformScalarField* Slope = Dataset.FindScalarField(EonformTerrainFieldNames::SlopeDegrees);
		if (!Elevation || !Convexity || !Mountain || !Slope)
		{
			Error = TEXT("Peaks could not resolve Elevation, Convexity, Mountain, and SlopeDegrees.");
			return false;
		}

		const float Falloff = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Falloff"), 0.42)), 0.001f, 1.0f);
		const bool bPrecise = Node.GetBool(TEXT("Precise"), false);
		const float Threshold = FMath::Lerp(0.76f, 0.40f, Falloff);

		FEonformScalarField Peaks = MakeSemanticField(Elevation->Domain, EonformTerrainFieldNames::Peaks);
		for (int32 Y = 0; Y < Elevation->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Elevation->Domain.Dimensions.X; ++X)
			{
				const float E = FMath::Clamp(Elevation->AtInterior(X, Y), 0.0f, 1.0f);
				const float C = FMath::Clamp(Convexity->AtInterior(X, Y), 0.0f, 1.0f);
				const float M = FMath::Clamp(Mountain->AtInterior(X, Y), 0.0f, 1.0f);
				const float S = FMath::Clamp(Slope->AtInterior(X, Y) / 65.0f, 0.0f, 1.0f);
				float Score = E * 0.42f + C * 0.30f + M * 0.20f + S * 0.08f;

				if (bPrecise)
				{
					float NeighborMax = 0.0f;
					float NeighborMean = 0.0f;
					int32 NeighborCount = 0;
					for (int32 OY = -1; OY <= 1; ++OY)
					{
						for (int32 OX = -1; OX <= 1; ++OX)
						{
							if (OX == 0 && OY == 0) continue;
							const int32 SX = FMath::Clamp(X + OX, 0, Elevation->Domain.Dimensions.X - 1);
							const int32 SY = FMath::Clamp(Y + OY, 0, Elevation->Domain.Dimensions.Y - 1);
							const float Neighbor = FMath::Clamp(Elevation->AtInterior(SX, SY), 0.0f, 1.0f);
							NeighborMax = FMath::Max(NeighborMax, Neighbor);
							NeighborMean += Neighbor;
							++NeighborCount;
						}
					}
					NeighborMean /= FMath::Max(NeighborCount, 1);
					const float Prominence = FMath::Clamp((E - NeighborMean) * 8.0f + 0.5f, 0.0f, 1.0f);
					const float IsLocalHigh = E + 0.002f >= NeighborMax ? 1.0f : 0.35f;
					Score *= FMath::Lerp(0.55f, 1.20f, Prominence) * IsLocalHigh;
				}

				Peaks.AtInterior(X, Y) = Smooth01((Score - Threshold + Falloff) / Falloff);
			}
		}

		FEonformScalarField Output = Peaks;
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(Peaks)))
		{
			Error = TEXT("Peaks could not publish its semantic field.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		Out.Outputs.Add(TEXT("Mask"), FEonformTerrainValue::MakeScalarField(MoveTemp(Output)));
		Error.Reset();
		return true;
	}

	bool EvaluateRockMap(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("RockMap requires a valid terrain input 'Terrain'.");
			return false;
		}

		FEonformTerrainDataset Dataset;
		if (!PrepareContextAndGeology(*Input, Context, Dataset, Error)) return false;
		const FEonformScalarField* Slope = Dataset.FindScalarField(EonformTerrainFieldNames::SlopeDegrees);
		const FEonformScalarField* Hardness = Dataset.FindScalarField(EonformTerrainFieldNames::RockHardness);
		const FEonformScalarField* Weathering = Dataset.FindScalarField(EonformTerrainFieldNames::Weathering);
		const FEonformScalarField* SoilDepth = Dataset.FindScalarField(EonformTerrainFieldNames::SoilDepth);
		const FEonformScalarField* Convexity = Dataset.FindScalarField(EonformTerrainFieldNames::Convexity);
		if (!Slope || !Hardness || !Weathering || !SoilDepth || !Convexity)
		{
			Error = TEXT("RockMap could not resolve terrain geology/context fields.");
			return false;
		}

		const float Coverage = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Coverage"), 0.55)), 0.0f, 1.0f);
		const float Density = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Density"), 0.52)), 0.0f, 1.0f);

		FEonformScalarField RockMap = MakeSemanticField(Slope->Domain, EonformTerrainFieldNames::RockMap);
		for (int32 Y = 0; Y < Slope->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Slope->Domain.Dimensions.X; ++X)
			{
				const float S = FMath::Clamp(Slope->AtInterior(X, Y) / 70.0f, 0.0f, 1.0f);
				const float H = FMath::Clamp(Hardness->AtInterior(X, Y), 0.0f, 1.0f);
				const float W = FMath::Clamp(Weathering->AtInterior(X, Y), 0.0f, 1.0f);
				const float Soil = FMath::Clamp(SoilDepth->AtInterior(X, Y), 0.0f, 1.0f);
				const float Convex = FMath::Clamp(Convexity->AtInterior(X, Y), 0.0f, 1.0f);
				const float Structural = H * 0.46f + S * FMath::Lerp(0.25f, 0.52f, Density) + Convex * 0.12f;
				const float CoverSuppression = Soil * 0.52f + W * 0.22f;
				const float Raw = FMath::Clamp((Structural - CoverSuppression + Coverage * 0.22f) * 1.25f, 0.0f, 1.0f);
				const float DensityExponent = FMath::Lerp(1.65f, 0.72f, Density);
				RockMap.AtInterior(X, Y) = FMath::Pow(Raw, DensityExponent) * FMath::Lerp(0.35f, 1.0f, Coverage);
			}
		}

		FEonformScalarField Output = RockMap;
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(RockMap)))
		{
			Error = TEXT("RockMap could not publish its semantic field.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		Out.Outputs.Add(TEXT("Mask"), FEonformTerrainValue::MakeScalarField(MoveTemp(Output)));
		Error.Reset();
		return true;
	}

	bool EvaluateSoil(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs& Inputs,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FEonformTerrainValue* const* InputPtr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = InputPtr ? *InputPtr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Soil requires a valid terrain input 'Terrain'.");
			return false;
		}

		FEonformTerrainDataset Dataset;
		if (!PrepareContextAndGeology(*Input, Context, Dataset, Error)) return false;
		if (!FEonformTerrainDerivedData::EnsureHydrology(Dataset, Input->HeightScale, Context.PhysicalMetrics, &Error)) return false;
		const FEonformScalarField* SoilDepth = Dataset.FindScalarField(EonformTerrainFieldNames::SoilDepth);
		const FEonformScalarField* Concavity = Dataset.FindScalarField(EonformTerrainFieldNames::Concavity);
		const FEonformScalarField* Deposition = Dataset.FindScalarField(EonformTerrainFieldNames::Deposition);
		const FEonformScalarField* Rainfall = Dataset.FindScalarField(EonformTerrainFieldNames::Rainfall);
		const FEonformScalarField* Evaporation = Dataset.FindScalarField(EonformTerrainFieldNames::Evaporation);
		const FEonformScalarField* Slope = Dataset.FindScalarField(EonformTerrainFieldNames::SlopeDegrees);
		const FEonformScalarField* Catchment = Dataset.FindScalarField(EonformTerrainFieldNames::CatchmentAreaKm2);
		if (!SoilDepth || !Concavity || !Deposition || !Rainfall || !Evaporation || !Slope || !Catchment)
		{
			Error = TEXT("Soil could not resolve terrain context, process, geology, and hydrology fields.");
			return false;
		}

		const float Amount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Amount"), 0.70)), 0.0f, 1.0f);
		const float Bias = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Bias"), 0.58)), 0.0f, 1.0f);

		double MaxCatchment = UE_DOUBLE_SMALL_NUMBER;
		for (int32 Y = 0; Y < Catchment->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Catchment->Domain.Dimensions.X; ++X)
			{
				MaxCatchment = FMath::Max(MaxCatchment, static_cast<double>(Catchment->AtInterior(X, Y)));
			}
		}
		const double LogMaxCatchment = FMath::Loge(1.0 + MaxCatchment);

		const FEonformScalarField* ExistingRock = Dataset.FindScalarField(EonformTerrainFieldNames::RockMap);
		FEonformScalarField Soil = MakeSemanticField(SoilDepth->Domain, EonformTerrainFieldNames::Soil);
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
				const float Accumulation = Base * 0.36f + Valley * FMath::Lerp(0.12f, 0.30f, Bias) + Deposited * 0.18f + Moisture * 0.10f + Catchment01 * 0.08f;
				const float Distribution = FMath::Pow(FMath::Clamp(Accumulation, 0.0f, 1.0f), FMath::Lerp(1.5f, 0.72f, Bias));
				Soil.AtInterior(X, Y) = FMath::Clamp(Distribution * SlopeStable * Amount * (1.0f - Rock * 0.72f), 0.0f, 1.0f);
			}
		}

		FEonformScalarField Output = Soil;
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(Soil)))
		{
			Error = TEXT("Soil could not publish its semantic field.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		Out.Outputs.Add(TEXT("Mask"), FEonformTerrainValue::MakeScalarField(MoveTemp(Output)));
		Error.Reset();
		return true;
	}
}

void RegisterEonformTerrainSemanticNodes()
{
	{
		FEonformTerrainNodeDescriptor Descriptor;
		Descriptor.Type = EonformTerrainNodeTypes::Peaks;
		Descriptor.DisplayName = TEXT("Peaks");
		Descriptor.Category = TEXT("Derive");
		Descriptor.Description = TEXT("Derives summit and high-ridge prominence from elevation, convexity, mountain context, and slope.");
		Descriptor.Inputs.Add(SemanticTerrainPort(TEXT("Terrain"), TEXT("Input")));
		Descriptor.Outputs.Add(SemanticTerrainPort(TEXT("Out"), TEXT("Out")));
		Descriptor.Outputs.Add(SemanticScalarPort(TEXT("Mask"), TEXT("Peaks")));
		Descriptor.Parameters.Add(SemanticNumber(TEXT("Falloff"), TEXT("Falloff"), 0.42, 0.001, 1.0, TEXT("Selection")));
		Descriptor.Parameters.Add(SemanticBoolean(TEXT("Precise"), TEXT("Precise"), false, TEXT("Selection")));
		FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
		FEonformTerrainNodeRegistry::Register(Descriptor.Type, EvaluatePeaks);
	}

	{
		FEonformTerrainNodeDescriptor Descriptor;
		Descriptor.Type = EonformTerrainNodeTypes::RockMap;
		Descriptor.DisplayName = TEXT("RockMap");
		Descriptor.Category = TEXT("Derive");
		Descriptor.Description = TEXT("Derives exposed-rock suitability from slope, lithologic hardness, weathering, convexity, and soil cover.");
		Descriptor.Inputs.Add(SemanticTerrainPort(TEXT("Terrain"), TEXT("Input")));
		Descriptor.Outputs.Add(SemanticTerrainPort(TEXT("Out"), TEXT("Out")));
		Descriptor.Outputs.Add(SemanticScalarPort(TEXT("Mask"), TEXT("Rock Map")));
		Descriptor.Parameters.Add(SemanticNumber(TEXT("Coverage"), TEXT("Coverage"), 0.55, 0.0, 1.0, TEXT("Rock")));
		Descriptor.Parameters.Add(SemanticNumber(TEXT("Density"), TEXT("Density"), 0.52, 0.0, 1.0, TEXT("Rock")));
		FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
		FEonformTerrainNodeRegistry::Register(Descriptor.Type, EvaluateRockMap);
	}

	{
		FEonformTerrainNodeDescriptor Descriptor;
		Descriptor.Type = EonformTerrainNodeTypes::Soil;
		Descriptor.DisplayName = TEXT("Soil");
		Descriptor.Category = TEXT("Derive");
		Descriptor.Description = TEXT("Derives stable soil-cover suitability from geology, deposition, terrain shelter, hydrologic accumulation, moisture, and slope.");
		Descriptor.Inputs.Add(SemanticTerrainPort(TEXT("Terrain"), TEXT("Input")));
		Descriptor.Outputs.Add(SemanticTerrainPort(TEXT("Out"), TEXT("Out")));
		Descriptor.Outputs.Add(SemanticScalarPort(TEXT("Mask"), TEXT("Soil")));
		Descriptor.Parameters.Add(SemanticNumber(TEXT("Amount"), TEXT("Amount"), 0.70, 0.0, 1.0, TEXT("Soil")));
		Descriptor.Parameters.Add(SemanticNumber(TEXT("Bias"), TEXT("Bias"), 0.58, 0.0, 1.0, TEXT("Soil")));
		FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
		FEonformTerrainNodeRegistry::Register(Descriptor.Type, EvaluateSoil);
	}
}