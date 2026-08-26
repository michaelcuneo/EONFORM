#include "EonformUtilityNodes.h"

#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"
#include "EonformTerrainUtilityOps.h"

namespace
{
	FEonformTerrainPortDescriptor Port(FName Name, const TCHAR* DisplayName, FName Type)
	{
		FEonformTerrainPortDescriptor P; P.Name = Name; P.DisplayName = DisplayName; P.DataType = Type; return P;
	}
	FEonformTerrainParameterDescriptor Number(FName Name, const TCHAR* DisplayName, double Default, double Min, double Max)
	{
		FEonformTerrainParameterDescriptor P; P.Name=Name; P.DisplayName=DisplayName; P.Type=EEonformTerrainParameterType::Number; P.DefaultNumber=Default; P.bHasMinimum=true; P.Minimum=Min; P.bHasMaximum=true; P.Maximum=Max; return P;
	}
	FEonformTerrainParameterDescriptor Integer(FName Name, const TCHAR* DisplayName, int64 Default, int64 Min, int64 Max)
	{
		FEonformTerrainParameterDescriptor P; P.Name=Name; P.DisplayName=DisplayName; P.Type=EEonformTerrainParameterType::Integer; P.DefaultInteger=Default; P.bHasMinimum=true; P.Minimum=Min; P.bHasMaximum=true; P.Maximum=Max; return P;
	}
	FEonformTerrainParameterDescriptor Boolean(FName Name, const TCHAR* DisplayName, bool Default)
	{
		FEonformTerrainParameterDescriptor P; P.Name=Name; P.DisplayName=DisplayName; P.Type=EEonformTerrainParameterType::Boolean; P.DefaultBoolean=Default; return P;
	}
	FEonformTerrainParameterDescriptor Name(FName ParamName, const TCHAR* DisplayName, FName Default, std::initializer_list<FName> Options = {})
	{
		FEonformTerrainParameterDescriptor P; P.Name=ParamName; P.DisplayName=DisplayName; P.Type=EEonformTerrainParameterType::Name; P.DefaultName=Default; for (FName O : Options) P.NameOptions.Add(O); return P;
	}

	const FEonformTerrainValue* Input(const FEonformTerrainNodeInputs& Inputs, FName Name)
	{
		const FEonformTerrainValue* const* P = Inputs.Find(Name); return P ? *P : nullptr;
	}

	const FEonformScalarField* AsField(const FEonformTerrainValue* V)
	{
		if (!V || !V->IsValid()) return nullptr;
		if (V->Type == EEonformTerrainValueType::ScalarField) return &V->ScalarField;
		if (V->Type == EEonformTerrainValueType::Terrain) return V->TerrainDataset.FindScalarField(EonformTerrainFieldNames::Height);
		return nullptr;
	}

