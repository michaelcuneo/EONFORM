#include "GaeaReferenceFidelityTransposeNode.h"

#include "GaeaScalarField.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFidelityNodes.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"

namespace
{
	FGaeaTerrainPortDescriptor Port(FName Name, const TCHAR* Label, FName Type)
	{
		FGaeaTerrainPortDescriptor P; P.Name = Name; P.DisplayName = Label; P.DataType = Type; return P;
	}
	FGaeaTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max)
	{
		FGaeaTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EGaeaTerrainParameterType::Number; P.DefaultNumber = Default; P.bHasMinimum = true; P.Minimum = Min; P.bHasMaximum = true; P.Maximum = Max; return P;
	}
	FGaeaTerrainParameterDescriptor Bool(FName Name, const TCHAR* Label, bool Default)
	{
		FGaeaTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EGaeaTerrainParameterType::Boolean; P.DefaultBoolean = Default; return P;
	}
	FGaeaTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Options)
	{
		FGaeaTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EGaeaTerrainParameterType::Name; P.DefaultName = Default; for (const FName O : Options) P.NameOptions.Add(O); return P;
	}

	const FGaeaTerrainValue* Input(const FGaeaTerrainNodeInputs& Inputs, FName Name)
	{
		const FGaeaTerrainValue* const* P = Inputs.Find(Name); return P ? *P : nullptr;
	}
	const FGaeaScalarField* Field(const FGaeaTerrainValue* V)
	{
		if (!V || !V->IsValid()) return nullptr;
		if (V->Type == EGaeaTerrainValueType::ScalarField) return &V->ScalarField;
		if (V->Type == EGaeaTerrainValueType::Terrain) return V->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		return nullptr;
	}
	float Mirror(float V, int32 Max)
	{
		if (Max <= 0) return 0.0f;
		const float Period = static_cast<float>(Max * 2);
		float R = FMath::Fmod(V, Period); if (R < 0.0f) R += Period;
		return R > Max ? Period - R : R;
	}
	float Sample(const FGaeaScalarField& F, float X, float Y, bool bMirror)
	{
		const int32 W = F.Domain.Dimensions.X, H = F.Domain.Dimensions.Y;
		if (bMirror) { X = Mirror(X, W - 1); Y = Mirror(Y, H - 1); }
		else { X = FMath::Clamp(X, 0.0f, static_cast<float>(W - 1)); Y = FMath::Clamp(Y, 0.0f, static_cast<float>(H - 1)); }
		const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
		const int32 X1 = FMath::Min(X0 + 1, W - 1), Y1 = FMath::Min(Y0 + 1, H - 1);
		const float TX = X - X0, TY = Y - Y0;
		return FMath::Lerp(FMath::Lerp(F.AtInterior(X0, Y0), F.AtInterior(X1, Y0), TX), FMath::Lerp(F.AtInterior(X0, Y1), F.AtInterior(X1, Y1), TX), TY);
	}
	float LocalMean(const FGaeaScalarField& F, int32 X, int32 Y, int32 Radius)
	{
		float Sum = 0.0f; int32 Count = 0;
		for (int32 DY = -Radius; DY <= Radius; ++DY) for (int32 DX = -Radius; DX <= Radius; ++DX)
		{
			Sum += Sample(F, static_cast<float>(X + DX), static_cast<float>(Y + DY), true); ++Count;
		}
		return Count > 0 ? Sum / Count : F.AtInterior(X, Y);
	}

	bool EvaluateTranspose(const FGaeaTerrainNode& Node, const FGaeaTerrainNodeInputs& Inputs, const FGaeaTerrainEvaluationContext&, FGaeaTerrainNodeEvaluation& Out, FString& Error)
	{
		const FGaeaTerrainValue* InputValue = Input(Inputs, TEXT("Input"));
		const FGaeaTerrainValue* ReferenceValue = Input(Inputs, TEXT("Reference"));
		const FGaeaScalarField* Base = Field(InputValue);
		const FGaeaScalarField* Ref = Field(ReferenceValue);
		if (!InputValue || InputValue->Type != EGaeaTerrainValueType::Terrain || !Base || !Ref)
		{
			Error = TEXT("Transpose requires a terrain Input and a terrain/scalar Reference."); return false;
		}
		if (Base->Domain != Ref->Domain) { Error = TEXT("Transpose Reference must share the Input domain."); return false; }

		const FName Mode = Node.GetName(TEXT("Mode"), TEXT("Transpose"));
		const float Amount = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Amount"), 1.0)), 0.0f, 1.0f);
		const bool bExtend = Node.GetBool(TEXT("Extend"), false);
		const bool bFlatten = Node.GetBool(TEXT("Flatten"), false);
		const float Threshold = FMath::Clamp(static_cast<float>(Node.GetNumber(TEXT("Threshold"), 0.0)), -1.0f, 1.0f);
		const FName Boundary = Node.GetName(TEXT("Boundary"), TEXT("Mirror"));
		const bool bMirror = Boundary == TEXT("Mirror") || bExtend;
		if (Mode != TEXT("Transpose") && Mode != TEXT("Embed") && Mode != TEXT("Insert"))
		{
			Error = TEXT("Transpose Mode must be Transpose, Embed, or Insert."); return false;
		}

		FGaeaScalarField Result = *Base;
		const int32 DetailRadius = FMath::Clamp(FMath::RoundToInt(FMath::Min(Base->Domain.Dimensions.X, Base->Domain.Dimensions.Y) * 0.012f), 2, 18);
		float RefMinimum = TNumericLimits<float>::Max();
		for (const float V : Ref->Values) RefMinimum = FMath::Min(RefMinimum, V);

		for (int32 Y = 0; Y < Base->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Base->Domain.Dimensions.X; ++X)
			{
				const float BaseValue = Base->AtInterior(X, Y);
				const float Reference = Sample(*Ref, static_cast<float>(X), static_cast<float>(Y), bMirror);
				float Target = BaseValue;
				if (Mode == TEXT("Transpose"))
				{
					const float Mean = LocalMean(*Ref, X, Y, DetailRadius);
					Target = BaseValue + (Reference - Mean);
				}
				else if (Mode == TEXT("Embed"))
				{
					const float Baseline = bFlatten ? RefMinimum : LocalMean(*Ref, X, Y, DetailRadius);
					Target = BaseValue + (Reference - Baseline);
				}
				else if (Reference >= Threshold)
				{
					const float Baseline = bFlatten ? Threshold : FMath::Max(Threshold, RefMinimum);
					Target = BaseValue + (Reference - Baseline);
				}
				Result.AtInterior(X, Y) = FMath::Lerp(BaseValue, Target, Amount);
			}
		}

		Result.Descriptor.Name = GaeaTerrainFieldNames::Height;
		FGaeaTerrainDataset Dataset = InputValue->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(Result))) { Error = TEXT("Transpose could not publish Height."); return false; }
		FGaeaTerrainValue Terrain = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), InputValue->HeightScale);
		if (!Terrain.IsValid()) { Error = TEXT("Transpose produced invalid terrain output."); return false; }
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Terrain));
		return true;
	}
}

