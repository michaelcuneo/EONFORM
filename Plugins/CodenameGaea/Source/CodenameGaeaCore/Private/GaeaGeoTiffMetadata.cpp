#include "GaeaGeoTiffMetadata.h"

#include "Misc/FileHelper.h"

namespace
{
	struct FTiffReader
	{
		const TArray64<uint8>& Data;
		bool bLittleEndian = true;

		bool CanRead(int64 Offset, int64 Size) const
		{
			return Offset >= 0 && Size >= 0 && Offset + Size <= Data.Num();
		}

		uint16 U16(int64 Offset) const
		{
			if (!CanRead(Offset, 2)) return 0;
			if (bLittleEndian) return static_cast<uint16>(Data[Offset]) | (static_cast<uint16>(Data[Offset + 1]) << 8);
			return (static_cast<uint16>(Data[Offset]) << 8) | static_cast<uint16>(Data[Offset + 1]);
		}

		uint32 U32(int64 Offset) const
		{
			if (!CanRead(Offset, 4)) return 0;
			if (bLittleEndian)
			{
				return static_cast<uint32>(Data[Offset])
					| (static_cast<uint32>(Data[Offset + 1]) << 8)
					| (static_cast<uint32>(Data[Offset + 2]) << 16)
					| (static_cast<uint32>(Data[Offset + 3]) << 24);
			}
			return (static_cast<uint32>(Data[Offset]) << 24)
				| (static_cast<uint32>(Data[Offset + 1]) << 16)
				| (static_cast<uint32>(Data[Offset + 2]) << 8)
				| static_cast<uint32>(Data[Offset + 3]);
		}

		double F64(int64 Offset) const
		{
			if (!CanRead(Offset, 8)) return 0.0;
			uint8 Bytes[8];
			for (int32 I = 0; I < 8; ++I) Bytes[I] = Data[Offset + (bLittleEndian ? I : 7 - I)];
		double Value = 0.0;
		FMemory::Memcpy(&Value, Bytes, sizeof(double));
		return Value;
		}
	};

	int32 TypeSize(uint16 Type)
	{
		switch (Type)
		{
		case 1: return 1; // BYTE
		case 2: return 1; // ASCII
		case 3: return 2; // SHORT
		case 4: return 4; // LONG
		case 5: return 8; // RATIONAL
		case 11: return 4; // FLOAT
		case 12: return 8; // DOUBLE
		default: return 0;
		}
	}

	bool EntryDataOffset(const FTiffReader& Reader, int64 EntryOffset, int64& OutOffset, uint16& OutType, uint32& OutCount)
	{
		OutType = Reader.U16(EntryOffset + 2);
		OutCount = Reader.U32(EntryOffset + 4);
		const int32 ElementSize = TypeSize(OutType);
		if (ElementSize <= 0 || OutCount == 0) return false;
		const uint64 ByteCount = static_cast<uint64>(ElementSize) * OutCount;
		OutOffset = ByteCount <= 4 ? EntryOffset + 8 : static_cast<int64>(Reader.U32(EntryOffset + 8));
		return Reader.CanRead(OutOffset, static_cast<int64>(ByteCount));
	}

	bool ReadDoubles(const FTiffReader& Reader, int64 EntryOffset, TArray<double>& OutValues)
	{
		int64 DataOffset = 0;
		uint16 Type = 0;
		uint32 Count = 0;
		if (!EntryDataOffset(Reader, EntryOffset, DataOffset, Type, Count) || Type != 12) return false;
		OutValues.SetNumUninitialized(static_cast<int32>(Count));
		for (uint32 I = 0; I < Count; ++I) OutValues[static_cast<int32>(I)] = Reader.F64(DataOffset + static_cast<int64>(I) * 8);
		return true;
	}

	bool ReadShorts(const FTiffReader& Reader, int64 EntryOffset, TArray<uint16>& OutValues)
	{
		int64 DataOffset = 0;
		uint16 Type = 0;
		uint32 Count = 0;
		if (!EntryDataOffset(Reader, EntryOffset, DataOffset, Type, Count) || Type != 3) return false;
		OutValues.SetNumUninitialized(static_cast<int32>(Count));
		for (uint32 I = 0; I < Count; ++I) OutValues[static_cast<int32>(I)] = Reader.U16(DataOffset + static_cast<int64>(I) * 2);
		return true;
	}

	void ParseGeoKeys(const TArray<uint16>& Keys, bool& bOutGeographic, int32& OutEpsg)
	{
		if (Keys.Num() < 4) return;
		const int32 KeyCount = Keys[3];
		for (int32 I = 0; I < KeyCount; ++I)
		{
			const int32 Base = 4 + I * 4;
			if (Base + 3 >= Keys.Num()) break;
			const uint16 KeyId = Keys[Base];
			const uint16 Location = Keys[Base + 1];
			const uint16 Count = Keys[Base + 2];
			const uint16 Value = Keys[Base + 3];
			if (Location != 0 || Count != 1) continue;

			if (KeyId == 1024) // GTModelTypeGeoKey
			{
				bOutGeographic = Value == 2;
			}
			else if (KeyId == 2048 || KeyId == 3072) // GeographicType / ProjectedCSType
			{
				if (Value > 0 && Value != 32767) OutEpsg = Value;
			}
		}
	}
}

