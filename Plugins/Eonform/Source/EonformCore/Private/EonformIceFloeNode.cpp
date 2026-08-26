#include "EonformCryosphereNodes.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace EonformIceFloeNode
{
	FEonformTerrainPortDescriptor TerrainPort(FName Name, const TCHAR* DisplayName)
	{
		FEonformTerrainPortDescriptor P; P.Name = Name; P.DisplayName = DisplayName; P.DataType = TEXT("Terrain"); return P;
	}
	FEonformTerrainPortDescriptor ScalarPort(FName Name, const TCHAR* DisplayName)
	{
		FEonformTerrainPortDescriptor P; P.Name = Name; P.DisplayName = DisplayName; P.DataType = TEXT("ScalarField"); return P;
	}
	FEonformTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Number; P.DefaultNumber = Default;
		P.bHasMinimum = true; P.Minimum = Min; P.bHasMaximum = true; P.Maximum = Max; P.Group = Group; return P;
	}
	FEonformScalarField MakeScalar(const FEonformGridDomain& Domain, FName Name, EEonformFieldUnit Unit = EEonformFieldUnit::Normalized)
	{
		FEonformFieldDescriptor D; D.Name = Name; D.Unit = Unit; D.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField F; F.Initialize(Domain, D, 0.0f); return F;
	}
	float HashNoise(int32 X, int32 Y, int32 Seed)
	{
		uint32 H = static_cast<uint32>(X) * 374761393u + static_cast<uint32>(Y) * 668265263u + static_cast<uint32>(Seed) * 2246822519u;
		H = (H ^ (H >> 13u)) * 1274126177u; H ^= H >> 16u;
		return static_cast<float>(H & 0x00ffffffu) / 16777215.0f;
	}
	bool EvaluateIceFloe(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext& Context, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* const* Ptr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = Ptr ? *Ptr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid()) { Error = TEXT("IceFloe requires a valid terrain input 'Terrain'."); return false; }
		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		const FEonformScalarField* Sea = Dataset.FindScalarField(TEXT("Sea"));
		const FEonformScalarField* SeaDepth = Dataset.FindScalarField(TEXT("SeaDepth"));
		if (!Sea || !SeaDepth || !Sea->IsValid() || !SeaDepth->IsValid() || Sea->Domain != SeaDepth->Domain)
		{
			Error = TEXT("IceFloe requires Sea and SeaDepth fields from an upstream Sea process."); return false;
		}
		const double AirTemperatureC = Node.GetNumber(TEXT("AirTemperatureC"), -8.0);
		const double FreezeTemperatureC = Node.GetNumber(TEXT("FreezeTemperatureC"), -1.8);
		const double TransitionC = FMath::Max(Node.GetNumber(TEXT("TemperatureTransitionC"), 5.0), 0.1);
		const double MaxThicknessMeters = FMath::Max(Node.GetNumber(TEXT("MaxThicknessMeters"), 1.5), 0.0);
		const double CoastalAnchoring = FMath::Clamp(Node.GetNumber(TEXT("CoastalAnchoring"), 0.35), 0.0, 1.0);
		const double Breakup = FMath::Clamp(Node.GetNumber(TEXT("Breakup"), 0.35), 0.0, 1.0);
		const double Coverage = FMath::Clamp(Node.GetNumber(TEXT("Coverage"), 0.8), 0.0, 1.0);
		const int32 Seed = FMath::RoundToInt(Node.GetNumber(TEXT("Seed"), 1337.0));

		FEonformScalarField Floe = MakeScalar(Sea->Domain, TEXT("IceFloe"));
		FEonformScalarField Thickness = MakeScalar(Sea->Domain, TEXT("IceThickness"), EEonformFieldUnit::Meters);
		FEonformScalarField Leads = MakeScalar(Sea->Domain, TEXT("IceLeads"));
		const float Cold = FMath::Clamp(static_cast<float>((FreezeTemperatureC - AirTemperatureC) / TransitionC), 0.0f, 1.0f);
		const int32 W = Sea->Domain.Dimensions.X, H = Sea->Domain.Dimensions.Y;
		for (int32 Y = 0; Y < H; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				if (Sea->AtInterior(X, Y) <= 0.5f) continue;
				const double Depth = FMath::Max(static_cast<double>(SeaDepth->AtInterior(X, Y)), 0.0);
				const double Anchor = FMath::Clamp(1.0 - Depth / 80.0, 0.0, 1.0);
				const double Noise = HashNoise(X, Y, Seed);
				const double Lead = FMath::Clamp((Noise - (1.0 - Breakup)) / FMath::Max(Breakup, 0.001), 0.0, 1.0);
				const double Concentration = FMath::Clamp(static_cast<double>(Cold) * Coverage * FMath::Lerp(1.0, 0.7 + 0.3 * Anchor, CoastalAnchoring) * (1.0 - Lead), 0.0, 1.0);
				Floe.AtInterior(X, Y) = static_cast<float>(Concentration);
				Thickness.AtInterior(X, Y) = static_cast<float>(MaxThicknessMeters * Concentration);
				Leads.AtInterior(X, Y) = static_cast<float>(Lead);
			}
		}
		FEonformScalarField FloeOut = Floe, ThicknessOut = Thickness, LeadsOut = Leads;
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(Floe)) || !Dataset.SetHeightDerivedScalarField(MoveTemp(Thickness)) || !Dataset.SetHeightDerivedScalarField(MoveTemp(Leads)))
		{ Error = TEXT("IceFloe could not publish marine ice fields."); return false; }
		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Input->HeightScale));
		Out.Outputs.Add(TEXT("IceFloe"), FEonformTerrainValue::MakeScalarField(MoveTemp(FloeOut)));
		Out.Outputs.Add(TEXT("Thickness"), FEonformTerrainValue::MakeScalarField(MoveTemp(ThicknessOut)));
		Out.Outputs.Add(TEXT("Leads"), FEonformTerrainValue::MakeScalarField(MoveTemp(LeadsOut)));
		Error.Reset(); return true;
	}
}

