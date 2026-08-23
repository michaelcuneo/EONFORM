#include "GaeaEcologyNodes.h"

#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace GaeaEcology
{
	FGaeaTerrainPortDescriptor TerrainPort(FName Name, const TCHAR* DisplayName)
	{
		FGaeaTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = DisplayName;
		P.DataType = TEXT("Terrain");
		return P;
	}

	FGaeaTerrainPortDescriptor ScalarPort(FName Name, const TCHAR* DisplayName)
	{
		FGaeaTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = DisplayName;
		P.DataType = TEXT("ScalarField");
		return P;
	}

	FGaeaTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Number;
		P.DefaultNumber = Default;
		P.bHasMinimum = true;
		P.Minimum = Min;
		P.bHasMaximum = true;
		P.Maximum = Max;
		P.Group = Group;
		return P;
	}

	FGaeaTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Integer;
		P.DefaultInteger = Default;
		P.bHasMinimum = true;
		P.Minimum = static_cast<double>(Min);
		P.bHasMaximum = true;
		P.Maximum = static_cast<double>(Max);
		P.Group = Group;
		return P;
	}

	uint32 Hash(uint32 X)
	{
		X ^= X >> 16;
		X *= 0x7feb352dU;
		X ^= X >> 15;
		X *= 0x846ca68bU;
		X ^= X >> 16;
		return X;
	}

	float Hash01(int32 X, int32 Y, int32 Seed)
	{
		return static_cast<float>(Hash(
			static_cast<uint32>(X) * 0x9e3779b9U
			^ static_cast<uint32>(Y) * 0x85ebca6bU
			^ static_cast<uint32>(Seed)) & 0x00ffffffU) / 16777215.0f;
	}

	float SmoothStep01(float V)
	{
		const float T = FMath::Clamp(V, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	FGaeaScalarField MakeScalar(const FGaeaGridDomain& Domain, FName Name)
	{
		FGaeaFieldDescriptor D;
		D.Name = Name;
		D.Unit = EGaeaFieldUnit::Normalized;
		D.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField F;
		F.Initialize(Domain, D, 0.0f);
		return F;
	}

	const FGaeaScalarField* FindOptional(const FGaeaTerrainDataset& Dataset, FName Name, const FGaeaGridDomain& Domain)
	{
		const FGaeaScalarField* F = Dataset.FindScalarField(Name);
		return F && F->IsValid() && F->Domain == Domain ? F : nullptr;
	}

	bool PrepareEcology(
		const FGaeaTerrainValue& Input,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainDataset& Dataset,
		FString& Error)
	{
		Dataset = Input.TerrainDataset;
		if (!FGaeaTerrainDerivedData::EnsureContext(Dataset, Input.HeightScale, Context.PhysicalMetrics, &Error)) return false;
		if (!FGaeaTerrainDerivedData::EnsureGeology(Dataset, Input.HeightScale, FGaeaTerrainDerivedDataSettings(), &Error)) return false;
		if (!FGaeaTerrainDerivedData::EnsureProcessMasks(Dataset, Input.HeightScale, FGaeaTerrainDerivedDataSettings(), &Error)) return false;
		if (!FGaeaTerrainDerivedData::EnsureFlowAnalysis(Dataset, Input.HeightScale, Context.PhysicalMetrics, &Error)) return false;
		return true;
	}

	bool EvaluateVegetation(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error,
		bool bShrubs)
	{
		const FGaeaTerrainValue* const* Ptr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = Ptr ? *Ptr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = bShrubs ? TEXT("Shrubs requires a valid terrain input 'Terrain'.") : TEXT("Trees requires a valid terrain input 'Terrain'.");
			return false;
		}

		FGaeaTerrainDataset Dataset;
		if (!PrepareEcology(*Input, Context, Dataset, Error)) return false;

		const FGaeaScalarField* Height = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		const FGaeaScalarField* Elevation = Dataset.FindScalarField(GaeaTerrainFieldNames::Elevation);
		const FGaeaScalarField* Slope = Dataset.FindScalarField(GaeaTerrainFieldNames::SlopeDegrees);
		const FGaeaScalarField* Concavity = Dataset.FindScalarField(GaeaTerrainFieldNames::Concavity);
		const FGaeaScalarField* Rainfall = Dataset.FindScalarField(GaeaTerrainFieldNames::Rainfall);
		const FGaeaScalarField* Evaporation = Dataset.FindScalarField(GaeaTerrainFieldNames::Evaporation);
		const FGaeaScalarField* SoilDepth = Dataset.FindScalarField(GaeaTerrainFieldNames::SoilDepth);
		const FGaeaScalarField* Deposition = Dataset.FindScalarField(GaeaTerrainFieldNames::Deposition);
		const FGaeaScalarField* Catchment = Dataset.FindScalarField(GaeaTerrainFieldNames::CatchmentAreaKm2);
		if (!Height || !Elevation || !Slope || !Concavity || !Rainfall || !Evaporation || !SoilDepth || !Deposition || !Catchment)
		{
			Error = TEXT("Ecology could not resolve required terrain, climate, geology, and flow-analysis fields.");
			return false;
		}

		const FGaeaGridDomain& Domain = Height->Domain;
		const FGaeaScalarField* Sea = FindOptional(Dataset, TEXT("Sea"), Domain);
		const FGaeaScalarField* Lake = FindOptional(Dataset, TEXT("Lake"), Domain);
		const FGaeaScalarField* Snow = FindOptional(Dataset, TEXT("Snow"), Domain);
		const FGaeaScalarField* Glacier = FindOptional(Dataset, TEXT("Glacier"), Domain);
		const FGaeaScalarField* Temperature = FindOptional(Dataset, TEXT("TemperatureC"), Domain);

		const float Density = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Density"), bShrubs ? 0.72 : 0.62)), 0.0f, 1.0f);
		const float MoisturePreference = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("MoisturePreference"), bShrubs ? 0.48 : 0.68)), 0.0f, 1.0f);
		const float SoilPreference = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("SoilPreference"), bShrubs ? 0.35 : 0.65)), 0.0f, 1.0f);
		const float MaxSlope = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("MaxSlopeDegrees"), bShrubs ? 46.0 : 32.0)), 1.0f, 89.0f);
		const float ElevationBias = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ElevationBias"), bShrubs ? 0.15 : -0.05)), -1.0f, 1.0f);
		const float ShelterStrength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("ShelterStrength"), bShrubs ? 0.25 : 0.5)), 0.0f, 1.0f);
		const float RiparianStrength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("RiparianStrength"), bShrubs ? 0.4 : 0.65)), 0.0f, 1.0f);
		const float SnowTolerance = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("SnowTolerance"), bShrubs ? 0.6 : 0.25)), 0.0f, 1.0f);
		const int32 Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), bShrubs ? 4242 : 31337));

		float MaxCatchment = 0.0f;
		for (const float V : Catchment->Values) MaxCatchment = FMath::Max(MaxCatchment, V);
		const float CatchmentDenom = FMath::Max(FMath::Loge(1.0f + MaxCatchment), UE_SMALL_NUMBER);

		FGaeaScalarField Suitability = MakeScalar(Domain, bShrubs ? TEXT("ShrubSuitability") : TEXT("TreeSuitability"));
		FGaeaScalarField DensityField = MakeScalar(Domain, bShrubs ? TEXT("Shrubs") : TEXT("Trees"));
		FGaeaScalarField Moisture = MakeScalar(Domain, TEXT("VegetationMoisture"));

		for (int32 Y = 0; Y < Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Domain.Dimensions.X; ++X)
			{
				const float OpenWater = FMath::Max(Sea ? Sea->AtInterior(X, Y) : 0.0f, Lake ? Lake->AtInterior(X, Y) : 0.0f);
				const float Ice = Glacier ? Glacier->AtInterior(X, Y) : 0.0f;
				if (OpenWater > 0.25f || Ice > 0.25f) continue;

				const float Rain = Rainfall->AtInterior(X, Y);
				const float Evap = Evaporation->AtInterior(X, Y);
				const float Catch = FMath::Clamp(FMath::Loge(1.0f + Catchment->AtInterior(X, Y)) / CatchmentDenom, 0.0f, 1.0f);
				const float MoistureValue = FMath::Clamp(Rain * 0.62f + Catch * 0.24f + Concavity->AtInterior(X, Y) * 0.14f - Evap * 0.35f, 0.0f, 1.0f);
				Moisture.AtInterior(X, Y) = MoistureValue;

				const float RawMoistureFit = 1.0f - FMath::Clamp(FMath::Abs(MoistureValue - MoisturePreference) / FMath::Max(0.55f, MoisturePreference), 0.0f, 1.0f);
				const float MoistureFit = 0.15f + 0.85f * RawMoistureFit;
				const float Soil = FMath::Clamp(SoilDepth->AtInterior(X, Y) * 0.72f + Deposition->AtInterior(X, Y) * 0.28f, 0.0f, 1.0f);
				const float RawSoilFit = SmoothStep01((Soil - SoilPreference * 0.35f) / FMath::Max(0.7f - SoilPreference * 0.35f, 0.1f));
				const float SoilFit = (bShrubs ? 0.18f : 0.08f) + (bShrubs ? 0.82f : 0.92f) * RawSoilFit;
				const float SlopeFit = 1.0f - SmoothStep01(Slope->AtInterior(X, Y) / MaxSlope);
				const float Shelter = FMath::Lerp(1.0f, 0.55f + Concavity->AtInterior(X, Y) * 0.45f, ShelterStrength);
				const float ElevationFit = FMath::Clamp(1.0f + (Elevation->AtInterior(X, Y) - 0.5f) * ElevationBias, 0.0f, 1.0f);
				const float Riparian = FMath::Lerp(1.0f, 0.55f + Catch * 0.45f, RiparianStrength);

				float ClimateFit = 1.0f;
				if (Temperature)
				{
					const float T = Temperature->AtInterior(X, Y);
					const float ColdPenalty = bShrubs ? SmoothStep01((-18.0f - T) / 12.0f) : SmoothStep01((-8.0f - T) / 10.0f);
					const float HeatPenalty = bShrubs ? SmoothStep01((T - 38.0f) / 10.0f) : SmoothStep01((T - 32.0f) / 10.0f);
					ClimateFit = FMath::Clamp(1.0f - ColdPenalty - HeatPenalty, 0.0f, 1.0f);
				}

				const float SnowCover = Snow ? Snow->AtInterior(X, Y) : 0.0f;
				const float SnowFit = FMath::Lerp(1.0f - SnowCover, 1.0f, SnowTolerance);
				const float BaseSuitability = FMath::Clamp(MoistureFit * SoilFit * SlopeFit * Shelter * ElevationFit * Riparian * ClimateFit * SnowFit, 0.0f, 1.0f);
				const float Patch = 0.72f + 0.28f * Hash01(X / 2, Y / 2, Seed);
				const float Suit = FMath::Clamp(BaseSuitability * Patch, 0.0f, 1.0f);
				Suitability.AtInterior(X, Y) = Suit;
				DensityField.AtInterior(X, Y) = FMath::Clamp(Suit * Density, 0.0f, 1.0f);
			}
		}

		FGaeaScalarField SuitabilityOutput = Suitability;
		FGaeaScalarField DensityOutput = DensityField;
		FGaeaScalarField MoistureOutput = Moisture;
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(Suitability))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(DensityField))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Moisture)))
		{
			Error = TEXT("Ecology could not publish vegetation suitability fields.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		Out.Outputs.Add(TEXT("Suitability"), FGaeaTerrainValue::MakeScalarField(MoveTemp(SuitabilityOutput)));
		Out.Outputs.Add(TEXT("Density"), FGaeaTerrainValue::MakeScalarField(MoveTemp(DensityOutput)));
		Out.Outputs.Add(TEXT("Moisture"), FGaeaTerrainValue::MakeScalarField(MoveTemp(MoistureOutput)));
		Error.Reset();
		return true;
	}

	bool EvaluateTrees(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext& Context, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		return EvaluateVegetation(Node, Inputs, Context, Out, Error, false);
	}

	bool EvaluateShrubs(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext& Context, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		return EvaluateVegetation(Node, Inputs, Context, Out, Error, true);
	}

	void RegisterVegetationDescriptor(FName Type, const TCHAR* DisplayName, bool bShrubs, FGaeaTerrainNodeEvaluator Evaluator)
	{
		FGaeaTerrainNodeDescriptor D;
		D.Type = Type;
		D.DisplayName = DisplayName;
		D.Category = TEXT("Simulate");
		D.Description = bShrubs
			? TEXT("Derives shrub suitability from moisture balance, soil and deposition, slope, terrain shelter, hydrology, elevation, snow, and ice. Intended as semantic density for PCG vegetation placement rather than baked geometry.")
			: TEXT("Derives tree suitability from moisture balance, soil and deposition, slope, terrain shelter, hydrology, elevation, snow, and ice. Intended as semantic density for PCG vegetation placement rather than baked geometry.");
		D.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
		D.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
		D.Outputs.Add(ScalarPort(TEXT("Suitability"), TEXT("Suitability")));
		D.Outputs.Add(ScalarPort(TEXT("Density"), TEXT("Density")));
		D.Outputs.Add(ScalarPort(TEXT("Moisture"), TEXT("Moisture")));
		D.Parameters.Add(Num(TEXT("Density"), TEXT("Density"), bShrubs ? 0.72 : 0.62, 0.0, 1.0, TEXT("Population")));
		D.Parameters.Add(Num(TEXT("MoisturePreference"), TEXT("Moisture Preference"), bShrubs ? 0.48 : 0.68, 0.0, 1.0, TEXT("Habitat")));
		D.Parameters.Add(Num(TEXT("SoilPreference"), TEXT("Soil Preference"), bShrubs ? 0.35 : 0.65, 0.0, 1.0, TEXT("Habitat")));
		D.Parameters.Add(Num(TEXT("MaxSlopeDegrees"), TEXT("Maximum Slope (deg)"), bShrubs ? 46.0 : 32.0, 1.0, 89.0, TEXT("Habitat")));
		D.Parameters.Add(Num(TEXT("ElevationBias"), TEXT("Elevation Bias"), bShrubs ? 0.15 : -0.05, -1.0, 1.0, TEXT("Habitat")));
		D.Parameters.Add(Num(TEXT("ShelterStrength"), TEXT("Shelter Strength"), bShrubs ? 0.25 : 0.5, 0.0, 1.0, TEXT("Habitat")));
		D.Parameters.Add(Num(TEXT("RiparianStrength"), TEXT("Riparian Strength"), bShrubs ? 0.4 : 0.65, 0.0, 1.0, TEXT("Habitat")));
		D.Parameters.Add(Num(TEXT("SnowTolerance"), TEXT("Snow Tolerance"), bShrubs ? 0.6 : 0.25, 0.0, 1.0, TEXT("Climate")));
		D.Parameters.Add(Int(TEXT("Seed"), TEXT("Seed"), bShrubs ? 4242 : 31337, -1000000, 1000000, TEXT("Population")));
		FGaeaTerrainNodeDescriptorRegistry::Register(D);
		FGaeaTerrainNodeRegistry::Register(D.Type, Evaluator);
	}
}

void RegisterGaeaEcologyNodes()
{
	using namespace GaeaEcology;
	RegisterVegetationDescriptor(GaeaTerrainNodeTypes::Trees, TEXT("Trees"), false, EvaluateTrees);
	RegisterVegetationDescriptor(GaeaTerrainNodeTypes::Shrubs, TEXT("Shrubs"), true, EvaluateShrubs);
}