void RegisterGaeaReferenceFidelityTransposeNode()
{
	FGaeaTerrainNodeDescriptor D;
	D.Type = GaeaTerrainNodeTypes::Transpose;
	D.DisplayName = TEXT("Transpose");
	D.Category = TEXT("Modify");
	D.Description = TEXT("Transfers reference surface character to a target terrain, or embeds/inserts sparse relief while preserving the target's broad form.");
	D.Inputs.Add(Port(TEXT("Input"), TEXT("Input"), TEXT("Terrain")));
	D.Inputs.Add(Port(TEXT("Reference"), TEXT("Reference"), TEXT("Any")));
	D.Outputs.Add(Port(TEXT("Out"), TEXT("Out"), TEXT("Terrain")));
	D.Parameters = {
		Choice(TEXT("Mode"), TEXT("Mode"), TEXT("Transpose"), { TEXT("Transpose"), TEXT("Embed"), TEXT("Insert") }),
		Num(TEXT("Amount"), TEXT("Amount"), 1.0, 0.0, 1.0),
		Bool(TEXT("Extend"), TEXT("Extend"), false),
		Bool(TEXT("Flatten"), TEXT("Flatten"), false),
		Num(TEXT("Threshold"), TEXT("Threshold"), 0.0, -1.0, 1.0),
		Choice(TEXT("Boundary"), TEXT("Boundary"), TEXT("Mirror"), { TEXT("Clamp"), TEXT("Mirror") })
	};
	FGaeaTerrainNodeDescriptorRegistry::Register(D);
	FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateTranspose);

	// This function is already called after the legacy/reference node families.
	// Register corrected shared terrain primitives here so they are authoritative.
	RegisterGaeaTerrainFidelityNodes();
}
