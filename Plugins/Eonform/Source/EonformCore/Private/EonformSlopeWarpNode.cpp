#include "EonformSlopeWarpNode.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor Port(FName Name, const TCHAR* Label)
	{
		FEonformTerrainPortDescriptor P; P.Name = Name; P.DisplayName = Label; P.DataType = TEXT("Any"); return P;
	}
	FEonformTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Number; P.DefaultNumber = Default; P.bHasMinimum = true; P.Minimum = Min; P.bHasMaximum = true; P.Maximum = Max; return P;
	}
	FEonformTerrainParameterDescriptor Int(FName Name, const TCHAR* Label, int64 Default, int64 Min, int64 Max)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Integer; P.DefaultInteger = Default; P.bHasMinimum = true; P.Minimum = static_cast<double>(Min); P.bHasMaximum = true; P.Maximum = static_cast<double>(Max); return P;
	}
	FEonformTerrainParameterDescriptor Bool(FName Name, const TCHAR* Label, bool Default)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Boolean; P.DefaultBoolean = Default; return P;
	}
	FEonformTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Options, const TCHAR* Group = nullptr)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Name; P.DefaultName = Default; if (Group) P.Group = Group; for (const FName O : Options) P.NameOptions.Add(O); return P;
	}
	const FEonformTerrainValue* Input(const FEonformTerrainNodeInputs& Inputs, FName Name)
	{
		const FEonformTerrainValue* const* P = Inputs.Find(Name); return P ? *P : nullptr;
	}
	const FEonformScalarField* AsField(const FEonformTerrainValue* V)
	{
		if (!V || !V->IsValid()) return nullptr;
		if (V->Type == EEonformTerrainValueType::ScalarField) return &V->ScalarField;
		if (V->Type == EEonformTerrainValueType::Terrain) return V->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		return nullptr;
	}
	float Sample(const FEonformScalarField& F, int32 X, int32 Y)
	{
		return F.AtInterior(FMath::Clamp(X, 0, F.Domain.Dimensions.X - 1), FMath::Clamp(Y, 0, F.Domain.Dimensions.Y - 1));
	}
	float Bilinear(const FEonformScalarField& F, float X, float Y)
	{
		X = FMath::Clamp(X, 0.0f, static_cast<float>(F.Domain.Dimensions.X - 1)); Y = FMath::Clamp(Y, 0.0f, static_cast<float>(F.Domain.Dimensions.Y - 1));
		const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y); const int32 X1 = FMath::Min(X0 + 1, F.Domain.Dimensions.X - 1), Y1 = FMath::Min(Y0 + 1, F.Domain.Dimensions.Y - 1);
		const float TX = X - X0, TY = Y - Y0;
		return FMath::Lerp(FMath::Lerp(F.AtInterior(X0,Y0), F.AtInterior(X1,Y0), TX), FMath::Lerp(F.AtInterior(X0,Y1), F.AtInterior(X1,Y1), TX), TY);
	}
	void GradientAt(const FEonformScalarField& Field, int32 X, int32 Y, int32 Radius, float& GX, float& GY)
	{
		GX = 0.5f * (Sample(Field, X + Radius, Y) - Sample(Field, X - Radius, Y)) / FMath::Max(Radius, 1);
		GY = 0.5f * (Sample(Field, X, Y + Radius) - Sample(Field, X, Y - Radius)) / FMath::Max(Radius, 1);
	}
	bool PublishLike(const FEonformTerrainValue& Prototype, FEonformScalarField&& Field, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		if (Prototype.Type == EEonformTerrainValueType::ScalarField) { Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeScalarField(MoveTemp(Field))); return true; }
		if (Prototype.Type == EEonformTerrainValueType::Terrain)
		{
			Field.Descriptor.Name = EonformTerrainFieldNames::Height; FEonformTerrainDataset Dataset = Prototype.TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(Field))) { Error = TEXT("SlopeWarp could not publish Height."); return false; }
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Prototype.HeightScale)); return true;
		}
		Error = TEXT("SlopeWarp received unsupported input type."); return false;
	}
	bool EvaluateSlopeWarp(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* SourceValue = Input(Inputs, TEXT("Input")); const FEonformScalarField* Source = AsField(SourceValue);
		if (!SourceValue || !Source) { Error = TEXT("SlopeWarp requires Input."); return false; }
		const FEonformScalarField* GuideInput = AsField(Input(Inputs, TEXT("Guide")));
		if (GuideInput && GuideInput->Domain != Source->Domain) { Error = TEXT("SlopeWarp Guide must share the Input domain."); return false; }
		const FEonformScalarField& Guide = GuideInput ? *GuideInput : *Source;
		const float Intensity = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Intensity"), 0.2)), 0.0f, 1.0f);
		const int32 Iterations = FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Iterations"), 1)), 1, 16);
		const float DirectionRadians = FMath::DegreesToRadians(static_cast<float>(Node.GetNumber(TEXT("Direction"), 0.0)));
		const bool bNormalized = Node.GetBool(TEXT("Normalized"), true);
		const FName Quality = Node.GetName(TEXT("Quality"), TEXT("High")); const FName Antialiasing = Node.GetName(TEXT("Antialiasing"), TEXT("X 4"));
		const int32 GradientRadius = Quality == TEXT("Low") ? 1 : Quality == TEXT("Medium") ? 2 : Quality == TEXT("Ultra") ? 4 : 3;
		const int32 AASamples = Antialiasing == TEXT("Off") ? 1 : Antialiasing == TEXT("X 16") ? 16 : 4;
		const float CS = FMath::Cos(DirectionRadians), SN = FMath::Sin(DirectionRadians);
		const float MaxDisplacement = Intensity * FMath::Min(Source->Domain.Dimensions.X, Source->Domain.Dimensions.Y) * 0.035f;
		FEonformScalarField Current = *Source;
		for (int32 Iter = 0; Iter < Iterations; ++Iter)
		{
			FEonformScalarField Next = Current;
			for (int32 Y = 0; Y < Current.Domain.Dimensions.Y; ++Y) for (int32 X = 0; X < Current.Domain.Dimensions.X; ++X)
			{
				float GX = 0.0f, GY = 0.0f; GradientAt(Guide, X, Y, GradientRadius, GX, GY); const float Magnitude = FMath::Sqrt(GX * GX + GY * GY); if (Magnitude <= UE_SMALL_NUMBER) continue;
				float DX = GX, DY = GY; if (bNormalized) { DX /= Magnitude; DY /= Magnitude; }
				const float RX = DX * CS - DY * SN, RY = DX * SN + DY * CS;
				const float D = MaxDisplacement * (bNormalized ? 1.0f : FMath::Clamp(Magnitude * 16.0f, 0.0f, 1.0f)); const float TX = X - RX * D, TY = Y - RY * D;
				if (AASamples == 1) Next.AtInterior(X,Y) = Bilinear(Current, TX, TY);
				else
				{
					const FVector2D Tangent(-RY, RX); float Sum = 0.0f;
					for (int32 S = 0; S < AASamples; ++S) { const float Offset = (static_cast<float>(S) + 0.5f) / AASamples - 0.5f; Sum += Bilinear(Current, TX + Tangent.X * Offset, TY + Tangent.Y * Offset); }
					Next.AtInterior(X,Y) = Sum / AASamples;
				}
			}
			Current = MoveTemp(Next);
		}
		return PublishLike(*SourceValue, MoveTemp(Current), Out, Error);
	}
}

