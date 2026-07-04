#include "Address.h"

namespace TF3DHud::Address
{
	REL::Relocation<ActivateAnimationGraphManager_t*> ActivateAnimationGraphManager{ REL::ID(950096) };
	REL::Relocation<AddArmorToBiped_t*> AddArmorToBiped{ REL::ID(724793) };
	REL::Relocation<ActorAnimationGraphManagerCallback_t*> ActorPostLoadAnimationGraphManager{ REL::ID{ 348865, 2230547 } };
	REL::Relocation<ActorAnimationGraphManagerCallback_t*> ActorPreLoadAnimationGraphManager{ REL::ID{ 1053762, 2230546 } };
	REL::Relocation<ActorAnimationGraphManagerCallback_t*> ActorPreUpdateAnimationGraphManager{ REL::ID{ 442032, 2230545 } };
	REL::Relocation<ActorBoolCallback_t*> GetFreezeGraphLocomotionChannel{ REL::ID{ 458107, 2230387 } };
	REL::Relocation<ActorFloatCallback_t*> GetActorDirection{ REL::ID{ 279535, 2230411 } };
	REL::Relocation<BipedAnimCtor_t*> BipedAnimCtor{ REL::ID(724121) };
	REL::Relocation<BipedAnimDtor_t*> BipedAnimDtor{ REL::ID(1494601) };
	REL::Relocation<BShkbAnimationGraphCtor_t*> ConstructBShkbAnimationGraph{ REL::ID{ 1074981, 2256827 } };
	REL::Relocation<CalculateBodyTintColor_t*> CalculateBodyTintColor{ REL::ID(134537) };
	REL::Relocation<CalculateSpeedAdjustToSyncAnimationCycles_t*> CalculateSpeedAdjustToSyncAnimationCycles{
		REL::ID{ 552450, 2214290 }
	};
	REL::Relocation<ConvertNodeTree_t*> ConvertNodeTree{ REL::ID(633230) };
	REL::Relocation<CreateAnimationGraphManager_t*> CreateAnimationGraphManager{ REL::ID{ 532453, 2214553 } };
	REL::Relocation<CreateClothFor3D_t*> CreateClothFor3D{ REL::ID(1322043) };
	REL::Relocation<CreateHeadForNPC_t*> CreateHeadForNPC{ REL::ID(1455012) };
	REL::Relocation<DestroyAdjustmentArena_t*> DestroyAdjustmentArena{ REL::ID(1000046) };
	REL::Relocation<DoAdjustSkinComplexion_t*> DoAdjustSkinComplexion{ REL::ID(1295935) };
	REL::Relocation<GenerateFlattenedHeadPartArray_t*> GenerateFlattenedHeadPartArray{ REL::ID(72114) };
	REL::Relocation<ForceUpgradeTextures_t*> ForceUpgradeTextures{ REL::ID{ 1417022, 2229490 } };
	REL::Relocation<GetActorBodyPart3D_t*> GetActorBodyPart3D{ REL::ID(157573) };
	REL::Relocation<GetActiveContourFromHolder_t*> GetActiveContourFromHolder{ REL::ID(1388221) };
	REL::Relocation<GetAll3DUpdateFlags_t*> GetAll3DUpdateFlags{ REL::ID{ 582098, 2232393 } };
	REL::Relocation<GetCellPriority_t*> GetCellPriority{ REL::ID{ 665767, 2192052 } };
	REL::Relocation<GetChargenModelName_t*> GetChargenModelName{ REL::ID(1493791) };
	REL::Relocation<GetDefaultAction_t*> GetDefaultObjectForActionInitializeToBaseState{ REL::ID(639576) };
	REL::Relocation<GetDefaultAction_t*> GetDefaultObjectForActionInstantInitializeToBaseState{ REL::ID{ 1517112, 2214310 } };
	REL::Relocation<GetDefaultRaceHeadPart_t*> GetDefaultRaceHeadPart{ REL::ID(1120148) };
	REL::Relocation<GetGraphSpeedForRequestedSpeedAndDirection_t*> GetGraphSpeedForRequestedSpeedAndDirection{
		REL::ID(289793)
	};
	REL::Relocation<GetGraphVariableBool_t*> GetBShkbGraphVariableBool{ REL::ID(815875) };
	REL::Relocation<GetGraphVariableFloat_t*> GetBShkbGraphVariableFloat{ REL::ID(1254547) };
	REL::Relocation<GetGraphVariableInt_t*> GetBShkbGraphVariableInt{ REL::ID(110974) };
	REL::Relocation<GetNPCHeadPart_t*> GetNPCHeadPart{ REL::ID(946253) };
	REL::Relocation<GetNumSegments_t*> GetNumSegments{ REL::ID(331465) };
	REL::Relocation<GetSubSegmentCount_t*> GetSubSegmentCount{ REL::ID(374480) };
	REL::Relocation<GetProjectForActor_t*> GetProjectForActor{ REL::ID{ 804224, 2236395 } };
	REL::Relocation<GetReferenceScale_t*> GetReferenceScale{ REL::ID{ 911188, 2200892 } };
	REL::Relocation<GetSkin_t*> GetSkin{ REL::ID(1042540) };
	REL::Relocation<GetSubSegmentIndex_t*> GetSubSegmentIndex{ REL::ID(453731) };
	REL::Relocation<GetUserIndex_t*> GetUserIndex{ REL::ID(985810) };
	REL::Relocation<InitWornObject_t*> InitWornObject{ REL::ID(1374346) };
	// IDA OG 1.10.163: BSFaceGenUtils::CreateHeadForNPC applies these after
	// headpart creation. The preview customization pass reuses that apply half
	// without replacing the existing BSFaceGenNiNode/headpart tree.
	REL::Relocation<ApplyAllCustomizationMorphs_t*> ApplyAllCustomizationMorphs{ REL::ID(877425) };
	REL::Relocation<ApplyWeightFaceMorph_t*> ApplyWeightFaceMorph{ REL::ID(1582451) };
	REL::Relocation<PrepareHeadForShaders_t*> PrepareHeadForShaders{ REL::ID(305300) };
	REL::Relocation<ScaleFaceBones_t*> ScaleFaceBones{ REL::ID{ 839112, 2207358 } };
	REL::Relocation<ScaleFaceSkinBones_t*> ScaleFaceSkinBones{ REL::ID(1011655) };
	REL::Relocation<UpdateAllChildrenMorphData_t*> UpdateAllChildrenMorphData{ REL::ID{ 213436, 2209481 } };
	// IDA OG 1.10.163: BSBehaviorGraphSwapSingleton::InitializeSubGraph at 0x1416F3890.
	// The AE ID is not present in the local OG->AE mapping CSV yet.
	REL::Relocation<InitializeSubGraph_t*> InitializeSubGraph{ REL::ID(649876) };
	REL::Relocation<InterpretAction_t*> InterpretAction{ REL::ID{ 10433, 2229530 } };
	REL::Relocation<NotifyAnimationGraphImpl_t*> NotifyAnimationGraphImpl{ REL::ID{ 1379025, 2214561 } };
	REL::Relocation<QUpdateEditorDeadActorModel_t*> QUpdateEditorDeadActorModel{ REL::ID{ 16281, 2231571 } };
	REL::Relocation<QTiledLighting_t*> QTiledLighting{ REL::ID{ 1154650, 2318371 } };
	REL::Relocation<ResetFaceGenCurrentMorphs_t*> ResetFaceGenCurrentMorphs{ REL::ID(1174798) };
	REL::Relocation<RetrieveSubGraphData_t*> RetrieveSubGraphData{ REL::ID{ 1291992, 2188860 } };
	REL::Relocation<SetAnimationGraphTarget_t*> SetAnimationGraphTarget{ REL::ID{ 1340816, 2214556 } };
	REL::Relocation<SetClothSettleOnTransitionToSim_t*> SetClothSettleOnTransitionToSim{ REL::ID(638869) };
	REL::Relocation<SetClothWorld_t*> SetClothWorld{ REL::ID(19064) };
	REL::Relocation<SetDoTiledLighting_t*> SetDoTiledLighting{ REL::ID{ 716351, 2318370 } };
	// IDA OG 1.10.163: TESObjectREFR::FixDisplayedHeadParts enables all
	// RaceHeadSkinned segments before replaying headpart hair hides.
	REL::Relocation<EnableAllSegments_t*> EnableAllSegments{ REL::ID(246125) };
	REL::Relocation<SetSegmentDisableCount_t*> SetSegmentDisableCount{ REL::ID(1227683) };
	REL::Relocation<SetSegmentEnabled_t*> DisableSegment{ REL::ID(1466142) };
	REL::Relocation<SetSegmentEnabled_t*> EnableSegment{ REL::ID(1134184) };
	// Ghidra OG 1.10.163: BGSMod::Attachment::Mod::TryAttach3DRecurse at 0x1402F3360.
	// Used by PowerArmor::SyncFurnitureVisualsToInventory when replaying OMOD-driven PA visuals.
	REL::Relocation<TryAttachMod3DRecurse_t*> TryAttachMod3DRecurse{ REL::ID(832528) };
	REL::Relocation<TESActionDataCtor_t*> ConstructTESActionData{ REL::ID(1307135) };
	REL::Relocation<TESActionDataDtor_t*> DestroyTESActionData{ REL::ID(229573) };
	REL::Relocation<UpdateAnimationGraphManager_t*> UpdateAnimationGraphManager{ REL::ID{ 1492656, 2214536 } };
	REL::Relocation<UpdateAnimationGraphManagerFloat_t*> UpdateAnimationGraphManagerFloat{ REL::ID(973903) };
	REL::Relocation<UpdateBodyTintColorsOnScene_t*> UpdateBodyTintColorsOnScene{ REL::ID(49935) };
	REL::Relocation<UseSpeedContoursForMovementCalculations_t*> UseSpeedContoursForMovementCalculations{
		REL::ID{ 272153, 2236396 }
	};
	REL::Relocation<FixFaceGenHeadSkinInstances_t*> FixFaceGenHeadSkinInstances{ REL::ID(1131949) };

