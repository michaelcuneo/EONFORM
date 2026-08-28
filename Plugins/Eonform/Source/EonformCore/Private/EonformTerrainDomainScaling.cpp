#include "EonformTerrainDomainScaling.h"

#include "EonformTerrainFieldNames.h"

namespace EonformTerrainDomainScaling
{
	namespace
	{
		const FEonformGridDomain* FindValueDomain(const FEonformTerrainValue& Value)
		{
			switch (Value.Type)
			{
			case EEonformTerrainValueType::Terrain:
			{
				if (const FEonformScalarField* Height = Value.TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height))
				{
					if (Height->IsValid()) return &Height->Domain;
				}
				TArray<FName> Names;
				Value.TerrainDataset.GetScalarFieldNames(Names);
				Names.Sort(FNameLexicalLess());
				for (const FName Name : Names)
				{
					if (const FEonformScalarField* Field = Value.TerrainDataset.FindScalarField(Name))
					{
						if (Field->IsValid()) return &Field->Domain;
					}
				}
				return nullptr;
			}
			case EEonformTerrainValueType::ScalarField:
				return Value.ScalarField.IsValid() ? &Value.ScalarField.Domain : nullptr;
			case EEonformTerrainValueType::Color:
				return Value.ColorField.IsValid() ? &Value.ColorField.Domain : nullptr;
			default:
				return nullptr;
			}
		}

		FEonformGridDomain BuildTargetDomain(
			const FEonformGridDomain& Fallback,
			const FEonformTerrainEvaluationContext& Context)
		{
			const bool bExplicitResolution = Context.TargetResolution.X > 1 && Context.TargetResolution.Y > 1;
			return Context.ResolveTargetDomain(
				Fallback.Dimensions,
				Fallback.WorldMin,
				Fallback.WorldMax,
				bExplicitResolution ? 0 : Fallback.BorderSamples);
		}

		float SampleScalarStorage(
			const FEonformScalarField& Source,
			double SX,
			double SY)
		{
			const FIntPoint SourceStorage = Source.Domain.GetStorageDimensions();
			SX = FMath::Clamp(SX, 0.0, static_cast<double>(SourceStorage.X - 1));
			SY = FMath::Clamp(SY, 0.0, static_cast<double>(SourceStorage.Y - 1));

			if (Source.Descriptor.Interpolation == EEonformInterpolation::Nearest)
			{
				return Source.AtStorage(
					FMath::Clamp(FMath::RoundToInt(SX), 0, SourceStorage.X - 1),
					FMath::Clamp(FMath::RoundToInt(SY), 0, SourceStorage.Y - 1));
			}

			const int32 X0 = FMath::Clamp(FMath::FloorToInt(SX), 0, SourceStorage.X - 1);
			const int32 Y0 = FMath::Clamp(FMath::FloorToInt(SY), 0, SourceStorage.Y - 1);
			const int32 X1 = FMath::Min(X0 + 1, SourceStorage.X - 1);
			const int32 Y1 = FMath::Min(Y0 + 1, SourceStorage.Y - 1);
			const double TX = FMath::Clamp(SX - static_cast<double>(X0), 0.0, 1.0);
			const double TY = FMath::Clamp(SY - static_cast<double>(Y0), 0.0, 1.0);

			const double Top = FMath::Lerp(
				static_cast<double>(Source.AtStorage(X0, Y0)),
				static_cast<double>(Source.AtStorage(X1, Y0)),
				TX);
			const double Bottom = FMath::Lerp(
				static_cast<double>(Source.AtStorage(X0, Y1)),
				static_cast<double>(Source.AtStorage(X1, Y1)),
				TX);
			return static_cast<float>(FMath::Lerp(Top, Bottom, TY));
		}

		FLinearColor SampleColorStorage(
			const FEonformColorField& Source,
			double SX,
			double SY)
		{
			const FIntPoint SourceStorage = Source.Domain.GetStorageDimensions();
			SX = FMath::Clamp(SX, 0.0, static_cast<double>(SourceStorage.X - 1));
			SY = FMath::Clamp(SY, 0.0, static_cast<double>(SourceStorage.Y - 1));
			const int32 X0 = FMath::Clamp(FMath::FloorToInt(SX), 0, SourceStorage.X - 1);
			const int32 Y0 = FMath::Clamp(FMath::FloorToInt(SY), 0, SourceStorage.Y - 1);
			const int32 X1 = FMath::Min(X0 + 1, SourceStorage.X - 1);
			const int32 Y1 = FMath::Min(Y0 + 1, SourceStorage.Y - 1);
			const float TX = static_cast<float>(FMath::Clamp(SX - static_cast<double>(X0), 0.0, 1.0));
			const float TY = static_cast<float>(FMath::Clamp(SY - static_cast<double>(Y0), 0.0, 1.0));

			const int32 A = Y0 * SourceStorage.X + X0;
			const int32 B = Y0 * SourceStorage.X + X1;
			const int32 C = Y1 * SourceStorage.X + X0;
			const int32 D = Y1 * SourceStorage.X + X1;
			return FMath::Lerp(
				FMath::Lerp(Source.Values[A], Source.Values[B], TX),
				FMath::Lerp(Source.Values[C], Source.Values[D], TX),
				TY);
		}

