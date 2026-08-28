#include "EonformRadialGradientNode.h"

#include "EonformGridDomain.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor TerrainPort(FName Name)
	{
		FEonformTerrainPortDescriptor P; P.Name = Name; P.DisplayName = TEXT("Out"); P.DataType = TEXT("Terrain"); return P;
	}
	FEonformTerrainParameterDescriptor Number(FName Name, const TCHAR* Label, double Default, double Min, double Max)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Number; P.DefaultNumber = Default;
		P.bHasMinimum = true; P.Minimum = Min; P.bHasMaximum = true; P.Maximum = Max; return P;
	}

	FEonformGridDomain BuildDomain(const FEonformTerrainNode& Node, const FEonformTerrainEvaluationContext& Context)
	{
		const int32 RequestedX = Context.TargetResolution.X > 1 ? Context.TargetResolution.X : 257;
		const int32 RequestedY = Context.TargetResolution.Y > 1 ? Context.TargetResolution.Y : RequestedX;
		const int32 LegacyResolution = static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 0));
		const int32 Width = FMath::Clamp(LegacyResolution > 1 ? LegacyResolution : RequestedX, 2, 4097);
		const int32 Height = FMath::Clamp(LegacyResolution > 1 ? LegacyResolution : RequestedY, 2, 4097);
		double WorldWidthCm = 100000.0;
		double WorldDepthCm = 100000.0;
		if (Context.PhysicalMetrics.HasWorldDimensions())
		{
			WorldWidthCm = Context.PhysicalMetrics.WorldWidthMeters * 100.0;
			WorldDepthCm = Context.PhysicalMetrics.WorldDepthMeters * 100.0;
		}
		else
		{
			const double LegacyWorld = FMath::Max(Node.GetNumber(TEXT("WorldSize"), 100000.0), 1.0);
			WorldWidthCm = LegacyWorld;
			WorldDepthCm = LegacyWorld;
		}
		return FEonformGridDomain::Make(FIntPoint(Width, Height), FVector2d(-WorldWidthCm * 0.5, -WorldDepthCm * 0.5), FVector2d(WorldWidthCm * 0.5, WorldDepthCm * 0.5));
	}

	float ResolveHeightScale(const FEonformTerrainNode& Node, const FEonformTerrainEvaluationContext& Context)
	{
		if (Context.PhysicalMetrics.HasElevationScale()) return static_cast<float>(Context.PhysicalMetrics.ElevationScaleMeters * 100.0);
		return FMath::Max(static_cast<float>(Node.GetNumber(TEXT("HeightScale"), Context.HeightScale)), 1.0f);
	}

	bool EvaluateRadialGradientNode(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs&, const FEonformTerrainEvaluationContext& Context, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformGridDomain Domain = BuildDomain(Node, Context);
		if (!Domain.IsValid()) { Error = TEXT("RadialGradient produced an invalid domain."); return false; }
		const float Scale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Scale"), 0.5)), 0.001f, 4.0f);
		const float PeakHeight = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Height"), 1.0)), -4.0f, 4.0f);
		const float OffsetX = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("X"), 0.0)), -2.0f, 2.0f);
		const float OffsetY = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Y"), 0.0)), -2.0f, 2.0f);

		FEonformFieldDescriptor Descriptor; Descriptor.Name = EonformTerrainFieldNames::Height; Descriptor.Unit = EEonformFieldUnit::Normalized; Descriptor.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Height; Height.Initialize(Domain, Descriptor, 0.0f);
		const FVector2d WorldSize = Domain.WorldSize();
		const double Reference = FMath::Max(FMath::Min(FMath::Abs(WorldSize.X), FMath::Abs(WorldSize.Y)), UE_DOUBLE_SMALL_NUMBER);
		const FVector2d Center(Domain.WorldMin.X + WorldSize.X * (0.5 + OffsetX * 0.5), Domain.WorldMin.Y + WorldSize.Y * (0.5 + OffsetY * 0.5));
		const double Radius = FMath::Max(Reference * 0.5 * Scale, UE_DOUBLE_SMALL_NUMBER);
		for (int32 Y = 0; Y < Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Domain.Dimensions.X; ++X)
			{
				const FVector2d W = Domain.InteriorSampleToWorld(X, Y);
				const double Distance = FVector2d::Distance(W, Center) / Radius;
				Height.AtInterior(X, Y) = FMath::Clamp(1.0f - static_cast<float>(Distance), 0.0f, 1.0f) * PeakHeight;
			}
		}
		FEonformTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Height))) { Error = TEXT("RadialGradient could not publish Height."); return false; }
		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), ResolveHeightScale(Node, Context)));
		return true;
	}
}

void RegisterEonformRadialGradientNode()
{
	FEonformTerrainNodeDescriptor D;
	D.Type = EonformTerrainNodeTypes::RadialGradient;
	D.DisplayName = TEXT("RadialGradient");
	D.Category = TEXT("Primitive");
	D.Description = TEXT("Generates circular falloff geometry at the active graph resolution and physical domain.");
	D.Outputs.Add(TerrainPort(TEXT("Out")));
	D.Parameters = {
		Number(TEXT("Scale"), TEXT("Scale"), 0.5, 0.001, 4.0),
		Number(TEXT("Height"), TEXT("Height"), 1.0, -4.0, 4.0),
		Number(TEXT("X"), TEXT("X"), 0.0, -2.0, 2.0),
		Number(TEXT("Y"), TEXT("Y"), 0.0, -2.0, 2.0)
	};
	FEonformTerrainNodeDescriptorRegistry::Register(D);
	FEonformTerrainNodeRegistry::Register(D.Type, EvaluateRadialGradientNode);
}
