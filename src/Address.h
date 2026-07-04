#pragma once

#include "RE/AnimationSpeedContour.h"
#include "RE/BSAnimationUpdateData.h"
#include "RE/BSFaceGenAnimationData.h"
#include "RE/BSSkin.h"
#include "RE/BShkbAnimationGraph.h"
#include "RE/EquipEventSource.h"

#include "RE/A/AIProcess.h"
#include "RE/A/ActionInput.h"
#include "RE/A/Actor.h"
#include "RE/B/BGSAction.h"
#include "RE/B/BGSAnimationSystemUtils.h"
#include "RE/B/BGSHeadPart.h"
#include "RE/B/BGSMod.h"
#include "RE/B/BGSObjectInstance.h"
#include "RE/B/BSAnimationGraphManager.h"
#include "RE/B/BSFixedString.h"
#include "RE/B/BSGeometry.h"
#include "RE/B/BSTObjectArena.h"
#include "RE/B/BSTArray.h"
#include "RE/B/BSTSmartPointer.h"
#include "RE/B/BipedAnim.h"
#include "RE/I/IAnimationGraphManagerHolder.h"
#include "RE/I/Interface3D.h"
#include "RE/N/NiAVObject.h"
#include "RE/N/NiCamera.h"
#include "RE/N/NiColor.h"
#include "RE/N/NiExtraData.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/S/SubgraphHandle.h"
#include "RE/S/SubgraphIdentifier.h"
#include "RE/T/TES.h"
#include "RE/T/TESNPC.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESObjectARMO.h"
#include "RE/T/TESObjectREFR.h"
#include "RE/T/TESRace.h"
#include "REL/Relocation.h"
#include "REX/FModule.h"

#include <cstdint>

namespace RE
{
	class BSShaderAccumulator;
	class BSCullingProcess;
	class BGSHeadPart;
	class ProcessLists;
	class ShadowSceneNode;
	class TESRace;
	class BSGeometrySegmentData;
}

namespace TF3DHud::Address
{
	struct VariantOffset
	{
		std::uintptr_t og;
		std::uintptr_t ae;

		[[nodiscard]] std::uintptr_t value() const noexcept
		{
			return REX::FModule::IsRuntimeOG() ? og : ae;
		}
	};

	struct IDOffset
	{
		REL::ID id;
		VariantOffset offset;

		[[nodiscard]] std::uintptr_t address() const
		{
			return REL::Relocation<std::uintptr_t>{ id }.address() + offset.value();
		}
	};

	struct BorrowedBipedPointer
	{
		RE::BipedAnim* biped{ nullptr };
	};
	static_assert(sizeof(BorrowedBipedPointer) == sizeof(void*));

	struct SkinComplexionContext
	{
		RE::BIPED_OBJECT slot{ RE::BIPED_OBJECT::kNone };
		std::uint32_t pad{ 0 };
		RE::BIPOBJECT* objects{ nullptr };
		RE::ObjectRefHandle actorRef{};
		std::uint32_t pad2{ 0 };
	};
	static_assert(offsetof(SkinComplexionContext, objects) == 0x8);
	static_assert(offsetof(SkinComplexionContext, actorRef) == 0x10);

	enum class SyncPointType : std::uint32_t
	{
		kLeft = 0,
		kRight = 1,
		kSecondLeft = 2,
		kSecondRight = 3,
	};

