#pragma once

#include "CoreMinimal.h"
#include "EonformColorField.h"
#include "EonformScalarField.h"
#include "EonformTerrainDataset.h"

enum class EEonformTerrainValueType : uint8
{
	Invalid,
	Terrain,
	ScalarField,
	Color
};

/** A named value traveling across a terrain graph connection. */
struct EONFORMCORE_API FEonformTerrainValue
{
	EEonformTerrainValueType Type = EEonformTerrainValueType::Invalid;
	float HeightScale = 1000.0f;
	FEonformTerrainDataset TerrainDataset;
	FEonformScalarField ScalarField;
	FEonformColorField ColorField;

	static FEonformTerrainValue MakeTerrain(const FEonformTerrainDataset& Dataset, float InHeightScale)
	{
		FEonformTerrainValue Value;
		Value.Type = EEonformTerrainValueType::Terrain;
		Value.HeightScale = InHeightScale;
		Value.TerrainDataset = Dataset;
		return Value;
	}

	static FEonformTerrainValue MakeTerrain(FEonformTerrainDataset&& Dataset, float InHeightScale)
	{
		FEonformTerrainValue Value;
		Value.Type = EEonformTerrainValueType::Terrain;
		Value.HeightScale = InHeightScale;
		Value.TerrainDataset = MoveTemp(Dataset);
		return Value;
	}

	static FEonformTerrainValue MakeScalarField(const FEonformScalarField& Field)
	{
		FEonformTerrainValue Value;
		Value.Type = EEonformTerrainValueType::ScalarField;
		Value.ScalarField = Field;
		return Value;
	}

	static FEonformTerrainValue MakeScalarField(FEonformScalarField&& Field)
	{
		FEonformTerrainValue Value;
		Value.Type = EEonformTerrainValueType::ScalarField;
		Value.ScalarField = MoveTemp(Field);
		return Value;
	}

	static FEonformTerrainValue MakeColor(const FEonformColorField& Field)
	{
		FEonformTerrainValue Value;
		Value.Type = EEonformTerrainValueType::Color;
		Value.ColorField = Field;
		return Value;
	}

	static FEonformTerrainValue MakeColor(FEonformColorField&& Field)
	{
		FEonformTerrainValue Value;
		Value.Type = EEonformTerrainValueType::Color;
		Value.ColorField = MoveTemp(Field);
		return Value;
	}

	bool IsValid() const
	{
		switch (Type)
		{
		case EEonformTerrainValueType::Terrain:
			return HeightScale > UE_SMALL_NUMBER && !TerrainDataset.IsEmpty();
		case EEonformTerrainValueType::ScalarField:
			return ScalarField.IsValid();
		case EEonformTerrainValueType::Color:
			return ColorField.IsValid();
		default:
			return false;
		}
	}
};
