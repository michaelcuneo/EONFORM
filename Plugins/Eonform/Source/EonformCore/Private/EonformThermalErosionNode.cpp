#include "EonformThermalErosionNode.h"

#include "EonformTerrainDerivedData.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"
#include "EonformThermalErosion.h"

namespace
{
	FEonformTerrainPortDescriptor Port(FName Name, const TCHAR* Label, FName Type)
	{
		FEonformTerrainPortDescriptor P; P.Name = Name; P.DisplayName = Label; P.DataType = Type; return P;
	}
	FEonformTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Number; P.DefaultNumber = Default; P.bHasMinimum = true; P.Minimum = Min; P.bHasMaximum = true; P.Maximum = Max; if (Group) P.Group = Group; return P;
	}
	FEonformTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Integer; P.DefaultInteger = Default; P.bHasMinimum = true; P.Minimum = static_cast<double>(Min); P.bHasMaximum = true; P.Maximum = static_cast<double>(Max); if (Group) P.Group = Group; return P;
	}
	FEonformTerrainParameterDescriptor Bool(FName Name, const TCHAR* Label, bool Default, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Boolean; P.DefaultBoolean = Default; if (Group) P.Group = Group; return P;
	}
	uint32 Hash(uint32 X)
	{
		X ^= X >> 16; X *= 0x7feb352dU; X ^= X >> 15; X *= 0x846ca68bU; X ^= X >> 16; return X;
	}
	float Hash01(int32 X, int32 Y, int32 Seed, uint32 Salt = 0)
	{
		uint32 H = static_cast<uint32>(X) * 0x9e3779b9U; H ^= static_cast<uint32>(Y) * 0x85ebca6bU; H ^= static_cast<uint32>(Seed) * 0xc2b2ae35U; H ^= Salt;
		return static_cast<float>(Hash(H) & 0x00ffffffU) / 16777215.0f;
	}
	FEonformScalarField MakeScalar(const FEonformGridDomain& Domain, FName Name)
	{
		FEonformFieldDescriptor D; D.Name = Name; D.Unit = EEonformFieldUnit::Normalized; D.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField F; F.Initialize(Domain, D, 0.0f); return F;
	}
	float EffectiveSolverHeightScale(const FEonformScalarField& Height, float HeightScale, const FEonformTerrainEvaluationContext& Context)
	{
		const FVector2d DomainCell = Height.Domain.GetCellSize();
		const double DomainRepresentative = FMath::Max(FMath::Min(FMath::Abs(DomainCell.X), FMath::Abs(DomainCell.Y)), UE_DOUBLE_SMALL_NUMBER);
		const double PhysicalSpacing = Context.PhysicalMetrics.ResolveRepresentativeSampleSpacingMeters(Height.Domain.Dimensions, DomainCell);
		const double PhysicalElevation = Context.PhysicalMetrics.ResolveElevationScaleMeters(HeightScale);
		return static_cast<float>(FMath::Max(PhysicalElevation / FMath::Max(PhysicalSpacing, UE_DOUBLE_SMALL_NUMBER) * DomainRepresentative, 1.0));
	}
	bool PublishTerrain(FEonformTerrainDataset&& Dataset, float HeightScale, const FEonformTerrainEvaluationContext& Context, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		if (!FEonformTerrainDerivedData::EnsureContext(Dataset, FMath::Max(HeightScale, 1.0f), Context.PhysicalMetrics, &Error)) return false;
		FEonformTerrainValue Value = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Value.IsValid()) { Error = TEXT("Thermal node produced an invalid terrain output."); return false; }
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Value)); return true;
	}
	bool EvaluateThermalCommon(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext& Context, FEonformTerrainNodeEvaluation& Out, FString& Error, bool bClassic)
	{
		const FEonformTerrainValue* const* Ptr = Inputs.Find(TEXT("Terrain"));
		const FEonformTerrainValue* Input = Ptr ? *Ptr : nullptr;
		if (!Input || Input->Type != EEonformTerrainValueType::Terrain || !Input->IsValid()) { Error = TEXT("Thermal requires Terrain input."); return false; }
		FEonformTerrainDataset Dataset = Input->TerrainDataset;
		FEonformTerrainDerivedDataSettings Derived;
		if (!FEonformTerrainDerivedData::EnsureHydraulicInputs(Dataset, Input->HeightScale, Context.PhysicalMetrics, Derived, &Error)) return false;
		const FEonformScalarField* Source = Dataset.FindScalarField(EonformTerrainFieldNames::Height);
		if (!Source || !Source->IsValid()) { Error = TEXT("Thermal input has no valid Height field."); return false; }

		const int32 Duration = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Duration"), bClassic ? 12 : 16)), 1, 4096);
		const float Strength = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Strength"), bClassic ? 0.35 : 0.45)), 0.0f, 1.0f);
		const float Anisotropy = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Anisotropy"), 0.0)), 0.0f, 1.0f);
		const float Angle = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Angle"), 34.0)), 0.0f, 89.9f);
		const float SedimentRemoval = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("SedimentRemoval"), 0.0)), 0.0f, 1.0f);
		const float Settling = bClassic ? FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Settling"), 0.5)), 0.0f, 1.0f) : 0.68f;
		const int32 Seed = bClassic ? static_cast<int32>(Node.GetInteger(TEXT("Seed"), 1337)) : 1337;
		const double AuthoredFeatureMeters = FMath::Clamp(Node.GetNumber(TEXT("FeatureScale"), bClassic ? 1000.0 : 80.0), 1.0, 20000.0);
		const bool bRealScale = bClassic ? Node.GetBool(TEXT("RealScale"), true) : true;
		const double TerrainScale = bClassic ? FMath::Clamp(Node.GetNumber(TEXT("TerrainScale"), 1.0), 0.01, 100.0) : 1.0;
		const float Verticality = bClassic ? FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Verticality"), 1.0)), 0.01f, 10.0f) : 1.0f;
		const double FeatureMeters = bRealScale ? AuthoredFeatureMeters : AuthoredFeatureMeters * TerrainScale;
		const double SampleSpacing = Context.PhysicalMetrics.ResolveRepresentativeSampleSpacingMeters(Source->Domain.Dimensions, Source->Domain.GetCellSize());

		FEonformThermalErosionSettings Settings;
		Settings.Iterations = Duration;
		Settings.TalusAngleDegrees = FMath::Clamp(Angle / Verticality, 0.0f, 89.9f);
		Settings.Strength = Strength;
		Settings.FeatureScaleSamples = static_cast<float>(FeatureMeters / FMath::Max(SampleSpacing, UE_DOUBLE_SMALL_NUMBER));
		Settings.Anisotropy = Anisotropy;
		Settings.Settling = Settling;
		Settings.SedimentRemoval = SedimentRemoval;
		Settings.Seed = Seed;

		FEonformScalarField Height = *Source;
		FEonformScalarField SeedMask;
		const FEonformScalarField* AreaMask = nullptr;
		if (bClassic)
		{
			SeedMask = MakeScalar(Source->Domain, TEXT("ThermalSeedVariation"));
			for (int32 Y = 0; Y < Source->Domain.Dimensions.Y; ++Y)
				for (int32 X = 0; X < Source->Domain.Dimensions.X; ++X)
					SeedMask.AtInterior(X, Y) = 0.88f + Hash01(X, Y, Seed, 0x71u) * 0.12f;
			AreaMask = &SeedMask;
		}

		if (!FEonformThermalErosion::ApplyInPlace(
			Height,
			EffectiveSolverHeightScale(*Source, Input->HeightScale, Context),
			Settings,
			Dataset.FindScalarField(EonformTerrainFieldNames::Thermal),
			Dataset.FindScalarField(EonformTerrainFieldNames::RockHardness),
			AreaMask,
			&Error)) return false;

		FEonformScalarField Talus = MakeScalar(Source->Domain, TEXT("Talus"));
		for (int32 Y = 0; Y < Source->Domain.Dimensions.Y; ++Y)
			for (int32 X = 0; X < Source->Domain.Dimensions.X; ++X)
				Talus.AtInterior(X, Y) = FMath::Clamp((Height.AtInterior(X, Y) - Source->AtInterior(X, Y)) * 24.0f, 0.0f, 1.0f);
		FEonformScalarField TalusOutput = Talus;
		Height.Descriptor.Name = EonformTerrainFieldNames::Height;
		if (!Dataset.SetScalarField(MoveTemp(Height)) || !Dataset.SetScalarField(Talus)) { Error = TEXT("Thermal could not publish terrain fields."); return false; }
		if (!PublishTerrain(MoveTemp(Dataset), Input->HeightScale, Context, Out, Error)) return false;
		Out.Outputs.Add(TEXT("Talus"), FEonformTerrainValue::MakeScalarField(MoveTemp(TalusOutput)));
		return true;
	}
}