		bool ResampleColorField(
			const FEonformColorField& Source,
			const FEonformGridDomain& TargetDomain,
			FEonformColorField& OutField,
			FString* OutError)
		{
			if (!Source.IsValid() || !TargetDomain.IsValid())
			{
				if (OutError) *OutError = TEXT("Color-field scaling requires a valid source and target domain.");
				return false;
			}
			if (Source.Domain == TargetDomain)
			{
				OutField = Source;
				if (OutError) OutError->Reset();
				return true;
			}

			OutField.Initialize(TargetDomain);
			const FIntPoint TargetStorage = TargetDomain.GetStorageDimensions();
			for (int32 Y = 0; Y < TargetStorage.Y; ++Y)
			{
				for (int32 X = 0; X < TargetStorage.X; ++X)
				{
					const FVector2d World = TargetDomain.StorageSampleToWorld(X, Y);
					const FVector2d SourceCoordinate = Source.Domain.WorldToStorageCoordinate(World);
					OutField.Values[Y * TargetStorage.X + X] = SampleColorStorage(Source, SourceCoordinate.X, SourceCoordinate.Y);
				}
			}
			if (OutError) OutError->Reset();
			return OutField.IsValid();
		}

		bool ResampleTerrainDataset(
			const FEonformTerrainDataset& Source,
			const FEonformGridDomain& TargetDomain,
			FEonformTerrainDataset& OutDataset,
			FString* OutError)
		{
			if (Source.IsEmpty() || !TargetDomain.IsValid())
			{
				if (OutError) *OutError = TEXT("Terrain scaling requires a non-empty dataset and valid target domain.");
				return false;
			}

			OutDataset.Reset();
			TArray<FName> Names;
			Source.GetScalarFieldNames(Names);
			Names.Sort(FNameLexicalLess());

			if (const FEonformScalarField* Height = Source.FindScalarField(EonformTerrainFieldNames::Height))
			{
				FEonformScalarField ScaledHeight;
				if (!ResampleScalarField(*Height, TargetDomain, ScaledHeight, OutError)
					|| !OutDataset.SetScalarField(MoveTemp(ScaledHeight)))
				{
					if (OutError && OutError->IsEmpty()) *OutError = TEXT("Terrain scaling could not publish Height.");
					return false;
				}
			}

			for (const FName Name : Names)
			{
				if (Name == EonformTerrainFieldNames::Height) continue;
				const FEonformScalarField* Field = Source.FindScalarField(Name);
				if (!Field || !Field->IsValid()) continue;

				FEonformScalarField Scaled;
				if (!ResampleScalarField(*Field, TargetDomain, Scaled, OutError)) return false;
				const bool bPublished = Source.IsHeightDerivedScalarField(Name)
					? OutDataset.SetHeightDerivedScalarField(MoveTemp(Scaled))
					: OutDataset.SetScalarField(MoveTemp(Scaled));
				if (!bPublished)
				{
					if (OutError) *OutError = FString::Printf(TEXT("Terrain scaling could not publish field '%s'."), *Name.ToString());
					return false;
				}
			}

			if (OutError) OutError->Reset();
			return !OutDataset.IsEmpty();
		}
	}

	bool ResampleScalarField(
		const FEonformScalarField& Source,
		const FEonformGridDomain& TargetDomain,
		FEonformScalarField& OutField,
		FString* OutError)
	{
		if (!Source.IsValid() || !TargetDomain.IsValid())
		{
			if (OutError) *OutError = TEXT("Scalar-field scaling requires a valid source and target domain.");
			return false;
		}
		if (Source.Domain == TargetDomain)
		{
			OutField = Source;
			if (OutError) OutError->Reset();
			return true;
		}

		OutField.Initialize(TargetDomain, Source.Descriptor, 0.0f);
		const FIntPoint TargetStorage = TargetDomain.GetStorageDimensions();
		for (int32 Y = 0; Y < TargetStorage.Y; ++Y)
		{
			for (int32 X = 0; X < TargetStorage.X; ++X)
			{
				const FVector2d World = TargetDomain.StorageSampleToWorld(X, Y);
				const FVector2d SourceCoordinate = Source.Domain.WorldToStorageCoordinate(World);
				OutField.AtStorage(X, Y) = SampleScalarStorage(Source, SourceCoordinate.X, SourceCoordinate.Y);
			}
		}
		if (OutError) OutError->Reset();
		return OutField.IsValid();
	}

	bool ResampleValue(
		const FEonformTerrainValue& Source,
		const FEonformGridDomain& TargetDomain,
		FEonformTerrainValue& OutValue,
		FString* OutError)
	{
		if (!Source.IsValid() || !TargetDomain.IsValid())
		{
			if (OutError) *OutError = TEXT("Terrain value scaling requires a valid value and target domain.");
			return false;
		}

		switch (Source.Type)
		{
		case EEonformTerrainValueType::Terrain:
		{
			FEonformTerrainDataset Dataset;
			if (!ResampleTerrainDataset(Source.TerrainDataset, TargetDomain, Dataset, OutError)) return false;
			OutValue = FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Source.HeightScale);
			break;
		}
		case EEonformTerrainValueType::ScalarField:
		{
			FEonformScalarField Field;
			if (!ResampleScalarField(Source.ScalarField, TargetDomain, Field, OutError)) return false;
			OutValue = FEonformTerrainValue::MakeScalarField(MoveTemp(Field));
			break;
		}
		case EEonformTerrainValueType::Color:
		{
			FEonformColorField Field;
			if (!ResampleColorField(Source.ColorField, TargetDomain, Field, OutError)) return false;
			OutValue = FEonformTerrainValue::MakeColor(MoveTemp(Field));
			break;
		}
		default:
			if (OutError) *OutError = TEXT("Terrain value scaling received an unsupported value type.");
			return false;
		}

		if (OutError) OutError->Reset();
		return OutValue.IsValid();
	}

	bool NormalizeInputs(
		const FEonformTerrainNodeInputs& RawInputs,
		const FEonformTerrainEvaluationContext& Context,
		TMap<FName, FEonformTerrainValue>& OutOwnedInputs,
		FEonformTerrainNodeInputs& OutInputs,
		FString* OutError)
	{
		OutOwnedInputs.Reset();
		OutInputs.Reset();
		OutOwnedInputs.Reserve(RawInputs.Num());
		OutInputs.Reserve(RawInputs.Num());

		TArray<FName> Keys;
		RawInputs.GetKeys(Keys);
		Keys.Sort(FNameLexicalLess());

		const FEonformGridDomain* FirstDomain = nullptr;
		for (const FName Key : Keys)
		{
			const FEonformTerrainValue* Value = RawInputs.FindRef(Key);
			if (Value && Value->IsValid())
			{
				if (const FEonformGridDomain* Domain = FindValueDomain(*Value))
				{
					FirstDomain = Domain;
					break;
				}
			}
		}

		if (!FirstDomain)
		{
			for (const FName Key : Keys) OutInputs.Add(Key, RawInputs.FindRef(Key));
			if (OutError) OutError->Reset();
			return true;
		}

		const FEonformGridDomain TargetDomain = BuildTargetDomain(*FirstDomain, Context);
		if (!TargetDomain.IsValid())
		{
			if (OutError) *OutError = TEXT("Could not resolve a compatible node input domain.");
			return false;
		}

		for (const FName Key : Keys)
		{
			const FEonformTerrainValue* Value = RawInputs.FindRef(Key);
			if (!Value || !Value->IsValid()) continue;
			const FEonformGridDomain* Domain = FindValueDomain(*Value);
			if (Domain && *Domain != TargetDomain)
			{
				FEonformTerrainValue Scaled;
				if (!ResampleValue(*Value, TargetDomain, Scaled, OutError)) return false;
				OutOwnedInputs.Add(Key, MoveTemp(Scaled));
			}
		}

		for (const FName Key : Keys)
		{
			if (const FEonformTerrainValue* Owned = OutOwnedInputs.Find(Key)) OutInputs.Add(Key, Owned);
			else OutInputs.Add(Key, RawInputs.FindRef(Key));
		}

		if (OutError) OutError->Reset();
		return true;
	}

	bool NormalizeOutputs(
		FEonformTerrainNodeEvaluation& InOutEvaluation,
		const FEonformTerrainEvaluationContext& Context,
		FString* OutError)
	{
		const bool bHasExplicitResolution = Context.TargetResolution.X > 1 && Context.TargetResolution.Y > 1;
		if (!bHasExplicitResolution && !Context.HasRegion())
		{
			if (OutError) OutError->Reset();
			return true;
		}

		TArray<FName> OutputNames;
		InOutEvaluation.Outputs.GetKeys(OutputNames);
		OutputNames.Sort(FNameLexicalLess());
		for (const FName Name : OutputNames)
		{
			FEonformTerrainValue* Value = InOutEvaluation.Outputs.Find(Name);
			if (!Value || !Value->IsValid()) continue;
			const FEonformGridDomain* Domain = FindValueDomain(*Value);
			if (!Domain) continue;
			const FEonformGridDomain TargetDomain = BuildTargetDomain(*Domain, Context);
			if (*Domain == TargetDomain) continue;

			FEonformTerrainValue Scaled;
			if (!ResampleValue(*Value, TargetDomain, Scaled, OutError)) return false;
			*Value = MoveTemp(Scaled);
		}

		if (OutError) OutError->Reset();
		return true;
	}
}