	REL::Relocation<void (*)(RE::NiAVObject*)> CreateBoneMap{ REL::ID(1131947) };
	REL::Relocation<void**> AnimationSubGraphDataSingleton{ REL::ID(1363506) };
	REL::Relocation<void**> BehaviorGraphSwapSingleton{ REL::ID(153510) };
	REL::Relocation<RE::EquipEventSource*> EquipEventSourceSingleton{ REL::ID{ 485633, 2691240, 4798533 } };
	REL::Relocation<std::uintptr_t> ClipCursor{ REL::ID{ 641385, 4823626 } };
	REL::Relocation<std::uintptr_t> ProcessGraphEventTarget{ REL::ID(1199489) };
	REL::Relocation<std::uintptr_t> RenderPrepassesAndMenusTarget{ REL::ID(1189309) };
	REL::Relocation<std::uintptr_t> Update3DModelTarget{ REL::ID{ 986782, 2231882 } };
	// IDA OG 1.10.163: PowerArmorGeometry::~PowerArmorGeometry at 0x14127A000
	// calls Interface3D::Renderer::Create for "PowerArmorRenderer" at +0x8B
	// and "HUDRainRenderer" at +0xF0. AE call sites still need IDA validation.
	const IDOffset PowerArmorGeometryPowerArmorRendererCreateCall{
		REL::ID{ 1207506 },
		VariantOffset{ 0x8B, 0x0 }
	};
	const IDOffset PowerArmorGeometryHUDRainRendererCreateCall{
		REL::ID{ 1207506 },
		VariantOffset{ 0xF0, 0x0 }
	};
	// IDA OG 1.10.163: Interface3D::anonymous namespace::RenderersRWLock.
	REL::Relocation<std::uintptr_t> Interface3DRenderersRWLock{ REL::ID(778095) };

	const IDOffset D3D11CreateDeviceAndSwapChainCall{
		REL::ID{ 224250, 4492363 },
		VariantOffset{ 0x419, 0x410 }
	};
	const IDOffset Interface3DDrawModelRenderSceneDeferredCall{
		REL::ID{ 917134, 2222570 },
		VariantOffset{ 0x51A, 0x51A }
	};
	const IDOffset RunActorUpdatesCall{
		REL::ID{ 556439, 2227611 },
		VariantOffset{ 0x17, 0x23 }
	};
}
