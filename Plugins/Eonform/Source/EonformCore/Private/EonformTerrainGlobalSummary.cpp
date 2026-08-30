#include "EonformTerrainGlobalSummary.h"

#include "EonformTerrainFieldNames.h"
#include "EonformTerrainRegionalSupport.h"
#include "Misc/ScopeLock.h"

bool FEonformTerrainGlobalSummaryCache::Find(uint64 Key, float& OutValue) const
{
	FScopeLock Lock(&Mutex);
	if (const float* Found = Scalars.Find(Key))
	{
		OutValue = *Found;
		return true;
	}
	return false;
}

void FEonformTerrainGlobalSummaryCache::Store(uint64 Key, float Value)
{
	FScopeLock Lock(&Mutex);
	Scalars.Add(Key, Value);
}

void FEonformTerrainGlobalSummaryCache::Reset()
{
	FScopeLock Lock(&Mutex);
	Scalars.Reset();
}

namespace
{
	uint64 MixSummaryKey(uint64 Hash, uint64 Value)
	{
		Hash ^= Value + 0x9E3779B97F4A7C15ull + (Hash << 6) + (Hash >> 2);
		return Hash;
	}

	uint64 MakeSummaryKey(
		const FEonformTerrainRecipe& Recipe,
		const FEonformTerrainEvaluationContext& Context,
		const FGuid& NodeId,
		FName OutputName,
		uint64 Kind)
	{
		uint64 Key = 0x474C4F42414C5355ull;
		Key = MixSummaryKey(Key, static_cast<uint64>(Recipe.GetDeterministicHash()));
		Key = MixSummaryKey(Key, (static_cast<uint64>(NodeId.A) << 32) | static_cast<uint64>(NodeId.B));
		Key = MixSummaryKey(Key, (static_cast<uint64>(NodeId.C) << 32) | static_cast<uint64>(NodeId.D));
		Key = MixSummaryKey(Key, static_cast<uint64>(GetTypeHash(OutputName)));
		Key = MixSummaryKey(Key, static_cast<uint64>(static_cast<uint32>(Context.ReferenceResolution.X)) << 32
			| static_cast<uint64>(static_cast<uint32>(Context.ReferenceResolution.Y)));
		Key = MixSummaryKey(Key, Context.CacheContextRevision);
		Key = MixSummaryKey(Key, Kind);
		return Key;
	}

	const FEonformScalarField* ResolveSummaryField(const FEonformTerrainValue& Value, FString& Error)
	{
		if (Value.Type == EEonformTerrainValueType::ScalarField)
		{
			if (!Value.ScalarField.IsValid())
			{
				Error = TEXT("Global summary received an invalid scalar output.");
				return nullptr;
			}
			return &Value.ScalarField;
		}
		if (Value.Type == EEonformTerrainValueType::Terrain)
		{
			const FEonformScalarField* Height = Value.TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
			if (!Height || !Height->IsValid())
			{
				Error = TEXT("Global summary terrain output has no valid Height field.");
				return nullptr;
			}
			return Height;
		}

		Error = TEXT("Global summary supports Terrain and ScalarField outputs only.");
		return nullptr;
	}
}

