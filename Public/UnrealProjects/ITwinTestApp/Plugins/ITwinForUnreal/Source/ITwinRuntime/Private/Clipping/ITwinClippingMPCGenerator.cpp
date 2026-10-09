/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingMPCGenerator.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_EDITOR
#include <Clipping/ITwinClippingConstants.h>

#include <Materials/MaterialParameterCollection.h>

namespace ITwinClipping
{

void RegenerateClippingMPC()
{
	UMaterialParameterCollection* MPC = LoadObject<UMaterialParameterCollection>(
		nullptr, TEXT("/ITwinForUnreal/ITwin/Materials/MPC_Clipping"));
	if (!ensure(MPC))
		return;

	MPC->Modify();
	MPC->PreEditChange(nullptr);

	// ---- Preserve existing GUIDs (mandatory for scalars referenced by graph nodes) ----
	TMap<FName, FGuid> ExistingScalarIds, ExistingVectorIds;
	for (auto const& P : MPC->ScalarParameters) ExistingScalarIds.Add(P.ParameterName, P.Id);
	for (auto const& P : MPC->VectorParameters) ExistingVectorIds.Add(P.ParameterName, P.Id);

	const auto AddScalar = [&](FName Name, float Default)
	{
		FCollectionScalarParameter P;
		P.ParameterName = Name;
		P.DefaultValue = Default;
		P.Id = ExistingScalarIds.Contains(Name) ? ExistingScalarIds[Name] : FGuid::NewGuid();
		MPC->ScalarParameters.Add(P);
	};
	const auto AddVector = [&](FName Name, FLinearColor Default)
	{
		FCollectionVectorParameter P;
		P.ParameterName = Name;
		P.DefaultValue = Default;
		P.Id = ExistingVectorIds.Contains(Name) ? ExistingVectorIds[Name] : FGuid::NewGuid();
		MPC->VectorParameters.Add(P);
	};

	// ------------------------------- SCALARS -------------------------------
	MPC->ScalarParameters.Empty();

	// Loop bounds (written by TUpdatePrimitiveCountInMPC).
	AddScalar(TEXT("BoxCount"), 0.f);
	AddScalar(TEXT("PlaneCount"), 0.f);

	// Layout bases, transmitted to the shaders. Real defaults baked in below.
	AddScalar(TEXT("BoxVectorBase"), 0.f);   // in float4 slots
	AddScalar(TEXT("PlaneVectorBase"), 0.f);   // in float4 slots
	AddScalar(TEXT("BoxMaskScalarBase"), 0.f);   // in scalar indices
	AddScalar(TEXT("PlaneMaskScalarBase"), 0.f);   // in scalar indices

	// Per-primitive "affected model groups" bitmasks, ITWIN_CLIPPING_MASK_WORDS scalars each,
	// 24 usable bits per word. Group 0 is reserved for "influenced by nothing".
	// NB: activation no longer travels through Custom Primitive Data; CPD now only carries
	// the model group id (see ITwinClipping3DTilesetHelper).
	const int32 BoxMaskScalarBase = MPC->ScalarParameters.Num();
	for (int32 i = 0; i < ITwin::MAX_CLIPPING_BOXES; ++i)
		for (int32 w = 0; w < ITwin::CLIPPING_MASK_WORDS; ++w)
			AddScalar(*FString::Printf(TEXT("BoxActivationMask_%d_%d"), i, w), 0.f);

	const int32 PlaneMaskScalarBase = MPC->ScalarParameters.Num();
	for (int32 i = 0; i < ITwin::MAX_CLIPPING_PLANES; ++i)
		for (int32 w = 0; w < ITwin::CLIPPING_MASK_WORDS; ++w)
			AddScalar(*FString::Printf(TEXT("PlaneActivationMask_%d_%d"), i, w), 0.f);

	// NB: FlipBoxes_* / FlipPlanes_* are gone.
	//  - box flip is stored in BoxTranslation_i.w
	//  - plane flip is folded into the negated plane equation at upload time

	// ------------------------------- VECTORS -------------------------------
	MPC->VectorParameters.Empty();

	for (int32 i = 0; i < ITwin::MAX_CLIPPING_BOXES; ++i) // interleaved, stride 4
	{
		AddVector(*FString::Printf(TEXT("BoxInvMatrix_col0_%d"), i), FLinearColor::Black);
		AddVector(*FString::Printf(TEXT("BoxInvMatrix_col1_%d"), i), FLinearColor::Black);
		AddVector(*FString::Printf(TEXT("BoxInvMatrix_col2_%d"), i), FLinearColor::Black);
		AddVector(*FString::Printf(TEXT("BoxTranslation_%d"), i), FLinearColor::Black);
	}
	for (int32 i = 0; i < ITwin::MAX_CLIPPING_PLANES; ++i) // stride 1
	{
		AddVector(*FString::Printf(TEXT("PlaneEquation_%d"), i), FLinearColor::Black);
	}

	// -------------------- BAKE COMPUTED BASES AS DEFAULTS --------------------
	// Scalars are packed 4 per float4 at the head of Vectors[]; vectors follow.
	const int32 ScalarSlots = FMath::DivideAndRoundUp(MPC->ScalarParameters.Num(), 4);
	const int32 BoxVectorBase = ScalarSlots;
	const int32 PlaneVectorBase = ScalarSlots + 4 * ITwin::MAX_CLIPPING_BOXES;

	const auto SetScalarDefault = [&](FName Name, float Value)
	{
		FCollectionScalarParameter* P = MPC->ScalarParameters.FindByPredicate(
			[&](FCollectionScalarParameter const& S) { return S.ParameterName == Name; });
		if (ensure(P)) P->DefaultValue = Value;
	};
	SetScalarDefault(TEXT("BoxVectorBase"), static_cast<float>(BoxVectorBase));
	SetScalarDefault(TEXT("PlaneVectorBase"), static_cast<float>(PlaneVectorBase));
	SetScalarDefault(TEXT("BoxMaskScalarBase"), static_cast<float>(BoxMaskScalarBase));
	SetScalarDefault(TEXT("PlaneMaskScalarBase"), static_cast<float>(PlaneMaskScalarBase));

	// -------------------------------- SANITY --------------------------------
	ensure(MPC->VectorParameters.Num()
		== 4 * ITwin::MAX_CLIPPING_BOXES + ITwin::MAX_CLIPPING_PLANES);
	ensure(MPC->ScalarParameters.Num() <= 1024);
	ensure(MPC->VectorParameters.Num() <= 1024);
	// Masks must stay exactly representable in a float mantissa.
	static_assert(ITwin::CLIPPING_MASK_WORDS * 24 <= 64, "model group budget");

	MPC->PostEditChange();   // rebuilds the uniform buffer struct, recompiles dependent materials
	MPC->MarkPackageDirty();
}

}

static FAutoConsoleCommand GITwinRegenerateClippingMPC(
    TEXT("ITwin.Clipping.RegenerateMPC"),
    TEXT("Regenerates MPC_Clipping asset from the C++ layout description."),
    FConsoleCommandDelegate::CreateStatic(&ITwinClipping::RegenerateClippingMPC));

#endif // WITH_EDITOR