	bool PublishLike(const FEonformTerrainValue& Prototype, FEonformScalarField&& Field, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		if (Prototype.Type == EEonformTerrainValueType::ScalarField)
		{
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeScalarField(MoveTemp(Field))); return true;
		}
		if (Prototype.Type == EEonformTerrainValueType::Terrain)
		{
			Field.Descriptor.Name = EonformTerrainFieldNames::Height;
			FEonformTerrainDataset Dataset = Prototype.TerrainDataset;
			if (!Dataset.SetScalarField(MoveTemp(Field))) { Error = TEXT("Utility node could not publish Height."); return false; }
			Out.Outputs.Add(TEXT("Out"), FEonformTerrainValue::MakeTerrain(MoveTemp(Dataset), Prototype.HeightScale)); return true;
		}
		Error = TEXT("Utility node received an unsupported value type."); return false;
	}

	bool EvalMath(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* AValue = Input(Inputs, TEXT("A"));
		const FEonformScalarField* A = AsField(AValue); if (!AValue || !A) { Error=TEXT("Math requires A terrain/scalar input."); return false; }
		const FEonformScalarField* B = AsField(Input(Inputs, TEXT("B")));
		const FEonformScalarField* C = AsField(Input(Inputs, TEXT("C")));
		FEonformTerrainUtilityMathSettings Settings; Settings.Expression = Node.GetName(TEXT("Expression"), TEXT("a")).ToString(); Settings.bNormalizedCoordinates = Node.GetBool(TEXT("NormalizedCoordinates"), true);
		FEonformScalarField Result; if (!FEonformTerrainUtilityOps::EvaluateMath(*A, B, C, Settings, Result, &Error)) return false;
		return PublishLike(*AValue, MoveTemp(Result), Out, Error);
	}

	bool EvalCompare(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformScalarField* A=AsField(Input(Inputs,TEXT("A"))); const FEonformScalarField* B=AsField(Input(Inputs,TEXT("B")));
		if (!A || !B) { Error=TEXT("Compare requires A and B terrain/scalar inputs."); return false; }
		FEonformScalarField Result; FEonformColorField Color;
		if (!FEonformTerrainUtilityOps::Compare(*A,*B,static_cast<float>(Node.GetNumber(TEXT("Ratio"),0.25)),Node.GetBool(TEXT("Perpendicular"),false),Node.GetBool(TEXT("Swap"),false),Result,&Color,&Error)) return false;
		Out.Outputs.Add(TEXT("Out"),FEonformTerrainValue::MakeScalarField(MoveTemp(Result))); Out.Outputs.Add(TEXT("Color"),FEonformTerrainValue::MakeColor(MoveTemp(Color))); return true;
	}

	bool EvalMask(const FEonformTerrainNode&, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* AfterV=Input(Inputs,TEXT("After")); const FEonformTerrainValue* BeforeV=Input(Inputs,TEXT("Before")); const FEonformScalarField* Mask=AsField(Input(Inputs,TEXT("Mask")));
		const FEonformScalarField* After=AsField(AfterV); const FEonformScalarField* Before=AsField(BeforeV);
		if (!AfterV || !BeforeV || !After || !Before || !Mask || AfterV->Type != BeforeV->Type) { Error=TEXT("Mask requires matching After/Before terrain or scalar inputs plus a scalar Mask."); return false; }
		FEonformScalarField Result; if (!FEonformTerrainUtilityOps::ApplyMask(*After,*Before,*Mask,Result,&Error)) return false; return PublishLike(*AfterV,MoveTemp(Result),Out,Error);
	}

	bool EvalSeamless(const FEonformTerrainNode& Node, const FEonformTerrainNodeInputs& Inputs, const FEonformTerrainEvaluationContext&, FEonformTerrainNodeEvaluation& Out, FString& Error)
	{
		const FEonformTerrainValue* V=Input(Inputs,TEXT("Input")); if (!V || !V->IsValid()) { Error=TEXT("Seamless requires Input."); return false; }
		const float Edge=static_cast<float>(Node.GetNumber(TEXT("Edge"),0.12)); const float SX=static_cast<float>(Node.GetNumber(TEXT("ShiftX"),0.0)); const float SY=static_cast<float>(Node.GetNumber(TEXT("ShiftY"),0.0));
		if (V->Type==EEonformTerrainValueType::Color) { FEonformColorField R; if(!FEonformTerrainUtilityOps::MakeSeamless(V->ColorField,Edge,SX,SY,R,&Error)) return false; Out.Outputs.Add(TEXT("Out"),FEonformTerrainValue::MakeColor(MoveTemp(R))); return true; }
		const FEonformScalarField* F=AsField(V); if(!F){Error=TEXT("Seamless supports terrain, scalar, or color inputs."); return false;} FEonformScalarField R; if(!FEonformTerrainUtilityOps::MakeSeamless(*F,Edge,SX,SY,R,&Error)) return false; return PublishLike(*V,MoveTemp(R),Out,Error);
	}

	bool EvalRepeat(const FEonformTerrainNode& Node,const FEonformTerrainNodeInputs& Inputs,const FEonformTerrainEvaluationContext&,FEonformTerrainNodeEvaluation& Out,FString& Error)
	{
		const FEonformTerrainValue* V=Input(Inputs,TEXT("Input")); const FEonformScalarField* F=AsField(V); if(!V||!F){Error=TEXT("Repeat requires terrain/scalar Input.");return false;} FEonformScalarField R;
		if(!FEonformTerrainUtilityOps::Repeat(*F,static_cast<int32>(Node.GetInteger(TEXT("Tiles"),2)),Node.GetBool(TEXT("CompensateHeight"),false),static_cast<float>(Node.GetNumber(TEXT("Height"),1.0)),R,&Error))return false; return PublishLike(*V,MoveTemp(R),Out,Error);
	}

	bool EvalPassthrough(const FEonformTerrainNode&,const FEonformTerrainNodeInputs& Inputs,const FEonformTerrainEvaluationContext&,FEonformTerrainNodeEvaluation& Out,FString& Error)
	{
		const FEonformTerrainValue* V=Input(Inputs,TEXT("Input")); if(!V||!V->IsValid()){Error=TEXT("Utility passthrough requires Input.");return false;} Out.Outputs.Add(TEXT("Out"),*V); return true;
	}

	bool EvalGate(const FEonformTerrainNode& Node,const FEonformTerrainNodeInputs& Inputs,const FEonformTerrainEvaluationContext&,FEonformTerrainNodeEvaluation& Out,FString& Error)
	{
		const FEonformTerrainValue* V=Node.GetBool(TEXT("Enabled"),true)?Input(Inputs,TEXT("Input")):Input(Inputs,TEXT("Fallback"));
		if(!V||!V->IsValid()){Error=TEXT("Gate selected an unconnected/invalid input.");return false;} Out.Outputs.Add(TEXT("Out"),*V); return true;
	}

	bool EvalRoute(const FEonformTerrainNode&,const FEonformTerrainNodeInputs& Inputs,const FEonformTerrainEvaluationContext&,FEonformTerrainNodeEvaluation& Out,FString& Error)
	{
		const FEonformTerrainValue* V=Input(Inputs,TEXT("Input"));if(!V||!V->IsValid()){Error=TEXT("Route requires Input.");return false;} for(int32 I=1;I<=4;++I) Out.Outputs.Add(FName(*FString::Printf(TEXT("Out%d"),I)),*V); Out.Outputs.Add(TEXT("Out"),*V); return true;
	}

	bool EvalSwitch(const FEonformTerrainNode& Node,const FEonformTerrainNodeInputs& Inputs,const FEonformTerrainEvaluationContext&,FEonformTerrainNodeEvaluation& Out,FString& Error)
	{
		const int32 Index=FMath::Clamp(static_cast<int32>(Node.GetInteger(TEXT("Index"),0)),0,3); const FEonformTerrainValue* V=Input(Inputs,FName(*FString::Printf(TEXT("Input%d"),Index+1)));
		if(!V||!V->IsValid()){Error=TEXT("Switch selected an unconnected/invalid input.");return false;} Out.Outputs.Add(TEXT("Out"),*V); return true;
	}

	bool EvalDataExtractor(const FEonformTerrainNode& Node,const FEonformTerrainNodeInputs& Inputs,const FEonformTerrainEvaluationContext&,FEonformTerrainNodeEvaluation& Out,FString& Error)
	{
		const FEonformTerrainValue* V=Input(Inputs,TEXT("Terrain"));if(!V||V->Type!=EEonformTerrainValueType::Terrain||!V->IsValid()){Error=TEXT("DataExtractor requires Terrain.");return false;} const FName FieldName=Node.GetName(TEXT("Field"),EonformTerrainFieldNames::Height); const FEonformScalarField* F=V->TerrainDataset.FindScalarField(FieldName); if(!F){Error=FString::Printf(TEXT("DataExtractor field '%s' does not exist."),*FieldName.ToString());return false;} Out.Outputs.Add(TEXT("Out"),FEonformTerrainValue::MakeScalarField(*F)); Out.Outputs.Add(TEXT("Terrain"),*V); return true;
	}

	bool EvalAccumulator(const FEonformTerrainNode& Node,const FEonformTerrainNodeInputs& Inputs,const FEonformTerrainEvaluationContext&,FEonformTerrainNodeEvaluation& Out,FString& Error)
	{
		const FEonformTerrainValue* First=nullptr; TArray<const FEonformScalarField*> Fields; for(int32 I=1;I<=4;++I){const FEonformTerrainValue* V=Input(Inputs,FName(*FString::Printf(TEXT("Input%d"),I))); if(!V)continue; if(!First)First=V; if(First->Type!=V->Type){Error=TEXT("Accumulator inputs must share a value type.");return false;} const FEonformScalarField* F=AsField(V); if(!F){Error=TEXT("Accumulator currently supports terrain/scalar inputs.");return false;} Fields.Add(F);} if(!First||Fields.IsEmpty()){Error=TEXT("Accumulator requires at least one input.");return false;}
		FEonformScalarField R=*Fields[0]; const FName Mode=Node.GetName(TEXT("Mode"),TEXT("Average")); for(int32 Y=0;Y<R.Domain.Dimensions.Y;++Y)for(int32 X=0;X<R.Domain.Dimensions.X;++X){float V=Fields[0]->AtInterior(X,Y); if(Mode==TEXT("Max")||Mode==TEXT("Min")){for(int32 I=1;I<Fields.Num();++I)V=Mode==TEXT("Max")?FMath::Max(V,Fields[I]->AtInterior(X,Y)):FMath::Min(V,Fields[I]->AtInterior(X,Y));}else{for(int32 I=1;I<Fields.Num();++I)V+=Fields[I]->AtInterior(X,Y);if(Mode==TEXT("Average"))V/=Fields.Num();}R.AtInterior(X,Y)=V;} return PublishLike(*First,MoveTemp(R),Out,Error);
	}

	bool EvalLayers(const FEonformTerrainNode& Node,const FEonformTerrainNodeInputs& Inputs,const FEonformTerrainEvaluationContext&,FEonformTerrainNodeEvaluation& Out,FString& Error)
	{
		const FEonformTerrainValue* BaseV=Input(Inputs,TEXT("Input1"));const FEonformScalarField* Base=AsField(BaseV);if(!BaseV||!Base){Error=TEXT("Layers requires Input1 terrain/scalar.");return false;}FEonformScalarField R=*Base;
		for(int32 I=2;I<=4;++I){const FEonformTerrainValue* V=Input(Inputs,FName(*FString::Printf(TEXT("Input%d"),I)));if(!V)continue;if(V->Type!=BaseV->Type){Error=TEXT("Layers inputs must share a value type.");return false;}const FEonformScalarField* F=AsField(V);if(!F||F->Domain!=R.Domain){Error=TEXT("Layers inputs must share a domain.");return false;}const float Opacity=FMath::Clamp(static_cast<float>(Node.GetNumber(FName(*FString::Printf(TEXT("Opacity%d"),I)),1.0)),0.0f,1.0f);for(int32 Y=0;Y<R.Domain.Dimensions.Y;++Y)for(int32 X=0;X<R.Domain.Dimensions.X;++X)R.AtInterior(X,Y)=FMath::Lerp(R.AtInterior(X,Y),F->AtInterior(X,Y),Opacity);}return PublishLike(*BaseV,MoveTemp(R),Out,Error);
	}

	bool EvalMixer(const FEonformTerrainNode& Node,const FEonformTerrainNodeInputs& Inputs,const FEonformTerrainEvaluationContext&,FEonformTerrainNodeEvaluation& Out,FString& Error)
	{
		TArray<FEonformTerrainUtilityColorLayer> Layers; for(int32 I=1;I<=4;++I){const FEonformTerrainValue* V=Input(Inputs,FName(*FString::Printf(TEXT("Color%d"),I)));if(!V)continue;if(V->Type!=EEonformTerrainValueType::Color||!V->IsValid()){Error=TEXT("Mixer color inputs must be valid Color fields.");return false;}FEonformTerrainUtilityColorLayer L;L.Color=&V->ColorField;if(I>1){const FEonformTerrainValue* M=Input(Inputs,FName(*FString::Printf(TEXT("Mask%d"),I)));if(M){if(M->Type!=EEonformTerrainValueType::ScalarField){Error=TEXT("Mixer masks must be scalar fields.");return false;}L.Mask=&M->ScalarField;}L.Opacity=static_cast<float>(Node.GetNumber(FName(*FString::Printf(TEXT("Opacity%d"),I)),1.0));L.BlendMode=Node.GetName(FName(*FString::Printf(TEXT("Mode%d"),I)),TEXT("Blend"));}Layers.Add(L);}FEonformColorField R;if(!FEonformTerrainUtilityOps::MixColors(Layers,R,&Error))return false;Out.Outputs.Add(TEXT("Out"),FEonformTerrainValue::MakeColor(MoveTemp(R)));return true;
	}

	void Register(FName Type,const TCHAR* Display,const TCHAR* Description,TArray<FEonformTerrainPortDescriptor> Inputs,TArray<FEonformTerrainPortDescriptor> Outputs,TArray<FEonformTerrainParameterDescriptor> Params,FEonformTerrainNodeEvaluator Evaluator)
	{
		FEonformTerrainNodeDescriptor D;D.Type=Type;D.DisplayName=Display;D.Category=TEXT("Utility");D.Description=Description;D.Inputs=MoveTemp(Inputs);D.Outputs=MoveTemp(Outputs);D.Parameters=MoveTemp(Params);FEonformTerrainNodeDescriptorRegistry::Register(D);FEonformTerrainNodeRegistry::Register(Type,MoveTemp(Evaluator));
	}
}

