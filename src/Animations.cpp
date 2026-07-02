#include "Animations.h"

#include "Utils.h"

#include "RE/A/Actor.h"
#include "RE/A/ActionInput.h"
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
#include "RE/B/BSTSmartPointer.h"
#include "RE/I/IAnimationGraphManagerHolder.h"
#include "RE/M/MiddleHighProcessData.h"
#include "RE/M/MemoryManager.h"
#include "RE/N/NiAVObject.h"
#include "RE/N/NiPointer.h"
#include "RE/N/NiUpdateData.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/S/SubgraphHandle.h"
#include "RE/S/SubgraphIdentifier.h"
#include "RE/T/TES.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <mutex>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace RE
{
	class BSAnimationGraphChannel :
		public BSIntrusiveRefCounted
	{
	public:
		virtual ~BSAnimationGraphChannel() = default;

		BSFixedString variableName;  // 10
		std::uint32_t unk18{ 0 };    // 18
	};
	static_assert(offsetof(BSAnimationGraphChannel, variableName) == 0x10);

	class BShkbAnimationGraph :
		public BSIntrusiveRefCounted
	{
	public:
		virtual ~BShkbAnimationGraph() = default;
	};
	static_assert(sizeof(BShkbAnimationGraph) == 0x10);
}

namespace TF3DHud::Animations
{
	namespace
	{
		constexpr std::size_t kBShkbAnimationGraphSize = 0x3D0;
		constexpr std::size_t kBShkbAnimationGraphAlignment = 0x10;
		constexpr auto kLiveMirrorEventWhitelist = std::to_array<std::string_view>({
			"reloadStart",
			"reloadEnd",
			"reloadComplete",
			"reloadStateEnter",
			"reloadStateExit",
			"reloadStartSlave",
			"reloadEndSlave",
			"reloadStartSlaveLoop",
			"InjuredDownReloadStart",
			"reloadReserveStart",
			"weaponFire",
			"weaponFireEffect",
			"fireSingle",
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
			"meleeattackStart",
			"meleeattackSprintStart",
			"meleeAttackGun",
			"grenadeThrowStart",
			"blockStart",
			"weapEquip",
			"weapUnequip",
			"weaponDraw",
			"weaponSheathe",
			"BeginWeaponDraw",
			"BeginWeaponSheathe",
			"weapForceEquip",
			"g_weapForceEquipInstant",
			"weaponAttach",
			"weaponDetach",
			"AnimObjDraw",
			"AnimObjUnequip",
			"rifleSightedStart",
			"rifleSightedEnd",
			"rifleSightedStartOver",
			"sightedStateEnter",
			"sightedStateExit",
			"UpdateSighted",
			"STSAim",
			"jumpStart",
			"jumpFall",
			"jumpLand",
			"jumpLandSoft",
			"jumpEnd",
			"JumpUp",
			"JumpDown",
			"SprintJumpStart",
			"SprintJumpStop",
			"MoveStart",
			"MoveStop",
			"SprintStart",
			"SprintStop",
			"sneakStart",
			"sneakStop",
			"sneakStateEnter",
			"sneakStateExit",
			"SyncLeft",
			"SyncRight",
			"SyncCycleEnd",
			"syncIdleStart",
			"syncIdleStop",
		});

		constexpr auto kIntGraphVariableWhitelist = std::to_array<std::string_view>({
			"iSyncJumpState",
			"iSyncWeaponDrawState",
			"iSyncSightedState",
			"iSyncGunDown",
			"iSyncChargeState",
			"iWeaponChargeMode",
			"iAttackState",
			"CurrentJumpState",
			"iIsInSneak",
			"iSyncSneakWalkRun",
			"iSyncSprintState",
			"iSyncIdleLocomotion",
			"iSyncTurnState",
			"iSyncDirection",
		});

		constexpr auto kBoolGraphVariableWhitelist = std::to_array<std::string_view>({
			"isReloading",
			"isAttacking",
			"IsAttackReady",
			"isAttackNotReady",
			"isJumping",
			"bInJumpState",
			"IsSneaking",
			"bIsSneaking",
			"bEquipOk",
			"bAimActive",
			"bAimEnabled",
		});

