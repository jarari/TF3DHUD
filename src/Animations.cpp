#include "Animations.h"

#include "Address.h"
#include "Config.h"
#include "Previewer.h"
#include "Utils.h"

#include "RE/AnimationSpeedContour.h"
#include "RE/BSAnimationGraphChannel.h"
#include "RE/BSAnimationUpdateData.h"
#include "RE/BShkbAnimationGraph.h"

#include "RE/A/Actor.h"
#include "RE/A/ActionInput.h"
#include "RE/A/AnimationSpeedInformationTypes.h"
#include "RE/B/BGSDefaultObjectManager.h"
#include "RE/B/BSAnimationGraphManager.h"
#include "RE/B/BSAnimationGraphEvent.h"
#include "RE/B/BSFlattenedBoneTree.h"
#include "RE/B/BGSAction.h"
#include "RE/B/BGSAnimationSystemUtils.h"
#include "RE/B/BSFixedString.h"
#include "RE/B/BSIntrusiveRefCounted.h"
#include "RE/B/BSRandom.h"
#include "RE/B/BSResourceNiBinaryStream.h"
#include "RE/B/BSStringT.h"
#include "RE/B/BSTArray.h"
#include "RE/B/BSTEvent.h"
#include "RE/B/BSTObjectArena.h"
#include "RE/B/BSTSmartPointer.h"
#include "RE/D/DEFAULT_OBJECT.h"
#include "RE/I/IAnimationGraphManagerHolder.h"
#include "RE/M/MiddleHighProcessData.h"
#include "RE/M/MemoryManager.h"
#include "RE/N/NiAVObject.h"
#include "RE/N/NiPointer.h"
#include "RE/N/NiUpdateData.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/S/SubgraphHandle.h"
#include "RE/S/SubgraphIdentifier.h"
#include "RE/S/SubGraphIdleRootData.h"
#include "RE/T/TES.h"
#include "RE/T/TESIdleForm.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <mutex>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <limits>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace TF3DHud::Animations
{
	namespace
	{
		constexpr std::size_t kBShkbAnimationGraphSize = 0x3D0;
		constexpr std::size_t kBShkbAnimationGraphAlignment = 0x10;
		constexpr auto kDynamicActivationBehaviorGraphs = std::to_array<std::string_view>({
			"actors\\Character\\Behaviors\\RaiderRootBehavior.hkx",
			"Actors\\Character\\Behaviors\\MTBehavior.hkx",
			"Actors\\Character\\Behaviors\\WeaponBehavior.hkx",
		});
		constexpr auto kDynamicActivationEvents = std::to_array<std::string_view>({
			"dyn_Activation",
			"dyn_ActivationLoop",
		});
		constexpr auto kLiveMirrorLocomotionEvents = std::to_array<std::string_view>({
			"moveStart",
			"moveStartAnimated",
			"moveStop",
			"sprintStart",
			"sprintStop",
			"swimStart",
			"swimStop",
			"walkStart",
			"runStart",
			"walkRunBlendStart",
		});

		constexpr auto kLiveMirrorSneakEvents = std::to_array<std::string_view>({
			"sneakStart",
			"sneakStop",
			"sneakStateEnter",
			"sneakStateExit",
		});

		constexpr auto kLiveMirrorJumpEvents = std::to_array<std::string_view>({
			"jumpStart",
			"jumpStartFromWalk",
			"jumpFall",
			"jumpLand",
			"jumpLandSoft",
			"jumpLandToWalk",
			"jumpLandToRun",
			"jumpEnd",
			"jumpEndToRun",
			"JumpUp",
			"JumpDown",
			"JumpFullBody",
			"JumpPartialBody",
			"g_jumpStartFromSprint",
		});

		constexpr auto kLiveMirrorWeaponFireEvents = std::to_array<std::string_view>({
			"attackStart",
			"attackStartAuto",
			"attackRelease",
			"attackStop",
			"attackStateEnter",
			"attackStateExit",
			"attackInterrupt",
			"AttackEnd",
			"attackStartChargingHold",
			"attackReleaseChargingHold",
			"boltChargeStart",
		});

		constexpr auto kLiveMirrorWeaponReloadEvents = std::to_array<std::string_view>({
			"reloadStart",
			"reloadStateEnter",
			"reloadStateExit",
			"reloadReserveStart",
		});

		constexpr auto kLiveMirrorMeleeEvents = std::to_array<std::string_view>({
			"meleeattackStart",
			"meleeattackSprintStart",
			"meleeAttackGun",
			"blockStart",
		});

		constexpr auto kLiveMirrorThrowEvents = std::to_array<std::string_view>({
			"grenadeThrowStart",
			"mineThrowStart",
		});

		constexpr auto kLiveMirrorAlwaysEvents = std::to_array<std::string_view>({
			"weapEquip",
			"weapUnequip",
			"Unequip",
			"weaponDraw",
			"weaponSheathe",
			"BeginWeaponDraw",
			"BeginWeaponSheathe",
			"weapForceEquip",
			"g_weapForceEquipInstant",
			"weaponAttach",
			"weaponDetach",
			"rifleSightedStart",
			"rifleSightedEnd",
			"rifleSightedStartOver",
			"sightedStateEnter",
			"sightedStateExit",
			"UpdateSighted",
			"SyncLeft",
			"SyncRight",
			"SyncCycleEnd",
			"syncIdleStart",
			"syncIdleStop",
		});

		constexpr auto kIdleAnimationMirrorEvents = std::to_array<std::string_view>({
			"AnimObjLoad",
			"AnimObjDraw",
			"AnimObjUnequip",
		});

		constexpr std::string_view kWeaponDrawSyncVariable{ "iSyncWeaponDrawState" };
		constexpr std::string_view kReadyAlertRelaxedSyncVariable{ "iSyncReadyAlertRelaxed" };
		constexpr std::string_view kBaseStateStartInstantEvent{ "g_archetypeBaseStateStartInstant" };
		constexpr std::string_view kRelaxedStateStartInstantEvent{ "g_archetypeRelaxedStateStartInstant" };
		constexpr std::string_view kBaseStateStartEvent{ "g_archetypeBaseStateStart" };
		constexpr std::string_view kRelaxedStateStartEvent{ "g_archetypeRelaxedStateStart" };
		constexpr float kIdleRequestRetrySeconds = 0.25F;
		constexpr std::uint32_t kIdleRequestRetryFrames = 12;

		constexpr auto kAlwaysIntGraphVariables = std::to_array<std::string_view>({
			"iSyncWeaponDrawState",
			"iSyncSightedState",
			"iSyncGunDown",
			"iSyncChargeState",
			"iWeaponChargeMode",
			"iAttackState",
		});

		constexpr auto kLocomotionIntGraphVariables = std::to_array<std::string_view>({
			"iLocomotionSpeedState",
			"iSyncSprintState",
			"iSyncIdleLocomotion",
			"iSyncSneakWalkRun",
		});

		constexpr auto kJumpIntGraphVariables = std::to_array<std::string_view>({
			"iSyncJumpState",
		});
		// WeaponBehavior.xml binds JumpStateMachine.currentStateId to CurrentJumpState.
		// Keep that output preview-owned; forcing the live value back onto the
		// preview graph can immediately disable the lower-body jump layer.

		constexpr auto kSneakIntGraphVariables = std::to_array<std::string_view>({
			"iIsInSneak",
		});

		constexpr auto kAlwaysBoolGraphVariables = std::to_array<std::string_view>({
			"isAttacking",
			"IsAttackReady",
			"isAttackNotReady",
			"bEquipOk",
		});

		constexpr auto kJumpBoolGraphVariables = std::to_array<std::string_view>({
			"isJumping",
			"bInJumpState",
		});

		constexpr auto kSneakBoolGraphVariables = std::to_array<std::string_view>({
			"IsSneaking",
			"bIsSneaking",
		});

		constexpr auto kWeaponReloadBoolGraphVariables = std::to_array<std::string_view>({
			"isReloading",
		});

		constexpr auto kAlwaysFloatGraphVariables = std::to_array<std::string_view>({
			"weaponSpeedMult",
			"reloadSpeedMult",
			"sightedSpeedMult",
		});

		constexpr auto kLocomotionFloatGraphVariables = std::to_array<std::string_view>({
			"SpeedSmoothed",
			"WalkSpeedMult",
			"fSpeedWalk",
			"JogSpeedMult",
			"RunSpeedMult",
			"fLocomotionWalkPlaybackSpeed",
			"fLocomotionJogPlaybackSpeed",
			"fLocomotionRunPlaybackSpeed",
			"fLocomotionSprintPlaybackSpeed",
			"fLocomotionSneakWalkPlaybackSpeed",
			"fLocomotionSneakRunPlaybackSpeed",
		});

		constexpr auto kPreviewSuppressedAnimationChannels = std::to_array<std::string_view>({
			"Speed",
			"Direction",
			"DirectionSmoothed",
			"Pitch",
			"Roll",
			"TurnDelta",
			"PitchDelta",
			"PitchDeltaSmoothed",
			"TurnDeltaSmoothed",
			"DirectionDegrees",
			"fControllerXSum",
			"fControllerYSum",
		});

		constexpr auto kIdlePreviewSuppressedAnimationChannels = std::to_array<std::string_view>({
			"bEquipOk",
			"iSyncReadyAlertRelaxed",
			"iSyncWeaponDrawState",
		});

		constexpr auto kPreviewNeutralBoolGraphVariables = std::to_array<std::string_view>({
			"bAimActive",
			"bAimEnabled",
			"bAimCaptureEnabled",
			"bShouldAimHeadTrack",
			"bSyncDirection",
			"m_bEnablePitchTwistModifier",
		});

		constexpr auto kPreviewEnabledBoolGraphVariables = std::to_array<std::string_view>({
			"DisableCharacterPitch",
			"bFreezeRotationUpdate",
		});

		constexpr auto kPreviewNeutralIntGraphVariables = std::to_array<std::string_view>({
			"iSyncRunDirection",
		});

		constexpr auto kPreviewReadyIntGraphVariables = std::to_array<std::string_view>({
			"iSyncTurnState",
		});

		constexpr auto kPreviewNeutralFloatGraphVariables = std::to_array<std::string_view>({
			"AimHeadingCurrent",
			"AimPitchCurrent",
			"BowAimOffsetHeading",
			"BowAimOffsetPitch",
			"CamPitch",
			"CamPitchDamped",
			"Direction",
			"DirectionDamped",
			"DirectionDegrees",
			"DirectionSmoothed",
			"PitchDelta",
			"PitchDeltaDamped",
			"PitchDeltaSmoothed",
			"PitchDeltaSmoothedDamped",
			"PitchOffset",
			"TurnDelta",
			"TurnDeltaDamped",
			"TurnDeltaSmoothed",
			"TurnDeltaSmoothedDamped",
			"TurnDeltaSpeedLimitedDampened",
			"fControllerXSum",
			"fControllerYSum",
			"speedDamped",
			"pitch",
		});

		template <std::size_t N>
		[[nodiscard]] bool ContainsEngineFixedString(
			const RE::BSFixedString& a_value,
			const std::array<std::string_view, N>& a_items)
		{
			return std::any_of(a_items.begin(), a_items.end(), [&](const std::string_view item) {
				return a_value == item;
			});
		}

		[[nodiscard]] bool IsWeaponSubgraphDependentEvent(const RE::BSFixedString& a_eventName)
		{
			return ContainsEngineFixedString(a_eventName, kLiveMirrorAlwaysEvents) ||
			       ContainsEngineFixedString(a_eventName, kLiveMirrorWeaponFireEvents) ||
			       ContainsEngineFixedString(a_eventName, kLiveMirrorWeaponReloadEvents) ||
			       ContainsEngineFixedString(a_eventName, kLiveMirrorMeleeEvents) ||
			       ContainsEngineFixedString(a_eventName, kLiveMirrorThrowEvents);
		}

		[[nodiscard]] bool IsWeaponForceEquipEvent(const RE::BSFixedString& a_eventName)
		{
			return a_eventName == std::string_view{ "weapForceEquip" } ||
			       a_eventName == std::string_view{ "g_weapForceEquipInstant" };
		}

		[[nodiscard]] bool IsWeaponEquipEvent(const RE::BSFixedString& a_eventName)
		{
			return a_eventName == std::string_view{ "weapEquip" };
		}

		[[nodiscard]] bool IsLiveMirrorEventWhitelisted(const char* a_event)
		{
			if (!a_event || a_event[0] == '\0') {
				return false;
			}

			// Graph requests are BSFixedString values. Compare in the same domain
			// so vanilla event casing such as "WeapEquip" matches the intended
			// whitelist entry without raw string case heuristics.
			const RE::BSFixedString eventName(a_event);
			if (ContainsEngineFixedString(eventName, kLiveMirrorAlwaysEvents)) {
				return true;
			}

			const auto& mirrorEvents = GetConfig().animation.mirrorEvents;
			return (mirrorEvents.locomotion && ContainsEngineFixedString(eventName, kLiveMirrorLocomotionEvents)) ||
				   (mirrorEvents.sneak && ContainsEngineFixedString(eventName, kLiveMirrorSneakEvents)) ||
				   (mirrorEvents.jump && ContainsEngineFixedString(eventName, kLiveMirrorJumpEvents)) ||
				   (mirrorEvents.weaponFire && ContainsEngineFixedString(eventName, kLiveMirrorWeaponFireEvents)) ||
				   (mirrorEvents.weaponReload && ContainsEngineFixedString(eventName, kLiveMirrorWeaponReloadEvents)) ||
				   (mirrorEvents.melee && ContainsEngineFixedString(eventName, kLiveMirrorMeleeEvents)) ||
				   (mirrorEvents.throwable && ContainsEngineFixedString(eventName, kLiveMirrorThrowEvents));
		}

		[[nodiscard]] bool IsIdleAnimationMirrorEventWhitelisted(const char* a_event)
		{
			if (!a_event || a_event[0] == '\0') {
				return false;
			}

			return ContainsEngineFixedString(RE::BSFixedString(a_event), kIdleAnimationMirrorEvents);
		}

		[[nodiscard]] bool IsSuppressedAnimationChannel(const RE::BSAnimationGraphChannel& a_channel)
		{
			return ContainsEngineFixedString(a_channel.variableName, kPreviewSuppressedAnimationChannels);
		}

		using SyncPointType = Address::SyncPointType;
		using SubgraphPreloadArena = RE::BSTObjectArena<RE::BSFixedString, RE::BSTObjectArenaScrapAlloc, 32>;

		struct LiveGraphRequest
		{
			RE::BSAnimationGraphManager* manager{ nullptr };
			RE::BSFixedString eventName;
			std::uint32_t result{ 0 };
		};

		enum class PreviewWeaponBehaviorGraph
		{
			kUnknown,
			kMelee,
			kWeapon,
		};

		std::string g_idlePlaybackKey;
		float g_idlePlaybackTime{ 0.0F };
		float g_idleClipDuration{ 0.0F };

		void LogDiagnostic(std::string a_message);
		[[nodiscard]] std::string DynamicActivationIdleKey(RE::TESIdleForm& a_idle);
		[[nodiscard]] RE::TESIdleForm* ResolveConfiguredDynamicActivationIdle();
		[[nodiscard]] RE::TESIdleForm* ResolvePlayableIdle(RE::PlayerCharacter& a_player, RE::TESIdleForm& a_idle);
		[[nodiscard]] std::string ResolveDynamicIdleFullPath(RE::PlayerCharacter& a_player, RE::TESIdleForm& a_idle);

		auto& g_constructBShkbAnimationGraph = Address::ConstructBShkbAnimationGraph;
		auto& g_notifyAnimationGraphImpl = Address::NotifyAnimationGraphImpl;
		auto& g_getBShkbGraphVariableBool = Address::GetBShkbGraphVariableBool;
		auto& g_getBShkbGraphVariableFloat = Address::GetBShkbGraphVariableFloat;
		auto& g_getBShkbGraphVariableInt = Address::GetBShkbGraphVariableInt;
		auto& g_getDynamicIdleFullFilePath = Address::GetDynamicIdleFullFilePath;
		auto& g_getKeywordForType = Address::GetKeywordForType;
		auto& g_getAnimationSex = Address::GetAnimationSex;
		auto& g_actorPreUpdateAnimationGraphManager = Address::ActorPreUpdateAnimationGraphManager;
		auto& g_actorPreLoadAnimationGraphManager = Address::ActorPreLoadAnimationGraphManager;
		auto& g_actorPostLoadAnimationGraphManager = Address::ActorPostLoadAnimationGraphManager;
		auto& g_createAnimationGraphManager = Address::CreateAnimationGraphManager;
		auto& g_updateAnimationGraphManager = Address::UpdateAnimationGraphManager;
		auto& g_updateAnimationGraphManagerFloat = Address::UpdateAnimationGraphManagerFloat;
		auto& g_getProjectForActor = Address::GetProjectForActor;
		auto& g_setAnimationGraphTarget = Address::SetAnimationGraphTarget;
		auto& g_getFreezeGraphLocomotionChannel = Address::GetFreezeGraphLocomotionChannel;
		auto& g_getReferenceScale = Address::GetReferenceScale;
		auto& g_useSpeedContoursForMovementCalculations = Address::UseSpeedContoursForMovementCalculations;
		auto& g_getActiveContourFromHolder = Address::GetActiveContourFromHolder;
		auto& g_getGraphSpeedForRequestedSpeedAndDirection = Address::GetGraphSpeedForRequestedSpeedAndDirection;
		auto& g_destroyAdjustmentArena = Address::DestroyAdjustmentArena;
		auto& g_calculateSpeedAdjustToSyncAnimationCycles = Address::CalculateSpeedAdjustToSyncAnimationCycles;
		auto& g_requestIdles = Address::RequestIdles;
		auto& g_retrieveSubGraphData = Address::RetrieveSubGraphData;
		auto& g_initializeSubGraph = Address::InitializeSubGraph;
		auto& g_isSubGraphLoaded = Address::IsSubGraphLoaded;
		auto& g_releaseSubGraph = Address::ReleaseSubGraph;
		auto& g_gatherPreloadAnimations = Address::GatherPreloadAnimations;
		auto& g_behaviorGraphSwapSingleton = Address::BehaviorGraphSwapSingleton;
		auto& g_animationSubGraphDataSingleton = Address::AnimationSubGraphDataSingleton;
		auto& g_activateAnimationGraphManager = Address::ActivateAnimationGraphManager;
		auto& g_getCellPriority = Address::GetCellPriority;
		auto& g_getDefaultObjectForActionInitializeToBaseState =
			Address::GetDefaultObjectForActionInitializeToBaseState;
		auto& g_getDefaultObjectForActionInstantInitializeToBaseState =
			Address::GetDefaultObjectForActionInstantInitializeToBaseState;
		auto& g_interpretAction = Address::InterpretAction;
		auto& g_constructActionInput = Address::ConstructActionInput;
		auto& g_destroyActionInput = Address::DestroyActionInput;

		struct AERequestIdlesData
		{
			RE::BSFixedString eventName;
			RE::BSFixedString path;
			RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
			bool result{ false };
		};

		static_assert(offsetof(AERequestIdlesData, path) == 0x8);
		static_assert(offsetof(AERequestIdlesData, manager) == 0x10);
		static_assert(offsetof(AERequestIdlesData, result) == 0x18);

		using RequestIdleFunctorAE_t =
			void(AERequestIdlesData*, const RE::BSTSmartPointer<RE::BShkbAnimationGraph>*);

		constexpr std::size_t kTESActionDataSize = 0x60;
		constexpr std::size_t kTESActionDataEventNameOffset = 0x28;
		constexpr std::size_t kTESActionDataTargetEventNameOffset = 0x30;
		constexpr std::size_t kTESActionDataFlagsOffset = 0x38;
		constexpr std::size_t kTESActionDataExtraDataOffset = 0x40;
		constexpr std::size_t kTESActionDataIdleFormOffset = 0x48;
		constexpr std::size_t kTESActionDataRunFlagsOffset = 0x50;
		constexpr std::size_t kTESActionDataResolveIdleOffset = 0x58;

		[[nodiscard]] std::uintptr_t BGSActionDataVTable()
		{
			static REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE::BGSActionData[0] };
			return vtable.address();
		}

		[[nodiscard]] std::uintptr_t TESActionDataVTable()
		{
			static REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE::TESActionData[0] };
			return vtable.address();
		}

		void* ConstructTESActionDataAE(
			void* a_actionData,
			const RE::ActionInput::ACTIONPRIORITY a_priority,
			RE::TESObjectREFR* a_ref,
			RE::BGSAction* a_action,
			RE::TESObjectREFR* a_target,
			const RE::ActionInput::Data a_inputData)
		{
			auto* const data = static_cast<std::byte*>(a_actionData);
			std::memset(data, 0, kTESActionDataSize);

			g_constructActionInput(a_actionData, a_priority, a_ref, a_action, a_target, a_inputData);
			std::construct_at(reinterpret_cast<RE::BSFixedString*>(data + kTESActionDataEventNameOffset));
			std::construct_at(reinterpret_cast<RE::BSFixedString*>(data + kTESActionDataTargetEventNameOffset));

			*reinterpret_cast<std::uintptr_t*>(data) = TESActionDataVTable();
			*reinterpret_cast<std::uint32_t*>(data + kTESActionDataFlagsOffset) = 0;
			*reinterpret_cast<void**>(data + kTESActionDataExtraDataOffset) = nullptr;
			*reinterpret_cast<RE::TESIdleForm**>(data + kTESActionDataIdleFormOffset) = nullptr;
			*reinterpret_cast<std::uint32_t*>(data + kTESActionDataRunFlagsOffset) = 0;
			*reinterpret_cast<std::uint32_t*>(data + kTESActionDataResolveIdleOffset) = 0;
			return a_actionData;
		}

		void DestroyTESActionDataAE(void* a_actionData)
		{
			auto* const data = static_cast<std::byte*>(a_actionData);

			*reinterpret_cast<std::uintptr_t*>(data) = BGSActionDataVTable();
			std::destroy_at(reinterpret_cast<RE::BSFixedString*>(data + kTESActionDataTargetEventNameOffset));
			std::destroy_at(reinterpret_cast<RE::BSFixedString*>(data + kTESActionDataEventNameOffset));
			g_destroyActionInput(a_actionData);
		}

		void* ConstructTESActionData(
			void* a_actionData,
			const RE::ActionInput::ACTIONPRIORITY a_priority,
			RE::TESObjectREFR* a_ref,
			RE::BGSAction* a_action,
			RE::TESObjectREFR* a_target,
			const RE::ActionInput::Data a_inputData)
		{
			if (REX::FModule::IsRuntimeOG()) {
				static REL::Relocation<Address::TESActionDataCtor_t*> ctor{ Address::ConstructTESActionDataID };
				return ctor(a_actionData, a_priority, a_ref, a_action, a_target, a_inputData);
			}

			return ConstructTESActionDataAE(a_actionData, a_priority, a_ref, a_action, a_target, a_inputData);
		}

		void DestroyTESActionData(void* a_actionData)
		{
			if (REX::FModule::IsRuntimeOG()) {
				static REL::Relocation<Address::TESActionDataDtor_t*> dtor{ Address::DestroyTESActionDataID };
				dtor(a_actionData);
				return;
			}

			DestroyTESActionDataAE(a_actionData);
		}

		[[nodiscard]] bool RequestIdles(
			const RE::BSFixedString& a_eventName,
			const RE::BSTSmartPointer<RE::BShkbAnimationGraph>& a_graph,
			const RE::BSFixedString& a_path,
			const RE::BSTSmartPointer<RE::BSAnimationGraphManager>& a_manager)
		{
			if (REX::FModule::IsRuntimeOG()) {
				// IDA OG 1.10.163: this member path does not read its singleton this pointer.
				void* const fileManager = nullptr;
				return g_requestIdles(fileManager, a_eventName, a_graph, a_path, a_manager);
			}

			// IDA AE 1.11.191: the mapped function has RequestIdleFunctor::operator()
			// ABI with the old RequestIdles body inlined.
			AERequestIdlesData request{ a_eventName, a_path, a_manager, false };
			const auto requestIdleFunctor = reinterpret_cast<RequestIdleFunctorAE_t*>(g_requestIdles.address());
			requestIdleFunctor(std::addressof(request), std::addressof(a_graph));
			return request.result;
		}

		template <class T, class Allocator>
		[[nodiscard]] const T* GetEngineSmallArrayStorage(const RE::BSTArray<T, Allocator>& a_source)
		{
			if (a_source.empty()) {
				return nullptr;
			}

			const auto* const base = reinterpret_cast<const std::byte*>(std::addressof(a_source));
			const auto capacityAndLocal = *reinterpret_cast<const std::int32_t*>(base);
			if (capacityAndLocal < 0) {
				return reinterpret_cast<const T*>(base + 0x08);
			}

			const auto* const heap = *reinterpret_cast<T* const*>(base + 0x08);
			if (heap) {
				return heap;
			}

			// IDA: SubBehaviorLoadingFunctor appends via
			// BSTSmallArrayHeapAllocator<16>; when the request array has a
			// valid count but no heap pointer, the elements are in the inline
			// 16-byte storage at +0x08.
			if (a_source.size() * sizeof(T) <= 0x10) {
				return reinterpret_cast<const T*>(base + 0x08);
			}

			return nullptr;
		}
		static_assert(sizeof(RE::BSTSmallArray<RE::SubgraphHandle, 2>) == 0x20);
		static_assert(sizeof(RE::BSTSmallArray<RE::SubgraphIdentifier, 2>) == 0x20);

		void InitializeActiveSyncInfo(RE::BGSAnimationSystemUtils::ActiveSyncInfo& a_info)
		{
			// IDA: vanilla callers initialize ActiveSyncInfo as current=0,
			// speed=1, total=-1 before AnimationSystemUtils::GetActiveSyncInfo.
			a_info.currentAnimTime = 0.0F;
			a_info.animSpeedMult = 1.0F;
			a_info.totalAnimTime = -1.0F;
		}

		[[nodiscard]] bool IsValidActiveSyncInfo(const RE::BGSAnimationSystemUtils::ActiveSyncInfo& a_info)
		{
			constexpr float kMinimumPositiveTime = 0.00000011920929F;
			return std::isfinite(a_info.currentAnimTime) &&
			       std::isfinite(a_info.animSpeedMult) &&
			       std::isfinite(a_info.totalAnimTime) &&
			       a_info.animSpeedMult > kMinimumPositiveTime &&
			       a_info.totalAnimTime > kMinimumPositiveTime;
		}

		[[nodiscard]] bool TryGetActiveSyncInfo(
			const RE::IAnimationGraphManagerHolder* a_holder,
			RE::BGSAnimationSystemUtils::ActiveSyncInfo& a_info)
		{
			InitializeActiveSyncInfo(a_info);
			return a_holder &&
			       RE::BGSAnimationSystemUtils::GetActiveSyncInfo(a_holder, a_info) &&
			       IsValidActiveSyncInfo(a_info);
		}

		[[nodiscard]] float WrapActiveSyncTime(float a_time, const float a_totalTime)
		{
			auto wrapped = std::fmod(a_time, a_totalTime);
			if (wrapped < 0.0F) {
				wrapped += a_totalTime;
			}
			return wrapped;
		}

		[[nodiscard]] float TimeUntilSyncPoint(
			const RE::BGSAnimationSystemUtils::ActiveSyncInfo& a_info,
			const float a_syncPointTime)
		{
			return WrapActiveSyncTime(a_syncPointTime - a_info.currentAnimTime, a_info.totalAnimTime);
		}

		[[nodiscard]] bool TryGetSyncPointType(const RE::BSFixedString& a_name, SyncPointType& a_type)
		{
			// IDA: AnimationSystemUtils::InitSDM initializes SyncPointTypes as:
			// 0=SyncLeft, 1=SyncRight, 2=SyncLeft2, 3=SyncRight2.
			if (a_name == std::string_view{ "SyncLeft" }) {
				a_type = SyncPointType::kLeft;
				return true;
			}
			if (a_name == std::string_view{ "SyncRight" }) {
				a_type = SyncPointType::kRight;
				return true;
			}
			if (a_name == std::string_view{ "SyncLeft2" }) {
				a_type = SyncPointType::kSecondLeft;
				return true;
			}
			if (a_name == std::string_view{ "SyncRight2" }) {
				a_type = SyncPointType::kSecondRight;
				return true;
			}
			return false;
		}

		[[nodiscard]] bool TryGetNextLiveSyncPointTarget(
			const RE::BGSAnimationSystemUtils::ActiveSyncInfo& a_liveInfo,
			SyncPointType& a_type,
			float& a_timeUntilSyncPoint)
		{
			bool found = false;
			float bestTime = 0.0F;
			SyncPointType bestType = SyncPointType::kLeft;

			for (const auto& syncPoint : a_liveInfo.otherSyncInfo) {
				SyncPointType candidateType;
				if (!TryGetSyncPointType(syncPoint.first, candidateType) ||
					!std::isfinite(syncPoint.second)) {
					continue;
				}

				const auto candidateTime = TimeUntilSyncPoint(a_liveInfo, syncPoint.second);
				if (!found || candidateTime < bestTime) {
					found = true;
					bestTime = candidateTime;
					bestType = candidateType;
				}
			}

			if (!found) {
				return false;
			}

			a_type = bestType;
			a_timeUntilSyncPoint = bestTime;
			return true;
		}

		[[nodiscard]] std::uint32_t CountSyncPoints(
			const RE::BGSAnimationSystemUtils::ActiveSyncInfo& a_info)
		{
			std::uint32_t count = 0;
			for ([[maybe_unused]] const auto& syncPoint : a_info.otherSyncInfo) {
				++count;
			}
			return count;
		}

		void FillActiveSyncDebugInfo(
			ActiveSyncDebugInfo& a_debugInfo,
			const bool a_active,
			const RE::BGSAnimationSystemUtils::ActiveSyncInfo& a_info)
		{
			a_debugInfo.active = a_active;
			a_debugInfo.currentTime = a_info.currentAnimTime;
			a_debugInfo.totalTime = a_info.totalAnimTime;
			a_debugInfo.speed = a_info.animSpeedMult;
			a_debugInfo.syncPointCount = CountSyncPoints(a_info);
		}

		struct BehaviorGraphSwapEntryDiagnostic
		{
			RE::SubgraphHandle handle;
			void* sharedData;
			std::byte pad10[0x30];
			std::uint8_t useCount;
			std::uint8_t pendingRemove;
			std::byte pad42[0x6];
		};
		static_assert(sizeof(BehaviorGraphSwapEntryDiagnostic) == 0x48);
		static_assert(offsetof(BehaviorGraphSwapEntryDiagnostic, sharedData) == 0x08);
		static_assert(offsetof(BehaviorGraphSwapEntryDiagnostic, useCount) == 0x40);
		static_assert(offsetof(BehaviorGraphSwapEntryDiagnostic, pendingRemove) == 0x41);

		struct BehaviorGraphSwapDataDiagnostic
		{
			BehaviorGraphSwapEntryDiagnostic* entries;
			std::uint32_t capacity;
			std::uint32_t pad0C;
			std::uint32_t size;
			std::uint32_t pad14;
			void* stateMachine;
			void* behavior;
			void* lock;
		};
		static_assert(sizeof(BehaviorGraphSwapDataDiagnostic) == 0x30);
		static_assert(offsetof(BehaviorGraphSwapDataDiagnostic, size) == 0x10);
		static_assert(offsetof(BehaviorGraphSwapDataDiagnostic, stateMachine) == 0x18);
		static_assert(offsetof(BehaviorGraphSwapDataDiagnostic, behavior) == 0x20);
		static_assert(offsetof(BehaviorGraphSwapDataDiagnostic, lock) == 0x28);

		template <std::size_t N>
		void CopyDebugText(std::array<char, N>& a_out, const char* a_text)
		{
			a_out[0] = '\0';
			if (!a_text || a_text[0] == '\0') {
				return;
			}

			const auto length = (std::min)(std::strlen(a_text), N - 1);
			std::memcpy(a_out.data(), a_text, length);
			a_out[length] = '\0';
		}

		void CaptureFixedStringArrayDebug(
			const std::uintptr_t a_dataBase,
			const std::uintptr_t a_arrayOffset,
			std::uint32_t& a_count,
			std::uint32_t& a_shown,
			std::array<SubgraphFileDebugInfo, kMaxSubgraphDebugFiles>& a_entries)
		{
			constexpr std::uint32_t kMaxReasonableFiles = 4096;

			a_count = 0;
			a_shown = 0;
			if (!a_dataBase) {
				return;
			}

			const auto array = a_dataBase + a_arrayOffset;
			const auto entries = *reinterpret_cast<const std::uintptr_t*>(array);
			const auto count = *reinterpret_cast<const std::uint32_t*>(array + 0x10);
			if (!entries || count > kMaxReasonableFiles) {
				return;
			}

			a_count = count;
			const auto dumped = (std::min)(count, kMaxSubgraphDebugFiles);
			for (std::uint32_t index = 0; index < dumped; ++index) {
				const auto* entry = reinterpret_cast<const RE::BSFixedString*>(
					entries + static_cast<std::uintptr_t>(index) * sizeof(RE::BSFixedString));
				CopyDebugText(a_entries[index].path, entry->c_str());
			}
			a_shown = dumped;
		}

		void CaptureSubgraphHandles(
			const RE::BSTSmallArray<RE::SubgraphHandle, 2>& a_source,
			std::uint32_t& a_count,
			std::uint32_t& a_shown,
			std::array<std::uint64_t, kMaxSubgraphDebugRequestEntries>& a_values)
		{
			constexpr std::uint32_t kMaxReasonableRequestEntries = 16;
			a_count = a_source.size();
			a_shown = 0;
			const auto* const data = GetEngineSmallArrayStorage(a_source);
			if (!data || a_count > kMaxReasonableRequestEntries) {
				return;
			}

			a_shown = (std::min)(a_count, kMaxSubgraphDebugRequestEntries);
			for (std::uint32_t index = 0; index < a_shown; ++index) {
				a_values[index] = data[index].handle;
			}
		}

		[[nodiscard]] const RE::SubgraphHandle* GetSubgraphHandleStorage(
			const RE::BSTSmallArray<RE::SubgraphHandle, 2>& a_source)
		{
			return GetEngineSmallArrayStorage(a_source);
		}

		void CaptureSubgraphIds(
			const RE::BSTSmallArray<RE::SubgraphIdentifier, 2>& a_source,
			std::uint32_t& a_count,
			std::uint32_t& a_shown,
			std::array<std::uint64_t, kMaxSubgraphDebugRequestEntries>& a_values)
		{
			constexpr std::uint32_t kMaxReasonableRequestEntries = 16;
			a_count = a_source.size();
			a_shown = 0;
			const auto* const data = GetEngineSmallArrayStorage(a_source);
			if (!data || a_count > kMaxReasonableRequestEntries) {
				return;
			}

			a_shown = (std::min)(a_count, kMaxSubgraphDebugRequestEntries);
			for (std::uint32_t index = 0; index < a_shown; ++index) {
				a_values[index] = static_cast<std::uint64_t>(data[index].identifier);
			}
		}

		struct ActiveNodeArrayDiagnostic
		{
			void** data;
			std::int32_t size;
			std::int32_t capacityAndFlags;
		};
		static_assert(sizeof(ActiveNodeArrayDiagnostic) == 0x10);

		struct HkbBehaviorGraphDiagnostic
		{
			std::byte pad000[0x0E0];
			ActiveNodeArrayDiagnostic* activeNodes;
			std::byte pad0E8[0x0C2];
			bool isActive;
			bool isLinked;
			bool updateActiveNodes;
			bool stateOrTransitionChanged;
		};
		static_assert(offsetof(HkbBehaviorGraphDiagnostic, activeNodes) == 0x0E0);
		static_assert(offsetof(HkbBehaviorGraphDiagnostic, isActive) == 0x1AA);
		static_assert(offsetof(HkbBehaviorGraphDiagnostic, stateOrTransitionChanged) == 0x1AD);

		std::mutex g_resourceProbeLock;
		std::unordered_map<std::string, bool> g_resourceProbeCache;

		const char* SafeString(const char* a_str)
		{
			return a_str ? a_str : "";
		}

		[[nodiscard]] const char* ReadTaggedString(const std::uintptr_t a_address)
		{
			const auto raw = *reinterpret_cast<const std::uintptr_t*>(a_address) & ~static_cast<std::uintptr_t>(1);
			return raw ? reinterpret_cast<const char*>(raw) : nullptr;
		}

		[[nodiscard]] std::string NormalizeSeparators(std::string a_path)
		{
			for (auto& ch : a_path) {
				if (ch == '/') {
					ch = '\\';
				}
			}
			while (!a_path.empty() && a_path.back() == '\\') {
				a_path.pop_back();
			}
			return a_path;
		}

		[[nodiscard]] std::string ToLowerNormalized(std::string a_value)
		{
			a_value = NormalizeSeparators(std::move(a_value));
			std::ranges::transform(a_value, a_value.begin(), [](const unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return a_value;
		}

		[[nodiscard]] std::string GetClipLeaf(const char* a_authoredPath)
		{
			auto path = NormalizeSeparators(SafeString(a_authoredPath));
			const auto slash = path.rfind('\\');
			if (slash != std::string::npos) {
				path = path.substr(slash + 1);
			}
			const auto dot = path.rfind('.');
			if (dot != std::string::npos) {
				path = path.substr(0, dot);
			}
			return path;
		}

		[[nodiscard]] bool ResourcePathExists(const std::string& a_path)
		{
			if (a_path.empty()) {
				return false;
			}

			const auto normalized = NormalizeSeparators(a_path);
			const auto key = ToLowerNormalized(normalized);
			{
				std::scoped_lock lock(g_resourceProbeLock);
				if (const auto iter = g_resourceProbeCache.find(key); iter != g_resourceProbeCache.end()) {
					return iter->second;
				}
			}

			bool exists = false;
			{
				RE::BSResourceNiBinaryStream stream(normalized.c_str());
				exists = static_cast<bool>(stream);
			}
			if (!exists && !ToLowerNormalized(normalized).starts_with("meshes\\")) {
				RE::BSResourceNiBinaryStream stream((std::string("Meshes\\") + normalized).c_str());
				exists = static_cast<bool>(stream);
			}

			std::scoped_lock lock(g_resourceProbeLock);
			g_resourceProbeCache[key] = exists;
			return exists;
		}

		[[nodiscard]] std::string BuildSubgraphAnimationCandidate(const char* a_entryPath, const std::string& a_leaf)
		{
			if (a_leaf.empty()) {
				return {};
			}

			auto entry = NormalizeSeparators(SafeString(a_entryPath));
			if (entry.empty()) {
				return {};
			}

			const auto lowerEntry = ToLowerNormalized(entry);
			if (lowerEntry.ends_with(".hkx") || lowerEntry.ends_with(".hkt")) {
				if (ToLowerNormalized(GetClipLeaf(entry.c_str())) == ToLowerNormalized(a_leaf)) {
					const auto dot = entry.rfind('.');
					return NormalizeSeparators(entry.substr(0, dot) + ".hkx");
				}
				return {};
			}

			return NormalizeSeparators(entry + "\\" + a_leaf + ".hkx");
		}

		[[nodiscard]] std::uintptr_t ReadCurrentBehaviorRootId(const std::uintptr_t a_behaviorGraph)
		{
			return a_behaviorGraph ? *reinterpret_cast<const std::uintptr_t*>(a_behaviorGraph + 0x30) : 0;
		}

		[[nodiscard]] std::uintptr_t ResolveOwningGraphFromBehaviorGraph(
			const std::uintptr_t a_behaviorGraph,
			const std::uintptr_t a_fallbackGraph)
		{
			const auto graph = ReadCurrentBehaviorRootId(a_behaviorGraph);
			if (!graph) {
				return a_fallbackGraph;
			}

			const auto vtbl = *reinterpret_cast<const std::uintptr_t*>(graph);
			const auto graphVTable = REL::Relocation<std::uintptr_t>{ RE::VTABLE::BShkbAnimationGraph[0] }.address();
			return vtbl == graphVTable ? graph : a_fallbackGraph;
		}

		[[nodiscard]] std::uintptr_t FindSelectedSubgraphData(
			const std::uintptr_t a_graph,
			const std::uintptr_t a_behaviorGraph)
		{
			const auto rootId = ReadCurrentBehaviorRootId(a_behaviorGraph);
			if (!a_graph || !rootId) {
				return 0;
			}

			const auto swapArray = *reinterpret_cast<const std::uintptr_t*>(a_graph + 0x3A0);
			if (!swapArray) {
				return 0;
			}

			const auto entries = *reinterpret_cast<const std::uintptr_t*>(swapArray);
			const auto count = *reinterpret_cast<const std::uint32_t*>(swapArray + 0x10);
			if (!entries || count == 0 || count > 16) {
				return 0;
			}

			for (std::uint32_t index = 0; index < count; ++index) {
				const auto entry = entries + static_cast<std::uintptr_t>(index) * 0x48;
				const auto sharedData = *reinterpret_cast<const std::uintptr_t*>(entry + 0x08);
				if (sharedData && *reinterpret_cast<const std::uintptr_t*>(sharedData + 0xC0) == rootId) {
					return sharedData - 0x40;
				}
			}

			return 0;
		}

		[[nodiscard]] std::string ResolveFromSelectedSubgraphFiles(
			const std::uintptr_t a_graph,
			const std::uintptr_t a_behaviorGraph,
			const char* a_authoredPath)
		{
			const auto data = FindSelectedSubgraphData(a_graph, a_behaviorGraph);
			if (!data) {
				return {};
			}

			const auto leaf = GetClipLeaf(a_authoredPath);
			if (leaf.empty()) {
				return {};
			}

			const auto scanArray = [&](const std::uintptr_t a_arrayOffset) -> std::string {
				const auto array = data + a_arrayOffset;
				const auto entries = *reinterpret_cast<const std::uintptr_t*>(array);
				const auto size = *reinterpret_cast<const std::uint32_t*>(array + 0x10);
				if (!entries || size == 0 || size > 4096) {
					return {};
				}

				for (std::uint32_t index = 0; index < size; ++index) {
					const auto* entry = reinterpret_cast<const RE::BSFixedString*>(
						entries + static_cast<std::uintptr_t>(index) * sizeof(RE::BSFixedString));
					const auto candidate = BuildSubgraphAnimationCandidate(entry->c_str(), leaf);
					if (!candidate.empty() && ResourcePathExists(candidate)) {
						return candidate;
					}
				}
				return {};
			};

			if (auto resolved = scanArray(0x178); !resolved.empty()) {
				return resolved;
			}
			return scanArray(0x160);
		}

		[[nodiscard]] std::string ResolveClipDisplayPath(
			const std::uintptr_t a_graph,
			const std::uintptr_t a_behaviorGraph,
			const char* a_authoredPath)
		{
			if (auto resolved = ResolveFromSelectedSubgraphFiles(a_graph, a_behaviorGraph, a_authoredPath); !resolved.empty()) {
				return resolved;
			}
			return SafeString(a_authoredPath);
		}

		[[nodiscard]] bool IsClipGenerator(const std::uintptr_t a_candidate)
		{
			if (!a_candidate) {
				return false;
			}

			const auto clipVTable = REL::Relocation<std::uintptr_t>{ RE::VTABLE::hkbClipGenerator[0] }.address();
			return *reinterpret_cast<const std::uintptr_t*>(a_candidate) == clipVTable;
		}

		[[nodiscard]] std::uintptr_t SelectActiveClip(const std::uintptr_t a_activeNodeEntry)
		{
			if (IsClipGenerator(a_activeNodeEntry)) {
				return a_activeNodeEntry;
			}
			if (!a_activeNodeEntry) {
				return 0;
			}

			const auto nestedCandidate = *reinterpret_cast<const std::uintptr_t*>(a_activeNodeEntry + 0x08);
			return IsClipGenerator(nestedCandidate) ? nestedCandidate : 0;
		}

		[[nodiscard]] std::uintptr_t ReadActiveNodePointer(const std::uintptr_t a_activeNodeEntry)
		{
			if (!a_activeNodeEntry) {
				return 0;
			}
			if (IsClipGenerator(a_activeNodeEntry)) {
				return a_activeNodeEntry;
			}
			return *reinterpret_cast<const std::uintptr_t*>(a_activeNodeEntry + 0x08);
		}

		[[nodiscard]] std::uintptr_t ReadNestedBehaviorGraph(const std::uintptr_t a_activeNodeEntry)
		{
			return a_activeNodeEntry ? *reinterpret_cast<const std::uintptr_t*>(a_activeNodeEntry + 0x10) : 0;
		}

		[[nodiscard]] float NormalizeClipTime(const float a_time, const float a_duration)
		{
			if (!std::isfinite(a_time) || a_time <= 0.0F) {
				return 0.0F;
			}
			if (!std::isfinite(a_duration) || a_duration <= 0.0F) {
				return a_time;
			}

			const auto wrapped = std::fmod(a_time, a_duration);
			return wrapped < 0.0F ? wrapped + a_duration : wrapped;
		}

		void OverwriteClipLocalTime(const std::uintptr_t a_clip, const float a_time)
		{
			if (!a_clip || !std::isfinite(a_time)) {
				return;
			}

			// HaBCR seek pattern: hkbClipGenerator::localTime, animationControl
			// local time, then the clip dirty byte.
			*reinterpret_cast<float*>(a_clip + 0x140) = a_time;
			const auto animControl = *reinterpret_cast<std::uintptr_t*>(a_clip + 0xD0);
			if (animControl) {
				*reinterpret_cast<float*>(animControl + 0x10) = a_time;
			}
			*reinterpret_cast<std::uint8_t*>(a_clip + 0x151) = 1;
		}

		struct ActiveClipTiming
		{
			std::uintptr_t clip{ 0 };
			float duration{ 0.0F };
		};

		[[nodiscard]] bool PathMatchesResolvedPath(const std::string& a_authoredPath, const std::string& a_targetPath)
		{
			const auto targetPath = ToLowerNormalized(NormalizeSeparators(a_targetPath));
			const auto targetLeaf = ToLowerNormalized(GetClipLeaf(targetPath.c_str()));
			const auto authoredPath = NormalizeSeparators(a_authoredPath);
			const auto authoredLower = ToLowerNormalized(authoredPath);
			const auto authoredLeaf = ToLowerNormalized(GetClipLeaf(authoredPath.c_str()));
			return (!targetPath.empty() && authoredLower == targetPath) ||
			       (!targetLeaf.empty() && authoredLeaf == targetLeaf);
		}

		[[nodiscard]] bool ClipMatchesResolvedPath(const std::uintptr_t a_clip, const std::string& a_targetPath)
		{
			if (!a_clip) {
				return false;
			}

			return PathMatchesResolvedPath(SafeString(ReadTaggedString(a_clip + 0x90)), a_targetPath);
		}

		[[nodiscard]] HkbBehaviorGraphDiagnostic* GetBehaviorGraphDiagnostic(RE::BShkbAnimationGraph& a_graph)
		{
			const auto* const graphBase = reinterpret_cast<const std::byte*>(std::addressof(a_graph));
			return *reinterpret_cast<HkbBehaviorGraphDiagnostic* const*>(graphBase + 0x378);
		}

		[[nodiscard]] ActiveClipTiming FindActiveClipTiming(
			RE::BShkbAnimationGraph& a_graph,
			const std::string& a_resolvedPath)
		{
			auto* behavior = GetBehaviorGraphDiagnostic(a_graph);
			if (!behavior || !behavior->activeNodes || !behavior->activeNodes->data ||
				behavior->activeNodes->size <= 0 || behavior->activeNodes->size > 1024) {
				return {};
			}

			for (std::int32_t index = 0; index < behavior->activeNodes->size; ++index) {
				const auto entry = reinterpret_cast<std::uintptr_t>(behavior->activeNodes->data[index]);
				const auto clip = SelectActiveClip(entry);
				if (!clip) {
					continue;
				}

				const auto animControl = *reinterpret_cast<std::uintptr_t*>(clip + 0xD0);
				const auto animBinding = animControl ? *reinterpret_cast<std::uintptr_t*>(animControl + 0x38) : 0;
				const auto anim = animBinding ? *reinterpret_cast<std::uintptr_t*>(animBinding + 0x18) : 0;
				const auto duration = anim ? *reinterpret_cast<float*>(anim + 0x14) : 0.0F;
				if (!std::isfinite(duration) || duration <= 0.0F) {
					continue;
				}

				if (ClipMatchesResolvedPath(clip, a_resolvedPath)) {
					return { clip, duration };
				}
			}

			return {};
		}

		[[nodiscard]] RE::BSTEventSource<RE::BSAnimationGraphEvent>* GetGraphEventSource(
			RE::BShkbAnimationGraph* a_graph)
		{
			if (!a_graph) {
				return nullptr;
			}

			// IDA: Actor::SetupAnimEventSinks registers BSAnimationGraphEvent sinks at graph+0x68.
			return reinterpret_cast<RE::BSTEventSource<RE::BSAnimationGraphEvent>*>(
				reinterpret_cast<std::byte*>(a_graph) + 0x68);
		}

		class AdjustmentArena final
		{
		public:
			using Adjustment = RE::AnimationSpeedInformationTypes::AnimationStateAdjustment;
			static constexpr std::uint32_t kPageItemCount = 16;

			AdjustmentArena()
			{
				scrapHeap_ = RE::MemoryManager::GetSingleton().GetThreadScrapHeap();
				next_ = std::addressof(head_);
			}

			AdjustmentArena(const AdjustmentArena&) = delete;
			AdjustmentArena& operator=(const AdjustmentArena&) = delete;

			~AdjustmentArena()
			{
				if (engineOwnedPages_) {
					g_destroyAdjustmentArena(this);
				} else {
					DestroyManualPages();
				}
			}

			void FillFrom(const std::vector<Adjustment>& a_adjustments)
			{
				DestroyManualPages();
				if (a_adjustments.empty()) {
					return;
				}

				std::uint32_t remaining = static_cast<std::uint32_t>(a_adjustments.size());
				std::uint32_t sourceIndex = 0;
				while (remaining > 0) {
					auto* page = new Page();
					ownedPages_.push_back(page);
					const bool firstPage = head_ == nullptr;
					*next_ = page;
					next_ = std::addressof(page->next);
					tail_ = page;
					if (firstPage) {
						begin_ = page->buffer;
					}

					const auto inPage = (std::min)(remaining, kPageItemCount);
					for (std::uint32_t index = 0; index < inPage; ++index) {
						std::construct_at(
							reinterpret_cast<Adjustment*>(page->buffer + index * sizeof(Adjustment)),
							a_adjustments[sourceIndex++]);
					}
					size_ += inPage;
					remaining -= inPage;
				}

				end_ = tail_->buffer + (size_ % kPageItemCount == 0 ? kPageItemCount : size_ % kPageItemCount) * sizeof(Adjustment);
			}

			template <class Func>
			void ForEach(Func&& a_func) const
			{
				auto* page = head_;
				std::uint32_t remaining = size_;
				while (page && remaining > 0) {
					const auto inPage = (std::min)(remaining, kPageItemCount);
					for (std::uint32_t index = 0; index < inPage; ++index) {
						const auto* adjustment = std::launder(
							reinterpret_cast<const Adjustment*>(page->buffer + index * sizeof(Adjustment)));
						a_func(*adjustment);
					}
					remaining -= inPage;
					page = page->next;
				}
			}

			[[nodiscard]] std::uint32_t Size() const { return size_; }

			void MarkEngineOwnedPages() { engineOwnedPages_ = true; }

		private:
			struct Page
			{
				std::byte buffer[sizeof(Adjustment) * kPageItemCount]{};
				Page* next{ nullptr };
			};
			static_assert(offsetof(Page, next) == 0x100);

			void DestroyManualPages()
			{
				if (size_ != 0) {
					ForEach([](const Adjustment& a_adjustment) {
						std::destroy_at(std::addressof(const_cast<Adjustment&>(a_adjustment)));
					});
				}
				for (auto* page : ownedPages_) {
					delete page;
				}
				ownedPages_.clear();
				head_ = nullptr;
				next_ = std::addressof(head_);
				tail_ = nullptr;
				free_ = nullptr;
				end_ = nullptr;
				begin_ = nullptr;
				size_ = 0;
			}

			RE::ScrapHeap* scrapHeap_{ nullptr };
			Page* head_{ nullptr };
			Page** next_{ nullptr };
			Page* tail_{ nullptr };
			Page* free_{ nullptr };
			std::byte* end_{ nullptr };
			std::byte* begin_{ nullptr };
			std::uint32_t size_{ 0 };
			std::uint32_t pad3C_{ 0 };
			std::vector<Page*> ownedPages_;
			bool engineOwnedPages_{ false };
		};
		static_assert(sizeof(AdjustmentArena) > 0x40);

		class PreviewSpeedAnimationChannel final :
			public RE::BSAnimationGraphChannel
		{
		public:
			PreviewSpeedAnimationChannel(
				RE::Actor& a_sourceActor,
				RE::IAnimationGraphManagerHolder& a_previewHolder,
				SpeedChannelDebugInfo& a_debugInfo) :
				sourceActor_(std::addressof(a_sourceActor)),
				previewHolder_(std::addressof(a_previewHolder)),
				debug_(std::addressof(a_debugInfo))
			{
				variableName = "Speed";
				scale_ = g_getReferenceScale(sourceActor_);
				if (scale_ < 1.0e-4F || !std::isfinite(scale_)) {
					scale_ = 1.0F;
				}

				float existingSpeed{ 0.0F };
				if (previewHolder_->GetGraphVariableImplFloat(variableName, existingSpeed)) {
					lastSpeed_ = existingSpeed;
					SetCurrentValue(existingSpeed);
				}
				if (debug_) {
					debug_->constructed = true;
					debug_->scale = scale_;
					debug_->lastSpeed = lastSpeed_;
					debug_->graphSpeed = lastSpeed_;
				}
			}

			~PreviewSpeedAnimationChannel() override = default;

			void PollChannelUpdate(const bool a_shouldApplyAdjustments) override
			{
				if (debug_) {
					++debug_->pollCount;
					debug_->polled = true;
					debug_->applyAdjustments = a_shouldApplyAdjustments;
					debug_->previewFreeze = false;
					debug_->actorFreeze = false;
					debug_->useContours = false;
					debug_->actorAllowsContours = false;
					debug_->contourResolved = false;
					debug_->contourState = false;
					debug_->contourApplied = false;
					debug_->contourResponse = 0;
					debug_->adjustmentCount = 0;
					debug_->scale = scale_;
					debug_->lastSpeed = lastSpeed_;
					debug_->graphSpeed = lastSpeed_;
				}

				if (!sourceActor_ || !previewHolder_) {
					SetCurrentValue(lastSpeed_);
					return;
				}

				bool freezeSpeedUpdate{ false };
				(void)previewHolder_->GetGraphVariableImplBool("bFreezeSpeedUpdate", freezeSpeedUpdate);
				const bool actorFreeze = g_getFreezeGraphLocomotionChannel(sourceActor_);
				if (debug_) {
					debug_->previewFreeze = freezeSpeedUpdate;
					debug_->actorFreeze = actorFreeze;
				}
				if (freezeSpeedUpdate || actorFreeze) {
					SetCurrentValue(lastSpeed_);
					return;
				}

				const float desiredSpeed = sourceActor_->GetDesiredSpeed();
				float speed = desiredSpeed / scale_;
				if (speed < 0.0F || !std::isfinite(speed)) {
					speed = 0.0F;
				}
				if (debug_) {
					debug_->desiredSpeed = desiredSpeed;
					debug_->rawSpeed = speed;
				}

				const bool actorAllowsContours = g_useSpeedContoursForMovementCalculations(sourceActor_, 4);
				const bool useContours = speed >= 0.0F;
				if (debug_) {
					debug_->useContours = useContours;
					debug_->actorAllowsContours = actorAllowsContours;
				}
				if (useContours) {
					RE::BSTSmartPointer<RE::AnimationSpeedContour> contour;
					bool hasContourState{ false };
					const bool contourResolved =
						g_getActiveContourFromHolder(previewHolder_, contour, std::addressof(hasContourState)) && contour;
					if (debug_) {
						debug_->contourResolved = contourResolved;
						debug_->contourState = hasContourState;
					}
					if (contourResolved) {
						ApplyContourSpeed(speed, *contour, a_shouldApplyAdjustments);
					}
				}

				lastSpeed_ = speed;
				SetCurrentValue(speed);
				if (debug_) {
					debug_->graphSpeed = speed;
					debug_->lastSpeed = lastSpeed_;
				}
			}

			void Reset() override
			{
				if (!sourceActor_ || !previewHolder_) {
					lastSpeed_ = 0.0F;
					scale_ = 1.0F;
					SetCurrentValue(lastSpeed_);
					lastContourAdjustments_.clear();
					return;
				}

				scale_ = g_getReferenceScale(sourceActor_);
				if (scale_ < 1.0e-4F || !std::isfinite(scale_)) {
					scale_ = 1.0F;
				}

				float existingSpeed{ 0.0F };
				if (previewHolder_->GetGraphVariableImplFloat(variableName, existingSpeed)) {
					lastSpeed_ = existingSpeed;
				}
				SetCurrentValue(lastSpeed_);
				lastContourAdjustments_.clear();
				if (debug_) {
					debug_->reset = true;
					debug_->scale = scale_;
					debug_->lastSpeed = lastSpeed_;
					debug_->graphSpeed = lastSpeed_;
				}
			}

		private:
			using Adjustment = RE::AnimationSpeedInformationTypes::AnimationStateAdjustment;

			void SetCurrentValue(const float a_value)
			{
				static_assert(sizeof(unk18) == sizeof(float));
				std::memcpy(std::addressof(unk18), std::addressof(a_value), sizeof(float));
			}

			void ApplyContourSpeed(
				float& a_speed,
				const RE::AnimationSpeedContour& a_contour,
				const bool a_shouldApplyAdjustments)
			{
				AdjustmentArena lastAdjustments;
				lastAdjustments.FillFrom(lastContourAdjustments_);

				AdjustmentArena nextAdjustments;
				nextAdjustments.MarkEngineOwnedPages();
				RE::AnimationSpeedInformationTypes::RequestedSpeed requestedSpeed{ a_speed };
				RE::AnimationSpeedInformationTypes::GraphSpeedInput graphSpeed{};
				constexpr float kNeutralPreviewDirection = 0.0F;
				const auto response = g_getGraphSpeedForRequestedSpeedAndDirection(
					std::addressof(a_contour),
					requestedSpeed,
					kNeutralPreviewDirection,
					static_cast<const void*>(std::addressof(lastAdjustments)),
					graphSpeed,
					static_cast<void*>(std::addressof(nextAdjustments)));
				if (debug_) {
					debug_->contourResponse = response;
					debug_->adjustmentCount = nextAdjustments.Size();
				}
				if (response == 0) {
					return;
				}

				a_speed = graphSpeed.speed;
				if (debug_) {
					debug_->contourApplied = true;
				}
				if (!a_shouldApplyAdjustments) {
					return;
				}

				ApplyAdjustments(nextAdjustments);

				lastContourAdjustments_.clear();
				nextAdjustments.ForEach([&](const Adjustment& adjustment) {
					lastContourAdjustments_.push_back(adjustment);
				});
			}

			void ApplyAdjustments(const AdjustmentArena& a_adjustments)
			{
				a_adjustments.ForEach([&](const Adjustment& adjustment) {
					if (adjustment.adjustmentName.empty()) {
						return;
					}

					if (adjustment.isVariable) {
						if (adjustment.useFloat) {
							(void)previewHolder_->SetGraphVariableFloat(
								adjustment.adjustmentName,
								adjustment.adjustmentVariable.variableFloatValue);
						} else {
							(void)previewHolder_->SetGraphVariableInt(
								adjustment.adjustmentName,
								adjustment.adjustmentVariable.variableIntValue);
						}
					} else {
						if (IsDuplicateEventAdjustment(adjustment)) {
							return;
						}
						(void)previewHolder_->NotifyAnimationGraphImpl(adjustment.adjustmentName);
					}
				});
			}

			[[nodiscard]] bool IsDuplicateEventAdjustment(const Adjustment& a_adjustment) const
			{
				if (a_adjustment.isVariable) {
					return false;
				}

				return std::any_of(
					lastContourAdjustments_.begin(),
					lastContourAdjustments_.end(),
					[&](const Adjustment& lastAdjustment) {
						return !lastAdjustment.isVariable &&
							   lastAdjustment.adjustmentName == a_adjustment.adjustmentName;
					});
			}

			RE::Actor* sourceActor_{ nullptr };
			RE::IAnimationGraphManagerHolder* previewHolder_{ nullptr };
			SpeedChannelDebugInfo* debug_{ nullptr };
			std::vector<Adjustment> lastContourAdjustments_;
			float lastSpeed_{ 0.0F };
			float scale_{ 1.0F };
		};
		static_assert(offsetof(PreviewSpeedAnimationChannel, variableName) == 0x10);
		static_assert(offsetof(PreviewSpeedAnimationChannel, unk18) == 0x18);

		class PreviewAnimationGraphHolder final :
			public RE::IAnimationGraphManagerHolder
		{
		public:
			class PreviewAnimationEventSink final :
				public RE::BSTEventSink<RE::BSAnimationGraphEvent>
			{
			public:
				explicit PreviewAnimationEventSink(PreviewAnimationGraphHolder& a_owner) :
					owner_(std::addressof(a_owner))
				{}

				RE::BSEventNotifyControl ProcessEvent(
					const RE::BSAnimationGraphEvent& a_event,
					[[maybe_unused]] RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source) override
				{
					if (owner_) {
						owner_->RecordPreviewAnimationEvent(a_event);
					}
					return RE::BSEventNotifyControl::kContinue;
				}

			private:
				PreviewAnimationGraphHolder* owner_{ nullptr };
			};

			PreviewAnimationGraphHolder(RE::PlayerCharacter& a_source, RE::NiAVObject& a_target) :
				sourceActor_(std::addressof(a_source)),
				sourceHolder_(std::addressof(GetAnimationGraphHolder(a_source))),
				targetRoot_(std::addressof(a_target)),
				targetGraphRoot_(std::addressof(a_target)),
				sourceManagerIdentity_(GetLiveSourceManager()),
				previewAnimationEventSink_(*this)
			{}

			~PreviewAnimationGraphHolder() override
			{
				UnregisterPreviewEventSource();
				manager_.reset();
			}

			bool NotifyAnimationGraphImpl(const RE::BSFixedString& a_eventName) override
			{
				return g_notifyAnimationGraphImpl(this, a_eventName);
			}

			bool GetAnimationGraphManagerImpl(RE::BSTSmartPointer<RE::BSAnimationGraphManager>& a_animGraphMgr) const override
			{
				a_animGraphMgr = manager_;
				return static_cast<bool>(a_animGraphMgr);
			}

			bool SetAnimationGraphManagerImpl(const RE::BSTSmartPointer<RE::BSAnimationGraphManager>& a_animGraphMgr) override
			{
				manager_ = a_animGraphMgr;
				return true;
			}

			bool PopulateGraphNodesToTarget(RE::BSScrapArray<RE::NiAVObject*>& a_nodesToAnimate) const override
			{
				if (!targetGraphRoot_) {
					return false;
				}

				a_nodesToAnimate.push_back(targetGraphRoot_);
				return true;
			}

			bool ConstructAnimationGraph(RE::BSTSmartPointer<RE::BShkbAnimationGraph>& a_animGraph) override
			{
				if (!sourceActor_) {
					return false;
				}

				// IDA: Actor/Player/Simple graph constructors allocate 0x3D0 bytes
				// with MemoryManager::Allocate(alignment=0x10, required=true).
				auto* memory = static_cast<RE::BShkbAnimationGraph*>(
					RE::aligned_alloc(kBShkbAnimationGraphAlignment, kBShkbAnimationGraphSize));
				if (!memory) {
					return false;
				}

				// IDA: PlayerCharacter::ConstructAnimationGraph constructs player
				// project graphs as BShkbAnimationGraph(player, false). The preview
				// still redirects graph targets to targetGraphRoot_ through
				// PopulateGraphNodesToTarget/SetAnimationGraphTargets.
				auto* graph = g_constructBShkbAnimationGraph(
					memory,
					sourceActor_,
					false);
				if (!graph) {
					RE::aligned_free(memory);
					return false;
				}

				a_animGraph.reset(graph);
				return static_cast<bool>(a_animGraph);
			}

			bool InitializeAnimationGraphVariables(
				const RE::BSTSmartPointer<RE::BShkbAnimationGraph>& a_newGraph) const override
			{
				return sourceHolder_ ? sourceHolder_->InitializeAnimationGraphVariables(a_newGraph) : false;
			}

			bool SetupAnimEventSinks(const RE::BSTSmartPointer<RE::BShkbAnimationGraph>& a_newGraph) override
			{
				// IDA: Actor::SetupAnimEventSinks registers movement, subgraph, and transform sinks
				// against the actor. The preview graph must not feed those events back into the live actor.
				RegisterPreviewEventSource(a_newGraph);
				return true;
			}

			bool CreateAnimationChannels(
				RE::BSScrapArray<RE::BSTSmartPointer<RE::BSAnimationGraphChannel>>& a_channels) override
			{
				if (!sourceActor_ || !sourceHolder_) {
					return false;
				}

				// IDA: IAnimationGraphManagerHolder::InitializeGraphManager calls vtable +0x58
				// to fill this temporary array immediately before RegisterChannels().
				RE::BSScrapArray<RE::BSTSmartPointer<RE::BSAnimationGraphChannel>> sourceChannels;
				if (!sourceHolder_->CreateAnimationChannels(sourceChannels)) {
					return false;
				}

				const bool idlePreviewMode = !GetConfig().animation.useLiveAnimation;
				for (const auto& channel : sourceChannels) {
					if (channel && !IsSuppressedAnimationChannel(*channel) &&
						(!idlePreviewMode ||
						 !ContainsEngineFixedString(channel->variableName, kIdlePreviewSuppressedAnimationChannels))) {
						a_channels.push_back(channel);
					}
				}

				a_channels.push_back(RE::BSTSmartPointer<RE::BSAnimationGraphChannel>(
					new PreviewSpeedAnimationChannel(*sourceActor_, *this, speedDebug_)));
				return true;
			}

			bool ShouldUpdateAnimation() override { return true; }

			std::uint32_t GetGraphVariableCacheSize() const override
			{
				return sourceHolder_ ? sourceHolder_->GetGraphVariableCacheSize() : 0;
			}

			bool GetGraphVariableImpl(std::uint32_t a_graphVarID, float& a_out) const override
			{
				return sourceHolder_ && sourceHolder_->GetGraphVariableImpl(a_graphVarID, a_out);
			}

			bool GetGraphVariableImpl(std::uint32_t a_graphVarID, bool& a_out) const override
			{
				return sourceHolder_ && sourceHolder_->GetGraphVariableImpl(a_graphVarID, a_out);
			}

			bool GetGraphVariableImpl(std::uint32_t a_graphVarID, std::int32_t& a_out) const override
			{
				return sourceHolder_ && sourceHolder_->GetGraphVariableImpl(a_graphVarID, a_out);
			}

			bool GetGraphVariableImplFloat(const RE::BSFixedString& a_variable, float& a_out) const override
			{
				if (auto graph = GetActivePreviewGraph(); graph && g_getBShkbGraphVariableFloat(graph.get(), a_variable, a_out)) {
					return true;
				}
				return sourceHolder_ && sourceHolder_->GetGraphVariableImplFloat(a_variable, a_out);
			}

			bool GetGraphVariableImplInt(const RE::BSFixedString& a_variable, std::int32_t& a_out) const override
			{
				if (TryGetIdlePreviewIntGraphVariable(a_variable, a_out)) {
					return true;
				}
				if (auto graph = GetActivePreviewGraph(); graph && g_getBShkbGraphVariableInt(graph.get(), a_variable, a_out)) {
					return true;
				}
				return sourceHolder_ && sourceHolder_->GetGraphVariableImplInt(a_variable, a_out);
			}

			bool GetGraphVariableImplBool(const RE::BSFixedString& a_variable, bool& a_out) const override
			{
				if (auto graph = GetActivePreviewGraph(); graph && g_getBShkbGraphVariableBool(graph.get(), a_variable, a_out)) {
					return true;
				}
				return sourceHolder_ && sourceHolder_->GetGraphVariableImplBool(a_variable, a_out);
			}

			void PreUpdateAnimationGraphManager(
				const RE::BSTSmartPointer<RE::BSAnimationGraphManager>& a_animGraphMgr) const override
			{
				if (sourceHolder_) {
					g_actorPreUpdateAnimationGraphManager(sourceHolder_, a_animGraphMgr);
				}
			}

			void PreLoadAnimationGraphManager(
				const RE::BSTSmartPointer<RE::BSAnimationGraphManager>& a_animGraphMgr) override
			{
				if (sourceHolder_) {
					g_actorPreLoadAnimationGraphManager(sourceHolder_, a_animGraphMgr);
				}
			}

			void PostLoadAnimationGraphManager(
				const RE::BSTSmartPointer<RE::BSAnimationGraphManager>& a_animGraphMgr) override
			{
				if (sourceHolder_) {
					g_actorPostLoadAnimationGraphManager(sourceHolder_, a_animGraphMgr);
				}
			}

			void PrepareGraphManagerForSubgraphs()
			{
				if (!manager_) {
					return;
				}

				// IDA: Actor::PreLoadAnimationGraphManager calls
				// BSBehaviorGraphSwapSingleton::UpdateForManager, which is required
				// before InitializeSubGraph can use graph+0x3A0 swap data.
				PreLoadAnimationGraphManager(manager_);
				PostLoadAnimationGraphManager(manager_);
			}

			bool TargetAnimationGraph()
			{
				if (!targetGraphRoot_) {
					return false;
				}

				// IDA: Weapon/UI/effect background-load holders call
				// IAnimationGraphManagerHolder::SetAnimationGraphTargets(root, true).
				// Engine holders pass the 3D root; CreateBoneMapping then uses
				// BSFlattenedBoneTree::GetBoneByName(root, ...) to resolve bone refs.
				return g_setAnimationGraphTarget(this, targetGraphRoot_, true);
			}

			[[nodiscard]] bool LoadMirroredSubgraph(
				const RE::SubgraphIdentifier& a_identifier,
				const std::int32_t& a_priority,
				RE::BSTSmallArray<RE::SubgraphHandle, 2>& a_handles,
				RE::BSTSmallArray<RE::SubgraphIdentifier, 2>& a_identifiers)
			{
				if (!sourceActor_ || !manager_) {
					return false;
				}

				auto graph = GetActivePreviewGraph();
				if (!graph) {
					return false;
				}

				auto* const subGraphData = *g_animationSubGraphDataSingleton;
				auto* const swapSingleton = *g_behaviorGraphSwapSingleton;
				if (!subGraphData || !swapSingleton) {
					return false;
				}

				auto* race = sourceActor_->GetVisualsRace();
				if (!race) {
					race = sourceActor_->race;
				}
				if (!race) {
					return false;
				}

				RE::BSFixedString rootName;
				SubgraphPreloadArena preloadFiles;
				if (!g_retrieveSubGraphData(
						subGraphData,
						race->formID,
						std::addressof(a_identifier),
						std::addressof(rootName),
						std::addressof(preloadFiles)) ||
					rootName.empty()) {
					return false;
				}

				SubgraphPreloadArena lowPriorityPreloadFiles;
				// IDA: native default/weapon SubBehaviorLoadingFunctor gathers
				// actor-specific dynamic animation files into the second preload arena.
				// These include the transition clips used when swapping weapon graphs.
				(void)g_gatherPreloadAnimations(
					*race,
					*sourceActor_,
					a_identifier,
					g_getAnimationSex(sourceActor_),
					false,
					lowPriorityPreloadFiles);
				RE::SubgraphHandle handle;
				if (!g_initializeSubGraph(
						swapSingleton,
						graph.get(),
						std::addressof(rootName),
						std::addressof(preloadFiles),
						std::addressof(lowPriorityPreloadFiles),
						std::addressof(a_priority),
						std::addressof(handle)) ||
					handle.handle == 0) {
					return false;
				}

				a_handles.push_back(handle);
				a_identifiers.push_back(a_identifier);
				return true;
			}

			[[nodiscard]] static bool ContainsSubgraphId(
				const RE::BSTSmallArray<RE::SubgraphIdentifier, 2>& a_ids,
				const RE::SubgraphIdentifier& a_id)
			{
				return std::any_of(a_ids.begin(), a_ids.end(), [&](const auto& candidate) {
					return candidate.identifier == a_id.identifier;
				});
			}

			[[nodiscard]] static bool ContainsSubgraphId(
				const std::vector<RE::SubgraphIdentifier>& a_ids,
				const RE::SubgraphIdentifier& a_id)
			{
				return std::any_of(a_ids.begin(), a_ids.end(), [&](const auto& candidate) {
					return candidate.identifier == a_id.identifier;
				});
			}

			[[nodiscard]] static bool SubgraphIdsEqual(
				const RE::BSTSmallArray<RE::SubgraphIdentifier, 2>& a_loaded,
				const std::vector<RE::SubgraphIdentifier>& a_requested)
			{
				if (a_loaded.size() != a_requested.size()) {
					return false;
				}

				return std::all_of(a_loaded.begin(), a_loaded.end(), [&](const auto& loaded) {
					return ContainsSubgraphId(a_requested, loaded);
				});
			}

			[[nodiscard]] static bool LoadedSubgraphsAreRequested(
				const RE::BSTSmallArray<RE::SubgraphIdentifier, 2>& a_loaded,
				const std::vector<RE::SubgraphIdentifier>& a_requested)
			{
				return std::all_of(a_loaded.begin(), a_loaded.end(), [&](const auto& loaded) {
					return ContainsSubgraphId(a_requested, loaded);
				});
			}

			[[nodiscard]] bool AreSubgraphsReady(
				const RE::BSTSmallArray<RE::SubgraphHandle, 2>& a_handles,
				const std::int32_t& a_priority) const
			{
				if (a_handles.empty()) {
					return true;
				}

				auto graph = GetActivePreviewGraph();
				auto* const swapSingleton = *g_behaviorGraphSwapSingleton;
				const auto* const handles = GetEngineSmallArrayStorage(a_handles);
				if (!graph || !swapSingleton || !handles) {
					return false;
				}

				for (std::uint32_t index = 0; index < a_handles.size(); ++index) {
					// IDA: IsSubGraphLoaded returns 0 when the handle is absent, 1 while
					// loading, and 2 only after RetrieveAndSetupSharedData has installed
					// the per-graph event, variable, and character-property symbol maps.
					if (g_isSubGraphLoaded(
							swapSingleton,
							graph.get(),
							std::addressof(handles[index]),
							std::addressof(a_priority)) != 2) {
						return false;
					}
				}

				return true;
			}

			bool ReleaseMirroredSubgraphs(
				RE::BSTSmallArray<RE::SubgraphHandle, 2>& a_handles,
				RE::BSTSmallArray<RE::SubgraphIdentifier, 2>& a_identifiers)
			{
				if (a_handles.empty() && a_identifiers.empty()) {
					return true;
				}

				auto graph = GetActivePreviewGraph();
				auto* const swapSingleton = *g_behaviorGraphSwapSingleton;
				const auto* const handles = GetEngineSmallArrayStorage(a_handles);
				const auto* const identifiers = GetEngineSmallArrayStorage(a_identifiers);
				if (!graph || !swapSingleton || !handles || !identifiers || a_handles.size() != a_identifiers.size()) {
					return false;
				}

				RE::BSTSmallArray<RE::SubgraphHandle, 2> retainedHandles;
				RE::BSTSmallArray<RE::SubgraphIdentifier, 2> retainedIdentifiers;
				for (std::uint32_t index = 0; index < a_handles.size(); ++index) {
					auto identifier = identifiers[index];
					if (!g_releaseSubGraph(
							swapSingleton,
							graph.get(),
							std::addressof(handles[index]),
							std::addressof(identifier),
							true)) {
						retainedHandles.push_back(handles[index]);
						retainedIdentifiers.push_back(identifiers[index]);
					}
				}

				a_handles = std::move(retainedHandles);
				a_identifiers = std::move(retainedIdentifiers);
				return a_handles.empty();
			}

			bool ReconcileSubgraphSet(
				const std::vector<RE::SubgraphIdentifier>& a_requested,
				const std::int32_t& a_priority,
				RE::BSTSmallArray<RE::SubgraphHandle, 2>& a_currentHandles,
				RE::BSTSmallArray<RE::SubgraphIdentifier, 2>& a_currentIdentifiers,
				RE::BSTSmallArray<RE::SubgraphHandle, 2>& a_loadingHandles,
				RE::BSTSmallArray<RE::SubgraphIdentifier, 2>& a_loadingIdentifiers)
			{
				if (SubgraphIdsEqual(a_currentIdentifiers, a_requested)) {
					// A rapid request can return to the current role while a different
					// replacement is still loading. Vanilla discards only that loading set.
					return ReleaseMirroredSubgraphs(a_loadingHandles, a_loadingIdentifiers);
				}

				// IDA: RequestLoadAnimationsForWeaponChange leaves current handles
				// active and replaces only the separate loading-handle container.
				if (!LoadedSubgraphsAreRequested(a_loadingIdentifiers, a_requested) &&
					!ReleaseMirroredSubgraphs(a_loadingHandles, a_loadingIdentifiers)) {
					return false;
				}

				for (const auto& identifier : a_requested) {
					if (!ContainsSubgraphId(a_loadingIdentifiers, identifier) &&
						!LoadMirroredSubgraph(
							identifier, a_priority, a_loadingHandles, a_loadingIdentifiers)) {
						return false;
					}
				}

				if (!a_requested.empty() && !AreSubgraphsReady(a_loadingHandles, a_priority)) {
					return false;
				}

				// IDA: ClearUnusedSubgraph swaps loading/current containers only after
				// IsAnimationSubGraphLoaded succeeds, then releases the old current set.
				// Release keeps an active old state pending until its blend exits.
				std::swap(a_currentHandles, a_loadingHandles);
				std::swap(a_currentIdentifiers, a_loadingIdentifiers);
				return ReleaseMirroredSubgraphs(a_loadingHandles, a_loadingIdentifiers);
			}

			bool ReconcileRequestedSubgraphs()
			{
				if (!sourceActor_ || !sourceActor_->currentProcess || !manager_) {
					return false;
				}

				auto* const tes = RE::TES::GetSingleton();
				auto* const parentCell = sourceActor_->GetParentCell();
				if (!tes || !parentCell) {
					return false;
				}

				// IDA: AIProcess::LoadInitialSubGraphs computes IO priority with
				// TES::GetCellPriority(actor.parentCell, nullptr), then forwards it
				// through RequestDefaultSubGraph/RequestWeaponSubGraph.
				const std::int32_t priority = g_getCellPriority(tes, parentCell, nullptr);
				auto* const middleHigh = sourceActor_->currentProcess->middleHigh;
				if (!middleHigh) {
					return false;
				}

				std::vector<RE::SubgraphIdentifier> requestedDefault;
				std::vector<RE::SubgraphIdentifier> requestedWeapon;
				// IDA: requested IDs describe an in-flight replacement. Once the
				// request commits, AIProcess retains the role in current IDs and can
				// clear the requested array. Use requested only while it is populated.
				const auto& defaultIntent = middleHigh->requestedDefaultSubGraphID.empty() ?
					middleHigh->currentDefaultSubGraphID : middleHigh->requestedDefaultSubGraphID;
				const auto& weaponIntent = middleHigh->requestedWeaponSubGraphID.empty() ?
					middleHigh->currentWeaponSubGraphID : middleHigh->requestedWeaponSubGraphID;
				for (const auto& root : middleHigh->subGraphIdleManagerRoots) {
					if (root.forFirstPerson || root.subGraphID.identifier == 0) {
						continue;
					}

					if (ContainsSubgraphId(defaultIntent, root.subGraphID) &&
						!ContainsSubgraphId(requestedDefault, root.subGraphID)) {
						requestedDefault.push_back(root.subGraphID);
					} else if (ContainsSubgraphId(weaponIntent, root.subGraphID) &&
						!ContainsSubgraphId(requestedWeapon, root.subGraphID)) {
						requestedWeapon.push_back(root.subGraphID);
					}
				}

				// A non-empty request without a resolved third-person idle root is still
				// loading. Keep the previous role until the new root becomes observable.
				const bool defaultReady = defaultIntent.empty() || !requestedDefault.empty();
				const bool weaponReady = weaponIntent.empty() || !requestedWeapon.empty();
				const bool weaponRoleChanging = weaponReady &&
					!SubgraphIdsEqual(weaponSubgraphIds_, requestedWeapon);
				bool reconciled = true;
				if (defaultReady) {
					reconciled &= ReconcileSubgraphSet(
						requestedDefault,
						priority,
						defaultSubgraphHandles_,
						defaultSubgraphIds_,
						loadingDefaultSubgraphHandles_,
						loadingDefaultSubgraphIds_);
				}
				bool weaponReconciled = false;
				if (weaponReady) {
					weaponReconciled = ReconcileSubgraphSet(
						requestedWeapon,
						priority,
						weaponSubgraphHandles_,
						weaponSubgraphIds_,
						loadingWeaponSubgraphHandles_,
						loadingWeaponSubgraphIds_);
						reconciled &= weaponReconciled;
				}
				if (weaponRoleChanging && !weaponSubgraphIds_.empty() &&
					SubgraphIdsEqual(weaponSubgraphIds_, requestedWeapon) && IsWeaponSubgraphLinked()) {
					ScheduleWeaponEquipAfterCommit();
				}
				weaponSubgraphRequestPending_ = !weaponReady || !weaponReconciled;
				return reconciled && defaultReady && weaponReady;
			}

			bool ActivatePreviewGraphManager()
			{
				if (!manager_) {
					return false;
				}

				auto graph = GetActivePreviewGraph();
				const auto behavior = graph ?
					*reinterpret_cast<HkbBehaviorGraphDiagnostic* const*>(
						reinterpret_cast<const std::byte*>(graph.get()) + 0x378) :
					nullptr;

				// IDA: BShkbAnimationGraph::ActivateImpl returns false when
				// graph+0x378 is null or hkbBehaviorGraph+0x1AA is already set.
				// Already-active is not a failed activation path.
				if (behavior && behavior->isActive) {
					return true;
				}

				return g_activateAnimationGraphManager(manager_.get());
			}

			bool IsSubgraphLinked(
				const RE::BSTSmallArray<RE::SubgraphHandle, 2>& a_handles,
				const RE::BSTSmallArray<RE::SubgraphIdentifier, 2>& a_identifiers) const
			{
				auto graph = GetActivePreviewGraph();
				if (!graph || a_handles.empty()) {
					return false;
				}

				auto* const swapData = *reinterpret_cast<BehaviorGraphSwapDataDiagnostic* const*>(
					reinterpret_cast<const std::byte*>(graph.get()) + 0x3A0);
				if (!swapData || !swapData->entries || swapData->size > 16) {
					return false;
				}

				const auto* const handles = GetSubgraphHandleStorage(a_handles);
				if (!handles || a_handles.size() != a_identifiers.size()) {
					return false;
				}

				for (std::uint32_t handleIndex = 0; handleIndex < a_handles.size(); ++handleIndex) {
					if (handles[handleIndex].handle == 0) {
						return false;
					}

					bool linked = false;
					for (std::uint32_t index = 0; index < swapData->size; ++index) {
						const auto& entry = swapData->entries[index];
						if (entry.handle.handle == handles[handleIndex].handle &&
							entry.sharedData && entry.useCount != 0 && entry.pendingRemove == 0) {
							linked = true;
							break;
						}
					}
					if (!linked) {
						return false;
					}
				}
				return true;
			}

			bool IsDefaultSubgraphLinked() const
			{
				return IsSubgraphLinked(defaultSubgraphHandles_, defaultSubgraphIds_);
			}

			bool IsWeaponSubgraphLinked() const
			{
				return IsSubgraphLinked(weaponSubgraphHandles_, weaponSubgraphIds_);
			}

			bool AreRequestedSubgraphsLinked() const
			{
				return IsDefaultSubgraphLinked() && (weaponSubgraphHandles_.empty() || IsWeaponSubgraphLinked());
			}

			void RefreshPendingSubgraphLoads()
			{
				if (!manager_ || AreRequestedSubgraphsLinked()) {
					return;
				}

				PrepareGraphManagerForSubgraphs();
			}

			bool RunInitialBaseStateAction(RE::BGSAction* a_action)
			{
				if (!a_action || !sourceActor_) {
					return false;
				}

				alignas(8) std::array<std::byte, 0x60> actionData{};
				RE::ActionInput::Data inputData{};
				ConstructTESActionData(
					actionData.data(),
					RE::ActionInput::ACTIONPRIORITY::kTry,
					static_cast<RE::TESObjectREFR*>(sourceActor_),
					a_action,
					nullptr,
					inputData);

				// IDA: RunActionOnActorGetFile sets this BGSActionData flag
				// before ActionInterpreter resolves the animation event name.
				*reinterpret_cast<std::uint32_t*>(actionData.data() + 0x58) = 1;
				const bool interpreted = g_interpretAction(actionData.data());
				const auto& eventName = *reinterpret_cast<const RE::BSFixedString*>(actionData.data() + 0x28);
				const bool processed = interpreted && !eventName.empty() && NotifyAnimationGraphImpl(eventName);

				DestroyTESActionData(actionData.data());
				return processed;
			}

			bool NotifyPreviewActionEvent(RE::BGSAction* a_action, const RE::ActionInput::ACTIONPRIORITY a_priority)
			{
				if (!a_action || !sourceActor_) {
					return false;
				}

				alignas(8) std::array<std::byte, 0x60> actionData{};
				RE::ActionInput::Data inputData{};
				ConstructTESActionData(
					actionData.data(),
					a_priority,
					static_cast<RE::TESObjectREFR*>(sourceActor_),
					a_action,
					nullptr,
					inputData);

				const bool interpreted = g_interpretAction(actionData.data());
				const auto& eventName = *reinterpret_cast<const RE::BSFixedString*>(actionData.data() + 0x28);
				const bool processed = interpreted && !eventName.empty() && NotifyAnimationGraphImpl(eventName);

				DestroyTESActionData(actionData.data());
				return processed;
			}

			bool UpdateGraphManagerPreservingPreviewRoot(const RE::BSAnimationUpdateData& a_updateData)
			{
				const auto previewRootLocal = targetRoot_ ? targetRoot_->GetLocalTransform() : RE::NiTransform{};
				const bool updated = g_updateAnimationGraphManager(this, a_updateData);
				if (targetRoot_) {
					targetRoot_->SetLocalTransform(previewRootLocal);
				}
				return updated;
			}

			bool UpdateGraphManagerPreservingPreviewRoot(const float a_deltaTime)
			{
				const auto previewRootLocal = targetRoot_ ? targetRoot_->GetLocalTransform() : RE::NiTransform{};
				const bool updated = g_updateAnimationGraphManagerFloat(this, a_deltaTime);
				if (targetRoot_) {
					targetRoot_->SetLocalTransform(previewRootLocal);
				}
				return updated;
			}

			[[nodiscard]] RE::BGSAction* GetDefaultAction(const RE::DEFAULT_OBJECT a_object)
			{
				auto* defaultObjectManager = RE::BGSDefaultObjectManager::GetSingleton();
				return defaultObjectManager ? defaultObjectManager->GetDefaultObject<RE::BGSAction>(a_object) : nullptr;
			}

			void TryApplyInitialAnimationState()
			{
				if (initialStateApplied_ || !manager_ || !AreRequestedSubgraphsLinked()) {
					return;
				}

				ApplyPreviewGraphVariableOverrides();

				if (!GetConfig().animation.useLiveAnimation) {
					const auto& animation = GetConfig().animation;
					const auto instantEvent = animation.sheatheWeaponDuringIdleAnimation ?
						kBaseStateStartInstantEvent :
						kRelaxedStateStartInstantEvent;
					const auto fallbackEvent = animation.sheatheWeaponDuringIdleAnimation ?
						kBaseStateStartEvent :
						kRelaxedStateStartEvent;

					bool processed = NotifyAnimationGraphImpl(RE::BSFixedString(instantEvent.data()));
					if (!processed) {
						processed = NotifyAnimationGraphImpl(RE::BSFixedString(fallbackEvent.data()));
					}

					RE::BSAnimationUpdateData updateData;
					updateData.deltaTime = 0.0001F;
					updateData.flags18 = 0xFFFF;
					updateData.flags1C = 1;
					(void)UpdateGraphManagerPreservingPreviewRoot(updateData);
					if (processed) {
						// The freshly selected base/weapon root needs one full
						// settle tick before it reliably consumes dynamic-idle
						// requests such as dyn_ActivationLoop.
						(void)UpdateGraphManagerPreservingPreviewRoot(2.0F);
					}
					ApplyPreviewGraphVariableOverrides();

					if (processed) {
						initialStateApplied_ = true;
					}
					return;
				}

				// IDA: Actor::InitializeAnimationGraphVariables seeds
				// iSyncWeaponDrawState=1. InitializeActorInstant replaces it with
				// 2 only while selecting the drawn base state, then resets it.
				SetGraphVariableInt(kWeaponDrawSyncVariable.data(), GetInitialWeaponDrawSyncState());
				SetGraphVariableFloat("fRandomClipStartTimePercentage", RE::BSRandom::Float0To1());
				bool processed = RunInitialBaseStateAction(g_getDefaultObjectForActionInstantInitializeToBaseState());
				if (!processed) {
					SetGraphVariableInt(kWeaponDrawSyncVariable.data(), 0);
					processed = RunInitialBaseStateAction(g_getDefaultObjectForActionInitializeToBaseState());
				}

				// IDA: InitializeActorInstant performs a tiny graph update after
				// the initialize action so the state machine consumes the event
				// immediately.
				RE::BSAnimationUpdateData updateData;
				updateData.deltaTime = 0.0001F;
				updateData.flags18 = 0xFFFF;
				updateData.flags1C = 1;
				(void)UpdateGraphManagerPreservingPreviewRoot(updateData);
				SetGraphVariableInt(kWeaponDrawSyncVariable.data(), 0);
				ApplyPreviewGraphVariableOverrides();

				if (processed) {
					initialStateApplied_ = true;
				}
			}

			void CaptureSubgraphDebugSnapshot(DebugSnapshot& a_snapshot) const
			{
				CaptureSubgraphHandles(
					defaultSubgraphHandles_,
					a_snapshot.defaultSubgraphHandleCount,
					a_snapshot.defaultSubgraphHandleShown,
					a_snapshot.defaultSubgraphHandles);
				CaptureSubgraphIds(
					defaultSubgraphIds_,
					a_snapshot.defaultSubgraphIdCount,
					a_snapshot.defaultSubgraphIdShown,
					a_snapshot.defaultSubgraphIds);
				CaptureSubgraphHandles(
					loadingDefaultSubgraphHandles_,
					a_snapshot.loadingDefaultSubgraphHandleCount,
					a_snapshot.loadingDefaultSubgraphHandleShown,
					a_snapshot.loadingDefaultSubgraphHandles);
				CaptureSubgraphIds(
					loadingDefaultSubgraphIds_,
					a_snapshot.loadingDefaultSubgraphIdCount,
					a_snapshot.loadingDefaultSubgraphIdShown,
					a_snapshot.loadingDefaultSubgraphIds);
				CaptureSubgraphHandles(
					weaponSubgraphHandles_,
					a_snapshot.weaponSubgraphHandleCount,
					a_snapshot.weaponSubgraphHandleShown,
					a_snapshot.weaponSubgraphHandles);
				CaptureSubgraphIds(
					weaponSubgraphIds_,
					a_snapshot.weaponSubgraphIdCount,
					a_snapshot.weaponSubgraphIdShown,
					a_snapshot.weaponSubgraphIds);
				CaptureSubgraphHandles(
					loadingWeaponSubgraphHandles_,
					a_snapshot.loadingWeaponSubgraphHandleCount,
					a_snapshot.loadingWeaponSubgraphHandleShown,
					a_snapshot.loadingWeaponSubgraphHandles);
				CaptureSubgraphIds(
					loadingWeaponSubgraphIds_,
					a_snapshot.loadingWeaponSubgraphIdCount,
					a_snapshot.loadingWeaponSubgraphIdShown,
					a_snapshot.loadingWeaponSubgraphIds);

				auto graph = GetActivePreviewGraph();
				if (!graph) {
					return;
				}

				auto* const swapData = *reinterpret_cast<BehaviorGraphSwapDataDiagnostic* const*>(
					reinterpret_cast<const std::byte*>(graph.get()) + 0x3A0);
				if (!swapData) {
					return;
				}

				a_snapshot.hasSubgraphSwapData = true;
				a_snapshot.subgraphSwapData = reinterpret_cast<std::uintptr_t>(swapData);
				a_snapshot.subgraphSwapCapacity = swapData->capacity;
				a_snapshot.subgraphSwapSlots = swapData->size;
				a_snapshot.subgraphSwapStateMachine = reinterpret_cast<std::uintptr_t>(swapData->stateMachine);
				a_snapshot.subgraphSwapBehavior = reinterpret_cast<std::uintptr_t>(swapData->behavior);

				constexpr std::uint32_t kMaxReasonableSubgraphSlots = 16;
				if (!swapData->entries || swapData->size > kMaxReasonableSubgraphSlots) {
					return;
				}

				a_snapshot.subgraphSlotShown = swapData->size;
				for (std::uint32_t index = 0; index < a_snapshot.subgraphSlotShown; ++index) {
					const auto& entry = swapData->entries[index];
					if (entry.sharedData) {
						++a_snapshot.subgraphSwapLinkedSlots;
					}
					if (entry.useCount != 0) {
						++a_snapshot.subgraphSwapRequestedSlots;
						a_snapshot.subgraphSwapUseCountTotal += entry.useCount;
					}
					if (entry.pendingRemove != 0) {
						++a_snapshot.subgraphSwapPendingRemoveSlots;
					}

					SubgraphSlotDebugInfo slot;
					slot.index = index;
					// IDA: BSBehaviorGraphSwapSingleton::OnActivateImpl/UpdateForManager
					// register swap entry index i as hkbStateMachine state i + 1.
					slot.stateId = index + 1;
					slot.handle = entry.handle.handle;
					slot.sharedData = reinterpret_cast<std::uintptr_t>(entry.sharedData);
					slot.useCount = entry.useCount;
					slot.pendingRemove = entry.pendingRemove;
					if (slot.sharedData) {
						slot.rootId = *reinterpret_cast<const std::uintptr_t*>(slot.sharedData + 0xC0);
						if (slot.rootId) {
							CopyDebugText(slot.rootName, ReadTaggedString(slot.rootId + 0x38));
						}
						const auto dataBase = slot.sharedData - 0x40;
						CaptureFixedStringArrayDebug(dataBase, 0x160, slot.files160Count, slot.files160Shown, slot.files160);
						CaptureFixedStringArrayDebug(dataBase, 0x178, slot.files178Count, slot.files178Shown, slot.files178);
					}
					a_snapshot.subgraphSlots[index] = slot;
				}
			}

			void ObserveLiveGraphRequest(
				const char* a_eventName,
				const std::uint32_t a_result,
				const bool a_idleAnimationMode)
			{
				if (!a_eventName || a_eventName[0] == '\0') {
					return;
				}

				const bool whitelisted = a_idleAnimationMode ?
					IsIdleAnimationMirrorEventWhitelisted(a_eventName) :
					IsLiveMirrorEventWhitelisted(a_eventName);
				if (!whitelisted) {
					return;
				}

				const RE::BSFixedString eventName(a_eventName);
				if (!a_idleAnimationMode && IsWeaponForceEquipEvent(eventName)) {
					// The preview schedules its own third-person force-ready event when its
					// replacement weapon subgraph commits. Mirroring the live graph's IDLE
					// event races initialization and can address the wrong behavior graph.
					return;
				}
				if (!a_idleAnimationMode && weaponEquipAfterUpdate_ && IsWeaponEquipEvent(eventName)) {
					return;
				}

				// IDA: BSAnimationGraphManager::ProcessGraphEvent returns whether
				// the active graph accepted the request. The live actor can be on
				// a first-person graph while the preview is third-person, so this
				// result is not a reliable preview eligibility test.
				(void)a_result;
				QueueMirroredEvent(GetPreviewMirroredEvent(eventName));
			}

			void ReconcileJumpLandingFromLive()
			{
				if (!GetConfig().animation.mirrorEvents.jump) {
					lastLiveSyncJumpState_ = 0;
					return;
				}

				if (!sourceHolder_) {
					return;
				}

				std::int32_t liveSyncJumpState{ 0 };
				if (!TryGetLiveGraphVariableInt("iSyncJumpState", liveSyncJumpState)) {
					return;
				}

				std::int32_t liveCurrentJumpState{ 0 };
				(void)TryGetLiveGraphVariableInt("CurrentJumpState", liveCurrentJumpState);
				if (lastLiveSyncJumpState_ != 0 && liveSyncJumpState == 0 && liveCurrentJumpState == 0) {
					QueueMirroredEvent(RE::BSFixedString("jumpLand"));
				}
				lastLiveSyncJumpState_ = liveSyncJumpState;
			}

			void QueueMirroredEvent(const RE::BSFixedString& a_eventName)
			{
				if (a_eventName.empty()) {
					return;
				}

				std::scoped_lock lock(pendingMirroredEventsLock_);
				pendingMirroredEvents_.emplace_back(a_eventName);
			}

			[[nodiscard]] RE::BSFixedString GetPreviewMirroredEvent(const RE::BSFixedString& a_eventName) const
			{
				if (a_eventName != std::string_view{ "moveStart" } &&
					a_eventName != std::string_view{ "MoveStart" }) {
					return a_eventName;
				}

				std::int32_t isSneaking = 0;
				std::int32_t syncIdleLocomotion = 0;
				if (!TryGetLiveGraphVariableInt("iIsInSneak", isSneaking) || isSneaking == 0 ||
					!TryGetLiveGraphVariableInt("iSyncIdleLocomotion", syncIdleLocomotion) ||
					syncIdleLocomotion != 1 || !IsLiveWeaponHolstered()) {
					return a_eventName;
				}

				// The live first-person graph can report MoveStart here, but the
				// third-person MT sneak-holstered path uses moveStartAnimated.
				return RE::BSFixedString("moveStartAnimated");
			}

			[[nodiscard]] PreviewWeaponBehaviorGraph GetTargetWeaponBehaviorGraph() const
			{
				const auto* process = sourceActor_ ? sourceActor_->currentProcess : nullptr;
				const auto* middleHigh = process ? process->middleHigh : nullptr;
				if (!middleHigh || weaponSubgraphIds_.empty()) {
					return PreviewWeaponBehaviorGraph::kUnknown;
				}

				PreviewWeaponBehaviorGraph result = PreviewWeaponBehaviorGraph::kUnknown;
				for (const auto& root : middleHigh->subGraphIdleManagerRoots) {
					if (root.forFirstPerson || root.idleRootName.empty() ||
						!ContainsSubgraphId(weaponSubgraphIds_, root.subGraphID)) {
						continue;
					}

					const auto behaviorLeaf = ToLowerNormalized(GetClipLeaf(root.idleRootName.c_str()));
					PreviewWeaponBehaviorGraph candidate = PreviewWeaponBehaviorGraph::kUnknown;
					if (behaviorLeaf == "meleebehavior" ||
						behaviorLeaf.ends_with("meleewrappingbehavior") ||
						behaviorLeaf.ends_with("powerfistwrappingbehavior")) {
						candidate = PreviewWeaponBehaviorGraph::kMelee;
					} else if (behaviorLeaf == "weaponbehavior" ||
						behaviorLeaf.ends_with("weaponwrappingbehavior") ||
						behaviorLeaf.ends_with("wrappingweaponbehavior")) {
						candidate = PreviewWeaponBehaviorGraph::kWeapon;
					}

					if (candidate == PreviewWeaponBehaviorGraph::kUnknown) {
						continue;
					}
					if (result != PreviewWeaponBehaviorGraph::kUnknown && result != candidate) {
						return PreviewWeaponBehaviorGraph::kUnknown;
					}
					result = candidate;
				}

				return result;
			}

			[[nodiscard]] bool IsLiveWeaponDrawingOrDrawn() const
			{
				return sourceActor_ &&
				       (sourceActor_->weaponState == RE::WEAPON_STATE::kDrawing ||
						sourceActor_->weaponState == RE::WEAPON_STATE::kDrawn);
			}

			void ScheduleWeaponEquipAfterCommit()
			{
				if (!GetConfig().animation.useLiveAnimation || !initialStateApplied_ ||
					!IsLiveWeaponDrawingOrDrawn() ||
					GetTargetWeaponBehaviorGraph() == PreviewWeaponBehaviorGraph::kUnknown) {
					return;
				}

				// Reconcile runs before the preview manager update. ProcessMirroredEvents
				// consumes this afterward, so the linked subgraph receives one initialization
				// update before the target graph's equip transition is requested.
				weaponEquipAfterUpdate_ = true;
				std::scoped_lock lock(pendingMirroredEventsLock_);
				std::erase_if(pendingMirroredEvents_, [](const auto& eventName) {
					return IsWeaponEquipEvent(eventName);
				});
			}

			[[nodiscard]] bool IsLiveWeaponHolstered() const
			{
				const auto* process = sourceActor_ ? sourceActor_->currentProcess : nullptr;
				const auto* middleHigh = process ? process->middleHigh : nullptr;
				return middleHigh && middleHigh->weaponCullCounter != 0;
			}

			void ProcessMirroredEvents()
			{
				std::vector<RE::BSFixedString> events;
				{
					std::scoped_lock lock(pendingMirroredEventsLock_);
					if (pendingMirroredEvents_.empty() && !weaponEquipAfterUpdate_) {
						return;
					}
					events.swap(pendingMirroredEvents_);
				}

				if (weaponEquipAfterUpdate_) {
					weaponEquipAfterUpdate_ = false;
					(void)NotifyAnimationGraphImpl(RE::BSFixedString("weapEquip"));
				}

				std::size_t processed = 0;
				for (; processed < events.size(); ++processed) {
					const auto& eventName = events[processed];
					// IDA: Actor::PollItemEquip holds the equip action until the new
					// weapon subgraph finishes loading. Preserve that ordering in the
					// preview instead of dispatching equip/fire events into the old graph.
					if (GetConfig().animation.useLiveAnimation &&
						(weaponSubgraphRequestPending_ ||
							(!weaponSubgraphHandles_.empty() && !IsWeaponSubgraphLinked())) &&
						IsWeaponSubgraphDependentEvent(eventName)) {
						break;
					}

					ApplyPreviewWeaponVisibilityEvent(eventName);
					(void)NotifyAnimationGraphImpl(eventName);
				}

				if (processed < events.size()) {
					std::scoped_lock lock(pendingMirroredEventsLock_);
					std::vector<RE::BSFixedString> deferred;
					deferred.reserve(events.size() - processed + pendingMirroredEvents_.size());
					deferred.insert(deferred.end(), events.begin() + processed, events.end());
					deferred.insert(
						deferred.end(),
						std::make_move_iterator(pendingMirroredEvents_.begin()),
						std::make_move_iterator(pendingMirroredEvents_.end()));
					pendingMirroredEvents_ = std::move(deferred);
				}
			}

			void RecordPreviewAnimationEvent(const RE::BSAnimationGraphEvent& a_event)
			{
				if (a_event.tag.empty()) {
					return;
				}

				if (IsIdleAnimationMirrorEventWhitelisted(a_event.tag.c_str())) {
					Previewer::HandleAnimationObjectEvent(a_event.tag, a_event.payload);
				}

				ApplyPreviewCullBoneEvent(a_event);
			}

			void ApplyPreviewCullBoneEvent(const RE::BSAnimationGraphEvent& a_event)
			{
				bool cull = false;
				if (a_event.tag == std::string_view{ "CullBone" }) {
					cull = true;
				} else if (a_event.tag == std::string_view{ "UncullBone" }) {
					cull = false;
				} else {
					return;
				}

				const auto targets = SetPreviewBoneCulled(a_event.payload, cull);
				if (targets == 0 && (a_event.payload.empty() || !targetRoot_)) {
					return;
				}

				(void)targets;
			}

			void ApplyPreviewWeaponVisibilityEvent(const RE::BSFixedString& a_eventName)
			{
				bool cull = false;
				if (a_eventName == std::string_view{ "weaponSheathe" }) {
					cull = true;
				} else if (a_eventName == std::string_view{ "weaponDraw" }) {
					cull = false;
				} else {
					return;
				}

				(void)SetPreviewBoneCulled(RE::BSFixedString("Weapon"), cull);
			}

			std::uint32_t SetPreviewBoneCulled(const RE::BSFixedString& a_boneName, const bool a_cull)
			{
				if (a_boneName.empty() || !targetRoot_) {
					return 0;
				}

				std::unordered_set<RE::NiAVObject*> targets;
				const auto addIfMatched = [&](RE::NiAVObject* a_object) {
					if (!a_object || a_object->GetName() != a_boneName) {
						return;
					}
					targets.emplace(a_object);
				};

				ForEachAVObject(targetRoot_.get(), [&](RE::NiAVObject& a_object) {
					addIfMatched(std::addressof(a_object));
				});

				if (auto* flattened = FindFlattenedBoneTree(targetRoot_.get());
					flattened && flattened->bone && flattened->boneCount > 0 && flattened->boneCount <= 1024) {
					for (std::int32_t index = 0; index < flattened->boneCount; ++index) {
						const auto& bone = flattened->bone[index];
						if (bone.name == a_boneName) {
							addIfMatched(bone.node.get());
						}
					}
				}

				for (auto* target : targets) {
					target->SetAppCulled(a_cull);
				}

				return static_cast<std::uint32_t>(targets.size());
			}

			void SyncWhitelistedGraphVariablesFromLive()
			{
				if (!sourceHolder_) {
					return;
				}

				const auto syncIntVariables = [&](const auto& a_variables) {
					for (const auto& variableName : a_variables) {
						std::int32_t value{ 0 };
						if (TryGetLiveGraphVariableInt(variableName.data(), value)) {
							SetGraphVariableInt(variableName.data(), value);
							if (variableName == std::string_view{ "iSyncWeaponDrawState" }) {
								ApplyPreviewWeaponDrawState(value);
							}
						}
					}
				};

				const auto syncBoolVariables = [&](const auto& a_variables) {
					for (const auto& variableName : a_variables) {
						bool value{ false };
						if (TryGetLiveGraphVariableBool(variableName.data(), value)) {
							SetGraphVariableBool(variableName.data(), value);
						}
					}
				};

				const auto syncFloatVariables = [&](const auto& a_variables) {
					for (const auto& variableName : a_variables) {
						float value{ 0.0F };
						if (TryGetLiveGraphVariableFloat(variableName.data(), value)) {
							SetGraphVariableFloat(variableName.data(), value);
						}
					}
				};

				syncIntVariables(kAlwaysIntGraphVariables);
				syncBoolVariables(kAlwaysBoolGraphVariables);
				syncFloatVariables(kAlwaysFloatGraphVariables);

				const auto& mirrorEvents = GetConfig().animation.mirrorEvents;
				if (mirrorEvents.locomotion) {
					syncIntVariables(kLocomotionIntGraphVariables);
					syncFloatVariables(kLocomotionFloatGraphVariables);
				}
				if (mirrorEvents.jump) {
					syncIntVariables(kJumpIntGraphVariables);
					syncBoolVariables(kJumpBoolGraphVariables);
				}
				if (mirrorEvents.sneak) {
					syncIntVariables(kSneakIntGraphVariables);
					syncBoolVariables(kSneakBoolGraphVariables);
				}
				if (mirrorEvents.weaponReload) {
					syncBoolVariables(kWeaponReloadBoolGraphVariables);
				}
			}

			void ApplyPreviewGraphVariableOverrides()
			{
				// IDA: BShkbAnimationGraph::ReceiveChannelsImpl only writes registered
				// channel values. Live dumps showed these behavior variables still
				// seeded by graph initialization, so keep them preview-owned.
				for (const auto& variableName : kPreviewNeutralBoolGraphVariables) {
					SetGraphVariableBool(variableName.data(), false);
				}
				for (const auto& variableName : kPreviewEnabledBoolGraphVariables) {
					SetGraphVariableBool(variableName.data(), true);
				}
				for (const auto& variableName : kPreviewNeutralIntGraphVariables) {
					SetGraphVariableInt(variableName.data(), 0);
				}
				for (const auto& variableName : kPreviewReadyIntGraphVariables) {
					SetGraphVariableInt(variableName.data(), 1);
				}
				for (const auto& variableName : kPreviewNeutralFloatGraphVariables) {
					SetGraphVariableFloat(variableName.data(), 0.0F);
				}

				ApplyIdlePreviewWeaponGraphState();

				if (!previewStopEventsApplied_) {
					// WeaponBehavior.xml exposes these stop events for state machines.
					// Do not send moveStop while the live graph is already in locomotion:
					// iSyncIdleLocomotion/iLocomotionSpeedState are start-state inputs
					// during graph rebuild, and moveStop would immediately undo them.
					(void)NotifyAnimationGraphImpl("TurnStop");
					std::int32_t liveLocomotionState{ 0 };
					if (!sourceHolder_ ||
						!TryGetLiveGraphVariableInt("iSyncIdleLocomotion", liveLocomotionState) ||
						liveLocomotionState != 1) {
						(void)NotifyAnimationGraphImpl("moveStop");
					}
					previewStopEventsApplied_ = true;
				}
			}

			void SyncWeaponCullStateFromLive()
			{
				const auto* process = sourceActor_ ? sourceActor_->currentProcess : nullptr;
				const auto* middleHigh = process ? process->middleHigh : nullptr;
				const bool cullWeapon = !middleHigh || middleHigh->weaponCullCounter != 0;
				(void)SetPreviewBoneCulled(RE::BSFixedString("Weapon"), cullWeapon);
			}

			void ApplyIdleWeaponCullState()
			{
				(void)SetPreviewBoneCulled(
					RE::BSFixedString("Weapon"),
					GetConfig().animation.hideWeaponDuringIdleAnimation);
			}

			void ApplyIdlePreviewWeaponGraphState()
			{
				const auto& animation = GetConfig().animation;
				if (animation.useLiveAnimation) {
					return;
				}

				// WeaponBehavior.xml: bEquipOkIsActiveMod reads bEquipOk and
				// gates the drawn rifle branch under NoHandIKRelaxedWeapon.
				const bool drawWeapon = !animation.sheatheWeaponDuringIdleAnimation;
				SetGraphVariableBool("bEquipOk", drawWeapon);

				// WeaponBehavior.xml: GunPlayBehavior syncs on
				// iSyncReadyAlertRelaxed; state 2 is RelaxedState. Keep idle
				// preview-owned so the weapon subgraph does not inherit the live
				// actor's ready/alert/relaxed state after a graph reset.
				SetGraphVariableInt(kReadyAlertRelaxedSyncVariable.data(), drawWeapon ? 2 : 0);
			}

			void ApplyPreviewWeaponDrawState(const std::int32_t a_state)
			{
				// IDA: InitializeActorInstant sets iSyncWeaponDrawState=2 while
				// selecting the drawn base state, then resets it to 0 after the
				// tiny update. Do not treat the reset value as a sheathe signal.
				if (a_state != 2) {
					return;
				}

				(void)SetPreviewBoneCulled(RE::BSFixedString("Weapon"), false);
			}

			float GetActiveClipSynchronizedDeltaTime(const float a_deltaTime)
			{
				RE::BGSAnimationSystemUtils::ActiveSyncInfo liveInfo;
				RE::BGSAnimationSystemUtils::ActiveSyncInfo previewInfo;
				const bool liveActive = TryGetActiveSyncInfo(sourceHolder_, liveInfo);
				const bool previewActive = TryGetActiveSyncInfo(this, previewInfo);
				if (!liveActive || !previewActive) {
					return a_deltaTime;
				}

				SyncPointType syncPointType;
				float liveTimeUntilSyncPoint = 0.0F;
				if (!TryGetNextLiveSyncPointTarget(liveInfo, syncPointType, liveTimeUntilSyncPoint)) {
					return a_deltaTime;
				}

				float speedAdjust = 0.0F;
				(void)g_calculateSpeedAdjustToSyncAnimationCycles(
					1.0F,
					a_deltaTime,
					previewInfo,
					liveTimeUntilSyncPoint,
					syncPointType,
					speedAdjust);
				if (!std::isfinite(speedAdjust) || speedAdjust == 0.0F) {
					return a_deltaTime;
				}

				const auto adjustedDeltaTime = a_deltaTime * (1.0F + speedAdjust);
				if (adjustedDeltaTime <= 0.0F || !std::isfinite(adjustedDeltaTime)) {
					return a_deltaTime;
				}

				return adjustedDeltaTime;
			}

			void RequestIdleAnimation(RE::PlayerCharacter& a_player, const float a_deltaTime)
			{
				auto* idle = ResolveConfiguredDynamicActivationIdle();
				if (!idle || !manager_ || !initialStateApplied_) {
					return;
				}

				auto key = DynamicActivationIdleKey(*idle);
				if (key.empty()) {
					return;
				}
				if (g_idlePlaybackKey != key) {
					g_idlePlaybackKey = key;
					g_idlePlaybackTime = 0.0F;
					g_idleClipDuration = 0.0F;
				}

				auto graph = GetActivePreviewGraph();
				if (!graph) {
					return;
				}

				if (idleRequestSubmitted_ && (idleRequestedKey_ != key || idleRequestedGraph_ != graph.get())) {
					ClearIdleRequestState();
				}

				if (idleRequestSubmitted_ && idleRequestedKey_ == key && idleRequestedGraph_ == graph.get()) {
					if (IsRequestedIdleClipActive(*graph)) {
						idleClipObserved_ = true;
						idleRequestRetrySeconds_ = 0.0F;
						idleRequestRetryFrames_ = 0;
						return;
					}

					AdvanceIdleRequestRetry(a_deltaTime);
					if (!ShouldRetryIdleRequest()) {
						return;
					}

					ClearIdleRequestState();
				}

				const auto resolvedPath = ResolveDynamicIdleFullPath(a_player, *idle);
				if (resolvedPath.empty()) {
					LogDiagnostic("idle request skipped: dynamic idle path resolution failed for '" + key + "'");
					return;
				}

				const RE::BSFixedString eventName("dyn_ActivationLoop");
				const RE::BSFixedString path(resolvedPath.c_str());
				if (!RequestIdles(eventName, graph, path, manager_)) {
					LogDiagnostic("idle request failed for '" + resolvedPath + "'");
					return;
				}

				idleRequestedKey_ = std::move(key);
				idleResolvedPath_ = resolvedPath;
				idleRequestedGraph_ = graph.get();
				idleRequestSubmitted_ = true;
				idleSeekPending_ = true;
				idleClipObserved_ = false;
				idleRequestRetrySeconds_ = 0.0F;
				idleRequestRetryFrames_ = 0;
			}

			void ClearIdleRequestState()
			{
				idleRequestedKey_.clear();
				idleResolvedPath_.clear();
				idleRequestedGraph_ = nullptr;
				idleRequestSubmitted_ = false;
				idleSeekPending_ = false;
				idleClipObserved_ = false;
				idleRequestRetrySeconds_ = 0.0F;
				idleRequestRetryFrames_ = 0;
			}

			[[nodiscard]] bool IsRequestedIdleClipActive(RE::BShkbAnimationGraph& a_graph) const
			{
				return !idleResolvedPath_.empty() && FindActiveClipTiming(a_graph, idleResolvedPath_).clip != 0;
			}

			void AdvanceIdleRequestRetry(const float a_deltaTime)
			{
				++idleRequestRetryFrames_;
				if (std::isfinite(a_deltaTime) && a_deltaTime > 0.0F) {
					idleRequestRetrySeconds_ += a_deltaTime;
				}
			}

			[[nodiscard]] bool ShouldRetryIdleRequest() const
			{
				return idleRequestRetryFrames_ >= kIdleRequestRetryFrames ||
				       idleRequestRetrySeconds_ >= kIdleRequestRetrySeconds;
			}

			void UpdateIdlePlaybackClock(const float a_deltaTime)
			{
				auto graph = GetActivePreviewGraph();
				if (!graph || idleResolvedPath_.empty()) {
					return;
				}

				const auto timing = FindActiveClipTiming(*graph, idleResolvedPath_);
				if (!timing.clip) {
					return;
				}

				if (timing.duration > 0.0F && std::isfinite(timing.duration)) {
					g_idleClipDuration = timing.duration;
					g_idlePlaybackTime = NormalizeClipTime(g_idlePlaybackTime, g_idleClipDuration);
				}

				if (idleSeekPending_) {
					OverwriteClipLocalTime(timing.clip, NormalizeClipTime(g_idlePlaybackTime, g_idleClipDuration));
					idleSeekPending_ = false;
					idleClipObserved_ = true;
					idleRequestRetrySeconds_ = 0.0F;
					idleRequestRetryFrames_ = 0;
					return;
				}

				if (idleClipObserved_ && std::isfinite(a_deltaTime) && a_deltaTime > 0.0F) {
					g_idlePlaybackTime = NormalizeClipTime(g_idlePlaybackTime + a_deltaTime, g_idleClipDuration);
				} else {
					idleClipObserved_ = true;
				}
				idleRequestRetrySeconds_ = 0.0F;
				idleRequestRetryFrames_ = 0;
			}

			[[nodiscard]] DebugSnapshot CaptureDebugSnapshot() const
			{
				DebugSnapshot snapshot;
				snapshot.speedChannel = speedDebug_;
				if (!manager_) {
					return snapshot;
				}

				snapshot.hasManager = true;
				snapshot.manager = reinterpret_cast<std::uintptr_t>(manager_.get());
				snapshot.activeGraphIndex = manager_->activeGraph;
				snapshot.graphCount = manager_->graph.size();
				CaptureSubgraphDebugSnapshot(snapshot);

				RE::BGSAnimationSystemUtils::ActiveSyncInfo liveSyncInfo;
				const bool liveSyncActive = TryGetActiveSyncInfo(sourceHolder_, liveSyncInfo);
				FillActiveSyncDebugInfo(snapshot.liveSync, liveSyncActive, liveSyncInfo);
				RE::BGSAnimationSystemUtils::ActiveSyncInfo previewSyncInfo;
				const bool previewSyncActive = TryGetActiveSyncInfo(this, previewSyncInfo);
				FillActiveSyncDebugInfo(snapshot.previewSync, previewSyncActive, previewSyncInfo);

				auto graph = GetActivePreviewGraph();
				if (!graph) {
					return snapshot;
				}

				snapshot.hasGraph = true;
				snapshot.graph = reinterpret_cast<std::uintptr_t>(graph.get());
				RE::BSFixedString speedVariable("Speed");
				snapshot.speedChannel.previewGraphSpeedHas =
					g_getBShkbGraphVariableFloat(graph.get(), speedVariable, snapshot.speedChannel.previewGraphSpeed);
				const auto* const graphBase = reinterpret_cast<const std::byte*>(graph.get());
				snapshot.generateHavokBones = *reinterpret_cast<const std::uint8_t*>(graphBase + 0x3C6) != 0;
				snapshot.hasRagdollInterface = *reinterpret_cast<void* const*>(graphBase + 0x210) != nullptr;
				snapshot.hasPhysicsWorld = *reinterpret_cast<void* const*>(graphBase + 0x3B8) != nullptr;

				auto* behavior = *reinterpret_cast<HkbBehaviorGraphDiagnostic* const*>(graphBase + 0x378);
				if (!behavior) {
					return snapshot;
				}

				snapshot.hasBehaviorGraph = true;
				snapshot.behaviorGraph = reinterpret_cast<std::uintptr_t>(behavior);
				snapshot.behaviorActive = behavior->isActive;
				snapshot.behaviorLinked = behavior->isLinked;
				snapshot.updateActiveNodes = behavior->updateActiveNodes;
				snapshot.stateOrTransitionChanged = behavior->stateOrTransitionChanged;

				auto* activeNodes = behavior->activeNodes;
				if (!activeNodes || !activeNodes->data || activeNodes->size <= 0 || activeNodes->size > 1024) {
					return snapshot;
				}

				snapshot.activeNodeCount = static_cast<std::uint32_t>(activeNodes->size);
				snapshot.activeNodesReady = !behavior->updateActiveNodes && !behavior->stateOrTransitionChanged;
				snapshot.activeNodes.reserve((std::min)(snapshot.activeNodeCount, 128u));
				for (std::int32_t index = 0; index < activeNodes->size && index < 128; ++index) {
					const auto entry = reinterpret_cast<std::uintptr_t>(activeNodes->data[index]);
					if (!entry) {
						continue;
					}

					ActiveNodeDebugInfo nodeInfo;
					nodeInfo.entry = entry;
					const bool entryIsClip = IsClipGenerator(entry);
					nodeInfo.node = ReadActiveNodePointer(entry);
					nodeInfo.nestedBehaviorGraph = entryIsClip ? 0 : ReadNestedBehaviorGraph(entry);
					nodeInfo.behaviorGraph = nodeInfo.nestedBehaviorGraph ? nodeInfo.nestedBehaviorGraph : snapshot.behaviorGraph;
					nodeInfo.behaviorRootId = ReadCurrentBehaviorRootId(nodeInfo.behaviorGraph);
					for (std::uint32_t slotIndex = 0; slotIndex < snapshot.subgraphSlotShown; ++slotIndex) {
						const auto& slot = snapshot.subgraphSlots[slotIndex];
						if (slot.rootId != 0 && slot.rootId == nodeInfo.behaviorRootId) {
							nodeInfo.inSubgraph = true;
							nodeInfo.subgraphSlot = slot.index;
							break;
						}
					}
					if (nodeInfo.node) {
						nodeInfo.nodeName = SafeString(ReadTaggedString(nodeInfo.node + 0x38));
					}

					nodeInfo.clip = SelectActiveClip(entry);
					nodeInfo.isClip = nodeInfo.clip != 0;
					if (nodeInfo.clip) {
						nodeInfo.clipName = SafeString(ReadTaggedString(nodeInfo.clip + 0x38));
						nodeInfo.authoredClipPath = SafeString(ReadTaggedString(nodeInfo.clip + 0x90));
						nodeInfo.clipId = *reinterpret_cast<const std::uint32_t*>(nodeInfo.clip + 0x40);
						nodeInfo.userControlledTimeFraction = *reinterpret_cast<const float*>(nodeInfo.clip + 0xB8);
						nodeInfo.playbackMode = *reinterpret_cast<const std::uint8_t*>(nodeInfo.clip + 0xBE);
						const auto graphForClip = ResolveOwningGraphFromBehaviorGraph(nodeInfo.behaviorGraph, snapshot.graph);
						nodeInfo.resolvedClipPath =
							ResolveClipDisplayPath(graphForClip, nodeInfo.behaviorGraph, nodeInfo.authoredClipPath.c_str());

						const auto animCtrl = *reinterpret_cast<const std::uintptr_t*>(nodeInfo.clip + 0xD0);
						if (animCtrl) {
							const auto animBinding = *reinterpret_cast<const std::uintptr_t*>(animCtrl + 0x38);
							const auto anim = animBinding ? *reinterpret_cast<const std::uintptr_t*>(animBinding + 0x18) : 0;
							if (anim) {
								nodeInfo.currentTime = *reinterpret_cast<const float*>(nodeInfo.clip + 0x140);
								nodeInfo.controlLocalTime = *reinterpret_cast<const float*>(animCtrl + 0x10);
								nodeInfo.duration = *reinterpret_cast<const float*>(anim + 0x14);
								nodeInfo.hasTiming = true;
								nodeInfo.hasControlLocalTime = true;
							}
						}
					}

					snapshot.activeNodes.push_back(std::move(nodeInfo));
				}

				return snapshot;
			}

			[[nodiscard]] bool HasManager() const { return static_cast<bool>(manager_); }
			[[nodiscard]] const RE::BSTSmartPointer<RE::BSAnimationGraphManager>& Manager() const { return manager_; }
			[[nodiscard]] RE::NiAVObject* TargetRoot() const { return targetRoot_.get(); }
			[[nodiscard]] RE::NiAVObject* TargetGraphRoot() const { return targetGraphRoot_; }
			[[nodiscard]] RE::PlayerCharacter* SourceActor() const { return sourceActor_; }
			[[nodiscard]] bool IsCurrentSourceManager() const
			{
				return sourceManagerIdentity_ && GetLiveSourceManager() == sourceManagerIdentity_;
			}
			[[nodiscard]] bool IsLiveSourceManager(const RE::BSAnimationGraphManager* a_manager) const
			{
				return a_manager && GetLiveSourceManager() == a_manager;
			}
			void ResetInitialState()
			{
				initialStateApplied_ = false;
				previewStopEventsApplied_ = false;
				lastLiveSyncJumpState_ = 0;
			}

		private:
			RE::PlayerCharacter* sourceActor_{ nullptr };
			RE::IAnimationGraphManagerHolder* sourceHolder_{ nullptr };
			RE::NiPointer<RE::NiAVObject> targetRoot_;
			RE::NiAVObject* targetGraphRoot_{ nullptr };
			RE::BSAnimationGraphManager* sourceManagerIdentity_{ nullptr };
			RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager_;
			PreviewAnimationEventSink previewAnimationEventSink_;
			RE::BSTSmartPointer<RE::BShkbAnimationGraph> previewEventGraph_;
			RE::BSTEventSource<RE::BSAnimationGraphEvent>* previewEventSource_{ nullptr };
			std::mutex pendingMirroredEventsLock_;
			std::vector<RE::BSFixedString> pendingMirroredEvents_;
			RE::BSTSmallArray<RE::SubgraphHandle, 2> defaultSubgraphHandles_;
			RE::BSTSmallArray<RE::SubgraphIdentifier, 2> defaultSubgraphIds_;
			RE::BSTSmallArray<RE::SubgraphHandle, 2> loadingDefaultSubgraphHandles_;
			RE::BSTSmallArray<RE::SubgraphIdentifier, 2> loadingDefaultSubgraphIds_;
			RE::BSTSmallArray<RE::SubgraphHandle, 2> weaponSubgraphHandles_;
			RE::BSTSmallArray<RE::SubgraphIdentifier, 2> weaponSubgraphIds_;
			RE::BSTSmallArray<RE::SubgraphHandle, 2> loadingWeaponSubgraphHandles_;
			RE::BSTSmallArray<RE::SubgraphIdentifier, 2> loadingWeaponSubgraphIds_;
			bool weaponSubgraphRequestPending_{ false };
			bool weaponEquipAfterUpdate_{ false };
			SpeedChannelDebugInfo speedDebug_;
			std::int32_t lastLiveSyncJumpState_{ 0 };
			bool initialStateApplied_{ false };
			bool previewStopEventsApplied_{ false };
			std::string idleRequestedKey_;
			std::string idleResolvedPath_;
			RE::BShkbAnimationGraph* idleRequestedGraph_{ nullptr };
			bool idleRequestSubmitted_{ false };
			bool idleSeekPending_{ false };
			bool idleClipObserved_{ false };
			float idleRequestRetrySeconds_{ 0.0F };
			std::uint32_t idleRequestRetryFrames_{ 0 };

			[[nodiscard]] std::int32_t GetInitialWeaponDrawSyncState() const
			{
				const auto& animation = GetConfig().animation;
				return !animation.useLiveAnimation && animation.sheatheWeaponDuringIdleAnimation ? 0 : 2;
			}

			[[nodiscard]] bool TryGetIdlePreviewIntGraphVariable(
				const RE::BSFixedString& a_variable,
				std::int32_t& a_out) const
			{
				const auto& animation = GetConfig().animation;
				if (animation.useLiveAnimation) {
					return false;
				}

				if (a_variable == kWeaponDrawSyncVariable) {
					a_out = initialStateApplied_ ? 0 : GetInitialWeaponDrawSyncState();
					return true;
				}
				if (a_variable == kReadyAlertRelaxedSyncVariable) {
					a_out = animation.sheatheWeaponDuringIdleAnimation ? 0 : 2;
					return true;
				}

				return false;
			}

			[[nodiscard]] RE::BSTSmartPointer<RE::BShkbAnimationGraph> GetActivePreviewGraph() const
			{
				if (!manager_ || manager_->graph.empty()) {
					return {};
				}

				const auto activeGraph = manager_->activeGraph;
				if (activeGraph >= manager_->graph.size()) {
					return {};
				}

				return manager_->graph[activeGraph];
			}

			[[nodiscard]] RE::BSTSmartPointer<RE::BSAnimationGraphManager> GetLiveSourceManagerSmart() const
			{
				RE::BSTSmartPointer<RE::BSAnimationGraphManager> liveManager;
				if (sourceHolder_) {
					(void)sourceHolder_->GetAnimationGraphManagerImpl(liveManager);
				}
				return liveManager;
			}

			template <class Getter, class Value>
			[[nodiscard]] bool TryGetLiveGraphVariable(
				const RE::BSFixedString& a_variable,
				Value& a_out,
				Getter a_getter,
				std::uint32_t* a_graphIndex = nullptr) const
			{
				auto liveManager = GetLiveSourceManagerSmart();
				if (!liveManager || liveManager->graph.empty()) {
					return false;
				}

				const auto tryGraph = [&](const std::uint32_t a_index) {
					if (a_index >= liveManager->graph.size()) {
						return false;
					}

					const auto& graph = liveManager->graph[a_index];
					if (!graph || !a_getter(graph.get(), a_variable, a_out)) {
						return false;
					}

					if (a_graphIndex) {
						*a_graphIndex = a_index;
					}
					return true;
				};

				if (tryGraph(liveManager->activeGraph)) {
					return true;
				}

				for (std::uint32_t index = 0; index < liveManager->graph.size(); ++index) {
					if (index != liveManager->activeGraph && tryGraph(index)) {
						return true;
					}
				}

				return false;
			}

			[[nodiscard]] bool TryGetLiveGraphVariableInt(
				const RE::BSFixedString& a_variable,
				std::int32_t& a_out,
				std::uint32_t* a_graphIndex = nullptr) const
			{
				if (TryGetLiveGraphVariable(a_variable, a_out, g_getBShkbGraphVariableInt.get(), a_graphIndex)) {
					return true;
				}
				if (sourceHolder_ && sourceHolder_->GetGraphVariableImplInt(a_variable, a_out)) {
					if (a_graphIndex) {
						*a_graphIndex = std::numeric_limits<std::uint32_t>::max();
					}
					return true;
				}
				return false;
			}

			[[nodiscard]] bool TryGetLiveGraphVariableBool(
				const RE::BSFixedString& a_variable,
				bool& a_out,
				std::uint32_t* a_graphIndex = nullptr) const
			{
				if (TryGetLiveGraphVariable(a_variable, a_out, g_getBShkbGraphVariableBool.get(), a_graphIndex)) {
					return true;
				}
				if (sourceHolder_ && sourceHolder_->GetGraphVariableImplBool(a_variable, a_out)) {
					if (a_graphIndex) {
						*a_graphIndex = std::numeric_limits<std::uint32_t>::max();
					}
					return true;
				}
				return false;
			}

			[[nodiscard]] bool TryGetLiveGraphVariableFloat(
				const RE::BSFixedString& a_variable,
				float& a_out,
				std::uint32_t* a_graphIndex = nullptr) const
			{
				if (TryGetLiveGraphVariable(a_variable, a_out, g_getBShkbGraphVariableFloat.get(), a_graphIndex)) {
					return true;
				}
				if (sourceHolder_ && sourceHolder_->GetGraphVariableImplFloat(a_variable, a_out)) {
					if (a_graphIndex) {
						*a_graphIndex = std::numeric_limits<std::uint32_t>::max();
					}
					return true;
				}
				return false;
			}

			[[nodiscard]] RE::BSAnimationGraphManager* GetLiveSourceManager() const
			{
				RE::BSTSmartPointer<RE::BSAnimationGraphManager> liveManager;
				if (!sourceHolder_ || !sourceHolder_->GetAnimationGraphManagerImpl(liveManager)) {
					return nullptr;
				}

				return liveManager.get();
			}

			void RegisterPreviewEventSource(const RE::BSTSmartPointer<RE::BShkbAnimationGraph>& a_graph)
			{
				UnregisterPreviewEventSource();
				auto* eventSource = GetGraphEventSource(a_graph.get());
				if (!eventSource) {
					return;
				}

				eventSource->RegisterSink(std::addressof(previewAnimationEventSink_));
				previewEventGraph_ = a_graph;
				previewEventSource_ = eventSource;
			}

			void UnregisterPreviewEventSource()
			{
				if (!previewEventSource_) {
					return;
				}

				previewEventSource_->UnregisterSink(std::addressof(previewAnimationEventSink_));
				previewEventSource_ = nullptr;
				previewEventGraph_.reset();
			}
		};

		std::unique_ptr<PreviewAnimationGraphHolder> g_holder;
		std::string g_project;
		std::string g_lastDiagnostic;
		RE::TESRace* g_graphRace{ nullptr };
		std::uint32_t g_graphSex{ 0 };
		bool g_liveAnimationMode{ false };
		bool g_graphTargetRefreshRequested{ false };
		std::recursive_mutex g_stateLock;
		DynamicActivationIdleMap g_dynamicActivationIdles;
		std::mutex g_pendingLiveGraphRequestsLock;
		std::vector<LiveGraphRequest> g_pendingLiveGraphRequests;

		void LogDiagnostic(std::string a_message)
		{
			if (a_message == g_lastDiagnostic) {
				return;
			}

			g_lastDiagnostic = std::move(a_message);
			REX::INFO("Animations: {}", g_lastDiagnostic);
		}

		[[nodiscard]] std::string GetActorProject(RE::PlayerCharacter& a_player)
		{
			auto* sourceRoot = a_player.Get3D(false);
			if (!sourceRoot) {
				sourceRoot = a_player.Get3D();
			}

			const auto* project = g_getProjectForActor(std::addressof(a_player), sourceRoot);
			return project && project[0] != '\0' ? std::string(project) : std::string{};
		}

		[[nodiscard]] bool HasLiveAnimationGraphManager(RE::PlayerCharacter& a_player)
		{
			RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
			return GetAnimationGraphHolder(a_player).GetAnimationGraphManagerImpl(manager) &&
				static_cast<bool>(manager);
		}

		void Clear(const bool a_preserveIdlePlayback = false)
		{
			g_holder.reset();
			g_project.clear();
			g_graphRace = nullptr;
			g_graphSex = 0;
			g_liveAnimationMode = false;
			g_graphTargetRefreshRequested = false;
			if (!a_preserveIdlePlayback) {
				g_idlePlaybackKey.clear();
				g_idlePlaybackTime = 0.0F;
				g_idleClipDuration = 0.0F;
			}

			std::scoped_lock lock(g_pendingLiveGraphRequestsLock);
			g_pendingLiveGraphRequests.clear();
		}

		void DrainPendingLiveGraphRequests(const bool a_idleAnimationMode)
		{
			std::vector<LiveGraphRequest> requests;
			{
				std::scoped_lock lock(g_pendingLiveGraphRequestsLock);
				if (g_pendingLiveGraphRequests.empty()) {
					return;
				}
				requests.swap(g_pendingLiveGraphRequests);
			}

			if (!g_holder) {
				return;
			}

			for (const auto& request : requests) {
				if (g_holder->IsLiveSourceManager(request.manager)) {
					g_holder->ObserveLiveGraphRequest(
						request.eventName.c_str(),
						request.result,
						a_idleAnimationMode);
				}
			}
		}

		template <std::size_t N>
		[[nodiscard]] bool FixedStringMatchesAny(
			const RE::BSFixedString& a_value,
			const std::array<std::string_view, N>& a_candidates)
		{
			if (a_value.empty()) {
				return false;
			}

			const std::string_view value{ a_value.c_str() };
			return std::any_of(a_candidates.begin(), a_candidates.end(), [&](const std::string_view candidate) {
				return value == candidate;
			});
		}

		struct DynamicActivationIdleMatch
		{
			bool graph{ false };
			bool event{ false };
			bool matched{ false };
		};

		[[nodiscard]] DynamicActivationIdleMatch EvaluateDynamicActivationIdleMatch(const RE::TESIdleForm& a_idle)
		{
			DynamicActivationIdleMatch result;
			result.graph = FixedStringMatchesAny(a_idle.behaviorGraphName, kDynamicActivationBehaviorGraphs);
			result.event = FixedStringMatchesAny(a_idle.animEventName, kDynamicActivationEvents);
			result.matched = result.graph && result.event;
			return result;
		}

		[[nodiscard]] std::string DynamicActivationIdleKey(RE::TESIdleForm& a_idle)
		{
			auto* file = a_idle.GetFile(0);
			if (!file || file->filename[0] == '\0') {
				return {};
			}

			std::array<char, 16> localID{};
			std::snprintf(localID.data(), localID.size(), "0x%X", a_idle.GetLocalFormID());
			return std::string{ file->filename } + "|" + localID.data();
		}

		[[nodiscard]] RE::TESIdleForm* ResolveConfiguredDynamicActivationIdle()
		{
			if (g_dynamicActivationIdles.empty()) {
				return nullptr;
			}

			const auto& key = GetConfig().animation.dynamicActivationIdle;
			if (!key.empty()) {
				for (const auto& [filename, idles] : g_dynamicActivationIdles) {
					for (auto* idle : idles) {
						if (idle && DynamicActivationIdleKey(*idle) == key) {
							return idle;
						}
					}
				}
			}

			for (const auto& [filename, idles] : g_dynamicActivationIdles) {
				if (!idles.empty()) {
					return idles.front();
				}
			}

			return nullptr;
		}

		[[nodiscard]] RE::TESIdleForm* ResolvePlayableIdle(RE::PlayerCharacter& a_player, RE::TESIdleForm& a_idle)
		{
			alignas(8) std::array<std::byte, 0x60> actionData{};
			RE::ActionInput::Data inputData{};
			ConstructTESActionData(
				actionData.data(),
				RE::ActionInput::ACTIONPRIORITY::kTry,
				static_cast<RE::TESObjectREFR*>(std::addressof(a_player)),
				nullptr,
				nullptr,
				inputData);

			*reinterpret_cast<RE::BSFixedString*>(actionData.data() + 0x28) = a_idle.animEventName;
			*reinterpret_cast<RE::TESIdleForm**>(actionData.data() + 0x48) = std::addressof(a_idle);
			// IDA: RunActionOnActorGetFile sets this before TESActionData resolves the idle.
			*reinterpret_cast<std::uint32_t*>(actionData.data() + 0x58) = 1;

			using resolve_t = bool(void*);
			const auto vtable = *reinterpret_cast<std::uintptr_t*>(actionData.data());
			const auto resolve = reinterpret_cast<resolve_t*>(*reinterpret_cast<std::uintptr_t*>(vtable + 0x28));
			const bool resolved = resolve && resolve(actionData.data());
			auto* playableIdle = resolved ? *reinterpret_cast<RE::TESIdleForm**>(actionData.data() + 0x48) : nullptr;

			DestroyTESActionData(actionData.data());
			return playableIdle ? playableIdle : std::addressof(a_idle);
		}

		[[nodiscard]] std::string ResolveDynamicIdleFullPath(RE::PlayerCharacter& a_player, RE::TESIdleForm& a_idle)
		{
			auto* playableIdle = ResolvePlayableIdle(a_player, a_idle);
			if (!playableIdle || playableIdle->animFileName.empty()) {
				return {};
			}

			RE::BSFixedString sourcePath(playableIdle->animFileName.c_str());
			RE::BSStaticStringT<260> resolvedPath;
			(void)resolvedPath.Set(playableIdle->animFileName.c_str(), 0);
			RE::BSScrapArray<RE::BSFixedString> wildcardPaths;

			const auto* archetype = g_getKeywordForType(a_player, RE::KeywordType::kAnimArchetype);
			const auto* flavor = g_getKeywordForType(a_player, RE::KeywordType::kAnimFlavor);
			(void)g_getDynamicIdleFullFilePath(
				sourcePath,
				a_player,
				resolvedPath,
				archetype,
				flavor,
				false,
				wildcardPaths,
				-1,
				false,
				nullptr,
				false);

			auto result = NormalizeSeparators(SafeString(resolvedPath.c_str()));
			if (result.empty()) {
				result = NormalizeSeparators(playableIdle->animFileName.c_str());
			}
			return result;
		}

		[[nodiscard]] bool EnsureGraph(RE::PlayerCharacter& a_player, RE::NiAVObject& a_previewRoot)
		{
			const auto project = GetActorProject(a_player);
			if (project.empty()) {
				Clear();
				LogDiagnostic("skipped: actor behavior project is empty");
				return false;
			}

			if (!HasLiveAnimationGraphManager(a_player)) {
				Clear();
				LogDiagnostic("skipped: live player animation graph manager is unavailable");
				return false;
			}

			auto* race = a_player.GetVisualsRace();
			if (!race) {
				race = a_player.race;
			}
			const auto sex = static_cast<std::uint32_t>(g_getAnimationSex(std::addressof(a_player)));
			const bool liveAnimationMode = GetConfig().animation.useLiveAnimation;

			if (g_holder &&
				g_holder->SourceActor() == std::addressof(a_player) &&
				g_holder->TargetRoot() == std::addressof(a_previewRoot) &&
				g_project == project &&
				g_graphRace == race &&
				g_graphSex == sex &&
				g_liveAnimationMode == liveAnimationMode &&
				g_holder->IsCurrentSourceManager() &&
				g_holder->HasManager()) {
				return true;
			}

			Clear();
			auto holder = std::make_unique<PreviewAnimationGraphHolder>(a_player, a_previewRoot);
			if (!g_createAnimationGraphManager(holder.get(), project.c_str()) || !holder->HasManager()) {
				LogDiagnostic("manager creation failed for project '" + project + "'");
				return false;
			}
			if (!holder->TargetAnimationGraph()) {
				LogDiagnostic("manager discarded: SetAnimationGraphTargets failed for project '" + project + "'");
				return false;
			}

			const auto targetStats = InspectGraphTargets(*holder, a_previewRoot);
			if (targetStats.refs == 0) {
				LogDiagnostic("manager discarded: graph target refs are empty for project '" + project + "'");
				return false;
			}
			holder->PrepareGraphManagerForSubgraphs();
			(void)holder->ReconcileRequestedSubgraphs();
			holder->PrepareGraphManagerForSubgraphs();
			if (!holder->ActivatePreviewGraphManager()) {
				LogDiagnostic("manager discarded: Activate failed for project '" + project + "'");
				return false;
			}
			g_project = project;
			g_graphRace = race;
			g_graphSex = sex;
			g_liveAnimationMode = liveAnimationMode;
			g_holder = std::move(holder);
			return true;
		}
	}

	void Reset()
	{
		std::scoped_lock lock(g_stateLock);
		Clear();
		g_lastDiagnostic.clear();
	}

	void ResetGraph()
	{
		std::scoped_lock lock(g_stateLock);
		Clear();
	}

	void ResetGraphPreservingIdlePlayback()
	{
		std::scoped_lock lock(g_stateLock);
		Clear(true);
	}

	void ResetInitialState()
	{
		std::scoped_lock lock(g_stateLock);
		if (g_holder) {
			g_holder->ResetInitialState();
		}
	}

	void RequestGraphTargetRefresh()
	{
		std::scoped_lock lock(g_stateLock);
		g_graphTargetRefreshRequested = true;
	}

	void ObserveLoadedIdle(RE::TESIdleForm* a_idle)
	{
		if (!a_idle) {
			return;
		}

		std::scoped_lock lock(g_stateLock);
		const auto match = EvaluateDynamicActivationIdleMatch(*a_idle);
		if (!match.matched) {
			return;
		}

		auto* file = a_idle->GetFile(0);
		if (!file || file->filename[0] == '\0') {
			return;
		}

		auto& idles = g_dynamicActivationIdles[file->filename];
		const auto found = std::find(idles.begin(), idles.end(), a_idle);
		if (found == idles.end()) {
			idles.push_back(a_idle);
		}
	}

	const DynamicActivationIdleMap& GetDynamicActivationIdles()
	{
		return g_dynamicActivationIdles;
	}

	void StopIdleAnimation()
	{
		std::scoped_lock lock(g_stateLock);
		Clear();
	}

	void Update(RE::PlayerCharacter& a_player, RE::NiAVObject& a_previewRoot, const float a_deltaTime)
	{
		std::scoped_lock lock(g_stateLock);
		const auto& animation = GetConfig().animation;

		if (!EnsureGraph(a_player, a_previewRoot) || !g_holder) {
			return;
		}

		const bool subgraphsReconciled = g_holder->ReconcileRequestedSubgraphs();
		g_holder->RefreshPendingSubgraphLoads();
		if (g_graphTargetRefreshRequested && subgraphsReconciled) {
			// IDA OG: BSAnimationGraphManager::SetTargets accepts the existing target.
			// BShkbAnimationGraph::SetTargetImpl still clears and recreates its bone
			// mapping when the final argument is true, without replacing the manager
			// or reactivating the behavior graph. Do this only after the requested
			// subgraphs have installed their shared data and animation skeletons.
			if (g_holder->TargetAnimationGraph()) {
				g_graphTargetRefreshRequested = false;
			} else {
				LogDiagnostic("graph target refresh failed; retrying after equipment sync");
			}
		}
		g_holder->TryApplyInitialAnimationState();

		const auto previewRootLocal = a_previewRoot.GetLocalTransform();
		float updateDelta = a_deltaTime;
		if (animation.useLiveAnimation) {
			DrainPendingLiveGraphRequests(false);
			g_holder->SyncWhitelistedGraphVariablesFromLive();
			g_holder->ReconcileJumpLandingFromLive();
			g_holder->SyncWeaponCullStateFromLive();
			g_holder->SetGraphVariableBool("bAimEnabled", false);
			updateDelta = g_holder->GetActiveClipSynchronizedDeltaTime(a_deltaTime);
		} else {
			DrainPendingLiveGraphRequests(true);
			g_holder->ApplyIdlePreviewWeaponGraphState();
			g_holder->RequestIdleAnimation(a_player, updateDelta);
		}

		const bool updated = g_updateAnimationGraphManagerFloat(g_holder.get(), updateDelta);
		a_previewRoot.SetLocalTransform(previewRootLocal);

		if (animation.useLiveAnimation) {
			g_holder->ProcessMirroredEvents();
		} else {
			g_holder->ProcessMirroredEvents();
			g_holder->ApplyIdleWeaponCullState();
			g_holder->UpdateIdlePlaybackClock(updateDelta);
		}

		RE::NiUpdateData niUpdateData;
		a_previewRoot.Update(niUpdateData);

		if (!updated) {
			LogDiagnostic("update returned false");
		}
	}

	void ObserveGraphRequest(RE::BSAnimationGraphManager* a_manager, const char* a_eventName, const std::uint32_t a_result)
	{
		if (!a_manager || !a_eventName || a_eventName[0] == '\0') {
			return;
		}

		// This hook runs while vanilla graph event dispatch owns its event-source lock.
		// Copy the request and let the preview update thread apply filtering/mirroring.
		std::scoped_lock lock(g_pendingLiveGraphRequestsLock);
		g_pendingLiveGraphRequests.push_back({
			.manager = a_manager,
			.eventName = RE::BSFixedString(a_eventName),
			.result = a_result,
		});
	}

	DebugSnapshot GetDebugSnapshot()
	{
		std::scoped_lock lock(g_stateLock);
		DebugSnapshot snapshot;
		if (g_holder) {
			snapshot = g_holder->CaptureDebugSnapshot();
		}
		snapshot.project = g_project;
		snapshot.lastDiagnostic = g_lastDiagnostic;
		return snapshot;
	}
}
