#include "GaeaFileNodeDecoder.h"

#include "GaeaColorField.h"
#include "GaeaGridDomain.h"
#include "GaeaTerrainEvaluator.h"
#include "GaeaTerrainFieldNames.h"
#include "GaeaTerrainRecipe.h"
#include "ImageCore.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace
{
	constexpr double UnrealUnitsPerMetre = 100.0;
	constexpr double Wgs84EquatorialRadiusMetres = 6378137.0;

	FString ResolveFileNodePath(const FGaeaTerrainNode& Node)
	{
		FString Path = Node.GetName(TEXT("File"), NAME_None).ToString();
		if (Path.IsEmpty() || Path == TEXT("None")) Path = Node.GetName(TEXT("RelativePath"), NAME_None).ToString();
		if (Path.IsEmpty() || Path == TEXT("None")) return FString();
		if (FPaths::IsRelative(Path)) Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
		FPaths::NormalizeFilename(Path);
		return Path;
	}

	bool MakeFileNodeDomain(
		int32 Width,
		int32 Height,
		double ExtentXUnrealUnits,
		double ExtentYUnrealUnits,
		FGaeaGridDomain& OutDomain,
		FString& Error)
	{
		if (Width < 2 || Height < 2)
		{
			Error = TEXT("File image must be at least 2x2 pixels.");
			return false;
		}
		if (!FMath::IsFinite(ExtentXUnrealUnits) || !FMath::IsFinite(ExtentYUnrealUnits)
			|| ExtentXUnrealUnits <= UE_DOUBLE_SMALL_NUMBER || ExtentYUnrealUnits <= UE_DOUBLE_SMALL_NUMBER)
		{
			Error = TEXT("File spatial extents must be finite and greater than zero.");
			return false;
		}

		const double HalfX = ExtentXUnrealUnits * 0.5;
		const double HalfY = ExtentYUnrealUnits * 0.5;
		OutDomain = FGaeaGridDomain::Make(
			FIntPoint(Width, Height),
			FVector2d(-HalfX, -HalfY),
			FVector2d(HalfX, HalfY));
		if (!OutDomain.IsValid())
		{
			Error = TEXT("File produced an invalid grid domain.");
			return false;
		}
		return true;
	}

	bool ResolveCoordinateBoundsMetres(
		const FGaeaTerrainNode& Node,
		double& OutExtentXMetres,
		double& OutExtentYMetres,
		FString& Error)
	{
		const double XMin = Node.GetNumber(TEXT("XMin"), 0.0);
		const double YMin = Node.GetNumber(TEXT("YMin"), 0.0);
		const double XMax = Node.GetNumber(TEXT("XMax"), 0.0);
		const double YMax = Node.GetNumber(TEXT("YMax"), 0.0);
		if (!FMath::IsFinite(XMin) || !FMath::IsFinite(YMin) || !FMath::IsFinite(XMax) || !FMath::IsFinite(YMax))
		{
			Error = TEXT("File coordinate bounds must be finite.");
			return false;
		}
		if (XMax <= XMin || YMax <= YMin)
		{
			Error = TEXT("File coordinate bounds require XMax > XMin and YMax > YMin.");
			return false;
		}

		if (Node.GetBool(TEXT("BoundsAreGeographic"), true))
		{
			if (XMin < -180.0 || XMax > 180.0 || YMin < -90.0 || YMax > 90.0)
			{
				Error = TEXT("Geographic File bounds must be valid WGS84 longitude/latitude values.");
				return false;
			}

			// Convert the WGS84 angular span to a local metric span at the centre
			// latitude. This equirectangular approximation is extremely accurate for
			// terrain-sized regions and avoids treating longitude/latitude degrees as metres.
			const double MidLatitudeRadians = FMath::DegreesToRadians((YMin + YMax) * 0.5);
			const double DeltaLongitudeRadians = FMath::DegreesToRadians(XMax - XMin);
			const double DeltaLatitudeRadians = FMath::DegreesToRadians(YMax - YMin);
			OutExtentXMetres = Wgs84EquatorialRadiusMetres * FMath::Cos(MidLatitudeRadians) * DeltaLongitudeRadians;
			OutExtentYMetres = Wgs84EquatorialRadiusMetres * DeltaLatitudeRadians;
		}
		else
		{
			// Projected bounds are expected to already be expressed in metres.
			OutExtentXMetres = XMax - XMin;
			OutExtentYMetres = YMax - YMin;
		}

		if (!FMath::IsFinite(OutExtentXMetres) || !FMath::IsFinite(OutExtentYMetres)
			|| OutExtentXMetres <= UE_DOUBLE_SMALL_NUMBER || OutExtentYMetres <= UE_DOUBLE_SMALL_NUMBER)
		{
			Error = TEXT("File coordinate bounds produced invalid physical extents.");
			return false;
		}
		return true;
	}

	bool ResolveFileNodeSpatialExtents(
		const FGaeaTerrainNode& Node,
		int32 SourceWidth,
		int32 SourceHeight,
		int32 OutputWidth,
		int32 OutputHeight,
		double& OutExtentXUnrealUnits,
		double& OutExtentYUnrealUnits,
		FString& Error)
	{
		if (SourceWidth < 2 || SourceHeight < 2 || OutputWidth < 2 || OutputHeight < 2)
		{
			Error = TEXT("File image dimensions are invalid for spatial scaling.");
			return false;
		}

		const double XYScale = FMath::Clamp(Node.GetNumber(TEXT("XYScale"), 1.0), 0.0001, 10000.0);
		const bool bUseCoordinateBounds = Node.GetBool(TEXT("UseCoordinateBounds"), false);
		const bool bUseGroundSampleDistance = Node.GetBool(TEXT("UseGroundSampleDistance"), false);

		double SourceExtentXMetres = 0.0;
		double SourceExtentYMetres = 0.0;
		if (bUseCoordinateBounds)
		{
			if (!ResolveCoordinateBoundsMetres(Node, SourceExtentXMetres, SourceExtentYMetres, Error)) return false;
		}
		else if (bUseGroundSampleDistance)
		{
			const double GroundSampleDistanceXMetres = FMath::Clamp(
				Node.GetNumber(TEXT("GroundSampleDistanceXMetres"), 1.0),
				0.0001,
				1000000.0);
			const double GroundSampleDistanceYMetres = FMath::Clamp(
				Node.GetNumber(TEXT("GroundSampleDistanceYMetres"), GroundSampleDistanceXMetres),
				0.0001,
				1000000.0);

			// Terrain rasters are sampled on a grid. The physical distance from the
			// first sample to the last is (sample count - 1) * sample spacing.
			SourceExtentXMetres = static_cast<double>(SourceWidth - 1) * GroundSampleDistanceXMetres;
			SourceExtentYMetres = static_cast<double>(SourceHeight - 1) * GroundSampleDistanceYMetres;
		}
		else
		{
			// WorldSize was the original File-node control and was expressed in UE
			// units. Keep it as a fallback so existing graphs retain their scale.
			const double LegacyWorldSizeUnrealUnits = FMath::Clamp(
				Node.GetNumber(TEXT("WorldSize"), 100000.0),
				1.0,
				10000000000.0);
			const double LegacyWorldSizeMetres = LegacyWorldSizeUnrealUnits / UnrealUnitsPerMetre;

			SourceExtentXMetres = FMath::Clamp(
				Node.GetNumber(TEXT("ExtentXMetres"), LegacyWorldSizeMetres),
				0.001,
				100000000.0);
			SourceExtentYMetres = FMath::Clamp(
				Node.GetNumber(TEXT("ExtentYMetres"), LegacyWorldSizeMetres),
				0.001,
				100000000.0);
		}

		// If CropToSquare removed samples, preserve the original ground sample
		// distance rather than stretching the cropped raster back over the full
		// source extent.
		const double OutputFractionX = static_cast<double>(OutputWidth - 1) / static_cast<double>(SourceWidth - 1);
		const double OutputFractionY = static_cast<double>(OutputHeight - 1) / static_cast<double>(SourceHeight - 1);
		const double OutputExtentXMetres = SourceExtentXMetres * OutputFractionX * XYScale;
		const double OutputExtentYMetres = SourceExtentYMetres * OutputFractionY * XYScale;

		OutExtentXUnrealUnits = OutputExtentXMetres * UnrealUnitsPerMetre;
		OutExtentYUnrealUnits = OutputExtentYMetres * UnrealUnitsPerMetre;
		return true;
	}

	bool DecodeFileNodeImage(const FString& Path, FImage& OutImage, FString& Error)
	{
		TArray64<uint8> Compressed;
		if (!FFileHelper::LoadFileToArray(Compressed, *Path))
		{
			Error = FString::Printf(TEXT("File could not read '%s'."), *Path);
			return false;
		}
		if (Compressed.IsEmpty())
		{
			Error = TEXT("File is empty.");
			return false;
		}

		IImageWrapperModule& ImageWrapperModule =
			FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		if (!ImageWrapperModule.DecompressImage(Compressed.GetData(), Compressed.Num(), OutImage))
		{
			Error = FString::Printf(TEXT("File could not decode '%s'. Supported raster formats include TIFF/GeoTIFF, PNG, JPEG, BMP, and EXR."), *Path);
			return false;
		}
		return true;
	}

	bool EvaluateFileNodeDecoded(
		const FGaeaTerrainNode& Node,
		const FGaeaTerrainNodeInputs&,
		const FGaeaTerrainEvaluationContext& Context,
		FGaeaTerrainNodeEvaluation& Out,
		FString& Error)
	{
		const FString Path = ResolveFileNodePath(Node);
		if (Path.IsEmpty())
		{
			Error = TEXT("File node has no file selected.");
			return false;
		}
		if (!FPaths::FileExists(Path))
		{
			Error = FString::Printf(TEXT("File does not exist: %s"), *Path);
			return false;
		}

		FImage Image;
		if (!DecodeFileNodeImage(Path, Image, Error)) return false;

		Image.ChangeFormat(ERawImageFormat::RGBA32F, EGammaSpace::Linear);
		const TArrayView64<const FLinearColor> Pixels = Image.AsRGBA32F();
		const int32 SourceWidth = Image.SizeX;
		const int32 SourceHeight = Image.SizeY;
		if (SourceWidth < 2 || SourceHeight < 2 || Pixels.Num() < static_cast<int64>(SourceWidth) * SourceHeight)
		{
			Error = TEXT("File decoded image data is invalid.");
			return false;
		}

		const bool bCropSquare = Node.GetBool(TEXT("CropToSquare"), false);
		const int32 OutputWidth = bCropSquare ? FMath::Min(SourceWidth, SourceHeight) : SourceWidth;
		const int32 OutputHeight = bCropSquare ? FMath::Min(SourceWidth, SourceHeight) : SourceHeight;
		const int32 CropX = bCropSquare ? (SourceWidth - OutputWidth) / 2 : 0;
		const int32 CropY = bCropSquare ? (SourceHeight - OutputHeight) / 2 : 0;

		double ExtentXUnrealUnits = 0.0;
		double ExtentYUnrealUnits = 0.0;
		if (!ResolveFileNodeSpatialExtents(
			Node,
			SourceWidth,
			SourceHeight,
			OutputWidth,
			OutputHeight,
			ExtentXUnrealUnits,
			ExtentYUnrealUnits,
			Error))
		{
			return false;
		}

		FGaeaGridDomain Domain;
		if (!MakeFileNodeDomain(OutputWidth, OutputHeight, ExtentXUnrealUnits, ExtentYUnrealUnits, Domain, Error)) return false;

		const bool bRGB = Node.GetBool(TEXT("IsRGB"), false);
		const bool bAllowUnclamped = Node.GetBool(TEXT("AllowUnclamped"), false);
		if (bRGB)
		{
			FGaeaColorField Color;
			Color.Initialize(Domain);
			for (int32 Y = 0; Y < OutputHeight; ++Y)
			{
				for (int32 X = 0; X < OutputWidth; ++X)
				{
					FLinearColor Value = Pixels[static_cast<int64>(Y + CropY) * SourceWidth + (X + CropX)];
					if (!bAllowUnclamped)
					{
						Value.R = FMath::Clamp(Value.R, 0.0f, 1.0f);
						Value.G = FMath::Clamp(Value.G, 0.0f, 1.0f);
						Value.B = FMath::Clamp(Value.B, 0.0f, 1.0f);
						Value.A = FMath::Clamp(Value.A, 0.0f, 1.0f);
					}
					Color.AtInterior(X, Y) = Value;
				}
			}
			Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeColor(MoveTemp(Color)));
			return true;
		}

		float MinElevation = TNumericLimits<float>::Max();
		float MaxElevation = TNumericLimits<float>::Lowest();
		for (int32 Y = 0; Y < OutputHeight; ++Y)
		{
			for (int32 X = 0; X < OutputWidth; ++X)
			{
				const float V = Pixels[static_cast<int64>(Y + CropY) * SourceWidth + (X + CropX)].R;
				if (!FMath::IsFinite(V)) continue;
				MinElevation = FMath::Min(MinElevation, V);
				MaxElevation = FMath::Max(MaxElevation, V);
			}
		}
		if (!FMath::IsFinite(MinElevation) || !FMath::IsFinite(MaxElevation))
		{
			Error = TEXT("File contains no finite elevation samples.");
			return false;
		}

		const FString Extension = FPaths::GetExtension(Path, false).ToLower();
		const bool bTiff = Extension == TEXT("tif") || Extension == TEXT("tiff");
		const bool bLooksLikeAbsoluteElevation = bTiff && (MinElevation < -UE_KINDA_SMALL_NUMBER || MaxElevation > 1.0001f);

		const double LegacyHeightScaleUnrealUnits = Context.HeightScale > UE_SMALL_NUMBER
			? static_cast<double>(Context.HeightScale)
			: 8000.0;
		const double HeightScaleMetres = FMath::Clamp(
			Node.GetNumber(TEXT("HeightScaleMetres"), LegacyHeightScaleUnrealUnits / UnrealUnitsPerMetre),
			0.001,
			1000000.0);
		float OutputHeightScaleUnrealUnits = static_cast<float>(HeightScaleMetres * UnrealUnitsPerMetre);
		float AbsoluteElevationNormalizationMetres = 1.0f;

		if (bLooksLikeAbsoluteElevation)
		{
			// GeoTIFF DEMs commonly store signed/float elevations directly in
			// metres. Keep zero exactly at sea level, normalize only for storage,
			// then convert the physical vertical range to UE centimetres.
			AbsoluteElevationNormalizationMetres = FMath::Max(
				FMath::Max(FMath::Abs(MinElevation), FMath::Abs(MaxElevation)),
				1.0f);
			OutputHeightScaleUnrealUnits = static_cast<float>(
				static_cast<double>(AbsoluteElevationNormalizationMetres) * UnrealUnitsPerMetre);
		}

		FGaeaFieldDescriptor Descriptor;
		Descriptor.Name = GaeaTerrainFieldNames::Height;
		Descriptor.Unit = EGaeaFieldUnit::Normalized;
		Descriptor.Interpolation = EGaeaInterpolation::Bilinear;
		FGaeaScalarField Height;
		Height.Initialize(Domain, Descriptor);

		for (int32 Y = 0; Y < OutputHeight; ++Y)
		{
			for (int32 X = 0; X < OutputWidth; ++X)
			{
				const float Raw = Pixels[static_cast<int64>(Y + CropY) * SourceWidth + (X + CropX)].R;
				float Value = FMath::IsFinite(Raw) ? Raw : 0.0f;
				if (bLooksLikeAbsoluteElevation)
				{
					Value /= AbsoluteElevationNormalizationMetres;
				}
				else if (!bAllowUnclamped)
				{
					Value = FMath::Clamp(Value, 0.0f, 1.0f);
				}
				Height.AtInterior(X, Y) = FMath::Clamp(Value, -1.0f, 1.0f);
			}
		}

		FGaeaTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Height)))
		{
			Error = TEXT("File could not publish its Height field.");
			return false;
		}
		Out.Outputs.Add(TEXT("Out"), FGaeaTerrainValue::MakeTerrain(MoveTemp(Dataset), OutputHeightScaleUnrealUnits));
		return true;
	}
}

void RegisterGaeaFileNodeDecoder()
{
	FGaeaTerrainNodeRegistry::Register(GaeaTerrainNodeTypes::File, EvaluateFileNodeDecoded);
}