void RegisterEonformThermalErosionNode()
{
	FEonformTerrainNodeDescriptor D; D.Type = EonformTerrainNodeTypes::ThermalErosion; D.DisplayName = TEXT("Thermal"); D.Category = TEXT("Simulate");
	D.Description = TEXT("Simulates thermal erosion and talus formation using physical-scale feature controls.");
	D.Inputs.Add(Port(TEXT("Terrain"), TEXT("Input"), TEXT("Terrain"))); D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Terrain"))); D.Outputs.Add(Port(TEXT("Talus"), TEXT("Talus"), TEXT("ScalarField")));
	D.Parameters = {
		Int(TEXT("Duration"), TEXT("Duration"), 12, 1, 4096, TEXT("Erosion")), Num(TEXT("Strength"), TEXT("Strength"), 0.35, 0.0, 1.0, TEXT("Erosion")), Num(TEXT("Anisotropy"), TEXT("Anisotropy"), 0.0, 0.0, 1.0, TEXT("Erosion")), Int(TEXT("Seed"), TEXT("Seed"), 1337, -2147483647, 2147483647, TEXT("Erosion")),
		Num(TEXT("Angle"), TEXT("Angle"), 34.0, 0.0, 89.9, TEXT("Talus")), Num(TEXT("Settling"), TEXT("Settling"), 0.5, 0.0, 1.0, TEXT("Talus")), Num(TEXT("SedimentRemoval"), TEXT("Sediment Removal"), 0.0, 0.0, 1.0, TEXT("Talus")),
		Num(TEXT("FeatureScale"), TEXT("Feature Scale"), 1000.0, 1.0, 20000.0, TEXT("Scale")), Bool(TEXT("RealScale"), TEXT("Real Scale"), true, TEXT("Scale")), Num(TEXT("TerrainScale"), TEXT("Terrain Scale"), 1.0, 0.01, 100.0, TEXT("Scale")), Num(TEXT("Verticality"), TEXT("Verticality"), 1.0, 0.01, 10.0, TEXT("Scale")) };
	FEonformTerrainNodeDescriptorRegistry::Register(D); FEonformTerrainNodeRegistry::Register(D.Type, [](const FEonformTerrainNode& N,const FEonformTerrainNodeInputs& I,const FEonformTerrainEvaluationContext& C,FEonformTerrainNodeEvaluation& O,FString& E){ return EvaluateThermalCommon(N,I,C,O,E,true); });
}

