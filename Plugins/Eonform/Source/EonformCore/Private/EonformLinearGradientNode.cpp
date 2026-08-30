#include "EonformLinearGradientNode.h"

#include "EonformGridDomain.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor LinearGradientTerrainPort(FName Name)
	{
		FEonformTerrainPortDescriptor Port;
		Port.Name = Name;
		Port.DisplayName = TEXT("Out");
		Port.DataType = TEXT("Terrain");
		return Port;
	}
	FEonformTerrainParameterDescriptor LinearGradientNumber(FName Name, const TCHAR *Label, double Default, double Min, double Max)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Number;
		P.DefaultNumber = Default;
		P.bHasMinimum = true;
		P.Minimum = Min;
		P.bHasMaximum = true;
		P.Maximum = Max;
		return P;
	}
	FEonformTerrainParameterDescriptor LinearGradientName(FName Name, const TCHAR *Label, FName Default, std::initializer_list<FName> Options)
	{
		FEonformTerrainParameterDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.Type = EEonformTerrainParameterType::Name;
		P.DefaultName = Default;
		for (const FName Option : Options)
			P.NameOptions.Add(Option);
		return P;
	}
	float LinearGradientApplyEdge(float V, FName EdgeBehavior)
	{
		if (EdgeBehavior == TEXT("Repeat"))
			return FMath::Frac(V);
		if (EdgeBehavior == TEXT("Mirror"))
		{
			const float T = FMath::Frac(V * 0.5f) * 2.0f;
			return T <= 1.0f ? T : 2.0f - T;
		}
		return FMath::Clamp(V, 0.0f, 1.0f);
	}
	bool EvaluateLinearGradientNode(const FEonformTerrainNode &Node, const FEonformTerrainNodeInputs &, const FEonformTerrainEvaluationContext &, FEonformTerrainNodeEvaluation &Out, FString &Error)
	{
		const int32 Resolution = FMath::Clamp<int32>(static_cast<int32>(Node.GetInteger(TEXT("Resolution"), 257)), 2, 4097);
		const float WorldSize = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("WorldSize"), 100000.0)), 1.0f, 10000000.0f);
		const float HeightScale = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("HeightScale"), 8000.0)), 1.0f, 1000000.0f);
		const float Scale = FMath::Max(static_cast<float>(Node.GetNumber(TEXT("Scale"), 1.0)), 0.001f);
		const float Direction = static_cast<float>(Node.GetNumber(TEXT("Direction"), 0.0));
		const FName EdgeBehavior = Node.GetName(TEXT("EdgeBehavior"), TEXT("Clip"));
		const double Half = static_cast<double>(WorldSize) * 0.5;
		const FEonformGridDomain Domain = FEonformGridDomain::Make(FIntPoint(Resolution, Resolution), FVector2d(-Half, -Half), FVector2d(Half, Half));
		if (!Domain.IsValid())
		{
			Error = TEXT("LinearGradient produced an invalid grid domain.");
			return false;
		}
		FEonformFieldDescriptor D;
		D.Name = EonformTerrainFieldNames::Height;
		D.Unit = EEonformFieldUnit::Normalized;
		D.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Field;
		Field.Initialize(Domain, D);
		const float A = FMath::DegreesToRadians(Direction);
		const FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const FVector2d W = Domain.InteriorSampleToWorld(X, Y);
				const FVector2D N(static_cast<float>(W.X / WorldSize), static_cast<float>(W.Y / WorldSize));
				const float V = 0.5f + FVector2D::DotProduct(N, Dir) / Scale;
				Field.AtInterior(X, Y) = LinearGradientApplyEdge(V, EdgeBehavior);
			}
		}
		FEonformTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Field)))
		{
			Error = TEXT("LinearGradient could not publish Height.");
			return false;
		}
		FEonformTerrainValue Result = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("LinearGradient produced invalid terrain.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Result));
		return true;
	}
}

void RegisterEonformLinearGradientNode()
{
	FEonformTerrainNodeDescriptor Descriptor;
	Descriptor.Type = EonformTerrainNodeTypes::LinearGradient;
	Descriptor.DisplayName = TEXT("LinearGradient");
	Descriptor.Category = TEXT("Primitive");
	Descriptor.Description = TEXT("Generates a straight linear gradient across the terrain.");
	Descriptor.Outputs.Add(LinearGradientTerrainPort(TEXT("Out")));
	Descriptor.Parameters.Add(LinearGradientNumber(TEXT("Scale"), TEXT("Scale"), 1.0, 0.001, 10.0));
	Descriptor.Parameters.Add(LinearGradientNumber(TEXT("Direction"), TEXT("Direction"), 0.0, -360.0, 360.0));
	Descriptor.Parameters.Add(LinearGradientName(TEXT("EdgeBehavior"), TEXT("Edge Behavior"), TEXT("Clip"), {TEXT("Clip"), TEXT("Repeat"), TEXT("Mirror")}));
	FEonformTerrainNodeDescriptorRegistry::Register(Descriptor);
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::LinearGradient, EvaluateLinearGradientNode);
}
