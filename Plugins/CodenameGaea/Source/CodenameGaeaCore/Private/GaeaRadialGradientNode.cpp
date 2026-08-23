#include "GaeaRadialGradientNode.h"

#include "GaeaGridDomain.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor RadialGradientTerrainPort(FName Name)
	{
		FGaeaTerrainPortDescriptor Port; Port.Name = Name; Port.DisplayName = TEXT("Out"); Port.DataType = TEXT("Terrain"); return Port;
	}
	FGaeaTerrainParameterDescriptor RadialGradientNumber(FName Name, const TCHAR* Label, double Default, double Min, double Max)
	{
		FGaeaTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EGaeaTerrainParameterType::Number; P.DefaultNumber = Default;
		P.bHasMinimum = true; P.Minimum = Min; P.bHasMaximum = true; P.Maximum = Max; return P;
	}
	bool EvaluateRadialGradientNode(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs&, const FGaeaTerrainEvaluationContext& Context, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const int32 NativeResolution = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 257)), 2, 1025);
		const FIntPoint Resolution(
			Context.TargetResolution.X > 1
				? FMath::Clamp(Context.TargetResolution.X, 2, 1025)
				: NativeResolution,
			Context.TargetResolution.Y > 1
				? FMath::Clamp(Context.TargetResolution.Y, 2, 1025)
				: NativeResolution);
		const float WorldSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WorldSize"), 100000.0)), 1.0f, 10000000.0f);
		const float HeightScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HeightScale"), 8000.0)), 1.0f, 1000000.0f);
		const float Scale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Scale"), 0.5)), 0.001f, 10.0f);
		const float HeightAmount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 1.0)), -4.0f, 4.0f);
		const float OffsetX = static_cast<float>(Node.GetNumber(TEXT("X"), 0.0));
		const float OffsetY = static_cast<float>(Node.GetNumber(TEXT("Y"), 0.0));
		const double Half = static_cast<double>(WorldSize) * 0.5;
		const FGaeaGridDomain Domain = FGaeaGridDomain::Make(Resolution, FVector2d(-Half, -Half), FVector2d(Half, Half));
		if (!Domain.IsValid()) { Error = TEXT("RadialGradient produced an invalid grid domain."); return false; }
		FGaeaFieldDescriptor D; D.Name = GaeaTerrainFieldNames::Height; D.Unit = EGaeaFieldUnit::Normalized; D.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Field; Field.Initialize(Domain, D);
		const float Radius = FMath::Max(WorldSize * 0.5f * Scale, 1.0f);
		for (int32 Y = 0; Y < Resolution.Y; ++Y)
		{
			for (int32 X = 0; X < Resolution.X; ++X)
			{
				const FVector2d W = Domain.InteriorSampleToWorld(X, Y);
				const float DX = static_cast<float>(W.X) - OffsetX;
				const float DY = static_cast<float>(W.Y) - OffsetY;
				const float V = FMath::Clamp(1.0f - FMath::Sqrt(DX * DX + DY * DY) / Radius, 0.0f, 1.0f);
				Field.AtInterior(X, Y) = FMath::Clamp(V * HeightAmount, -1.0f, 1.0f);
			}
		}
		FGaeaTerrainDataset Dataset; if (!Dataset.SetScalarField(MoveTemp(Field))) { Error = TEXT("RadialGradient could not publish Height."); return false; }
		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale); if (!Result.IsValid()) { Error = TEXT("RadialGradient produced invalid terrain."); return false; }
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result)); return true;
	}
}

void RegisterGaeaRadialGradientNode()
{
	FGaeaTerrainNodeDescriptor Descriptor; Descriptor.Type = GaeaTerrainNodeTypes::RadialGradient; Descriptor.DisplayName = TEXT("RadialGradient"); Descriptor.Category = TEXT("Primitive");
	Descriptor.Description = TEXT("Generates a circular gradient radiating outward from a center point."); Descriptor.Outputs.Add(RadialGradientTerrainPort(TEXT("Out")));
	Descriptor.Parameters.Add(RadialGradientNumber(TEXT("Scale"), TEXT("Scale"), 0.5, 0.001, 10.0));
	Descriptor.Parameters.Add(RadialGradientNumber(TEXT("Height"), TEXT("Height"), 1.0, -4.0, 4.0));
	Descriptor.Parameters.Add(RadialGradientNumber(TEXT("X"), TEXT("X"), 0.0, -1000000.0, 1000000.0));
	Descriptor.Parameters.Add(RadialGradientNumber(TEXT("Y"), TEXT("Y"), 0.0, -1000000.0, 1000000.0));
	FGaeaTerrainNodeDescriptorRegistry::Register(Descriptor); FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::RadialGradient, EvaluateRadialGradientNode);
}