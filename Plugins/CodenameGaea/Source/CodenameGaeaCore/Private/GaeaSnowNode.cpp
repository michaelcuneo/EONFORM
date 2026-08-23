#include "GaeaCryosphereNodes.h"

#include "GaeaTerrainDerivedData.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace GaeaSnowNode
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

	FGaeaTerrainParameterDescriptor Bool(FName Name, const TCHAR* Label, bool Default, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EGaeaTerrainParameterType::Boolean;
		P.DefaultBoolean = Default;
		P.Group = Group;
		return P;
	}

	FGaeaScalarField MakeScalar(const FGaeaGridDomain& Domain, FName Name, EGaeaFieldUnit Unit = EGaeaFieldUnit::Normalized)
	{
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = Unit;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Field;
		Field.Initialize(Domain, Descriptor, 0.0f);
		return Field;
	}

	float SmoothStep01(float Value)
	{
		const float T = FMath::Clamp(Value, 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	bool EvaluateSnow(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* Ptr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* Input = Ptr ? *Ptr : nullptr;
		if (!Input || Input->Type != EGaeaTerrainValueType::Terrain || !Input->IsValid())
		{
			Error = TEXT("Snow requires a valid terrain input 'Terrain'.");
			return false;
		}

		FGaeaTerrainDataset Dataset = Input->TerrainDataset;
		if (!FGaeaTerrainDerivedData::EnsureContext(Dataset, Input->HeightScale, Context.PhysicalMetrics, &Error)) return false;

		const FGaeaScalarField* Source = Dataset.FindScalarField(GaeaTerrainFieldNames::Height);
		const FGaeaScalarField* Slope = Dataset.FindScalarField(GaeaTerrainFieldNames::SlopeDegrees);
		const FGaeaScalarField* Concavity = Dataset.FindScalarField(GaeaTerrainFieldNames::Concavity);
		if (!Source || !Slope || !Concavity)
		{
			Error = TEXT("Snow could not resolve Height, SlopeDegrees, and Concavity fields.");
			return false;
		}

		const double ElevationScaleMeters = Context.PhysicalMetrics.ResolveElevationScaleMeters(Input->HeightScale);
		if (ElevationScaleMeters <= UE_DOUBLE_SMALL_NUMBER)
		{
			Error = TEXT("Snow could not resolve a physical elevation scale.");
			return false;
		}

		const double BaseTemperatureC = Node.GetNumber(TEXT("BaseTemperatureC"), 12.0);
		const double LapseRateCPerKm = Node.GetNumber(TEXT("LapseRateCPerKm"), 6.5);
		const double SnowTemperatureC = Node.GetNumber(TEXT("SnowTemperatureC"), 1.5);
		const double TransitionC = FMath::Max(Node.GetNumber(TEXT("TemperatureTransitionC"), 4.0), 0.1);
		const double MaxDepthMeters = FMath::Max(Node.GetNumber(TEXT("MaxDepthMeters"), 2.0), 0.0);
		const double MaxStableSlopeDegrees = FMath::Clamp(Node.GetNumber(TEXT("MaxStableSlopeDegrees"), 48.0), 1.0, 89.0);
		const double AccumulationSlopeDegrees = FMath::Clamp(Node.GetNumber(TEXT("AccumulationSlopeDegrees"), 22.0), 0.0, MaxStableSlopeDegrees);
		const double ShelterStrength = FMath::Clamp(Node.GetNumber(TEXT("ShelterStrength"), 0.45), 0.0, 1.0);
		const double Precipitation = FMath::Clamp(Node.GetNumber(TEXT("Precipitation"), 0.8), 0.0, 1.0);
		const bool bAffectHeight = Node.GetBool(TEXT("AffectHeight"), true);

		FGaeaScalarField Height = *Source;
		FGaeaScalarField Snow = MakeScalar(Source->Domain, TEXT("Snow"));
		FGaeaScalarField SnowDepth = MakeScalar(Source->Domain, TEXT("SnowDepth"), EGaeaFieldUnit::Meters);
		FGaeaScalarField Temperature = MakeScalar(Source->Domain, TEXT("TemperatureC"), EGaeaFieldUnit::Scalar);

		for (int32 Y = 0; Y < Source->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Source->Domain.Dimensions.X; ++X)
			{
				const double ElevationMeters = static_cast<double>(Source->AtInterior(X, Y)) * ElevationScaleMeters;
				const double TemperatureC = BaseTemperatureC - LapseRateCPerKm * (ElevationMeters / 1000.0);
				Temperature.AtInterior(X, Y) = static_cast<float>(TemperatureC);

				if (ElevationMeters <= 0.0) continue;
				const double Cold = SmoothStep01(static_cast<float>((SnowTemperatureC - TemperatureC) / TransitionC + 0.5));
				const double SlopeDegrees = static_cast<double>(Slope->AtInterior(X, Y));
				const double Stable = 1.0 - SmoothStep01(static_cast<float>((SlopeDegrees - AccumulationSlopeDegrees) / FMath::Max(MaxStableSlopeDegrees - AccumulationSlopeDegrees, 1.0)));
				const double Shelter = FMath::Lerp(1.0, 0.55 + 0.45 * static_cast<double>(Concavity->AtInterior(X, Y)), ShelterStrength);
				const double Potential = FMath::Clamp(Cold * Stable * Shelter * Precipitation, 0.0, 1.0);
				const double DepthMeters = MaxDepthMeters * Potential;

				Snow.AtInterior(X, Y) = static_cast<float>(Potential);
				SnowDepth.AtInterior(X, Y) = static_cast<float>(DepthMeters);
				if (bAffectHeight && DepthMeters > 0.0)
				{
					Height.AtInterior(X, Y) = FMath::Clamp(
						Source->AtInterior(X, Y) + static_cast<float>(DepthMeters / ElevationScaleMeters),
						-1.0f,
						1.0f);
				}
			}
		}

		if (bAffectHeight)
		{
			Height.Descriptor.Name = GaeaTerrainFieldNames::Height;
			if (!Dataset.SetScalarField(MoveTemp(Height)))
			{
				Error = TEXT("Snow could not publish accumulated Height.");
				return false;
			}
		}

		FGaeaScalarField SnowOutput = Snow;
		FGaeaScalarField DepthOutput = SnowDepth;
		FGaeaScalarField TemperatureOutput = Temperature;
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(Snow))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(SnowDepth))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(Temperature)))
		{
			Error = TEXT("Snow could not publish cryosphere fields.");
			return false;
		}

		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		Out.Outputs.Add(TEXT("Snow"), FGaeaTerrainValue::MakeScalarField(MoveTemp(SnowOutput)));
		Out.Outputs.Add(TEXT("Depth"), FGaeaTerrainValue::MakeScalarField(MoveTemp(DepthOutput)));
		Out.Outputs.Add(TEXT("Temperature"), FGaeaTerrainValue::MakeScalarField(MoveTemp(TemperatureOutput)));
		Error.Reset();
		return true;
	}
}