void RegisterEonformUtilityNodes()
{
	const FName Any=TEXT("Any"), Scalar=TEXT("ScalarField"), Terrain=TEXT("Terrain"), Color=TEXT("Color");
	Register(EonformTerrainNodeTypes::Math,TEXT("Math"),TEXT("Evaluates a deterministic per-sample expression over A/B/C and normalized x/y coordinates."),{Port(TEXT("A"),TEXT("A"),Any),Port(TEXT("B"),TEXT("B"),Any),Port(TEXT("C"),TEXT("C"),Any)},{Port(TEXT("Out"),TEXT("Out"),Any)},{Name(TEXT("Expression"),TEXT("Expression"),TEXT("a")),Boolean(TEXT("NormalizedCoordinates"),TEXT("Normalized Coordinates"),true)},EvalMath);
	Register(EonformTerrainNodeTypes::Compare,TEXT("Compare"),TEXT("Creates a normalized difference map and optional diagnostic color comparison."),{Port(TEXT("A"),TEXT("A"),Any),Port(TEXT("B"),TEXT("B"),Any)},{Port(TEXT("Out"),TEXT("Difference"),Scalar),Port(TEXT("Color"),TEXT("Color"),Color)},{Number(TEXT("Ratio"),TEXT("Ratio"),0.25,0.001,1.0),Boolean(TEXT("Perpendicular"),TEXT("Perpendicular"),false),Boolean(TEXT("Swap"),TEXT("Swap"),false)},EvalCompare);
	Register(EonformTerrainNodeTypes::Mask,TEXT("Mask"),TEXT("Post-masks an effect by blending After back toward Before using a scalar mask."),{Port(TEXT("After"),TEXT("After"),Any),Port(TEXT("Before"),TEXT("Before"),Any),Port(TEXT("Mask"),TEXT("Mask"),Scalar)},{Port(TEXT("Out"),TEXT("Out"),Any)},{},EvalMask);
	Register(EonformTerrainNodeTypes::Seamless,TEXT("Seamless"),TEXT("Converts terrain, scalar, or color data into an edge-blended tileable field."),{Port(TEXT("Input"),TEXT("Input"),Any)},{Port(TEXT("Out"),TEXT("Out"),Any)},{Number(TEXT("Edge"),TEXT("Edge"),0.12,0.001,0.5),Number(TEXT("ShiftX"),TEXT("Shift X"),0.0,-1.0,1.0),Number(TEXT("ShiftY"),TEXT("Shift Y"),0.0,-1.0,1.0)},EvalSeamless);
	Register(EonformTerrainNodeTypes::Repeat,TEXT("Repeat"),TEXT("Repeats a terrain or scalar field across its existing domain."),{Port(TEXT("Input"),TEXT("Input"),Any)},{Port(TEXT("Out"),TEXT("Out"),Any)},{Integer(TEXT("Tiles"),TEXT("Tiles"),2,1,64),Boolean(TEXT("CompensateHeight"),TEXT("Compensate Height"),false),Number(TEXT("Height"),TEXT("Height"),1.0,0.0,8.0)},EvalRepeat);
	Register(EonformTerrainNodeTypes::Gate,TEXT("Gate"),TEXT("Selects the primary input when enabled and an optional fallback when disabled."),{Port(TEXT("Input"),TEXT("Input"),Any),Port(TEXT("Fallback"),TEXT("Fallback"),Any)},{Port(TEXT("Out"),TEXT("Out"),Any)},{Boolean(TEXT("Enabled"),TEXT("Enabled"),true)},EvalGate);
	Register(EonformTerrainNodeTypes::Route,TEXT("Route"),TEXT("Fans one value out to multiple graph branches without changing it."),{Port(TEXT("Input"),TEXT("Input"),Any)},{Port(TEXT("Out"),TEXT("Out"),Any),Port(TEXT("Out1"),TEXT("1"),Any),Port(TEXT("Out2"),TEXT("2"),Any),Port(TEXT("Out3"),TEXT("3"),Any),Port(TEXT("Out4"),TEXT("4"),Any)},{},EvalRoute);
	Register(EonformTerrainNodeTypes::Switch,TEXT("Switch"),TEXT("Selects one of four compatible graph values by index."),{Port(TEXT("Input1"),TEXT("1"),Any),Port(TEXT("Input2"),TEXT("2"),Any),Port(TEXT("Input3"),TEXT("3"),Any),Port(TEXT("Input4"),TEXT("4"),Any)},{Port(TEXT("Out"),TEXT("Out"),Any)},{Integer(TEXT("Index"),TEXT("Index"),0,0,3)},EvalSwitch);
	Register(EonformTerrainNodeTypes::DataExtractor,TEXT("DataExtractor"),TEXT("Extracts a named semantic scalar field from a terrain dataset."),{Port(TEXT("Terrain"),TEXT("Terrain"),Terrain)},{Port(TEXT("Out"),TEXT("Field"),Scalar),Port(TEXT("Terrain"),TEXT("Terrain"),Terrain)},{Name(TEXT("Field"),TEXT("Field"),EonformTerrainFieldNames::Height)},EvalDataExtractor);
	Register(EonformTerrainNodeTypes::Accumulator,TEXT("Accumulator"),TEXT("Accumulates up to four terrain/scalar inputs using average, sum, min, or max."),{Port(TEXT("Input1"),TEXT("1"),Any),Port(TEXT("Input2"),TEXT("2"),Any),Port(TEXT("Input3"),TEXT("3"),Any),Port(TEXT("Input4"),TEXT("4"),Any)},{Port(TEXT("Out"),TEXT("Out"),Any)},{Name(TEXT("Mode"),TEXT("Mode"),TEXT("Average"),{TEXT("Average"),TEXT("Sum"),TEXT("Min"),TEXT("Max")})},EvalAccumulator);
	Register(EonformTerrainNodeTypes::Layers,TEXT("Layers"),TEXT("Builds a simple ordered terrain/scalar layer stack with per-layer opacity."),{Port(TEXT("Input1"),TEXT("1"),Any),Port(TEXT("Input2"),TEXT("2"),Any),Port(TEXT("Input3"),TEXT("3"),Any),Port(TEXT("Input4"),TEXT("4"),Any)},{Port(TEXT("Out"),TEXT("Out"),Any)},{Number(TEXT("Opacity2"),TEXT("Opacity 2"),1.0,0.0,1.0),Number(TEXT("Opacity3"),TEXT("Opacity 3"),1.0,0.0,1.0),Number(TEXT("Opacity4"),TEXT("Opacity 4"),1.0,0.0,1.0)},EvalLayers);
	Register(EonformTerrainNodeTypes::Mixer,TEXT("Mixer"),TEXT("Layers up to four color fields with optional masks, opacity, and blend modes."),{Port(TEXT("Color1"),TEXT("Color 1"),Color),Port(TEXT("Color2"),TEXT("Color 2"),Color),Port(TEXT("Mask2"),TEXT("Mask 2"),Scalar),Port(TEXT("Color3"),TEXT("Color 3"),Color),Port(TEXT("Mask3"),TEXT("Mask 3"),Scalar),Port(TEXT("Color4"),TEXT("Color 4"),Color),Port(TEXT("Mask4"),TEXT("Mask 4"),Scalar)},{Port(TEXT("Out"),TEXT("Out"),Color)},{Number(TEXT("Opacity2"),TEXT("Opacity 2"),1.0,0.0,1.0),Name(TEXT("Mode2"),TEXT("Mode 2"),TEXT("Blend"),{TEXT("Blend"),TEXT("Add"),TEXT("Multiply"),TEXT("Screen"),TEXT("Min"),TEXT("Max")}),Number(TEXT("Opacity3"),TEXT("Opacity 3"),1.0,0.0,1.0),Name(TEXT("Mode3"),TEXT("Mode 3"),TEXT("Blend")),Number(TEXT("Opacity4"),TEXT("Opacity 4"),1.0,0.0,1.0),Name(TEXT("Mode4"),TEXT("Mode 4"),TEXT("Blend"))},EvalMixer);
	Register(EonformTerrainNodeTypes::Chokepoint,TEXT("Chokepoint"),TEXT("Explicit dependency/cache checkpoint that passes data unchanged."),{Port(TEXT("Input"),TEXT("Input"),Any)},{Port(TEXT("Out"),TEXT("Out"),Any)},{},EvalPassthrough);
	Register(EonformTerrainNodeTypes::Reseed,TEXT("Reseed"),TEXT("Explicit deterministic seed boundary for reusable/composite graph operations; passes existing data unchanged."),{Port(TEXT("Input"),TEXT("Input"),Any)},{Port(TEXT("Out"),TEXT("Out"),Any)},{Integer(TEXT("Seed"),TEXT("Seed"),1337,-2147483647,2147483647)},EvalPassthrough);
	Register(EonformTerrainNodeTypes::Var,TEXT("Var"),TEXT("Named graph value checkpoint for readable reusable graph branches."),{Port(TEXT("Input"),TEXT("Input"),Any)},{Port(TEXT("Out"),TEXT("Out"),Any)},{Name(TEXT("Name"),TEXT("Name"),TEXT("Value"))},EvalPassthrough);
}