bool GaeaReadGeoTiffMetadata(
	const FString& Path,
	int32 RasterWidth,
	int32 RasterHeight,
	FGaeaGeoTiffMetadata& OutMetadata)
{
	OutMetadata = FGaeaGeoTiffMetadata();
	if (RasterWidth < 2 || RasterHeight < 2) return false;

	TArray64<uint8> Data;
	if (!FFileHelper::LoadFileToArray(Data, *Path) || Data.Num() < 8) return false;

	FTiffReader Reader{Data};
	if (Data[0] == 'I' && Data[1] == 'I') Reader.bLittleEndian = true;
	else if (Data[0] == 'M' && Data[1] == 'M') Reader.bLittleEndian = false;
	else return false;

	// Classic TIFF. BigTIFF raster decoding can still work through ImageWrapper,
	// but its 64-bit IFD layout is deliberately not guessed here.
	if (Reader.U16(2) != 42) return false;
	const int64 IfdOffset = static_cast<int64>(Reader.U32(4));
	if (!Reader.CanRead(IfdOffset, 2)) return false;

	TArray<double> PixelScale;
	TArray<double> TiePoints;
	TArray<double> Transform;
	TArray<uint16> GeoKeys;

	const uint16 EntryCount = Reader.U16(IfdOffset);
	for (uint16 I = 0; I < EntryCount; ++I)
	{
		const int64 Entry = IfdOffset + 2 + static_cast<int64>(I) * 12;
		if (!Reader.CanRead(Entry, 12)) break;
		const uint16 Tag = Reader.U16(Entry);
		if (Tag == 33550) ReadDoubles(Reader, Entry, PixelScale);      // ModelPixelScaleTag
		else if (Tag == 33922) ReadDoubles(Reader, Entry, TiePoints); // ModelTiepointTag
		else if (Tag == 34264) ReadDoubles(Reader, Entry, Transform); // ModelTransformationTag
		else if (Tag == 34735) ReadShorts(Reader, Entry, GeoKeys);    // GeoKeyDirectoryTag
	}

	ParseGeoKeys(GeoKeys, OutMetadata.bGeographic, OutMetadata.EpsgCode);

	auto StoreBounds = [&OutMetadata](double X0, double Y0, double X1, double Y1)
	{
		OutMetadata.XMin = FMath::Min(X0, X1);
		OutMetadata.XMax = FMath::Max(X0, X1);
		OutMetadata.YMin = FMath::Min(Y0, Y1);
		OutMetadata.YMax = FMath::Max(Y0, Y1);
		OutMetadata.bValid = FMath::IsFinite(OutMetadata.XMin)
			&& FMath::IsFinite(OutMetadata.XMax)
			&& FMath::IsFinite(OutMetadata.YMin)
			&& FMath::IsFinite(OutMetadata.YMax)
			&& OutMetadata.XMax > OutMetadata.XMin
			&& OutMetadata.YMax > OutMetadata.YMin;
	};

	if (Transform.Num() >= 16)
	{
		auto TransformPoint = [&Transform](double X, double Y)
		{
			return FVector2d(
				Transform[0] * X + Transform[1] * Y + Transform[3],
				Transform[4] * X + Transform[5] * Y + Transform[7]);
		};
		const FVector2d A = TransformPoint(0.0, 0.0);
		const FVector2d B = TransformPoint(static_cast<double>(RasterWidth - 1), static_cast<double>(RasterHeight - 1));
		StoreBounds(A.X, A.Y, B.X, B.Y);
		return OutMetadata.bValid;
	}

	if (PixelScale.Num() >= 2 && TiePoints.Num() >= 6)
	{
		const double RasterI = TiePoints[0];
		const double RasterJ = TiePoints[1];
		const double ModelX = TiePoints[3];
		const double ModelY = TiePoints[4];
		const double ScaleX = PixelScale[0];
		const double ScaleY = PixelScale[1];
		if (ScaleX <= 0.0 || ScaleY <= 0.0) return false;

		const double X0 = ModelX + (0.0 - RasterI) * ScaleX;
		const double Y0 = ModelY - (0.0 - RasterJ) * ScaleY;
		const double X1 = ModelX + (static_cast<double>(RasterWidth - 1) - RasterI) * ScaleX;
		const double Y1 = ModelY - (static_cast<double>(RasterHeight - 1) - RasterJ) * ScaleY;
		StoreBounds(X0, Y0, X1, Y1);
		return OutMetadata.bValid;
	}

	return false;
}
