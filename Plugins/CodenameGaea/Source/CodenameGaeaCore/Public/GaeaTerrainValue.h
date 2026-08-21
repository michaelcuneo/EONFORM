#pragma once

#include "CoreMinimal.h"
#include "GaeaColorField.h"
#include "GaeaScalarField.h"
#include "GaeaTerrainDataset.h"

enum class EGaeaTerrainValueType : uint8
{
	Invalid,
	Terrain,
	ScalarField,
	Color
};

/** A named value traveling across a terrain graph connection. */
struct CODENAMEGAEACORE_API FGaeaTerrainValue
{
	EGaeaTerrainValueType Type = EGaeaTerrainValueType::Invalid;
	float HeightScale = 1000.0f;
	FGaeaTerrainDataset TerrainDataset;
	FGaeaScalarField ScalarField;
	FGaeaColorField ColorField;

	static FGaeaTerrainValue MakeTerrain(const FGaeaTerrainDataset& Dataset, float InHeightScale)
	{
		FGaeaTerrainValue Value;
		Value.Type = EGaeaTerrainValueType::Terrain;
		Value.HeightScale = InHeightScale;
		Value.TerrainDataset = Dataset;
		return Value;
	}

	static FGaeaTerrainValue MakeTerrain(FGaeaTerrainDataset&& Dataset, float InHeightScale)
	{
		FGaeaTerrainValue Value;
		Value.Type = EGaeaTerrainValueType::Terrain;
		Value.HeightScale = InHeightScale;
		Value.TerrainDataset = MoveTemp(Dataset);
		return Value;
	}

	static FGaeaTerrainValue MakeScalarField(const FGaeaScalarField& Field)
	{
		FGaeaTerrainValue Value;
		Value.Type = EGaeaTerrainValueType::ScalarField;
		Value.ScalarField = Field;
		return Value;
	}

	static FGaeaTerrainValue MakeScalarField(FGaeaScalarField&& Field)
	{
		FGaeaTerrainValue Value;
		Value.Type = EGaeaTerrainValueType::ScalarField;
		Value.ScalarField = MoveTemp(Field);
		return Value;
	}

	static FGaeaTerrainValue MakeColor(const FGaeaColorField& Field)
	{
		FGaeaTerrainValue Value;
		Value.Type = EGaeaTerrainValueType::Color;
		Value.ColorField = Field;
		return Value;
	}

	static FGaeaTerrainValue MakeColor(FGaeaColorField&& Field)
	{
		FGaeaTerrainValue Value;
		Value.Type = EGaeaTerrainValueType::Color;
		Value.ColorField = MoveTemp(Field);
		return Value;
	}

	bool IsValid() const
	{
		switch (Type)
		{
		case EGaeaTerrainValueType::Terrain:
			return HeightScale > UE_SMALL_NUMBER && !TerrainDataset.IsEmpty();
		case EGaeaTerrainValueType::ScalarField:
			return ScalarField.IsValid();
		case EGaeaTerrainValueType::Color:
			return ColorField.IsValid();
		default:
			return false;
		}
	}
};