void RegisterEonformIceFloeNode()
{
	using namespace EonformIceFloeNode;
	FEonformTerrainNodeDescriptor D; D.Type = FName(TEXT("IceFloe")); D.DisplayName = TEXT("IceFloe"); D.Category = TEXT("Simulate");
	D.Description = TEXT("Forms fragmented sea ice only over ocean classified by Sea, using physical freezing conditions, sea depth, coastal anchoring, coverage, and deterministic breakup leads.");
	D.Inputs.Add(TerrainPort(TEXT("Terrain"), TEXT("Input"))); D.Outputs.Add(TerrainPort(TEXT("Out"), TEXT("Out")));
	D.Outputs.Add(ScalarPort(TEXT("IceFloe"), TEXT("Ice Floe"))); D.Outputs.Add(ScalarPort(TEXT("Thickness"), TEXT("Thickness"))); D.Outputs.Add(ScalarPort(TEXT("Leads"), TEXT("Leads")));
	D.Parameters.Add(Num(TEXT("AirTemperatureC"), TEXT("Air Temperature (C)"), -8.0, -80.0, 20.0, TEXT("Climate")));
	D.Parameters.Add(Num(TEXT("FreezeTemperatureC"), TEXT("Freeze Temperature (C)"), -1.8, -10.0, 5.0, TEXT("Climate")));
	D.Parameters.Add(Num(TEXT("TemperatureTransitionC"), TEXT("Temperature Transition (C)"), 5.0, 0.1, 30.0, TEXT("Climate")));
	D.Parameters.Add(Num(TEXT("MaxThicknessMeters"), TEXT("Maximum Thickness (m)"), 1.5, 0.0, 20.0, TEXT("Ice")));
	D.Parameters.Add(Num(TEXT("CoastalAnchoring"), TEXT("Coastal Anchoring"), 0.35, 0.0, 1.0, TEXT("Ice")));
	D.Parameters.Add(Num(TEXT("Breakup"), TEXT("Breakup"), 0.35, 0.0, 1.0, TEXT("Ice")));
	D.Parameters.Add(Num(TEXT("Coverage"), TEXT("Coverage"), 0.8, 0.0, 1.0, TEXT("Ice")));
	D.Parameters.Add(Num(TEXT("Seed"), TEXT("Seed"), 1337.0, 0.0, 1000000.0, TEXT("Ice")));
	FEonformTerrainNodeDescriptorRegistry::Register(D); FEonformTerrainNodeRegistry::Register(D.Type, EvaluateIceFloe);
}