void RegisterGaeaCryosphereNodes()
{
	using namespace GaeaSnowNode;

	FGaeaTerrainNodeDescriptor Descriptor;
	Descriptor.Type = GaeaTerrainNodeTypes::Snow;
	Descriptor.DisplayName = TEXT("Snow");
	Descriptor.Category = TEXT("Simulate");
	Descriptor.Description = TEXT("Accumulates snow from physically scaled elevation, atmospheric lapse rate, slope stability, and terrain shelter. Snow exists where cold and terrain conditions permit it rather than as a uniform height mask.");
	Descriptor.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input")));
	Descriptor.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Snow"), TEXT("Snow")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Depth"), TEXT("Depth")));
	Descriptor.Outputs.Add(ScalarPort(TEXT("Temperature"), TEXT("Temperature")));
	Descriptor.Parameters.Add(Num(TEXT("BaseTemperatureC"), TEXT("Base Temperature (C)"), 12.0, -80.0, 60.0, TEXT("Climate")));
	Descriptor.Parameters.Add(Num(TEXT("LapseRateCPerKm"), TEXT("Lapse Rate (C/km)"), 6.5, 0.0, 20.0, TEXT("Climate")));
	Descriptor.Parameters.Add(Num(TEXT("SnowTemperatureC"), TEXT("Snow Temperature (C)"), 1.5, -20.0, 10.0, TEXT("Climate")));
	Descriptor.Parameters.Add(Num(TEXT("TemperatureTransitionC"), TEXT("Temperature Transition (C)"), 4.0, 0.1, 30.0, TEXT("Climate")));
	Descriptor.Parameters.Add(Num(TEXT("Precipitation"), TEXT("Precipitation"), 0.8, 0.0, 1.0, TEXT("Climate")));
	Descriptor.Parameters.Add(Num(TEXT("MaxDepthMeters"), TEXT("Maximum Depth (m)"), 2.0, 0.0, 100.0, TEXT("Accumulation")));
	Descriptor.Parameters.Add(Num(TEXT("AccumulationSlopeDegrees"), TEXT("Accumulation Slope (deg)"), 22.0, 0.0, 89.0, TEXT("Accumulation")));
	Descriptor.Parameters.Add(Num(TEXT("MaxStableSlopeDegrees"), TEXT("Maximum Stable Slope (deg)"), 48.0, 1.0, 89.0, TEXT("Accumulation")));
	Descriptor.Parameters.Add(Num(TEXT("ShelterStrength"), TEXT("Shelter Strength"), 0.45, 0.0, 1.0, TEXT("Accumulation")));
	Descriptor.Parameters.Add(Bool(TEXT("AffectHeight"), TEXT("Affect Height"), true, TEXT("Output")));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor);
	FGaeaTerrainNodeRegistry::Register(Descriptor.Type, EvaluateSnow);

	RegisterGaeaIceFloeNode();
	RegisterGaeaDustingNode();
}
