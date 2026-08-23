#include "GaeaTerrainRecipe.h"

namespace GaeaTerrainNodeTypes
{
	const FName SourceDataset(TEXT("SourceDataset"));
	const FName PerlinNoise(TEXT("PerlinNoise"));
	const FName Cellular(TEXT("Cellular"));
	const FName Cellular3D(TEXT("Cellular3D"));
	const FName Cone(TEXT("Cone"));
	const FName Constant(TEXT("Constant"));
	const FName Cracks(TEXT("Cracks"));
	const FName DotNoise(TEXT("DotNoise"));
	const FName Draw(TEXT("Draw"));
	const FName DriftNoise(TEXT("DriftNoise"));
	const FName File(TEXT("File"));
	const FName Gabor(TEXT("Gabor"));
	const FName Hemisphere(TEXT("Hemisphere"));
	const FName LinearGradient(TEXT("LinearGradient"));
	const FName LineNoise(TEXT("LineNoise"));
	const FName MultiFractal(TEXT("MultiFractal"));
	const FName Noise(TEXT("Noise"));
	const FName Object(TEXT("Object"));
	const FName Pattern(TEXT("Pattern"));
	const FName RadialGradient(TEXT("RadialGradient"));
	const FName Shape(TEXT("Shape"));
	const FName TileInput(TEXT("TileInput"));
	const FName Voronoi(TEXT("Voronoi"));
	const FName WaveShine(TEXT("WaveShine"));
	const FName TerrainShape(TEXT("TerrainShape"));
	const FName TerrainContext(TEXT("TerrainContext"));
	const FName Geology(TEXT("Geology"));
	const FName ProcessMasks(TEXT("ProcessMasks"));
	const FName ThermalErosion(TEXT("ThermalErosion"));
	const FName HydraulicErosion(TEXT("HydraulicErosion"));
	const FName HydroFix(TEXT("HydroFix"));
	const FName Rivers(TEXT("Rivers"));
	const FName Lake(TEXT("Lake"));
	const FName Sea(TEXT("Sea"));
	const FName Snow(TEXT("Snow"));
	const FName Snowfield(TEXT("Snowfield"));
	const FName Glacier(TEXT("Glacier"));
	const FName Anastomosis(TEXT("Anastomosis"));
	const FName Lichtenberg(TEXT("Lichtenberg"));
	const FName Sediments(TEXT("Sediments"));
	const FName Debris(TEXT("Debris"));
	const FName Scree(TEXT("Scree"));
	const FName EasyErosion(TEXT("EasyErosion"));
	const FName Erosion2(TEXT("Erosion2"));
	const FName Thermal2(TEXT("Thermal2"));
	const FName Crumble(TEXT("Crumble"));
	const FName Hillify(TEXT("Hillify"));
	const FName Combine(TEXT("Combine"));
	const FName Clamp(TEXT("Clamp"));
	const FName Adjust(TEXT("Adjust"));
	const FName Aperture(TEXT("Aperture"));
	const FName AutoLevel(TEXT("AutoLevel"));
	const FName BlobRemover(TEXT("BlobRemover"));
	const FName Blur(TEXT("Blur"));
	const FName Clip(TEXT("Clip"));
	const FName Curve(TEXT("Curve"));
	const FName Deflate(TEXT("Deflate"));
	const FName Denoise(TEXT("Denoise"));
	const FName Dilate(TEXT("Dilate"));
	const FName DirectionalWarp(TEXT("DirectionalWarp"));
	const FName Distance(TEXT("Distance"));
	const FName Equalize(TEXT("Equalize"));
	const FName Extend(TEXT("Extend"));
	const FName Filter(TEXT("Filter"));
	const FName Flip(TEXT("Flip"));
	const FName Fold(TEXT("Fold"));
	const FName GraphicEQ(TEXT("GraphicEQ"));
	const FName Heal(TEXT("Heal"));
	const FName Match(TEXT("Match"));
	const FName Median(TEXT("Median"));
	const FName Meshify(TEXT("Meshify"));
	const FName Origami(TEXT("Origami"));
	const FName Pixelate(TEXT("Pixelate"));
	const FName Recurve(TEXT("Recurve"));
	const FName Shaper(TEXT("Shaper"));
	const FName Sharpen(TEXT("Sharpen"));
	const FName SlopeBlur(TEXT("SlopeBlur"));
	const FName SlopeWarp(TEXT("SlopeWarp"));
	const FName SoftClip(TEXT("SoftClip"));
	const FName Swirl(TEXT("Swirl"));
	const FName ThermalShaper(TEXT("ThermalShaper"));
	const FName Threshold(TEXT("Threshold"));
	const FName Transform(TEXT("Transform"));
	const FName Transpose(TEXT("Transpose"));
	const FName VariableBlur(TEXT("VariableBlur"));
	const FName Warp(TEXT("Warp"));
	const FName Whorl(TEXT("Whorl"));
	const FName Invert(TEXT("Invert"));
	const FName MultiCombine(TEXT("MultiCombine"));
	const FName Sine(TEXT("Sine"));
	const FName ZeroBorders(TEXT("ZeroBorders"));
	const FName FractalTerraces(TEXT("FractalTerraces"));
	const FName Terrace(TEXT("Terrace"));
	const FName Bomber(TEXT("Bomber"));
	const FName Bulbous(TEXT("Bulbous"));
	const FName Contours(TEXT("Contours"));
	const FName Craggy(TEXT("Craggy"));
	const FName Distress(TEXT("Distress"));
	const FName Grid(TEXT("Grid"));
	const FName GroundTexture(TEXT("GroundTexture"));
	const FName Outcrops(TEXT("Outcrops"));
	const FName Pockmarks(TEXT("Pockmarks"));
	const FName RockNoise(TEXT("RockNoise"));
	const FName Rockscape(TEXT("Rockscape"));
	const FName Roughen(TEXT("Roughen"));
	const FName Sand(TEXT("Sand"));
	const FName Sandstone(TEXT("Sandstone"));
	const FName Shatter(TEXT("Shatter"));
	const FName Shear(TEXT("Shear"));
	const FName Steps(TEXT("Steps"));
	const FName Stones(TEXT("Stones"));
	const FName Stratify(TEXT("Stratify"));
	const FName Slope(TEXT("Slope"));
	const FName Angle(TEXT("Angle"));
	const FName Curvature(TEXT("Curvature"));
	const FName Height(TEXT("Height"));
}