		constexpr auto kFloatGraphVariableWhitelist = std::to_array<std::string_view>({
			"weaponSpeedMult",
			"reloadSpeedMult",
			"sightedSpeedMult",
			"AimWobble",
			"AimWobbleSpeedMult",
		});

		constexpr auto kSuppressedControllerVariables = std::to_array<std::string_view>({
			"Pitch",
			"PitchDelta",
			"PitchDeltaSmoothed",
			"fControllerXSum",
			"fControllerYSum",
			"fControllerYSmoothed",
			"fControllerXSmoothed",
			"fControllerXRaw",
			"fControllerYRaw",
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

		[[nodiscard]] bool IsLiveMirrorEventWhitelisted(const char* a_event)
		{
			if (!a_event || a_event[0] == '\0') {
				return false;
			}

			// Graph requests are BSFixedString values. Compare in the same domain
			// so vanilla event casing such as "WeapEquip" matches the intended
			// whitelist entry without raw string case heuristics.
			return ContainsEngineFixedString(RE::BSFixedString(a_event), kLiveMirrorEventWhitelist);
		}

		[[nodiscard]] RE::BSFixedString GetAnimationChannelName(RE::BSAnimationGraphChannel* a_channel)
		{
			if (!a_channel) {
				return {};
			}

			return a_channel->variableName;
		}

		[[nodiscard]] bool IsSuppressedControllerChannel(RE::BSAnimationGraphChannel* a_channel)
		{
			return ContainsEngineFixedString(GetAnimationChannelName(a_channel), kSuppressedControllerVariables);
		}

		[[nodiscard]] bool IsSuppressedControllerVariable(const RE::BSFixedString& a_variable)
		{
			return ContainsEngineFixedString(a_variable, kSuppressedControllerVariables);
		}

		template <class ChannelArray>
		void PruneSuppressedControllerChannels(ChannelArray& a_channels)
		{
			for (std::uint32_t index = 0; index < a_channels.size();) {
				if (IsSuppressedControllerChannel(a_channels[index].get())) {
					a_channels.erase(a_channels.begin() + index);
				} else {
					++index;
				}
			}
		}

		using BShkbAnimationGraphCtor_t =
			RE::BShkbAnimationGraph*(RE::BShkbAnimationGraph*, RE::Actor*, bool);
		using NotifyAnimationGraphImpl_t = bool(RE::IAnimationGraphManagerHolder*, const RE::BSFixedString&);
		using ActorAnimationGraphManagerCallback_t =
			void(RE::IAnimationGraphManagerHolder*, const RE::BSTSmartPointer<RE::BSAnimationGraphManager>&);
		using CreateAnimationGraphManager_t = bool(RE::IAnimationGraphManagerHolder*, const char*);
		struct BSAnimationUpdateData
		{
			float deltaTime{ 0.0F };
			std::uint32_t pad04{ 0 };
			void* unk08{ nullptr };
			void* postUpdateFunctor{ nullptr };
			std::uint32_t flags18{ 0x01000000 };
			std::uint16_t flags1C{ 0x0101 };
			std::uint16_t pad1E{ 0 };
		};
		static_assert(offsetof(BSAnimationUpdateData, flags18) == 0x18);
		static_assert(offsetof(BSAnimationUpdateData, flags1C) == 0x1C);
		static_assert(sizeof(BSAnimationUpdateData) == 0x20);
		using UpdateAnimationGraphManager_t = bool(RE::IAnimationGraphManagerHolder*, const BSAnimationUpdateData&);
		using UpdateAnimationGraphManagerFloat_t = bool(RE::IAnimationGraphManagerHolder*, float);
		using GetProjectForActor_t = const char*(RE::Actor*, RE::NiAVObject*);
		using SetAnimationGraphTarget_t = bool(RE::IAnimationGraphManagerHolder*, RE::NiAVObject*, bool);
		enum class SyncPointType : std::uint32_t
		{
			kLeft = 0,
			kRight = 1,
			kSecondLeft = 2,
			kSecondRight = 3,
		};
		using CalculateSpeedAdjustToSyncAnimationCycles_t = bool(
			float,
			float,
			const RE::BGSAnimationSystemUtils::ActiveSyncInfo&,
			float,
			const SyncPointType&,
			float&);
		using RequestActorSubGraph_t = bool(
			RE::Actor*,
			RE::BSAnimationGraphManager*,
			const std::int32_t*,
			RE::BSTSmallArray<RE::SubgraphHandle, 2>*,
			RE::BSTSmallArray<RE::SubgraphIdentifier, 2>*);
		using ActivateAnimationGraphManager_t = bool(RE::BSAnimationGraphManager*);
		using GetCellPriority_t = std::int32_t(RE::TES*, const RE::TESObjectCELL*, RE::NiPoint3*);
		using GetDefaultAction_t = RE::BGSAction*(void);
		using TESActionDataCtor_t = void*(
			void*,
			RE::ActionInput::ACTIONPRIORITY,
			RE::TESObjectREFR*,
			RE::BGSAction*,
			RE::TESObjectREFR*,
			RE::ActionInput::Data);
		using TESActionDataDtor_t = void(void*);
		using InterpretAction_t = bool(void*);

		REL::Relocation<BShkbAnimationGraphCtor_t*> g_constructBShkbAnimationGraph{ REL::ID{ 1074981, 2256827 } };
		REL::Relocation<NotifyAnimationGraphImpl_t*> g_notifyAnimationGraphImpl{ REL::ID{ 1379025, 2214561 } };
		REL::Relocation<ActorAnimationGraphManagerCallback_t*> g_actorPreUpdateAnimationGraphManager{ REL::ID{ 442032, 2230545 } };
		REL::Relocation<ActorAnimationGraphManagerCallback_t*> g_actorPreLoadAnimationGraphManager{ REL::ID{ 1053762, 2230546 } };
		REL::Relocation<ActorAnimationGraphManagerCallback_t*> g_actorPostLoadAnimationGraphManager{ REL::ID{ 348865, 2230547 } };
		REL::Relocation<CreateAnimationGraphManager_t*> g_createAnimationGraphManager{ REL::ID{ 532453, 2214553 } };
		REL::Relocation<UpdateAnimationGraphManager_t*> g_updateAnimationGraphManager{ REL::ID{ 1492656, 2214536 } };
		REL::Relocation<UpdateAnimationGraphManagerFloat_t*> g_updateAnimationGraphManagerFloat{ REL::ID(973903) };
		REL::Relocation<GetProjectForActor_t*> g_getProjectForActor{ REL::ID{ 804224, 2236395 } };
		REL::Relocation<SetAnimationGraphTarget_t*> g_setAnimationGraphTarget{ REL::ID{ 1340816, 2214556 } };
		REL::Relocation<CalculateSpeedAdjustToSyncAnimationCycles_t*> g_calculateSpeedAdjustToSyncAnimationCycles{
			REL::ID{ 552450, 2214290 }
		};
		REL::Relocation<RequestActorSubGraph_t*> g_requestDefaultSubGraph{ REL::ID{ 1305500, 2232254 } };
		REL::Relocation<RequestActorSubGraph_t*> g_requestWeaponSubGraph{ REL::ID{ 973680, 2232255 } };
		REL::Relocation<ActivateAnimationGraphManager_t*> g_activateAnimationGraphManager{ REL::ID(950096) };
		REL::Relocation<GetCellPriority_t*> g_getCellPriority{ REL::ID{ 665767, 2192052 } };
		REL::Relocation<GetDefaultAction_t*> g_getDefaultObjectForActionInitializeToBaseState{ REL::ID(639576) };
		REL::Relocation<GetDefaultAction_t*> g_getDefaultObjectForActionInstantInitializeToBaseState{ REL::ID{ 1517112, 2214310 } };
		REL::Relocation<TESActionDataCtor_t*> g_constructTESActionData{ REL::ID(1307135) };
		REL::Relocation<TESActionDataDtor_t*> g_destroyTESActionData{ REL::ID(229573) };
		REL::Relocation<InterpretAction_t*> g_interpretAction{ REL::ID{ 10433, 2229530 } };

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

		class PreviewAnimationGraphHolder final :
			public RE::IAnimationGraphManagerHolder
		{
		public:
			class LiveAnimationEventSink final :
				public RE::BSTEventSink<RE::BSAnimationGraphEvent>
			{
			public:
				explicit LiveAnimationEventSink(PreviewAnimationGraphHolder& a_owner) :
					owner_(std::addressof(a_owner))
				{}

				RE::BSEventNotifyControl ProcessEvent(
					const RE::BSAnimationGraphEvent& a_event,
					[[maybe_unused]] RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source) override
				{
					if (owner_) {
						owner_->RecordLiveAnimationEvent(a_event);
					}
					return RE::BSEventNotifyControl::kContinue;
				}

			private:
				PreviewAnimationGraphHolder* owner_{ nullptr };
			};

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
				liveAnimationEventSink_(*this),
				previewAnimationEventSink_(*this)
			{}

			~PreviewAnimationGraphHolder() override
			{
				UnregisterLiveEventSource();
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
				if (manager_) {
					// IDA: BSAnimationGraphManager::UpdateChannels iterates
					// boundChannel immediately before BShkbAnimationGraph::ReceiveChannelsImpl.
					PruneSuppressedControllerChannels(manager_->boundChannel);
				}
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
				if (!sourceHolder_ || !sourceHolder_->CreateAnimationChannels(a_channels)) {
					return false;
				}

				PruneSuppressedControllerChannels(a_channels);
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
				if (IsSuppressedControllerVariable(a_variable)) {
					a_out = 0.0F;
					return true;
				}
				return sourceHolder_ && sourceHolder_->GetGraphVariableImplFloat(a_variable, a_out);
			}

			bool GetGraphVariableImplInt(const RE::BSFixedString& a_variable, std::int32_t& a_out) const override
			{
				if (IsSuppressedControllerVariable(a_variable)) {
					a_out = 0;
					return true;
				}
				return sourceHolder_ && sourceHolder_->GetGraphVariableImplInt(a_variable, a_out);
			}

			bool GetGraphVariableImplBool(const RE::BSFixedString& a_variable, bool& a_out) const override
			{
				if (IsSuppressedControllerVariable(a_variable)) {
					a_out = false;
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

			bool RequestInitialSubgraphs()
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
				const bool defaultRequested = g_requestDefaultSubGraph(
					sourceActor_,
					manager_.get(),
					std::addressof(priority),
					std::addressof(defaultSubgraphHandles_),
					std::addressof(defaultSubgraphIds_));
				const bool weaponRequested = g_requestWeaponSubGraph(
					sourceActor_,
					manager_.get(),
					std::addressof(priority),
					std::addressof(weaponSubgraphHandles_),
					std::addressof(weaponSubgraphIds_));

				return defaultRequested || weaponRequested;
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

			bool IsDefaultSubgraphLinked() const
			{
				auto graph = GetActivePreviewGraph();
				if (!graph || defaultSubgraphHandles_.empty()) {
					return false;
				}

				auto* const swapData = *reinterpret_cast<BehaviorGraphSwapDataDiagnostic* const*>(
					reinterpret_cast<const std::byte*>(graph.get()) + 0x3A0);
				if (!swapData || !swapData->entries || swapData->size > 16) {
					return false;
				}

				const auto* const defaultHandles = GetSubgraphHandleStorage(defaultSubgraphHandles_);
				if (defaultHandles && defaultHandles[0].handle != 0) {
					const auto defaultHandle = defaultHandles[0].handle;
					for (std::uint32_t index = 0; index < swapData->size; ++index) {
						const auto& entry = swapData->entries[index];
						if (entry.handle.handle == defaultHandle && entry.sharedData) {
							return true;
						}
					}
				}

				// IDA: BSBehaviorGraphSwapSingleton::UpdateForManager/
				// OnActivateImpl relinks swap entries as state index+1.
				// RequestInitialSubgraphs requests default first, then weapon,
				// so slot 0/state 1 is the selected default subgraph state.
				return swapData->size > 0 &&
				       swapData->entries[0].sharedData &&
				       swapData->entries[0].useCount != 0 &&
				       defaultSubgraphIds_.size() > 0;
			}

			void RefreshPendingSubgraphLoads()
			{
				if (!manager_ || IsDefaultSubgraphLinked()) {
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
				g_constructTESActionData(
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

				g_destroyTESActionData(actionData.data());
				return processed;
			}

			void TryApplyInitialAnimationState()
			{
				if (initialStateApplied_ || !manager_ || !IsDefaultSubgraphLinked()) {
					return;
				}

				// IDA: BGSAnimationSystemUtils::InitializeActorInstant scopes
				// iSyncWeaponDrawState=2 only around the instant base-state
				// action, then resets it after the tiny graph update.
				SetGraphVariableInt("iSyncWeaponDrawState", 2);
				SetGraphVariableFloat("fRandomClipStartTimePercentage", RE::BSRandom::Float0To1());
				bool processed = RunInitialBaseStateAction(g_getDefaultObjectForActionInstantInitializeToBaseState());
				if (!processed) {
					SetGraphVariableInt("iSyncWeaponDrawState", 0);
					processed = RunInitialBaseStateAction(g_getDefaultObjectForActionInitializeToBaseState());
				}

				// IDA: InitializeActorInstant performs a tiny graph update after
				// the initialize action so the state machine consumes the event
				// immediately.
				BSAnimationUpdateData updateData;
				updateData.deltaTime = 0.0001F;
				updateData.flags18 = 0xFFFF;
				updateData.flags1C = 1;
				(void)g_updateAnimationGraphManager(this, updateData);
				SetGraphVariableInt("iSyncWeaponDrawState", 0);

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
					weaponSubgraphHandles_,
					a_snapshot.weaponSubgraphHandleCount,
					a_snapshot.weaponSubgraphHandleShown,
					a_snapshot.weaponSubgraphHandles);
				CaptureSubgraphIds(
					weaponSubgraphIds_,
					a_snapshot.weaponSubgraphIdCount,
					a_snapshot.weaponSubgraphIdShown,
					a_snapshot.weaponSubgraphIds);

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

			void SyncLiveEventSource()
			{
				RE::BSTSmartPointer<RE::BSAnimationGraphManager> sourceManager;
				if (!sourceHolder_ ||
					!sourceHolder_->GetAnimationGraphManagerImpl(sourceManager) ||
					!sourceManager ||
					sourceManager->graph.empty()) {
					UnregisterLiveEventSource();
					return;
				}

				const auto activeGraph = sourceManager->activeGraph;
				if (activeGraph >= sourceManager->graph.size()) {
					UnregisterLiveEventSource();
					return;
				}

				auto liveGraph = sourceManager->graph[activeGraph];
				auto* eventSource = GetGraphEventSource(liveGraph.get());
				if (eventSource == liveEventSource_) {
					return;
				}

				UnregisterLiveEventSource();
				if (!eventSource) {
					return;
				}

				eventSource->RegisterSink(std::addressof(liveAnimationEventSink_));
				liveEventGraph_ = std::move(liveGraph);
				liveEventSource_ = eventSource;
			}

			void RecordLiveAnimationEvent(const RE::BSAnimationGraphEvent& a_event)
			{
				if (!a_event.tag.empty()) {
					// BShkbAnimationGraph::BroadcastQueuedEventsImpl emits graph events
					// after Generate(). Root swap selection uses normal graph events,
					// so replay whitelisted live graph events through the preview graph
					// rather than forcing the selected subgraph state.
					ApplyPreviewWeaponVisibilityEvent(a_event.tag);
					if (IsLiveMirrorEventWhitelisted(a_event.tag.c_str())) {
						QueueMirroredEvent(a_event.tag);
					}
				}
			}

			void ObserveLiveGraphRequest(const char* a_eventName, const std::uint32_t a_result)
			{
				if (!a_eventName || a_eventName[0] == '\0') {
					return;
				}

				const bool accepted = a_result != 0;
				const bool whitelisted = IsLiveMirrorEventWhitelisted(a_eventName);
				if (!accepted || !whitelisted) {
					return;
				}

				QueueMirroredEvent(RE::BSFixedString(a_eventName));
			}

			void QueueMirroredEvent(const RE::BSFixedString& a_eventName)
			{
				if (a_eventName.empty()) {
					return;
				}

				std::scoped_lock lock(pendingMirroredEventsLock_);
				pendingMirroredEvents_.emplace_back(a_eventName);
			}

			void ProcessMirroredEvents()
			{
				std::vector<RE::BSFixedString> events;
				{
					std::scoped_lock lock(pendingMirroredEventsLock_);
					if (pendingMirroredEvents_.empty()) {
						return;
					}
					events.swap(pendingMirroredEvents_);
				}

				for (const auto& eventName : events) {
					(void)NotifyAnimationGraphImpl(eventName);
				}
			}

			void RecordPreviewAnimationEvent(const RE::BSAnimationGraphEvent& a_event)
			{
				if (!a_event.tag.empty()) {
					ApplyPreviewCullBoneEvent(a_event);
				}
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

				for (const auto& variableName : kIntGraphVariableWhitelist) {
					std::int32_t value{ 0 };
					if (sourceHolder_->GetGraphVariableImplInt(variableName.data(), value)) {
						SetGraphVariableInt(variableName.data(), value);
						if (variableName == std::string_view{ "iSyncWeaponDrawState" }) {
							ApplyPreviewWeaponDrawState(value);
						}
					}
				}

				for (const auto& variableName : kBoolGraphVariableWhitelist) {
					bool value{ false };
					if (sourceHolder_->GetGraphVariableImplBool(variableName.data(), value)) {
						SetGraphVariableBool(variableName.data(), value);
					}
				}

				for (const auto& variableName : kFloatGraphVariableWhitelist) {
					float value{ 0.0F };
					if (sourceHolder_->GetGraphVariableImplFloat(variableName.data(), value)) {
						SetGraphVariableFloat(variableName.data(), value);
					}
				}
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

			[[nodiscard]] DebugSnapshot CaptureDebugSnapshot() const
			{
				DebugSnapshot snapshot;
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
								// OAR/HaBCR identify hkbClipGenerator::localTime at +0x140.
								// HaBCR mirrors writes to hkaDefaultAnimationControl +0x10.
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

		private:
			RE::PlayerCharacter* sourceActor_{ nullptr };
			RE::IAnimationGraphManagerHolder* sourceHolder_{ nullptr };
			RE::NiPointer<RE::NiAVObject> targetRoot_;
			RE::NiAVObject* targetGraphRoot_{ nullptr };
			RE::BSAnimationGraphManager* sourceManagerIdentity_{ nullptr };
			RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager_;
			LiveAnimationEventSink liveAnimationEventSink_;
			PreviewAnimationEventSink previewAnimationEventSink_;
			RE::BSTSmartPointer<RE::BShkbAnimationGraph> liveEventGraph_;
			RE::BSTSmartPointer<RE::BShkbAnimationGraph> previewEventGraph_;
			RE::BSTEventSource<RE::BSAnimationGraphEvent>* liveEventSource_{ nullptr };
			RE::BSTEventSource<RE::BSAnimationGraphEvent>* previewEventSource_{ nullptr };
			std::mutex pendingMirroredEventsLock_;
			std::vector<RE::BSFixedString> pendingMirroredEvents_;
			RE::BSTSmallArray<RE::SubgraphHandle, 2> defaultSubgraphHandles_;
			RE::BSTSmallArray<RE::SubgraphIdentifier, 2> defaultSubgraphIds_;
			RE::BSTSmallArray<RE::SubgraphHandle, 2> weaponSubgraphHandles_;
			RE::BSTSmallArray<RE::SubgraphIdentifier, 2> weaponSubgraphIds_;
			bool initialStateApplied_{ false };

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

			void UnregisterLiveEventSource()
			{
				if (!liveEventSource_) {
					return;
				}

				liveEventSource_->UnregisterSink(std::addressof(liveAnimationEventSink_));
				liveEventSource_ = nullptr;
				liveEventGraph_.reset();
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
		std::uint64_t g_liveSubgraphSignature{ 0 };
		std::recursive_mutex g_stateLock;

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

		void MixHash(std::uint64_t& a_hash, const std::uint64_t a_value)
		{
			a_hash ^= a_value + 0x9E3779B97F4A7C15ull + (a_hash << 6) + (a_hash >> 2);
		}

		void HashSubgraphIds(std::uint64_t& a_hash, const RE::BSTSmallArray<RE::SubgraphIdentifier, 2>& a_ids)
		{
			MixHash(a_hash, a_ids.size());
			for (const auto& id : a_ids) {
				MixHash(a_hash, static_cast<std::uint64_t>(id.identifier));
			}
		}

		[[nodiscard]] bool TryGetLiveSubgraphRequestSignature(
			RE::PlayerCharacter& a_player,
			std::uint64_t& a_signature,
			std::string& a_reason)
		{
			a_signature = 0;
			auto* process = a_player.currentProcess;
			if (!process) {
				a_reason = "live player process is null";
				return false;
			}

			auto* middleHigh = process->middleHigh;
			if (!middleHigh) {
				a_reason = "live player MiddleHighProcessData is null";
				return false;
			}

			std::uint64_t hash = 0xCBF29CE484222325ull;
			// Use the live requested IDs as preview intent. Current IDs and idle roots
			// are engine load results and can churn while requests are being serviced.
			HashSubgraphIds(hash, middleHigh->requestedDefaultSubGraphID);
			HashSubgraphIds(hash, middleHigh->requestedWeaponSubGraphID);
			a_signature = hash;
			return true;
		}

		void Clear()
		{
			g_holder.reset();
			g_project.clear();
			g_liveSubgraphSignature = 0;
		}

		[[nodiscard]] bool EnsureGraph(RE::PlayerCharacter& a_player, RE::NiAVObject& a_previewRoot)
		{
			const auto project = GetActorProject(a_player);
			if (project.empty()) {
				Clear();
				LogDiagnostic("skipped: actor behavior project is empty");
				return false;
			}

			std::uint64_t liveSubgraphSignature = 0;
			std::string liveSubgraphReason;
			if (!TryGetLiveSubgraphRequestSignature(a_player, liveSubgraphSignature, liveSubgraphReason)) {
				Clear();
				LogDiagnostic("skipped: " + liveSubgraphReason);
				return false;
			}

			if (!HasLiveAnimationGraphManager(a_player)) {
				Clear();
				LogDiagnostic("skipped: live player animation graph manager is unavailable");
				return false;
			}

			if (g_holder &&
				g_holder->SourceActor() == std::addressof(a_player) &&
				g_holder->TargetRoot() == std::addressof(a_previewRoot) &&
				g_project == project &&
				g_liveSubgraphSignature == liveSubgraphSignature &&
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
			(void)holder->RequestInitialSubgraphs();
			holder->PrepareGraphManagerForSubgraphs();
			if (!holder->ActivatePreviewGraphManager()) {
				LogDiagnostic("manager discarded: Activate failed for project '" + project + "'");
				return false;
			}
			g_project = project;
			g_liveSubgraphSignature = liveSubgraphSignature;
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

	void Update(RE::PlayerCharacter& a_player, RE::NiAVObject& a_previewRoot, const float a_deltaTime)
	{
		std::scoped_lock lock(g_stateLock);
		if (!EnsureGraph(a_player, a_previewRoot) || !g_holder) {
			return;
		}
		g_holder->SyncLiveEventSource();
		g_holder->SyncWhitelistedGraphVariablesFromLive();
		g_holder->RefreshPendingSubgraphLoads();
		g_holder->TryApplyInitialAnimationState();
		g_holder->ProcessMirroredEvents();

		const auto previewRootLocal = a_previewRoot.GetLocalTransform();
		const auto updateDelta = g_holder->GetActiveClipSynchronizedDeltaTime(a_deltaTime);

		const bool updated = g_updateAnimationGraphManagerFloat(g_holder.get(), updateDelta);

		a_previewRoot.SetLocalTransform(previewRootLocal);
		RE::NiUpdateData niUpdateData;
		a_previewRoot.Update(niUpdateData);

		if (!updated) {
			LogDiagnostic("update returned false");
		}
	}

	void ObserveGraphRequest(RE::BSAnimationGraphManager* a_manager, const char* a_eventName, const std::uint32_t a_result)
	{
		std::scoped_lock lock(g_stateLock);
		if (!g_holder || !g_holder->IsLiveSourceManager(a_manager)) {
			return;
		}

		g_holder->ObserveLiveGraphRequest(a_eventName, a_result);
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
