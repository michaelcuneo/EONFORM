#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainNodeDescriptor.h"
#include "GaeaTerrainRecipe.h"
#include "Misc/DelayedAutoRegister.h"

namespace
{
	const FName ApplyColorType(TEXT("ApplyColor"));

	FGaeaTerrainPortDescriptor Port(FName Name, const TCHAR* Label, FName Type)
	{
		FGaeaTerrainPortDescriptor P;
		P.Name = Name;
		P.DisplayName = Label;
		P.DataType = Type;
		return P;
	}

	FGaeaScalarField MakeChannel(const FGaeaColorField& Color, FName Name, int32 Channel)
	{
		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = Name;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;

		FGaeaScalarField Field;
		Field.Initialize(Color.Domain, Descriptor, 0.0f);
		for (int32 Y = 0; Y < Color.Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Color.Domain.Dimensions.X; ++X)
			{
				const FLinearColor C = Color.AtInterior(X, Y);
				const float Value = Channel == 0 ? C.R : (Channel == 1 ? C.G : C.B);
				Field.AtInterior(X, Y) = FMath::Clamp(Value, 0.0f, 1.0f);
			}
		}
		return Field;
	}

	bool EvaluateApplyColor(
		const FGaeaTerrainNode&,
		const FGaeaTerrainNodeInputs& Inputs,
		const FGaeaTerrainEvaluationContext&,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FGaeaTerrainValue* const* TerrainPtr = Inputs.Find(TEXT("Terrain"));
		const FGaeaTerrainValue* const* ColorPtr = Inputs.Find(TEXT("Color"));
		const FGaeaTerrainValue* Terrain = TerrainPtr ? *TerrainPtr : nullptr;
		const FGaeaTerrainValue* Color = ColorPtr ? *ColorPtr : nullptr;
		if (!Terrain || Terrain->Type != EGaeaTerrainValueType::Terrain || !Terrain->IsValid())
		{
			Error = TEXT("Apply Color requires a valid Terrain input.");
			return false;
		}
		if (!Color || Color->Type != EGaeaTerrainValueType::Color || !Color->ColorField.IsValid())
		{
			Error = TEXT("Apply Color requires a valid Color input.");
			return false;
		}

		const FGaeaScalarField* Height = Terrain->TerrainDataset.FindScalarField(GaeaTerrainFieldNames::Height);
		if (!Height || !Height->IsValid())
		{
			Error = TEXT("Apply Color terrain has no valid Height field.");
			return false;
		}
		if (Color->ColorField.Domain != Height->Domain)
		{
			Error = TEXT("Apply Color requires Color and Terrain to use the same grid domain.");
			return false;
		}

		FGaeaTerrainDataset Dataset = Terrain->TerrainDataset;
		FGaeaScalarField R = MakeChannel(Color->ColorField, GaeaTerrainFieldNames::BaseColorR, 0);
		FGaeaScalarField G = MakeChannel(Color->ColorField, GaeaTerrainFieldNames::BaseColorG, 1);
		FGaeaScalarField B = MakeChannel(Color->ColorField, GaeaTerrainFieldNames::BaseColorB, 2);

		// These colors describe the current surface. Any later Height mutation should
		// invalidate them rather than silently rendering stale color on new geometry.
		if (!Dataset.SetHeightDerivedScalarField(MoveTemp(R))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(G))
			|| !Dataset.SetHeightDerivedScalarField(MoveTemp(B)))
		{
			Error = TEXT("Apply Color could not publish terrain BaseColor fields.");
			return false;
		}

		FGaeaTerrainValue Result = FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), Terrain->HeightScale);
		if (!Result.IsValid())
		{
			Error = TEXT("Apply Color produced invalid terrain.");
			return false;
		}
		Out.Outputs.Add(TEXT("Terrain"), MoveTemp(Result));
		Error.Reset();
		return true;
	}

	void RegisterApplyColorNode()
	{
		FGaeaTerrainNodeDescriptor D;
		D.Type = ApplyColorType;
		D.DisplayName = TEXT("Apply Color");
		D.Category = TEXT("Colorize");
		D.Description = TEXT("Binds a Color field to Terrain as renderable BaseColor data for preview and Mesh Terrain output.");
		D.Inputs.Add(Port(TEXT("Terrain"), TEXT("Terrain"), TEXT("Terrain")));
		D.Inputs.Add(Port(TEXT("Color"), TEXT("Color"), TEXT("Color")));
		D.Outputs.Add(Port(TEXT("Terrain"), TEXT("Terrain"), TEXT("Terrain")));
		FGaeaTerrainNodeDescriptorRegistry::Register(D);
		FGaeaTerrainNodeRegistry::Register(D.Type, EvaluateApplyColor);
	}
}

static FDelayedAutoRegisterHelper GApplyColorAutoRegister(
	EDelayedRegisterRunPhase::EndOfEngineInit,
	[]()
	{
		RegisterApplyColorNode();
	});