void RegisterEonformThermal2Node()
{
	FEonformTerrainNodeDescriptor D; D.Type = EonformTerrainNodeTypes::Thermal2; D.DisplayName = TEXT("Thermal2"); D.Category = TEXT("Simulate");
	D.Description = TEXT("Creates physical-scale thermal weathering, talus, debris, and slope breakdown.");
	D.Inputs.Add(Port(TEXT("Terrain"), TEXT("Input"), TEXT("Terrain"))); D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Terrain"))); D.Outputs.Add(Port(TEXT("Talus"), TEXT("Talus"), TEXT("ScalarField")));
	D.Parameters = { Int(TEXT("Duration"), TEXT("Duration"), 16, 1, 1024, TEXT("Erosion")), Num(TEXT("Strength"), TEXT("Strength"), 0.45, 0.0, 1.0, TEXT("Erosion")), Num(TEXT("Anisotropy"), TEXT("Anisotropy"), 0.0, 0.0, 1.0, TEXT("Erosion")), Num(TEXT("Angle"), TEXT("Angle"), 34.0, 0.0, 89.9, TEXT("Talus")), Num(TEXT("SedimentRemoval"), TEXT("Sediment Removal"), 0.0, 0.0, 1.0, TEXT("Talus")), Num(TEXT("FeatureScale"), TEXT("Feature Scale"), 80.0, 1.0, 20000.0, TEXT("Scale")) };
	FEonformTerrainNodeDescriptorRegistry::Register(D); FEonformTerrainNodeRegistry::Register(D.Type, [](const FEonformTerrainNode& N,const FEonformTerrainNodeInputs& I,const FEonformTerrainEvaluationContext& C,FEonformTerrainNodeEvaluation& O,FString& E){ return EvaluateThermalCommon(N,I,C,O,E,false); });
}