	using AddArmorToBiped_t =
		void(const RE::BGSObjectInstance*, RE::TESRace*, const BorrowedBipedPointer*, RE::SEX, std::uint16_t);
	using ActivateAnimationGraphManager_t = bool(RE::BSAnimationGraphManager*);
	using ActorAnimationGraphManagerCallback_t =
		void(RE::IAnimationGraphManagerHolder*, const RE::BSTSmartPointer<RE::BSAnimationGraphManager>&);
	using ActorBoolCallback_t = bool(const RE::Actor*);
	using ActorFloatCallback_t = float(const RE::Actor*);
	using BipedAnimCtor_t = RE::BipedAnim*(RE::BipedAnim*, RE::TESObjectREFR*, bool);
	using BipedAnimDtor_t = void(RE::BipedAnim*);
	using BShkbAnimationGraphCtor_t =
		RE::BShkbAnimationGraph*(RE::BShkbAnimationGraph*, RE::Actor*, bool);
	using CalculateBodyTintColor_t =
		void(RE::TESNPC*, RE::NiColorA&, RE::BGSCharacterTint::Entry*, bool, bool);
	using CalculateSpeedAdjustToSyncAnimationCycles_t = bool(
		float,
		float,
		const RE::BGSAnimationSystemUtils::ActiveSyncInfo&,
		float,
		const SyncPointType&,
		float&);
	using ConvertNodeTree_t = RE::NiAVObject*(RE::NiAVObject*);
	using CreateAnimationGraphManager_t = bool(RE::IAnimationGraphManagerHolder*, const char*);
	using CreateClothFor3D_t =
		RE::NiExtraData*(RE::NiAVObject&, const char*, const RE::NiTransform&, RE::NiAVObject*);
	using CreateHeadForNPC_t = bool(RE::TESNPC*, RE::NiPointer<RE::BSFaceGenNiNode>&, bool, bool, void*);
	using DestroyAdjustmentArena_t = void(void*);
	using DoAdjustSkinComplexion_t = std::uint64_t(SkinComplexionContext*, RE::NiAVObject*);
	using GenerateFlattenedHeadPartArray_t = void(RE::TESNPC*, RE::BSScrapArray<RE::BGSHeadPart*>&);
	using ForceUpgradeTextures_t = void(RE::NiAVObject*, bool, bool);
	using GetActorBodyPart3D_t =
		RE::NiAVObject*(RE::Actor*, RE::NiAVObject*, const std::uint32_t*, bool, bool);
	using GetActiveContourFromHolder_t = bool(
		const RE::IAnimationGraphManagerHolder*,
		RE::BSTSmartPointer<RE::AnimationSpeedContour>&,
		bool*);
	using GetAll3DUpdateFlags_t = std::uint16_t(RE::AIProcess*);
	using GetCellPriority_t = std::int32_t(RE::TES*, const RE::TESObjectCELL*, RE::NiPoint3*);
	using GetChargenModelName_t = void(char*, std::uint32_t, const char*, bool);
	using GetDefaultAction_t = RE::BGSAction*(void);
	using GetDefaultRaceHeadPart_t =
		RE::BGSHeadPart*(const RE::TESRace*, RE::SEX, RE::BGSHeadPart::HeadPartType);
	using GetGraphSpeedForRequestedSpeedAndDirection_t = std::uint32_t(
		const RE::AnimationSpeedContour*,
		const RE::AnimationSpeedInformationTypes::RequestedSpeed&,
		float,
		const void*,
		RE::AnimationSpeedInformationTypes::GraphSpeedInput&,
		void*);
	using GetGraphVariableBool_t = bool(const RE::BShkbAnimationGraph*, const RE::BSFixedString&, bool&);
	using GetGraphVariableFloat_t = bool(const RE::BShkbAnimationGraph*, const RE::BSFixedString&, float&);
	using GetGraphVariableInt_t = bool(const RE::BShkbAnimationGraph*, const RE::BSFixedString&, std::int32_t&);
	using GetNPCHeadPart_t = RE::BGSHeadPart*(RE::TESNPC*, RE::BGSHeadPart::HeadPartType);
	using GetNumSegments_t = std::uint32_t(const RE::BSGeometrySegmentData*);
	using GetSubSegmentCount_t = std::uint32_t(const RE::BSGeometrySegmentData*, std::uint32_t);
	using GetProjectForActor_t = const char*(RE::Actor*, RE::NiAVObject*);
	using GetReferenceScale_t = float(const RE::TESObjectREFR*);
	using GetSkin_t = RE::TESObjectARMO*(RE::TESNPC*);
	using GetSubSegmentIndex_t =
		std::uint32_t(const RE::BSGeometrySegmentData*, std::uint32_t, std::uint32_t);
	using GetUserIndex_t = std::uint32_t(const RE::BSGeometrySegmentData*, std::uint32_t, std::uint32_t);
	using InitWornObject_t = bool(RE::TESNPC*, const BorrowedBipedPointer*, const RE::BGSObjectInstance*);
	using ApplyAllCustomizationMorphs_t =
		void(RE::TESNPC*, const RE::BSScrapArray<RE::BGSHeadPart*>&, RE::BSFaceGenNiNode*);
	using ApplyWeightFaceMorph_t = void(RE::TESNPC*, RE::BSFaceGenNiNode*);
	using PrepareHeadForShaders_t =
		void(const RE::BSScrapArray<RE::BGSHeadPart*>&, RE::BSFaceGenNiNode*, RE::TESNPC*, void*);
	using ScaleFaceBones_t = void(RE::TESNPC*, RE::NiAVObject*, bool);
	using ScaleFaceSkinBones_t = void(RE::TESNPC*, RE::BSSkin::Instance*);
	using UpdateAllChildrenMorphData_t = void(RE::BSFaceGenNiNode*, bool);
	using InitializeSubGraph_t = bool(
		void*,
		RE::BShkbAnimationGraph*,
		const RE::BSFixedString*,
		RE::BSTObjectArena<RE::BSFixedString, RE::BSTObjectArenaScrapAlloc, 32>*,
		RE::BSTObjectArena<RE::BSFixedString, RE::BSTObjectArenaScrapAlloc, 32>*,
		const std::int32_t*,
		RE::SubgraphHandle*);
	using InterpretAction_t = bool(void*);
	using NotifyAnimationGraphImpl_t = bool(RE::IAnimationGraphManagerHolder*, const RE::BSFixedString&);
	using ProcessGraphEvent_t = std::uint32_t(RE::BSAnimationGraphManager*, const RE::BSFixedString&);
	using QUpdateEditorDeadActorModel_t = bool(RE::AIProcess*);
	using QTiledLighting_t = bool();
	using RenderPrepassesAndMenus_t = void(RE::Interface3D::Renderer*);
	using RenderSceneDeferred_t = void(
		RE::NiCamera*,
		RE::BSShaderAccumulator*,
		RE::BSCullingProcess*,
		RE::ShadowSceneNode*,
		std::int32_t,
		std::int32_t,
		bool,
		bool);
	using ResetFaceGenCurrentMorphs_t = int(RE::BSFaceGenAnimationData*, float);
	using RetrieveSubGraphData_t = bool(
		void*,
		std::uint32_t,
		const RE::SubgraphIdentifier*,
		RE::BSFixedString*,
		RE::BSTObjectArena<RE::BSFixedString, RE::BSTObjectArenaScrapAlloc, 32>*);
	using RunActorUpdates_t = void(RE::ProcessLists*, float, bool);
	using SetAnimationGraphTarget_t = bool(RE::IAnimationGraphManagerHolder*, RE::NiAVObject*, bool);
	using SetClothSettleOnTransitionToSim_t = void(RE::NiExtraData*, bool);
	using SetClothWorld_t = void(RE::NiExtraData*, void*);
	using SetDoTiledLighting_t = void(bool);
	using EnableAllSegments_t = void(RE::BSGeometrySegmentData*);
	using SetSegmentDisableCount_t = void(RE::BSGeometrySegmentData*, std::uint32_t, std::uint32_t, std::uint8_t);
	using SetSegmentEnabled_t = void(RE::BSGeometrySegmentData*, std::uint32_t, std::uint32_t, bool);
	using TryAttachMod3DRecurse_t =
		bool(RE::BGSMod::Attachment::Mod*, RE::NiNode*, char*, RE::TBO_InstanceData*);
	using TESActionDataCtor_t = void*(
		void*,
		RE::ActionInput::ACTIONPRIORITY,
		RE::TESObjectREFR*,
		RE::BGSAction*,
		RE::TESObjectREFR*,
		RE::ActionInput::Data);
	using TESActionDataDtor_t = void(void*);
	using Update3DModel_t = void(RE::AIProcess*, RE::Actor*, bool);
	using UpdateAnimationGraphManager_t = bool(RE::IAnimationGraphManagerHolder*, const RE::BSAnimationUpdateData&);
	using UpdateAnimationGraphManagerFloat_t = bool(RE::IAnimationGraphManagerHolder*, float);
	using UpdateBodyTintColorsOnScene_t = void(RE::NiAVObject*, const RE::NiColorA&);
	using UseSpeedContoursForMovementCalculations_t = bool(const RE::Actor*, std::uint32_t);
	using FixFaceGenHeadSkinInstances_t = void(RE::BSFaceGenNiNode*, RE::NiAVObject*, bool);

