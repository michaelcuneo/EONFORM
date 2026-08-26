#include "EonformFileNodeDecoder.h"

#include "EonformColorField.h"
#include "EonformGeoTiffMetadata.h"
#include "EonformGridDomain.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainRecipe.h"
#include "ImageCore.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace
{
	constexpr double UnrealUnitsPerMetre = 100.0;
	constexpr double Wgs84EquatorialRadiusMetres = 6378137.0;

	FString ResolveFileNodePath(const FEonformTerrainNode& Node)
	{
		FString Path = Node.GetName(TEXT("File"), NAME_None).ToString();
		if (Path.IsEmpty() || Path == TEXT("None")) Path = Node.GetName(TEXT("RelativePath"), NAME_None).ToString();
		if (Path.IsEmpty() || Path == TEXT("None")) return FString();
		if (FPaths::IsRelative(Path)) Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
		FPaths::NormalizeFilename(Path);
		return Path;
	}

	bool HasValidCoordinateBounds(const FEonformTerrainNode& Node)
	{
		const double XMin = Node.GetNumber(TEXT("XMin"), 0.0);
		const double YMin = Node.GetNumber(TEXT("YMin"), 0.0);
		const double XMax = Node.GetNumber(TEXT("XMax"), 0.0);
		const double YMax = Node.GetNumber(TEXT("YMax"), 0.0);
		return FMath::IsFinite(XMin) && FMath::IsFinite(YMin) && FMath::IsFinite(XMax) && FMath::IsFinite(YMax)
			&& XMax > XMin && YMax > YMin;
	}

	bool MakeFileNodeDomain(
		int32 Width,
		int32 Height,
		double ExtentXUnrealUnits,
		double ExtentYUnrealUnits,
		FEonformGridDomain& OutDomain,
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
		OutDomain = FEonformGridDomain::Make(
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
		const FEonformTerrainNode& Node,
		double& OutExtentXMetres,
		double& OutExtentYMetres,
		FString& Error)
	{
		const double XMin = Node.GetNumber(TEXT("XMin"), 0.0);
		const double YMin = Node.GetNumber(TEXT("YMin"), 0.0);
		const double XMax = Node.GetNumber(TEXT("XMax"), 0.0);
		const double YMax = Node.GetNumber(TEXT("YMax"), 0.0);
		if (!HasValidCoordinateBounds(Node))
		{
			Error = TEXT("File coordinate bounds require finite values with XMax > XMin and YMax > YMin.");
			return false;
		}

		if (Node.GetBool(TEXT("BoundsAreGeographic"), true))
		{
			if (XMin < -180.0 || XMax > 180.0 || YMin < -90.0 || YMax > 90.0)
			{
				Error = TEXT("Geographic File bounds must be valid WGS84 longitude/latitude values.");
				return false;
			}

			const double MidLatitudeRadians = FMath::DegreesToRadians((YMin + YMax) * 0.5);
			const double DeltaLongitudeRadians = FMath::DegreesToRadians(XMax - XMin);
			const double DeltaLatitudeRadians = FMath::DegreesToRadians(YMax - YMin);
			OutExtentXMetres = Wgs84EquatorialRadiusMetres * FMath::Cos(MidLatitudeRadians) * DeltaLongitudeRadians;
			OutExtentYMetres = Wgs84EquatorialRadiusMetres * DeltaLatitudeRadians;
		}
		else
		{
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

	bool ResolveGeoTiffExtentsMetres(
		const FEonformGeoTiffMetadata& GeoTiff,
		double& OutExtentXMetres,
		double& OutExtentYMetres)
	{
		if (!GeoTiff.bValid) return false;
		if (GeoTiff.bGeographic)
		{
			if (GeoTiff.XMin < -180.0 || GeoTiff.XMax > 180.0 || GeoTiff.YMin < -90.0 || GeoTiff.YMax > 90.0) return false;
			const double MidLatitudeRadians = FMath::DegreesToRadians((GeoTiff.YMin + GeoTiff.YMax) * 0.5);
			OutExtentXMetres = Wgs84EquatorialRadiusMetres
				* FMath::Cos(MidLatitudeRadians)
				* FMath::DegreesToRadians(GeoTiff.XMax - GeoTiff.XMin);
			OutExtentYMetres = Wgs84EquatorialRadiusMetres
				* FMath::DegreesToRadians(GeoTiff.YMax - GeoTiff.YMin);
		}
		else
		{
			OutExtentXMetres = (GeoTiff.XMax - GeoTiff.XMin) * GeoTiff.LinearUnitsToMetres;
			OutExtentYMetres = (GeoTiff.YMax - GeoTiff.YMin) * GeoTiff.LinearUnitsToMetres;
		}
		return FMath::IsFinite(OutExtentXMetres) && FMath::IsFinite(OutExtentYMetres)
			&& OutExtentXMetres > UE_DOUBLE_SMALL_NUMBER && OutExtentYMetres > UE_DOUBLE_SMALL_NUMBER;
	}

	bool ResolveFileNodeSpatialExtents(
		const FEonformTerrainNode& Node,
		const FEonformGeoTiffMetadata* GeoTiff,
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
		const bool bUseCoordinateBounds = HasValidCoordinateBounds(Node) || Node.GetBool(TEXT("UseCoordinateBounds"), false);
		const bool bUseGroundSampleDistance = Node.GetBool(TEXT("UseGroundSampleDistance"), false);

		double SourceExtentXMetres = 0.0;
		double SourceExtentYMetres = 0.0;
		if (bUseCoordinateBounds)
		{
			if (!ResolveCoordinateBoundsMetres(Node, SourceExtentXMetres, SourceExtentYMetres, Error)) return false;
		}
		else if (!bUseGroundSampleDistance && GeoTiff && ResolveGeoTiffExtentsMetres(*GeoTiff, SourceExtentXMetres, SourceExtentYMetres))
		{
			// Embedded GeoTIFF georeferencing is authoritative unless the user
			// explicitly supplied coordinate bounds or metres-per-pixel overrides.
		}
		else if (bUseGroundSampleDistance)
		{
			const double GroundSampleDistanceXMetres = FMath::Clamp(
				Node.GetNumber(TEXT("GroundSampleDistanceXMetres"), 1.0), 0.0001, 1000000.0);
			const double GroundSampleDistanceYMetres = FMath::Clamp(
				Node.GetNumber(TEXT("GroundSampleDistanceYMetres"), GroundSampleDistanceXMetres), 0.0001, 1000000.0);
			SourceExtentXMetres = static_cast<double>(SourceWidth - 1) * GroundSampleDistanceXMetres;
			SourceExtentYMetres = static_cast<double>(SourceHeight - 1) * GroundSampleDistanceYMetres;
		}
		else
		{
			const double LegacyWorldSizeUnrealUnits = FMath::Clamp(
				Node.GetNumber(TEXT("WorldSize"), 100000.0), 1.0, 10000000000.0);
			const double LegacyWorldSizeMetres = LegacyWorldSizeUnrealUnits / UnrealUnitsPerMetre;
			SourceExtentXMetres = FMath::Clamp(
				Node.GetNumber(TEXT("ExtentXMetres"), LegacyWorldSizeMetres), 0.001, 100000000.0);
			SourceExtentYMetres = FMath::Clamp(
				Node.GetNumber(TEXT("ExtentYMetres"), LegacyWorldSizeMetres), 0.001, 100000000.0);
		}

		const double OutputFractionX = static_cast<double>(OutputWidth - 1) / static_cast<double>(SourceWidth - 1);
		const double OutputFractionY = static_cast<double>(OutputHeight - 1) / static_cast<double>(SourceHeight - 1);
		OutExtentXUnrealUnits = SourceExtentXMetres * OutputFractionX * XYScale * UnrealUnitsPerMetre;
		OutExtentYUnrealUnits = SourceExtentYMetres * OutputFractionY * XYScale * UnrealUnitsPerMetre;
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
			Error = FString::Printf(
				TEXT("File could not decode '%s'. Supported raster formats include TIFF/GeoTIFF, PNG, JPEG, BMP, and EXR."),
				*Path);
			return false;
		}
		return true;
	}

	FName ResolveElevationEncoding(const FEonformTerrainNode& Node, ERawImageFormat::Type SourceFormat)
	{
		const FName Requested = Node.GetName(TEXT("ElevationEncoding"), TEXT("Auto"));
		if (Requested != TEXT("Auto")) return Requested;

		switch (SourceFormat)
		{
		case ERawImageFormat::G16:
			return TEXT("UInt16");
		case ERawImageFormat::R16F:
		case ERawImageFormat::R32F:
		case ERawImageFormat::RGBA16F:
		case ERawImageFormat::RGBA32F:
			return TEXT("FloatMetres");
		default:
			return TEXT("Normalized");
		}
	}

	bool SourceFormatIsNormalizedInteger(ERawImageFormat::Type SourceFormat)
	{
		return SourceFormat == ERawImageFormat::G8
			|| SourceFormat == ERawImageFormat::G16
			|| SourceFormat == ERawImageFormat::BGRA8
			|| SourceFormat == ERawImageFormat::RGBA16;
	}

	double DecodeElevationMetres(float Raw, FName Encoding, bool bNormalizedIntegerSource, double NormalizedHeightRangeMetres)
	{
		if (!FMath::IsFinite(Raw)) return 0.0;

		const double V = static_cast<double>(Raw);
		if (Encoding == TEXT("FloatMetres")) return V;
		if (Encoding == TEXT("Normalized")) return FMath::Clamp(V, 0.0, 1.0) * NormalizedHeightRangeMetres;

		if (Encoding == TEXT("UInt16"))
		{
			return bNormalizedIntegerSource ? FMath::Clamp(V, 0.0, 1.0) * 65535.0 : V;
		}
		if (Encoding == TEXT("Int16"))
		{
			if (!bNormalizedIntegerSource) return V;
			return FMath::RoundToDouble(FMath::Clamp(V, 0.0, 1.0) * 65535.0) - 32768.0;
		}
		if (Encoding == TEXT("UInt32"))
		{
			return bNormalizedIntegerSource ? FMath::Clamp(V, 0.0, 1.0) * 4294967295.0 : V;
		}
		if (Encoding == TEXT("Int32"))
		{
			if (!bNormalizedIntegerSource) return V;
			return FMath::RoundToDouble(FMath::Clamp(V, 0.0, 1.0) * 4294967295.0) - 2147483648.0;
		}
		return V;
	}

	bool EvaluateFileNodeDecoded(
		const FEonformTerrainNode& Node,
		const FEonformTerrainNodeInputs&,
		const FEonformTerrainEvaluationContext& Context,
		FEonformTerrainNodeEvaluation& Out,
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

		const ERawImageFormat::Type SourceFormat = Image.Format;
		Image.ChangeFormat(ERawImageFormat::RGBA32F, EGammaSpace::Linear);
		const TArrayView64<const FLinearColor> Pixels = Image.AsRGBA32F();
		const int32 SourceWidth = Image.SizeX;
		const int32 SourceHeight = Image.SizeY;
		if (SourceWidth < 2 || SourceHeight < 2 || Pixels.Num() < static_cast<int64>(SourceWidth) * SourceHeight)
		{
			Error = TEXT("File decoded image data is invalid.");
			return false;
		}

		FEonformGeoTiffMetadata GeoTiff;
		const bool bHasGeoTiffMetadata = EonformReadGeoTiffMetadata(Path, SourceWidth, SourceHeight, GeoTiff);

		const bool bCropSquare = Node.GetBool(TEXT("CropToSquare"), false);
		const int32 OutputWidth = bCropSquare ? FMath::Min(SourceWidth, SourceHeight) : SourceWidth;
		const int32 OutputHeight = bCropSquare ? FMath::Min(SourceWidth, SourceHeight) : SourceHeight;
		const int32 CropX = bCropSquare ? (SourceWidth - OutputWidth) / 2 : 0;
		const int32 CropY = bCropSquare ? (SourceHeight - OutputHeight) / 2 : 0;

		double ExtentXUnrealUnits = 0.0;
		double ExtentYUnrealUnits = 0.0;
		if (!ResolveFileNodeSpatialExtents(
			Node,
			bHasGeoTiffMetadata ? &GeoTiff : nullptr,
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

		FEonformGridDomain Domain;
		if (!MakeFileNodeDomain(OutputWidth, OutputHeight, ExtentXUnrealUnits, ExtentYUnrealUnits, Domain, Error)) return false;

		const bool bRGB = Node.GetBool(TEXT("IsRGB"), false);
		const bool bAllowUnclamped = Node.GetBool(TEXT("AllowUnclamped"), false);
		if (bRGB)
		{
			FEonformColorField Color;
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
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeColor(MoveTemp(Color)));
			return true;
		}

		const double LegacyHeightScaleUnrealUnits = Context.HeightScale > UE_SMALL_NUMBER
			? static_cast<double>(Context.HeightScale)
			: 8000.0;
		const double NormalizedHeightRangeMetres = FMath::Clamp(
			Node.GetNumber(TEXT("HeightScaleMetres"), LegacyHeightScaleUnrealUnits / UnrealUnitsPerMetre),
			0.001,
			1000000.0);
		const FName ElevationEncoding = ResolveElevationEncoding(Node, SourceFormat);
		const bool bNormalizedIntegerSource = SourceFormatIsNormalizedInteger(SourceFormat);

		double MaxAbsElevationMetres = 0.0;
		for (int32 Y = 0; Y < OutputHeight; ++Y)
		{
			for (int32 X = 0; X < OutputWidth; ++X)
			{
				const float Raw = Pixels[static_cast<int64>(Y + CropY) * SourceWidth + (X + CropX)].R;
				const double ElevationMetres = DecodeElevationMetres(
					Raw,
					ElevationEncoding,
					bNormalizedIntegerSource,
					NormalizedHeightRangeMetres);
				if (!FMath::IsFinite(ElevationMetres)) continue;
				MaxAbsElevationMetres = FMath::Max(MaxAbsElevationMetres, FMath::Abs(ElevationMetres));
			}
		}

		if (!FMath::IsFinite(MaxAbsElevationMetres))
		{
			Error = TEXT("File contains no finite elevation samples.");
			return false;
		}
		MaxAbsElevationMetres = FMath::Max(MaxAbsElevationMetres, 0.01);

		FEonformFieldDescriptor Descriptor;
		Descriptor.Name = EonformTerrainFieldNames::Height;
		Descriptor.Unit = EEonformFieldUnit::Normalized;
		Descriptor.Interpolation = EEonformInterpolation::Bilinear;
		FEonformScalarField Height;
		Height.Initialize(Domain, Descriptor);

		for (int32 Y = 0; Y < OutputHeight; ++Y)
		{
			for (int32 X = 0; X < OutputWidth; ++X)
			{
				const float Raw = Pixels[static_cast<int64>(Y + CropY) * SourceWidth + (X + CropX)].R;
				const double ElevationMetres = DecodeElevationMetres(
					Raw,
					ElevationEncoding,
					bNormalizedIntegerSource,
					NormalizedHeightRangeMetres);
				const double NormalizedElevation = ElevationMetres / MaxAbsElevationMetres;
				Height.AtInterior(X, Y) = static_cast<float>(FMath::Clamp(NormalizedElevation, -1.0, 1.0));
			}
		}

		FEonformTerrainDataset Dataset;
		if (!Dataset.SetScalarField(MoveTemp(Height)))
		{
			Error = TEXT("File could not publish its Height field.");
			return false;
		}

		const float OutputHeightScaleUnrealUnits = static_cast<float>(MaxAbsElevationMetres * UnrealUnitsPerMetre);
		Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), OutputHeightScaleUnrealUnits));
		return true;
	}
}

void RegisterEonformFileNodeDecoder()
{
	FEonformTerrainNodeRegistry::Register(EonformTerrainNodeTypes::File, EvaluateFileNodeDecoded);
}
