#if WITH_DEV_AUTOMATION_TESTS

#include "EonformGridDomain.h"
#include "EonformTerrainDataset.h"
#include "EonformTerrainEvaluator.h"
#include "EonformTerrainFieldNames.h"
#include "EonformTerrainNodeDescriptor.h"
#include "EonformTerrainRecipe.h"
#include "EonformTerrainUtilityOps.h"
#include "EonformUtilityNodes.h"
#include "Misc/AutomationTest.h"

namespace
{
	FEonformScalarField MakeUtilityField(FName Name)
	{
		const FEonformGridDomain Domain = FEonformGridDomain::Make(FIntPoint(17,17),FVector2d(-8000.0,-8000.0),FVector2d(8000.0,8000.0));
		FEonformFieldDescriptor Descriptor; Descriptor.Name=Name; Descriptor.Unit=EEonformFieldUnit::Normalized; Descriptor.Interpolation=EEonformInterpolation::Bilinear;
		FEonformScalarField Field; Field.Initialize(Domain,Descriptor,0.0f);
		for(int32 Y=0;Y<17;++Y) for(int32 X=0;X<17;++X)
		{
			const float NX=static_cast<float>(X)/16.0f; const float NY=static_cast<float>(Y)/16.0f;
			Field.AtInterior(X,Y)=FMath::Clamp(0.15f+0.55f*NX+0.20f*FMath::Sin(NY*PI*2.0f),-1.0f,1.0f);
		}
		return Field;
	}

	FEonformTerrainEvaluationContext MakeContext()
	{
		FEonformTerrainEvaluationContext Context; Context.HeightScale=2400.0f; Context.SourceDataset.SetScalarField(MakeUtilityField(EonformTerrainFieldNames::Height)); return Context;
	}

	FEonformTerrainRecipe MakeMathRecipe()
	{
		FEonformTerrainRecipe Recipe;
		FEonformTerrainNode Source; Source.Id=FGuid(7701,1,1,1); Source.Type=EonformTerrainNodeTypes::SourceDataset;
		FEonformTerrainNode Math; Math.Id=FGuid(7702,2,2,2); Math.Type=EonformTerrainNodeTypes::Math; Math.NameParameters.Add(TEXT("Expression"),TEXT("a*0.5"));
		FEonformTerrainConnection C; C.FromNode=Source.Id; C.FromOutput=TEXT("Terrain"); C.ToNode=Math.Id; C.ToInput=TEXT("A");
		Recipe.Nodes={Source,Math}; Recipe.Connections={C}; Recipe.OutputNode=Math.Id; return Recipe;
	}