namespace
{
	template<typename ValueType>
	void HashNamedMap(uint32& Hash, const TMap<FName, ValueType>& Map)
	{
		TArray<FName> Keys;
		Map.GetKeys(Keys);
		Keys.Sort(FNameLexicalLess());
		for (const FName Key : Keys)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(Key));
			Hash = HashCombineFast(Hash, GetTypeHash(Map.FindChecked(Key)));
		}
	}
}

bool FGaeaTerrainNode::IsValid() const
{
	return Id.IsValid() && !Type.IsNone();
}

double FGaeaTerrainNode::GetNumber(FName Name, double DefaultValue) const
{
	if (const double* Value = NumericParameters.Find(Name)) return *Value;
	return DefaultValue;
}

int64 FGaeaTerrainNode::GetInteger(FName Name, int64 DefaultValue) const
{
	if (const int64* Value = IntegerParameters.Find(Name)) return *Value;
	return DefaultValue;
}

bool FGaeaTerrainNode::GetBool(FName Name, bool DefaultValue) const
{
	if (const bool* Value = BoolParameters.Find(Name)) return *Value;
	return DefaultValue;
}

FName FGaeaTerrainNode::GetName(FName Name, FName DefaultValue) const
{
	if (const FName* Value = NameParameters.Find(Name)) return *Value;
	return DefaultValue;
}

bool FGaeaTerrainConnection::IsValid() const
{
	return FromNode.IsValid()
		&& ToNode.IsValid()
		&& FromNode != ToNode
		&& !FromOutput.IsNone()
		&& !ToInput.IsNone();
}

const FGaeaTerrainNode* FGaeaTerrainRecipe::FindNode(const FGuid& NodeId) const
{
	return Nodes.FindByPredicate([&NodeId](const FGaeaTerrainNode& Node) { return Node.Id == NodeId; });
}

