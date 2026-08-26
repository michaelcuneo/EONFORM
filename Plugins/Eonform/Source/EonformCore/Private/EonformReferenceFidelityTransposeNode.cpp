#include "EonformReferenceFidelityTransposeNode.h"

#include "EonformScalarField.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFidelityNodes.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"

namespace
{
	FEonformTerrainPortDescriptor Port(FName Name, const TCHAR* Label, FName Type)
	{
		FEonformTerrainPortDescriptor P; P.Name = Name; P.DisplayName = Label; P.DataType = Type; return P;
	}
	FEonformTerrainParameterDescriptor Num(FName Name, const TCHAR* Label, double Default, double Min, double Max)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Number; P.DefaultNumber = Default; P.bHasMinimum = true; P.Minimum = Min; P.bHasMaximum = true; P.Maximum = Max; return P;
	}
	FEonformTerrainParameterDescriptor Bool(FName Name, const TCHAR* Label, bool Default)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Boolean; P.DefaultBoolean = Default; return P;
	}
	FEonformTerrainParameterDescriptor Choice(FName Name, const TCHAR* Label, FName Default, std::initializer_list<FName> Options)
	{
		FEonformTerrainParameterDescriptor P; P.Name = Name; P.DisplayName = Label; P.Type = EEonformTerrainParameterType::Name; P.DefaultName = Default; for (const FName O : Options) P.NameOptions.Add(O); return P;
	}

	const FEonformTerrainValue* Input(const FEonformTerrainNodeInputs& Inputs, FName Name)
	{
		const FEonformTerrainValue* const* P = Inputs.Find(Name); return P ? *P : nullptr;
	}
	const FEonformScalarField* Field(const FEonformTerrainValue* V)
	{
		if (!V || !V->IsValid()) return nullptr;
		if (V->Type == EEonformTerrainValueType::ScalarField) return &V->ScalarField;
		if (V->Type == EEonformTerrainValueType::Terrain) return V->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		return nullptr;
	}
	float Mirror(float V, int32 Max)
	{
		if (Max <= 0) return 0.0f;
		const float Period = static_cast<float>(Max * 2);
		float R = FMath::Fmod(V, Period); if (R < 0.0f) R += Period;
		return R > Max ? Period - R : R;
	}
	float Sample(const FEonformScalarField& F, float X, float Y, bool bMirror)
	{
		const int32 W = F.Domain.Dimensions.X, H = F.Domain.Dimensions.Y;
		if (bMirror) { X = Mirror(X, W - 1); Y = Mirror(Y, H - 1); }
		else { X = FMath::Clamp(X, 0.0f, static_cast<float>(W - 1)); Y = FMath::Clamp(Y, 0.0f, static_cast<float>(H - 1)); }
		const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
		const int32 X1 = FMath::Min(X0 + 1, W - 1), Y1 = FMath::Min(Y0 + 1, H - 1);
		const float TX = X - X0, TY = Y - Y0;
		return FMath::Lerp(FMath::Lerp(F.AtInterior(X0, Y0), F.AtInterior(X1, Y0), TX), FMath::Lerp(F.AtInterior(X0, Y1), F.AtInterior(X1, Y1), TX), TY);
	}
	float LocalMean(const FEonformScalarField& F, int32 X, int32 Y, int32 Radius)
	{
		float Sum = 0.0f; int32 Count = 0;
		for (int32 DY = -Radius; DY <= Radius; ++DY) for (int32 DX = -Radius; DX <= Radius; ++DX)
		{
			Sum += Sample(F, static_cast<float>(X + DX), static_cast<float>(Y + DY), true); ++Count;
		}
		return Count > 0 ? Sum / Count : F.AtInterior(X, Y);
	}

	bool EvaluateTranspose(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* InputValue = Input(Inputs, TEXT("Input"));
		const FEonformTerrainValue* ReferenceValue = Input(Inputs, TEXT("Reference"));
		const FEonformScalarField* Base = Field(InputValue);
		const FEonformScalarField* Ref = Field(ReferenceValue);
		if (!InputValue || InputValue->Type != EEonformTerrainValueType::Terrain || !Base || !Ref)
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

		FEonformScalarField Result = *Base;
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

		Result.Descriptor.Name = EonformTerrainFieldNames::Height;
		FEonformTerrainDataset Dataset = InputValue->TerrainDataset;
		if (!Dataset.SetScalarField(MoveTemp(Result))) { Error = TEXT("Transpose could not publish Height."); return false; }
		FEonformTerrainValue Terrain = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), InputValue->HeightScale);
		if (!Terrain.IsValid()) { Error = TEXT("Transpose produced invalid terrain output."); return false; }
		Out.Outputs.Add(TEXT("Out"), MoveTemp(Terrain));
		return true;
	}
}

void RegisterEonformReferenceFidelityTransposeNode()
{
	FEonformTerrainNodeDescriptor D;
	D.Type = EonformTerrainNodeTypes::Transpose;
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
	FEonformTerrainNodeDescriptorRegistry::Register(D);
	FEonformTerrainNodeRegistry::Register(D.Type, EvaluateTranspose);

	// This function is already called after the legacy/reference node families.
	// Register corrected shared terrain primitives here so they are authoritative.
	RegisterEonformTerrainFidelityNodes();
}