void RegisterEonformAuthoritativeSlopeWarpNode()
{
	FEonformTerrainNodeDescriptor D; D.Type = EonformTerrainNodeTypes::SlopeWarp; D.DisplayName = TEXT("SlopeWarp"); D.Category = TEXT("Modify"); D.Description = TEXT("Applies directional warping based on slopes of the input or Guide field with selectable precision and antialiasing.");
	D.Inputs.Add(Port(TEXT("Input"),TEXT("Input"))); D.Inputs.Add(Port(TEXT("Guide"),TEXT("Guide"))); D.Outputs.Add(Port(TEXT("Out"),TEXT("Out")));
	D.Parameters = { Num(TEXT("Intensity"),TEXT("Intensity"),0.2,0.0,1.0), Int(TEXT("Iterations"),TEXT("Iterations"),1,1,16), Num(TEXT("Direction"),TEXT("Direction"),0.0,-360.0,360.0), Bool(TEXT("Normalized"),TEXT("Normalized"),true), Choice(TEXT("Quality"),TEXT("Quality"),TEXT("High"),{TEXT("Low"),TEXT("Medium"),TEXT("High"),TEXT("Ultra")},TEXT("Quality")), Choice(TEXT("Antialiasing"),TEXT("Antialiasing"),TEXT("X 4"),{TEXT("Off"),TEXT("X 4"),TEXT("X 16")},TEXT("Quality")) };
	FEonformTerrainNodeDescriptorRegistry::Register(D); FEonformTerrainNodeRegistry::Register(D.Type, EvaluateSlopeWarp);
}