	FEonformTerrainRecipe MakeRoutingRecipe(FName Type)
	{
		FEonformTerrainRecipe Recipe;
		FEonformTerrainNode Source; Source.Id=FGuid(7801,1,1,1); Source.Type=EonformTerrainNodeTypes::SourceDataset;
		FEonformTerrainNode Utility; Utility.Id=FGuid(7802,2,2,GetTypeHash(Type)); Utility.Type=Type;
		if(Type==EonformTerrainNodeTypes::Switch) Utility.IntegerParameters.Add(TEXT("Index"),0);
		FEonformTerrainConnection C; C.FromNode=Source.Id; C.FromOutput=TEXT("Terrain"); C.ToNode=Utility.Id; C.ToInput=Type==EonformTerrainNodeTypes::Switch?TEXT("Input1"):TEXT("Input");
		Recipe.Nodes={Source,Utility}; Recipe.Connections={C}; Recipe.OutputNode=Utility.Id; return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEonformUtilityContractsTest,"Eonform.Core.Graph.UtilityContracts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEonformUtilityContractsTest::RunTest(const FString& Parameters)
{
	RegisterEonformUtilityNodes();
	const FName Types[]={EonformTerrainNodeTypes::Accumulator,EonformTerrainNodeTypes::Chokepoint,EonformTerrainNodeTypes::Compare,EonformTerrainNodeTypes::DataExtractor,EonformTerrainNodeTypes::Gate,EonformTerrainNodeTypes::Layers,EonformTerrainNodeTypes::Mask,EonformTerrainNodeTypes::Math,EonformTerrainNodeTypes::Mixer,EonformTerrainNodeTypes::Repeat,EonformTerrainNodeTypes::Reseed,EonformTerrainNodeTypes::Route,EonformTerrainNodeTypes::Seamless,EonformTerrainNodeTypes::Switch,EonformTerrainNodeTypes::Var};
	for(FName Type:Types)
	{
		FEonformTerrainNodeDescriptor D; TestTrue(*FString::Printf(TEXT("%s descriptor exists"),*Type.ToString()),FEonformTerrainNodeDescriptorRegistry::Get(Type,D));
		TestTrue(*FString::Printf(TEXT("%s evaluator exists"),*Type.ToString()),FEonformTerrainNodeRegistry::IsRegistered(Type));
		TestEqual(*FString::Printf(TEXT("%s is Utility"),*Type.ToString()),D.Category,FString(TEXT("Utility")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEonformUtilityInternalParityTest,"Eonform.Core.Graph.UtilityInternalParity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEonformUtilityInternalParityTest::RunTest(const FString& Parameters)
{
	RegisterEonformUtilityNodes();
	const FEonformTerrainEvaluationContext Context=MakeContext();
	const FEonformTerrainEvaluationResult GraphResult=FEonformTerrainEvaluator::Evaluate(MakeMathRecipe(),Context);
	TestTrue(TEXT("Math graph evaluates"),GraphResult.bSuccess); if(!GraphResult.bSuccess){AddError(GraphResult.Error);return false;}
	const FEonformScalarField* GraphHeight=GraphResult.Dataset.FindScalarField(EonformTerrainFieldNames::Height); const FEonformScalarField* Source=Context.SourceDataset.FindScalarField(EonformTerrainFieldNames::Height);
	TestNotNull(TEXT("Graph Height exists"),GraphHeight); TestNotNull(TEXT("Source Height exists"),Source); if(!GraphHeight||!Source)return false;
	FEonformTerrainUtilityMathSettings Settings; Settings.Expression=TEXT("a*0.5"); FEonformScalarField Direct; FString Error;
	TestTrue(TEXT("Internal Math evaluates"),FEonformTerrainUtilityOps::EvaluateMath(*Source,nullptr,nullptr,Settings,Direct,&Error)); if(!Error.IsEmpty())AddError(Error);
	for(int32 Y=0;Y<Source->Domain.Dimensions.Y;++Y)for(int32 X=0;X<Source->Domain.Dimensions.X;++X)
		TestEqual(TEXT("Graph Math equals internal Math"),GraphHeight->AtInterior(X,Y),Direct.AtInterior(X,Y));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEonformUtilityRoutingTest,"Eonform.Core.Graph.UtilityRouting",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEonformUtilityRoutingTest::RunTest(const FString& Parameters)
{
	RegisterEonformUtilityNodes(); const FEonformTerrainEvaluationContext Context=MakeContext();
	for(FName Type:{EonformTerrainNodeTypes::Chokepoint,EonformTerrainNodeTypes::Route,EonformTerrainNodeTypes::Switch,EonformTerrainNodeTypes::Reseed,EonformTerrainNodeTypes::Var})
	{
		const FEonformTerrainEvaluationResult Result=FEonformTerrainEvaluator::Evaluate(MakeRoutingRecipe(Type),Context);
		TestTrue(*FString::Printf(TEXT("%s routes terrain"),*Type.ToString()),Result.bSuccess); if(!Result.bSuccess)AddError(Result.Error);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEonformUtilityFieldOpsTest,"Eonform.Core.Graph.UtilityFieldOps",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEonformUtilityFieldOpsTest::RunTest(const FString& Parameters)
{
	FEonformScalarField A=MakeUtilityField(TEXT("A")); FEonformScalarField B=A; for(float& V:B.Values)V=1.0f-V;
	FEonformScalarField Comparison; FEonformColorField ComparisonColor; FString Error;
	TestTrue(TEXT("Compare works internally"),FEonformTerrainUtilityOps::Compare(A,B,0.5f,false,false,Comparison,&ComparisonColor,&Error));
	TestTrue(TEXT("Compare color is valid"),ComparisonColor.IsValid());
	FEonformScalarField Mask=A; for(float& V:Mask.Values)V=FMath::Clamp(V,0.0f,1.0f); FEonformScalarField Masked;
	TestTrue(TEXT("Mask works internally"),FEonformTerrainUtilityOps::ApplyMask(B,A,Mask,Masked,&Error));
	FEonformScalarField Seamless; TestTrue(TEXT("Seamless works internally"),FEonformTerrainUtilityOps::MakeSeamless(A,0.2f,0.0f,0.0f,Seamless,&Error));
	FEonformScalarField Repeated; TestTrue(TEXT("Repeat works internally"),FEonformTerrainUtilityOps::Repeat(A,3,false,1.0f,Repeated,&Error));
	TestTrue(TEXT("Reseed mutation is deterministic"),FEonformTerrainUtilityOps::MutateSeed(1337,42)==FEonformTerrainUtilityOps::MutateSeed(1337,42));
	TestTrue(TEXT("Reseed mutation changes seed"),FEonformTerrainUtilityOps::MutateSeed(1337,42)!=1337);
	return true;
}

#endif
