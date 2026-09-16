#include "CoreMinimal.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
// UE4.24 does not export these Persona helpers. Compile the installed engine's
// implementation locally, without maintaining a second triangulation algorithm.
#include "AnimationBlendSpaceHelpers.cpp"
#include "AnimationBlendSpace1DHelpers.cpp"

void RebuildBridgeBlendSpace(UBlendSpaceBase* Space)
{
    Space->ValidateSampleData();
    Space->EmptyGridElements();
    if (Cast<UBlendSpace1D>(Space))
    {
        FLineElementGenerator Generator;
        Generator.Init(Space->GetBlendParameter(0));
        for (const FBlendSample& Sample : Space->GetBlendSamples())
            if (Sample.bIsValid) Generator.SamplePointList.Add(Sample.SampleValue.X);
        if (Generator.SamplePointList.Num())
        {
            Generator.CalculateEditorElements();
            TArray<int32> Mapping;
            for (float Point : Generator.SamplePointList)
            {
                int32 Found = INDEX_NONE;
                for (int32 I = 0; I < Space->GetBlendSamples().Num(); ++I)
                    if (Space->GetBlendSample(I).SampleValue.X == Point) { Found = I; break; }
                Mapping.Add(Found);
            }
            Space->FillupGridElements(Mapping, Generator.EditorElements);
        }
    }
    else
    {
        FDelaunayTriangleGenerator Generator;
        FBlendSpaceGrid Grid;
        Grid.SetGridInfo(Space->GetBlendParameter(0), Space->GetBlendParameter(1));
        Generator.SetGridBox(Space->GetBlendParameter(0), Space->GetBlendParameter(1));
        for (int32 I = 0; I < Space->GetBlendSamples().Num(); ++I)
            if (Space->GetBlendSample(I).bIsValid) Generator.AddSamplePoint(Space->GetBlendSample(I).SampleValue, I);
        if (Space->GetBlendSamples().Num())
        {
            Generator.Triangulate();
            Grid.GenerateGridElements(Generator.GetSamplePointList(), Generator.GetTriangleList());
            if (Generator.GetTriangleList().Num()) Space->FillupGridElements(Generator.GetIndiceMapping(), Grid.GetElements());
        }
    }
}
