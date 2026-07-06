#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace RE
{
	class BSAnimationGraphManager;
	class NiAVObject;
	class PlayerCharacter;
	class TESIdleForm;
}

namespace TF3DHud::Animations
{
	using DynamicActivationIdleMap = std::map<std::string, std::vector<RE::TESIdleForm*>>;

	inline constexpr std::uint32_t kMaxSubgraphDebugRequestEntries = 4;
	inline constexpr std::uint32_t kMaxSubgraphDebugSlots = 16;
	inline constexpr std::uint32_t kMaxSubgraphDebugFiles = 12;
	inline constexpr std::uint32_t kMaxSubgraphDebugPath = 260;

	struct ActiveNodeDebugInfo
	{
		std::uintptr_t entry{ 0 };
		std::uintptr_t node{ 0 };
		std::uintptr_t behaviorGraph{ 0 };
		std::uintptr_t nestedBehaviorGraph{ 0 };
		std::uintptr_t clip{ 0 };
		std::uintptr_t behaviorRootId{ 0 };
		std::string nodeName;
		std::string clipName;
		std::string authoredClipPath;
		std::string resolvedClipPath;
		float currentTime{ 0.0F };
		float controlLocalTime{ 0.0F };
		float userControlledTimeFraction{ 0.0F };
		float duration{ -1.0F };
		std::uint32_t clipId{ 0 };
		std::uint8_t playbackMode{ 0 };
		std::uint32_t subgraphSlot{ 0 };
		bool isClip{ false };
		bool inSubgraph{ false };
		bool hasTiming{ false };
		bool hasControlLocalTime{ false };
	};

	struct ActiveSyncDebugInfo
	{
		float currentTime{ 0.0F };
		float totalTime{ -1.0F };
		float speed{ 1.0F };
		std::uint32_t syncPointCount{ 0 };
		bool active{ false };
	};

	struct SpeedChannelDebugInfo
	{
		float desiredSpeed{ 0.0F };
		float scale{ 1.0F };
		float rawSpeed{ 0.0F };
		float graphSpeed{ 0.0F };
		float lastSpeed{ 0.0F };
		float previewGraphSpeed{ 0.0F };
		std::uint32_t contourResponse{ 0 };
		std::uint32_t adjustmentCount{ 0 };
		std::uint32_t pollCount{ 0 };
		bool constructed{ false };
		bool reset{ false };
		bool polled{ false };
		bool applyAdjustments{ false };
		bool previewFreeze{ false };
		bool actorFreeze{ false };
		bool useContours{ false };
		bool actorAllowsContours{ false };
		bool contourResolved{ false };
		bool contourState{ false };
		bool contourApplied{ false };
		bool previewGraphSpeedHas{ false };
	};

	struct SubgraphFileDebugInfo
	{
		std::array<char, kMaxSubgraphDebugPath> path{};
	};

	struct SubgraphSlotDebugInfo
	{
		std::uint32_t index{ 0 };
		std::uint32_t stateId{ 0 };
		std::uint64_t handle{ 0 };
		std::uintptr_t sharedData{ 0 };
		std::uintptr_t rootId{ 0 };
		std::array<char, kMaxSubgraphDebugPath> rootName{};
		std::uint32_t useCount{ 0 };
		std::uint32_t pendingRemove{ 0 };
		std::uint32_t files160Count{ 0 };
		std::uint32_t files178Count{ 0 };
		std::uint32_t files160Shown{ 0 };
		std::uint32_t files178Shown{ 0 };
		std::array<SubgraphFileDebugInfo, kMaxSubgraphDebugFiles> files160;
		std::array<SubgraphFileDebugInfo, kMaxSubgraphDebugFiles> files178;
	};

	struct DebugSnapshot
	{
		std::string project;
		std::string lastDiagnostic;
		std::uintptr_t manager{ 0 };
		std::uintptr_t graph{ 0 };
		std::uintptr_t behaviorGraph{ 0 };
		std::uint32_t activeGraphIndex{ 0 };
		std::uint32_t graphCount{ 0 };
		std::uint32_t activeNodeCount{ 0 };
		bool hasManager{ false };
		bool hasGraph{ false };
		bool hasBehaviorGraph{ false };
		bool behaviorActive{ false };
		bool behaviorLinked{ false };
		bool updateActiveNodes{ false };
		bool stateOrTransitionChanged{ false };
		bool activeNodesReady{ false };
		bool generateHavokBones{ false };
		bool hasRagdollInterface{ false };
		bool hasPhysicsWorld{ false };
		bool hasSubgraphSwapData{ false };
		std::uintptr_t subgraphSwapData{ 0 };
		std::uintptr_t subgraphSwapStateMachine{ 0 };
		std::uintptr_t subgraphSwapBehavior{ 0 };
		std::uint32_t subgraphSwapCapacity{ 0 };
		std::uint32_t subgraphSwapSlots{ 0 };
		std::uint32_t subgraphSwapRequestedSlots{ 0 };
		std::uint32_t subgraphSwapLinkedSlots{ 0 };
		std::uint32_t subgraphSwapPendingRemoveSlots{ 0 };
		std::uint32_t subgraphSwapUseCountTotal{ 0 };
		ActiveSyncDebugInfo liveSync;
		ActiveSyncDebugInfo previewSync;
		SpeedChannelDebugInfo speedChannel;
		std::uint32_t defaultSubgraphHandleCount{ 0 };
		std::uint32_t defaultSubgraphHandleShown{ 0 };
		std::uint32_t defaultSubgraphIdCount{ 0 };
		std::uint32_t defaultSubgraphIdShown{ 0 };
		std::uint32_t weaponSubgraphHandleCount{ 0 };
		std::uint32_t weaponSubgraphHandleShown{ 0 };
		std::uint32_t weaponSubgraphIdCount{ 0 };
		std::uint32_t weaponSubgraphIdShown{ 0 };
		std::array<std::uint64_t, kMaxSubgraphDebugRequestEntries> defaultSubgraphHandles{};
		std::array<std::uint64_t, kMaxSubgraphDebugRequestEntries> defaultSubgraphIds{};
		std::array<std::uint64_t, kMaxSubgraphDebugRequestEntries> weaponSubgraphHandles{};
		std::array<std::uint64_t, kMaxSubgraphDebugRequestEntries> weaponSubgraphIds{};
		std::uint32_t subgraphSlotShown{ 0 };
		std::array<SubgraphSlotDebugInfo, kMaxSubgraphDebugSlots> subgraphSlots;
		std::vector<ActiveNodeDebugInfo> activeNodes;
	};

	void Reset();
	void ResetGraph();
	void ResetGraphPreservingIdlePlayback();
	void ResetInitialState();
	void ObserveLoadedIdle(RE::TESIdleForm* a_idle);
	[[nodiscard]] const DynamicActivationIdleMap& GetDynamicActivationIdles();
	void StopIdleAnimation();
	void Update(RE::PlayerCharacter& a_player, RE::NiAVObject& a_previewRoot, float a_deltaTime);
	void ObserveGraphRequest(RE::BSAnimationGraphManager* a_manager, const char* a_eventName, std::uint32_t a_result);
	DebugSnapshot GetDebugSnapshot();
}