bool FGaeaTerrainRecipe::Validate(FString* OutError) const
{
	auto Fail = [OutError](const FString& Message)
	{
		if (OutError) *OutError = Message;
		return false;
	};

	if (Version <= 0) return Fail(TEXT("Recipe version must be positive."));
	if (Nodes.IsEmpty())
	{
		if (OutputNode.IsValid()) return Fail(TEXT("Empty recipe must not reference an output node."));
		if (!Connections.IsEmpty()) return Fail(TEXT("Empty recipe must not contain connections."));
		if (OutError) OutError->Reset();
		return true;
	}
	if (!OutputNode.IsValid()) return Fail(TEXT("Recipe has no valid output node."));

	TSet<FGuid> NodeIds;
	for (const FGaeaTerrainNode& Node : Nodes)
	{
		if (!Node.IsValid()) return Fail(TEXT("Recipe contains an invalid node."));
		if (NodeIds.Contains(Node.Id)) return Fail(TEXT("Recipe contains duplicate node ids."));
		NodeIds.Add(Node.Id);
	}
	if (!NodeIds.Contains(OutputNode)) return Fail(TEXT("Recipe output node does not exist."));

	TSet<FString> InputKeys;
	for (const FGaeaTerrainConnection& Connection : Connections)
	{
		if (!Connection.IsValid()) return Fail(TEXT("Recipe contains an invalid connection."));
		if (!NodeIds.Contains(Connection.FromNode) || !NodeIds.Contains(Connection.ToNode)) return Fail(TEXT("Connection references a missing node."));
		const FString Key = Connection.ToNode.ToString(EGuidFormats::Digits) + TEXT(":") + Connection.ToInput.ToString();
		if (InputKeys.Contains(Key)) return Fail(TEXT("More than one connection targets the same node input."));
		InputKeys.Add(Key);
	}
	if (OutError) OutError->Reset();
	return true;
}

uint32 FGaeaTerrainRecipe::GetDeterministicHash() const
{
	uint32 Hash = GetTypeHash(Version);
	Hash = HashCombineFast(Hash, GetTypeHash(OutputNode));
	TArray<const FGaeaTerrainNode*> SortedNodes;
	for (const FGaeaTerrainNode& Node : Nodes) SortedNodes.Add(&Node);
	SortedNodes.Sort([](const FGaeaTerrainNode& A, const FGaeaTerrainNode& B) { return A.Id < B.Id; });
	for (const FGaeaTerrainNode* Node : SortedNodes)
	{
		Hash = HashCombineFast(Hash, GetTypeHash(Node->Id));
		Hash = HashCombineFast(Hash, GetTypeHash(Node->Type));
		HashNamedMap(Hash, Node->NumericParameters);
		HashNamedMap(Hash, Node->IntegerParameters);
		HashNamedMap(Hash, Node->BoolParameters);
		HashNamedMap(Hash, Node->NameParameters);
	}
	TArray<const FGaeaTerrainConnection*> SortedConnections;
	for (const FGaeaTerrainConnection& Connection : Connections) SortedConnections.Add(&Connection);
	SortedConnections.Sort([](const FGaeaTerrainConnection& A, const FGaeaTerrainConnection& B)
	{
		if (A.ToNode != B.ToNode) return A.ToNode < B.ToNode;
		if (A.ToInput != B.ToInput) return A.ToInput.LexicalLess(B.ToInput);
		if (A.FromNode != B.FromNode) return A.FromNode < B.FromNode;
		return A.FromOutput.LexicalLess(B.FromOutput);
	});
	for (const FGaeaTerrainConnection* Connection : SortedConnections)
	{
		Hash = HashCombineFast(Hash, GetTypeHash(Connection->FromNode));
		Hash = HashCombineFast(Hash, GetTypeHash(Connection->FromOutput));
		Hash = HashCombineFast(Hash, GetTypeHash(Connection->ToNode));
		Hash = HashCombineFast(Hash, GetTypeHash(Connection->ToInput));
	}
	return Hash;
}
