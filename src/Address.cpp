#include "Address.h"

namespace TF3DHud::Address
{
	REL::Relocation<ActivateAnimationGraphManager_t*> ActivateAnimationGraphManager{ REL::ID{ 950096, 2256657 } };
	REL::Relocation<ActorAnimationGraphManagerCallback_t*> ActorPostLoadAnimationGraphManager{ REL::ID{ 348865, 2230547 } };
	REL::Relocation<ActorAnimationGraphManagerCallback_t*> ActorPreLoadAnimationGraphManager{ REL::ID{ 1053762, 2230546 } };
	REL::Relocation<ActorAnimationGraphManagerCallback_t*> ActorPreUpdateAnimationGraphManager{ REL::ID{ 442032, 2230545 } };
	REL::Relocation<ActorBoolCallback_t*> GetFreezeGraphLocomotionChannel{ REL::ID{ 458107, 2230387 } };
	REL::Relocation<ActorFloatCallback_t*> GetActorDirection{ REL::ID{ 279535, 2230411 } };
	REL::Relocation<ArmorAddonUseModel_t*> ArmorAddonUseModel{ REL::ID{ 976291, 2198456 } };
	REL::Relocation<BipedAnimCtor_t*> BipedAnimCtor{ REL::ID{ 724121, 2194336 } };
	REL::Relocation<BipedAnimDtor_t*> BipedAnimDtor{ REL::ID{ 1494601, 2194337 } };
	REL::Relocation<BShkbAnimationGraphCtor_t*> ConstructBShkbAnimationGraph{ REL::ID{ 1074981, 2256827 } };
	REL::Relocation<CalculateBodyTintColor_t*> CalculateBodyTintColor{ REL::ID{ 134537, 2207435 } };
	REL::Relocation<CalculateSpeedAdjustToSyncAnimationCycles_t*> CalculateSpeedAdjustToSyncAnimationCycles{
		REL::ID{ 552450, 2214290 }
	};
	REL::Relocation<ConvertNodeTree_t*> ConvertNodeTree{ REL::ID{ 633230, 2270063 } };
	REL::Relocation<CreateAnimationGraphManager_t*> CreateAnimationGraphManager{ REL::ID{ 532453, 2214553 } };
	REL::Relocation<CreateClothFor3D_t*> CreateClothFor3D{ REL::ID{ 1322043, 2278338 } };
	REL::Relocation<DestroyAdjustmentArena_t*> DestroyAdjustmentArena{ REL::ID{ 1000046, 2235236 } };
	REL::Relocation<DoAdjustSkinComplexion_t*> DoAdjustSkinComplexion{ REL::ID{ 1295935, 2194419 } };
	REL::Relocation<GenerateFlattenedHeadPartArray_t*> GenerateFlattenedHeadPartArray{ REL::ID{ 72114, 2207454 } };
	REL::Relocation<ForceUpgradeTextures_t*> ForceUpgradeTextures{ REL::ID{ 1417022, 2229490 } };
	REL::Relocation<GetActorBodyPart3D_t*> GetActorBodyPart3D{ REL::ID{ 157573, 2227181 } };
	REL::Relocation<GetActiveContourFromHolder_t*> GetActiveContourFromHolder{ REL::ID{ 1388221, 2214292 } };
	REL::Relocation<GetAll3DUpdateFlags_t*> GetAll3DUpdateFlags{ REL::ID{ 582098, 2232393 } };
	REL::Relocation<GetCellPriority_t*> GetCellPriority{ REL::ID{ 665767, 2192052 } };
	REL::Relocation<GetDefaultAction_t*> GetDefaultObjectForActionInitializeToBaseState{ REL::ID{ 639576, 2214309 } };
	REL::Relocation<GetDefaultAction_t*> GetDefaultObjectForActionInstantInitializeToBaseState{ REL::ID{ 1517112, 2214310 } };
	REL::Relocation<GetDefaultRaceHeadPart_t*> GetDefaultRaceHeadPart{ REL::ID{ 1120148, 2208126 } };
	REL::Relocation<GetGraphSpeedForRequestedSpeedAndDirection_t*> GetGraphSpeedForRequestedSpeedAndDirection{
		REL::ID{ 289793, 2257819 }
	};
	REL::Relocation<GetGraphVariableBool_t*> GetBShkbGraphVariableBool{ REL::ID{ 815875, 2256880 } };
	REL::Relocation<GetGraphVariableFloat_t*> GetBShkbGraphVariableFloat{ REL::ID{ 1254547, 2256881 } };
	REL::Relocation<GetGraphVariableInt_t*> GetBShkbGraphVariableInt{ REL::ID{ 110974, 2256882 } };
	// IDA OG 1.10.163: anonymous GetDynamicIdleFullFilePath used by
	// BGSAnimationSystemUtils::RunActionOnActorGetFile for dynamic idle paths.
	REL::Relocation<GetDynamicIdleFullFilePath_t*> GetDynamicIdleFullFilePath{ REL::ID{ 1060877, 2236430 } };
	REL::Relocation<GetKeywordForType_t*> GetKeywordForType{ REL::ID{ 50302, 2227184 } };
	REL::Relocation<GetNPCHeadPart_t*> GetNPCHeadPart{ REL::ID{ 946253, 2207432 } };
	REL::Relocation<GetNumSegments_t*> GetNumSegments{ REL::ID{ 331465, 2194432 } };
	REL::Relocation<GetSubSegmentCount_t*> GetSubSegmentCount{ REL::ID{ 374480, 2270023 } };
	REL::Relocation<GetProjectForActor_t*> GetProjectForActor{ REL::ID{ 804224, 2236395 } };
	REL::Relocation<GetReferenceScale_t*> GetReferenceScale{ REL::ID{ 911188, 2200892 } };
	REL::Relocation<GetSkin_t*> GetSkin{ REL::ID{ 1042540, 2207422 } };
	REL::Relocation<GetSubSegmentIndex_t*> GetSubSegmentIndex{ REL::ID{ 453731, 2270025 } };
	REL::Relocation<GetUserIndex_t*> GetUserIndex{ REL::ID{ 985810, 2270024 } };
	REL::Relocation<InitWornObject_t*> InitWornObject{ REL::ID{ 1374346, 2207455 } };
	REL::Relocation<UpdateAllChildrenMorphData_t*> UpdateAllChildrenMorphData{ REL::ID{ 213436, 2209481 } };
	// IDA AE 1.11.191: BSBehaviorGraphSwapSingleton::InitializeSubGraph at 0x14138D8E0.
	REL::Relocation<InitializeSubGraph_t*> InitializeSubGraph{ REL::ID{ 649876, 2257933 } };
	REL::Relocation<InterpretAction_t*> InterpretAction{ REL::ID{ 10433, 2229530 } };
	REL::Relocation<NotifyAnimationGraphImpl_t*> NotifyAnimationGraphImpl{ REL::ID{ 1379025, 2214561 } };
	REL::Relocation<QUpdateEditorDeadActorModel_t*> QUpdateEditorDeadActorModel{ REL::ID{ 16281, 2231571 } };
	REL::Relocation<QTiledLighting_t*> QTiledLighting{ REL::ID{ 1154650, 2318371 } };
	REL::Relocation<ResetFaceGenCurrentMorphs_t*> ResetFaceGenCurrentMorphs{ REL::ID{ 1174798, 2209133 } };
	REL::Relocation<RequestIdles_t*> RequestIdles{ REL::ID{ 261705, 2257437 } };
	REL::Relocation<RetrieveSubGraphData_t*> RetrieveSubGraphData{ REL::ID{ 1291992, 2188860 } };
	REL::Relocation<SetAnimationGraphTarget_t*> SetAnimationGraphTarget{ REL::ID{ 1340816, 2214556 } };
	REL::Relocation<SetClothSettleOnTransitionToSim_t*> SetClothSettleOnTransitionToSim{
		REL::ID{ 638869, 2278314 }
	};
	REL::Relocation<SetClothWorld_t*> SetClothWorld{ REL::ID{ 19064, 2278307 } };
	REL::Relocation<SetDoTiledLighting_t*> SetDoTiledLighting{ REL::ID{ 716351, 2318370 } };
	// IDA OG 1.10.163: TESObjectREFR::FixDisplayedHeadParts enables all
	// RaceHeadSkinned segments before replaying headpart hair hides.
	REL::Relocation<EnableAllSegments_t*> EnableAllSegments{ REL::ID{ 246125, 2270009 } };
	REL::Relocation<SetSegmentDisableCount_t*> SetSegmentDisableCount{ REL::ID{ 1227683, 2270016 } };
	REL::Relocation<SetSegmentEnabled_t*> DisableSegment{ REL::ID{ 1466142, 2270018 } };
	REL::Relocation<SetSegmentEnabled_t*> EnableSegment{ REL::ID{ 1134184, 2270017 } };
	// Ghidra OG 1.10.163: BGSMod::Attachment::Mod::TryAttach3DRecurse at 0x1402F3360.
	// Used by PowerArmor::SyncFurnitureVisualsToInventory when replaying OMOD-driven PA visuals.
	REL::Relocation<TryAttachMod3DRecurse_t*> TryAttachMod3DRecurse{ REL::ID{ 832528, 2197531 } };
	REL::Relocation<ActionInputCtor_t*> ConstructActionInput{ REL::ID{ 1411457, 2193451 } };
	REL::Relocation<ActionInputDtor_t*> DestroyActionInput{ REL::ID{ 281897, 2193448 } };
	REL::Relocation<UpdateAnimationGraphManager_t*> UpdateAnimationGraphManager{ REL::ID{ 1492656, 2214536 } };
	REL::Relocation<UpdateAnimationGraphManagerFloat_t*> UpdateAnimationGraphManagerFloat{
		REL::ID{ 973903, 2214535 }
	};
	REL::Relocation<UpdateBodyTintColorsOnScene_t*> UpdateBodyTintColorsOnScene{ REL::ID{ 49935, 2209540 } };
	REL::Relocation<UseSpeedContoursForMovementCalculations_t*> UseSpeedContoursForMovementCalculations{
		REL::ID{ 272153, 2236396 }
	};
	REL::Relocation<FixFaceGenHeadSkinInstances_t*> FixFaceGenHeadSkinInstances{ REL::ID{ 1131949, 2209478 } };