	extern REL::Relocation<ActivateAnimationGraphManager_t*> ActivateAnimationGraphManager;
	extern REL::Relocation<AddArmorToBiped_t*> AddArmorToBiped;
	extern REL::Relocation<ActorAnimationGraphManagerCallback_t*> ActorPostLoadAnimationGraphManager;
	extern REL::Relocation<ActorAnimationGraphManagerCallback_t*> ActorPreLoadAnimationGraphManager;
	extern REL::Relocation<ActorAnimationGraphManagerCallback_t*> ActorPreUpdateAnimationGraphManager;
	extern REL::Relocation<ActorBoolCallback_t*> GetFreezeGraphLocomotionChannel;
	extern REL::Relocation<ActorFloatCallback_t*> GetActorDirection;
	extern REL::Relocation<BipedAnimCtor_t*> BipedAnimCtor;
	extern REL::Relocation<BipedAnimDtor_t*> BipedAnimDtor;
	extern REL::Relocation<BShkbAnimationGraphCtor_t*> ConstructBShkbAnimationGraph;
	extern REL::Relocation<CalculateBodyTintColor_t*> CalculateBodyTintColor;
	extern REL::Relocation<CalculateSpeedAdjustToSyncAnimationCycles_t*> CalculateSpeedAdjustToSyncAnimationCycles;
	extern REL::Relocation<ConvertNodeTree_t*> ConvertNodeTree;
	extern REL::Relocation<CreateAnimationGraphManager_t*> CreateAnimationGraphManager;
	extern REL::Relocation<CreateClothFor3D_t*> CreateClothFor3D;
	extern REL::Relocation<CreateHeadForNPC_t*> CreateHeadForNPC;
	extern REL::Relocation<DestroyAdjustmentArena_t*> DestroyAdjustmentArena;
	extern REL::Relocation<DoAdjustSkinComplexion_t*> DoAdjustSkinComplexion;
	extern REL::Relocation<GenerateFlattenedHeadPartArray_t*> GenerateFlattenedHeadPartArray;
	extern REL::Relocation<ForceUpgradeTextures_t*> ForceUpgradeTextures;
	extern REL::Relocation<GetActorBodyPart3D_t*> GetActorBodyPart3D;
	extern REL::Relocation<GetActiveContourFromHolder_t*> GetActiveContourFromHolder;
	extern REL::Relocation<GetAll3DUpdateFlags_t*> GetAll3DUpdateFlags;
	extern REL::Relocation<GetCellPriority_t*> GetCellPriority;
	extern REL::Relocation<GetChargenModelName_t*> GetChargenModelName;
	extern REL::Relocation<GetDefaultAction_t*> GetDefaultObjectForActionInitializeToBaseState;
	extern REL::Relocation<GetDefaultAction_t*> GetDefaultObjectForActionInstantInitializeToBaseState;
	extern REL::Relocation<GetDefaultRaceHeadPart_t*> GetDefaultRaceHeadPart;
	extern REL::Relocation<GetGraphSpeedForRequestedSpeedAndDirection_t*> GetGraphSpeedForRequestedSpeedAndDirection;
	extern REL::Relocation<GetGraphVariableBool_t*> GetBShkbGraphVariableBool;
	extern REL::Relocation<GetGraphVariableFloat_t*> GetBShkbGraphVariableFloat;
	extern REL::Relocation<GetGraphVariableInt_t*> GetBShkbGraphVariableInt;
	extern REL::Relocation<GetNPCHeadPart_t*> GetNPCHeadPart;
	extern REL::Relocation<GetNumSegments_t*> GetNumSegments;
	extern REL::Relocation<GetSubSegmentCount_t*> GetSubSegmentCount;
	extern REL::Relocation<GetProjectForActor_t*> GetProjectForActor;
	extern REL::Relocation<GetReferenceScale_t*> GetReferenceScale;
	extern REL::Relocation<GetSkin_t*> GetSkin;
	extern REL::Relocation<GetSubSegmentIndex_t*> GetSubSegmentIndex;
	extern REL::Relocation<GetUserIndex_t*> GetUserIndex;
	extern REL::Relocation<InitWornObject_t*> InitWornObject;
	extern REL::Relocation<ApplyAllCustomizationMorphs_t*> ApplyAllCustomizationMorphs;
	extern REL::Relocation<ApplyWeightFaceMorph_t*> ApplyWeightFaceMorph;
	extern REL::Relocation<PrepareHeadForShaders_t*> PrepareHeadForShaders;
	extern REL::Relocation<ScaleFaceBones_t*> ScaleFaceBones;
	extern REL::Relocation<ScaleFaceSkinBones_t*> ScaleFaceSkinBones;
	extern REL::Relocation<UpdateAllChildrenMorphData_t*> UpdateAllChildrenMorphData;
	extern REL::Relocation<InitializeSubGraph_t*> InitializeSubGraph;
	extern REL::Relocation<InterpretAction_t*> InterpretAction;
	extern REL::Relocation<NotifyAnimationGraphImpl_t*> NotifyAnimationGraphImpl;
	extern REL::Relocation<QUpdateEditorDeadActorModel_t*> QUpdateEditorDeadActorModel;
	extern REL::Relocation<QTiledLighting_t*> QTiledLighting;
	extern REL::Relocation<ResetFaceGenCurrentMorphs_t*> ResetFaceGenCurrentMorphs;
	extern REL::Relocation<RetrieveSubGraphData_t*> RetrieveSubGraphData;
	extern REL::Relocation<SetAnimationGraphTarget_t*> SetAnimationGraphTarget;
	extern REL::Relocation<SetClothSettleOnTransitionToSim_t*> SetClothSettleOnTransitionToSim;
	extern REL::Relocation<SetClothWorld_t*> SetClothWorld;
	extern REL::Relocation<SetDoTiledLighting_t*> SetDoTiledLighting;
	extern REL::Relocation<EnableAllSegments_t*> EnableAllSegments;
	extern REL::Relocation<SetSegmentDisableCount_t*> SetSegmentDisableCount;
	extern REL::Relocation<SetSegmentEnabled_t*> DisableSegment;
	extern REL::Relocation<SetSegmentEnabled_t*> EnableSegment;
	extern REL::Relocation<TryAttachMod3DRecurse_t*> TryAttachMod3DRecurse;
	extern REL::Relocation<TESActionDataCtor_t*> ConstructTESActionData;
	extern REL::Relocation<TESActionDataDtor_t*> DestroyTESActionData;
	extern REL::Relocation<UpdateAnimationGraphManager_t*> UpdateAnimationGraphManager;
	extern REL::Relocation<UpdateAnimationGraphManagerFloat_t*> UpdateAnimationGraphManagerFloat;
	extern REL::Relocation<UpdateBodyTintColorsOnScene_t*> UpdateBodyTintColorsOnScene;
	extern REL::Relocation<UseSpeedContoursForMovementCalculations_t*> UseSpeedContoursForMovementCalculations;
	extern REL::Relocation<FixFaceGenHeadSkinInstances_t*> FixFaceGenHeadSkinInstances;

	extern REL::Relocation<void (*)(RE::NiAVObject*)> CreateBoneMap;
	extern REL::Relocation<void**> AnimationSubGraphDataSingleton;
	extern REL::Relocation<void**> BehaviorGraphSwapSingleton;
	extern REL::Relocation<RE::EquipEventSource*> EquipEventSourceSingleton;
	extern REL::Relocation<std::uintptr_t> ClipCursor;
	extern REL::Relocation<std::uintptr_t> ProcessGraphEventTarget;
	extern REL::Relocation<std::uintptr_t> RenderPrepassesAndMenusTarget;
	extern REL::Relocation<std::uintptr_t> Update3DModelTarget;

	extern const IDOffset D3D11CreateDeviceAndSwapChainCall;
	extern const IDOffset Interface3DDrawModelRenderSceneDeferredCall;
	extern const IDOffset RunActorUpdatesCall;
}
