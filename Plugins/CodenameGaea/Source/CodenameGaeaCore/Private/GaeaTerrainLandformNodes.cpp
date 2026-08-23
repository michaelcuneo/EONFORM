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

	FGaeaTerrainParameterDescriptor Boolean(FName Name, const TCHAR* Label, bool Default, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Group = Group;
		P.Type = EGaeaTerrainParameterType::Boolean;
		P.DefaultBoolean = Default;
		return P;
	}

	FGaeaTerrainParameterDescriptor Name(FName NameValue, const TCHAR* Label, FName Default, std::initializer_list<FName> Options, const TCHAR* Group)
	{
		FGaeaTerrainParameterDescriptor P;
		P.Name = NameValue;
		P.DisplayName = Label;
		P.Group = Group;
		P.Type = EGaeaTerrainParameterType::Name;
		P.DefaultName = Default;
		for (const FName Option : Options) P.NameOptions.Add(Option);
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
		Settings.Resolution = 513;
		Settings.WorldSize = 100000.0f;
		Settings.HeightScale = Context.PhysicalMetrics.HasElevationScale()
			? static_cast<float>(Context.PhysicalMetrics.ElevationScaleMeters * 100.0)
			: 8000.0f;
		Settings.Scale = static_cast<float>(Node.GetNumber(TEXT("Scale"), 1.0));
		Settings.Height = static_cast<float>(Node.GetNumber(TEXT("Height"), 0.92));
		Settings.Style = Node.GetName(TEXT("Style"), TEXT("Basic"));
		Settings.Bulk = Node.GetName(TEXT("Bulk"), TEXT("Medium"));
		Settings.bReduceDetails = Node.GetBool(TEXT("ReduceDetails"), false);
		Settings.Seed = static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337));
		Settings.OffsetX = static_cast<float>(Node.GetNumber(TEXT("X"), 0.0));
		Settings.OffsetY = static_cast<float>(Node.GetNumber(TEXT("Y"), 0.0));

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
	D.Description = TEXT("Gaea-style Mountain primitive built from a modulated Voronoi pattern and distortions, ready for further modification and erosion.");
	D.Outputs.Add(TerrainOut());
	D.Outputs.Add(ScalarOut(TEXT("Mass"), TEXT("Mass")));
	D.Outputs.Add(ScalarOut(TEXT("Uplift"), TEXT("Uplift")));
	D.Outputs.Add(ScalarOut(TEXT("Ridges"), TEXT("Ridges")));
	D.Outputs.Add(ScalarOut(TEXT("DrainageReadiness"), TEXT("Drainage Readiness")));
	D.Outputs.Add(ScalarOut(TEXT("ErosionEligibility"), TEXT("Erosion Eligibility")));
	D.Outputs.Add(ScalarOut(TEXT("RockExposure"), TEXT("Rock Exposure")));
	D.Outputs.Add(ScalarOut(TEXT("CryosphereEligibility"), TEXT("Cryosphere Eligibility")));

	// Public controls intentionally follow Gaea's documented Mountain contract.
	D.Parameters.Add(Number(TEXT("Scale"), TEXT("Scale"), 1.0, 0.1, 2.0, TEXT("Mountain")));
	D.Parameters.Add(Number(TEXT("Height"), TEXT("Height"), 0.92, 0.0, 1.0, TEXT("Mountain")));
	D.Parameters.Add(Name(TEXT("Style"), TEXT("Style"), TEXT("Basic"), { TEXT("Basic"), TEXT("Eroded"), TEXT("Old"), TEXT("Alpine"), TEXT("Strata") }, TEXT("Mountain")));
	D.Parameters.Add(Name(TEXT("Bulk"), TEXT("Bulk"), TEXT("Medium"), { TEXT("Low"), TEXT("Medium"), TEXT("High") }, TEXT("Mountain")));
	D.Parameters.Add(Boolean(TEXT("ReduceDetails"), TEXT("Reduce Details"), false, TEXT("Mountain")));
	D.Parameters.Add(Integer(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Mountain")));
	D.Parameters.Add(Number(TEXT("X"), TEXT("X"), 0.0, -1.5, 1.5, TEXT("Position")));
	D.Parameters.Add(Number(TEXT("Y"), TEXT("Y"), 0.0, -1.5, 1.5, TEXT("Position")));

	FGaeaTerrainNodeDescriptorRegistry::Register(D);
	FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateMountain);
}
