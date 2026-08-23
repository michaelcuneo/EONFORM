#include "GaeaTerrainLandformNodes.h"

#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainLandformOps.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor TerrainOut()
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = TEXT("Out");
		Port.DisplayName = TEXT("Out");
		Port.DataType = TEXT("Terrain");
		return Port;
	}

	FGaeaTerrainPortDescriptor ScalarOut(FName Name, const TCHAR* Label)
	{
		FGaeaTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DisplayName = Label;
		Port.DataType = TEXT("ScalarField");
		return Port;
	}

	FGaeaTerrainParameterDescriptor Number(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Group = Group;
		P.Type = EGaeaTerrainParameterType::Number;
		P.DefaultNumber = Default;
		P.bHasMinimum = true;
		P.Minimum = Min;
		P.bHasMaximum = true;
		P.Maximum = Max;
		return P;
	}

	FGaeaTerrainParameterDescriptor Integer(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Group = Group;
		P.Type = EGaeaTerrainParameterType::Integer;
		P.DefaultInteger = Default;
		P.bHasMinimum = true;
		P.Minimum = static_cast<double>(Min);
		P.bHasMaximum = true;
		P.Maximum = static_cast<double>(Max);
		return P;
	}

	bool EvaluateMountain(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs&,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		FGaeaMountainLandformSettings Settings;
		Settings.Resolution = static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 513));
		Settings.WorldSize = static_cast<float>(Node.GetNumber(TEXT("WorldSize"), 100000.0));
		Settings.HeightScale = static_cast<float>(Node.GetNumber(TEXT("HeightScale"), 8000.0));
		Settings.Radius = static_cast<float>(Node.GetNumber(TEXT("Radius"), 0.72));
		Settings.Elongation = static_cast<float>(Node.GetNumber(TEXT("Elongation"), 0.52));
		Settings.OrientationDegrees = static_cast<float>(Node.GetNumber(TEXT("Orientation"), 20.0));
		Settings.PeakSharpness = static_cast<float>(Node.GetNumber(TEXT("PeakSharpness"), 0.58));
		Settings.RidgeStrength = static_cast<float>(Node.GetNumber(TEXT("RidgeStrength"), 0.72));
		Settings.RidgeFrequency = static_cast<float>(Node.GetNumber(TEXT("RidgeFrequency"), 4.0));
		Settings.Roughness = static_cast<float>(Node.GetNumber(TEXT("Roughness"), 0.34));
		Settings.Asymmetry = static_cast<float>(Node.GetNumber(TEXT("Asymmetry"), 0.18));
		Settings.BaseElevation = static_cast<float>(Node.GetNumber(TEXT("BaseElevation"), 0.02));
		Settings.Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));

		FGaeaMountainLandformResult Result;
		if (!FGaeaTerrainLandformOps::BuildMountain(Settings, Context.PhysicalMetrics, Result, &Error))
		{
			return false;
		}

		auto PublishScalar = [&Out, &Result](FName OutputName, FName FieldName)
		{
			if (const FGaeaScalarField* Field = Result.Dataset.FindScalarField(FieldName))
			{
				Out.Outputs.Add(OutputName, FGaeaTerrainValue::MakeScalarField(*Field));
			}
		};

		PublishScalar(TEXT("Mass"), GaeaTerrainFieldNames::MountainMass);
		PublishScalar(TEXT("Uplift"), GaeaTerrainFieldNames::Uplift);
		PublishScalar(TEXT("Ridges"), GaeaTerrainFieldNames::RidgeNetwork);
		PublishScalar(TEXT("DrainageReadiness"), GaeaTerrainFieldNames::DrainageReadiness);
		PublishScalar(TEXT("ErosionEligibility"), GaeaTerrainFieldNames::ErosionEligibility);
		PublishScalar(TEXT("RockExposure"), GaeaTerrainFieldNames::RockExposure);
		PublishScalar(TEXT("CryosphereEligibility"), GaeaTerrainFieldNames::CryosphereEligibility);

		FGaeaTerrainValue Terrain = FGaeaTerrainValue::MakeTerrain(MoveTemp(Result.Dataset), Result.HeightScale);
		if (!Terrain.IsValid())
		{
			Error = TEXT("Mountain produced an invalid terrain output.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Terrain));
		Error.Reset();
		return true;
	}
}

void RegisterGaeaTerrainLandformNodes()
{
	FGaeaTerrainNodeDescriptor D;
	D.Type = GaeaTerrainNodeTypes::Mountain;
	D.DisplayName = TEXT("Mountain");
	D.Category = TEXT("Terrain");
	D.Description = TEXT("Builds a structured mountain massif from uplift, ridge organization, asymmetry and macro relief. Publishes process-readiness fields without forcing hydrology.");
	D.Outputs.Add(TerrainOut());
	D.Outputs.Add(ScalarOut(TEXT("Mass"), TEXT("Mass")));
	D.Outputs.Add(ScalarOut(TEXT("Uplift"), TEXT("Uplift")));
	D.Outputs.Add(ScalarOut(TEXT("Ridges"), TEXT("Ridges")));
	D.Outputs.Add(ScalarOut(TEXT("DrainageReadiness"), TEXT("Drainage Readiness")));
	D.Outputs.Add(ScalarOut(TEXT("ErosionEligibility"), TEXT("Erosion Eligibility")));
	D.Outputs.Add(ScalarOut(TEXT("RockExposure"), TEXT("Rock Exposure")));
	D.Outputs.Add(ScalarOut(TEXT("CryosphereEligibility"), TEXT("Cryosphere Eligibility")));

	D.Parameters.Add(Integer(TEXT("Resolution"), TEXT("Resolution"), 513, 17, 4097, TEXT("Domain")));
	D.Parameters.Add(Number(TEXT("WorldSize"), TEXT("Fallback World Size"), 100000.0, 100.0, 10000000.0, TEXT("Domain")));
	D.Parameters.Add(Number(TEXT("HeightScale"), TEXT("Height Scale"), 8000.0, 1.0, 1000000.0, TEXT("Domain")));
	D.Parameters.Add(Number(TEXT("Radius"), TEXT("Mass Radius"), 0.72, 0.05, 1.5, TEXT("Mass")));
	D.Parameters.Add(Number(TEXT("Elongation"), TEXT("Elongation"), 0.52, 0.05, 1.0, TEXT("Mass")));
	D.Parameters.Add(Number(TEXT("Orientation"), TEXT("Orientation"), 20.0, -360.0, 360.0, TEXT("Mass")));
	D.Parameters.Add(Number(TEXT("PeakSharpness"), TEXT("Peak Sharpness"), 0.58, 0.0, 1.0, TEXT("Structure")));
	D.Parameters.Add(Number(TEXT("RidgeStrength"), TEXT("Ridge Strength"), 0.72, 0.0, 1.0, TEXT("Structure")));
	D.Parameters.Add(Number(TEXT("RidgeFrequency"), TEXT("Ridge Frequency"), 4.0, 0.25, 16.0, TEXT("Structure")));
	D.Parameters.Add(Number(TEXT("Roughness"), TEXT("Roughness"), 0.34, 0.0, 1.0, TEXT("Structure")));
	D.Parameters.Add(Number(TEXT("Asymmetry"), TEXT("Asymmetry"), 0.18, -1.0, 1.0, TEXT("Structure")));
	D.Parameters.Add(Number(TEXT("BaseElevation"), TEXT("Base Elevation"), 0.02, -1.0, 1.0, TEXT("Mass")));
	D.Parameters.Add(Integer(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Structure")));

	FGaeaTerrainNodeDescriptorRegistry::Register(D);
	FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateMountain);
}