	const REL::ID ConstructTESActionDataID{ 1307135 };
	const REL::ID DestroyTESActionDataID{ 229573 };

	REL::Relocation<void (*)(RE::NiAVObject*)> CreateBoneMap{ REL::ID{ 1131947, 2276147 } };
	REL::Relocation<void**> AnimationSubGraphDataSingleton{ REL::ID{ 1363506, 4796109 } };
	REL::Relocation<void**> BehaviorGraphSwapSingleton{ REL::ID{ 153510, 4800869 } };
	REL::Relocation<RE::EquipEventSource*> EquipEventSourceSingleton{ REL::ID{ 485633, 2691240, 4798533 } };
	REL::Relocation<std::uintptr_t> ClipCursor{ REL::ID{ 641385, 4823626 } };
	REL::Relocation<std::uintptr_t> ProcessGraphEventTarget{ REL::ID{ 1199489, 2256664 } };
	REL::Relocation<std::uintptr_t> RenderPrepassesAndMenusTarget{ REL::ID{ 1189309, 2222566 } };
	REL::Relocation<std::uintptr_t> Update3DModelTarget{ REL::ID{ 986782, 2231882 } };

	const IDOffset D3D11CreateDeviceAndSwapChainCall{
		REL::ID{ 224250, 4492363 },
		VariantOffset{ 0x419, 0x410 }
	};
	const IDOffset RenderSceneDeferredCompositePassCtorCall{
		REL::ID{ 73644, 2317577 },
		VariantOffset{ 0x158B, 0x161B }
	};
	const IDOffset Interface3DDrawModelRenderSceneDeferredCall{
		REL::ID{ 917134, 2222570 },
		VariantOffset{ 0x51A, 0x547 }
	};
	const IDOffset RunActorUpdatesCall{
		REL::ID{ 556439, 2227611 },
		VariantOffset{ 0x17, 0x23 }
	};
	const IDOffset TESIdleFormLoadAddLoadedIdleCall{
		REL::ID{ 1269730, 2207227 },
		VariantOffset{ 0x1D5, 0x1D6 }
	};
}