bool FEonformTerrainGlobalSummary::ResolveOutputRange(
	const FEonformTerrainRecipe& Recipe,
	const FEonformTerrainEvaluationContext& Context,
	const FGuid& NodeId,
	FName OutputName,
	float& OutMinimum,
	float& OutMaximum,
	FString* OutError)
{
	if (!NodeId.IsValid() || !Recipe.FindNode(NodeId))
	{
		if (OutError) *OutError = TEXT("Global summary could not resolve its upstream node.");
		return false;
	}

	FIntPoint ReferenceResolution = Context.ReferenceResolution;
	if (ReferenceResolution.X < 2 || ReferenceResolution.Y < 2)
	{
		if (Context.HasRegion())
		{
			if (OutError) *OutError = TEXT("Regional global summary requires an explicit full-world ReferenceResolution.");
			return false;
		}
		ReferenceResolution = Context.TargetResolution;
	}
	if (ReferenceResolution.X < 2 || ReferenceResolution.Y < 2)
	{
		if (OutError) *OutError = TEXT("Global summary requires a valid full-world reference resolution.");
		return false;
	}

	FEonformTerrainEvaluationContext SummaryBase = Context;
	SummaryBase.ReferenceResolution = ReferenceResolution;
	SummaryBase.ActiveRecipe = nullptr;

	const uint64 MinimumKey = MakeSummaryKey(Recipe, SummaryBase, NodeId, OutputName, 0x4D494Eull);
	const uint64 MaximumKey = MakeSummaryKey(Recipe, SummaryBase, NodeId, OutputName, 0x4D4158ull);
	if (SummaryBase.GlobalSummaryCache
		&& SummaryBase.GlobalSummaryCache->Find(MinimumKey, OutMinimum)
		&& SummaryBase.GlobalSummaryCache->Find(MaximumKey, OutMaximum))
	{
		if (OutError) OutError->Reset();
		return true;
	}

	FEonformTerrainRecipe UpstreamRecipe = Recipe;
	UpstreamRecipe.OutputNode = NodeId;
	const FEonformTerrainRegionalSupportReport Support =
		FEonformTerrainRegionalSupport::Analyze(UpstreamRecipe, ReferenceResolution);
	if (!Support.bSupported)
	{
		if (OutError)
		{
			*OutError = FString::Printf(
				TEXT("Global summary upstream branch is not region-equivalent: %s"),
				*Support.Describe());
		}
		return false;
	}

	const FEonformGridDomain ReferenceDomain = SummaryBase.ResolveReferenceDomain(
		ReferenceResolution,
		FVector2d(-50000.0, -50000.0),
		FVector2d(50000.0, 50000.0));
	if (!ReferenceDomain.IsValid())
	{
		if (OutError) *OutError = TEXT("Global summary could not resolve the full-world reference domain.");
		return false;
	}

	const FVector2d CellSize = ReferenceDomain.GetCellSize();
	float Minimum = TNumericLimits<float>::Max();
	float Maximum = TNumericLimits<float>::Lowest();
	bool bSawSample = false;

	int32 StartY = 0;
	while (StartY < ReferenceResolution.Y)
	{
		int32 Remaining = ReferenceResolution.Y - StartY;
		if (Remaining == 1 && StartY > 0)
		{
			--StartY;
			Remaining = 2;
		}
		const int32 RowCount = FMath::Min(PreferredStripRows, Remaining);
		const int32 EndY = StartY + RowCount - 1;

		FEonformTerrainEvaluationContext StripContext = SummaryBase;
		StripContext.TargetResolution = FIntPoint(ReferenceResolution.X, RowCount);
		StripContext.Region.WorldMinCm = FVector2d(
			ReferenceDomain.WorldMin.X,
			ReferenceDomain.WorldMin.Y + static_cast<double>(StartY) * CellSize.Y);
		StripContext.Region.WorldMaxCm = FVector2d(
			ReferenceDomain.WorldMax.X,
			ReferenceDomain.WorldMin.Y + static_cast<double>(EndY) * CellSize.Y);
		StripContext.Region.BorderSamples = Support.RequiredBorderSamples;

		FEonformTerrainValue StripValue;
		FString Error;
		if (!FEonformTerrainEvaluator::EvaluateOutput(
			UpstreamRecipe,
			StripContext,
			NodeId,
			OutputName,
			StripValue,
			&Error))
		{
			if (OutError)
			{
				*OutError = FString::Printf(TEXT("Global summary strip evaluation failed: %s"), *Error);
			}
			return false;
		}

		const FEonformScalarField* Field = ResolveSummaryField(StripValue, Error);
		if (!Field)
		{
			if (OutError) *OutError = Error;
			return false;
		}
		for (int32 Y = 0; Y < Field->Domain.Dimensions.Y; ++Y)
		{
			for (int32 X = 0; X < Field->Domain.Dimensions.X; ++X)
			{
				const float Value = Field->AtInterior(X, Y);
				Minimum = FMath::Min(Minimum, Value);
				Maximum = FMath::Max(Maximum, Value);
				bSawSample = true;
			}
		}

		if (EndY >= ReferenceResolution.Y - 1) break;
		StartY = EndY + 1;
	}

	if (!bSawSample || !FMath::IsFinite(Minimum) || !FMath::IsFinite(Maximum))
	{
		if (OutError) *OutError = TEXT("Global summary produced no finite samples.");
		return false;
	}

	OutMinimum = Minimum;
	OutMaximum = Maximum;
	if (SummaryBase.GlobalSummaryCache)
	{
		SummaryBase.GlobalSummaryCache->Store(MinimumKey, Minimum);
		SummaryBase.GlobalSummaryCache->Store(MaximumKey, Maximum);
	}
	if (OutError) OutError->Reset();
	return true;
}
