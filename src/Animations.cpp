#include "Animations.h"

#include "Utils.h"

#include "RE/A/Actor.h"
#include "RE/B/BSAnimationGraphManager.h"
#include "RE/B/BSAnimationGraphEvent.h"
#include "RE/B/BSFixedString.h"
#include "RE/B/BSIntrusiveRefCounted.h"
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
#include "RE/S/SubgraphIdentifier.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace RE
{
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

		using BShkbAnimationGraphCtor_t =
			RE::BShkbAnimationGraph*(RE::BShkbAnimationGraph*, RE::Actor*, bool);
		using NotifyAnimationGraphImpl_t = bool(RE::IAnimationGraphManagerHolder*, const RE::BSFixedString&);
		using ActorAnimationGraphManagerCallback_t =
			void(RE::IAnimationGraphManagerHolder*, const RE::BSTSmartPointer<RE::BSAnimationGraphManager>&);
		using CreateAnimationGraphManager_t = bool(RE::IAnimationGraphManagerHolder*, const char*);
		using UpdateAnimationGraphManager_t = bool(RE::IAnimationGraphManagerHolder*, const BSAnimationUpdateData&);
		using GetProjectForActor_t = const char*(RE::Actor*, RE::NiAVObject*);

		REL::Relocation<BShkbAnimationGraphCtor_t*> g_constructBShkbAnimationGraph{ REL::ID{ 1074981, 2256827 } };
		REL::Relocation<NotifyAnimationGraphImpl_t*> g_notifyAnimationGraphImpl{ REL::ID{ 1379025, 2214561 } };
		REL::Relocation<ActorAnimationGraphManagerCallback_t*> g_actorPreUpdateAnimationGraphManager{ REL::ID{ 442032, 2230545 } };
		REL::Relocation<ActorAnimationGraphManagerCallback_t*> g_actorPreLoadAnimationGraphManager{ REL::ID{ 1053762, 2230546 } };
		REL::Relocation<CreateAnimationGraphManager_t*> g_createAnimationGraphManager{ REL::ID{ 532453, 2214553 } };
		REL::Relocation<UpdateAnimationGraphManager_t*> g_updateAnimationGraphManager{ REL::ID{ 1492656, 2214536 } };
		REL::Relocation<GetProjectForActor_t*> g_getProjectForActor{ REL::ID{ 804224, 2236395 } };

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
						owner_->MirrorAnimationEvent(a_event);
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
				targetGraphRoot_(FindFlattenedBoneTree(std::addressof(a_target))),
				sourceManagerIdentity_(GetLiveSourceManager()),
				liveAnimationEventSink_(*this),
				previewAnimationEventSink_(*this)
			{
				if (!targetGraphRoot_) {
					targetGraphRoot_ = std::addressof(a_target);
				}
			}

			~PreviewAnimationGraphHolder() override
			{
				UnregisterLiveEventSource();
				UnregisterPreviewEventSource();
				if (mirroredEventCount_ > 0) {
					REX::INFO("Animations: mirrored animation events={}", mirroredEventCount_);
				}
				if (previewEventCount_ > 0) {
					REX::INFO("Animations: preview graph events={}", previewEventCount_);
				}
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

				// IDA: PlayerCharacter::ConstructAnimationGraph calls BShkbAnimationGraph(actor, false).
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
				return sourceHolder_ ? sourceHolder_->CreateAnimationChannels(a_channels) : false;
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
				return sourceHolder_ && sourceHolder_->GetGraphVariableImplFloat(a_variable, a_out);
			}

			bool GetGraphVariableImplInt(const RE::BSFixedString& a_variable, std::int32_t& a_out) const override
			{
				return sourceHolder_ && sourceHolder_->GetGraphVariableImplInt(a_variable, a_out);
			}

			bool GetGraphVariableImplBool(const RE::BSFixedString& a_variable, bool& a_out) const override
			{
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
				REX::INFO(
					"Animations: registered live graph event source graph={:X}",
					reinterpret_cast<std::uintptr_t>(liveEventGraph_.get()));
			}

			void MirrorAnimationEvent(const RE::BSAnimationGraphEvent& a_event)
			{
				if (a_event.tag.empty()) {
					return;
				}

				std::scoped_lock lock(pendingMirroredEventsLock_);
				pendingMirroredEvents_.push_back(a_event.tag);
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
					if (NotifyAnimationGraphImpl(eventName)) {
						++mirroredEventCount_;
					}
				}
			}

			void RecordPreviewAnimationEvent(const RE::BSAnimationGraphEvent& a_event)
			{
				if (!a_event.tag.empty()) {
					++previewEventCount_;
				}
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
			std::uint32_t mirroredEventCount_{ 0 };
			std::uint32_t previewEventCount_{ 0 };

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

		[[nodiscard]] bool SubgraphIdsEqual(
			const RE::BSTSmallArray<RE::SubgraphIdentifier, 2>& a_lhs,
			const RE::BSTSmallArray<RE::SubgraphIdentifier, 2>& a_rhs)
		{
			if (a_lhs.size() != a_rhs.size()) {
				return false;
			}

			for (std::uint32_t i = 0; i < a_lhs.size(); ++i) {
				if (a_lhs[i].identifier != a_rhs[i].identifier) {
					return false;
				}
			}
			return true;
		}

		void HashSubgraphIds(std::uint64_t& a_hash, const RE::BSTSmallArray<RE::SubgraphIdentifier, 2>& a_ids)
		{
			MixHash(a_hash, a_ids.size());
			for (const auto& id : a_ids) {
				MixHash(a_hash, static_cast<std::uint64_t>(id.identifier));
			}
		}

		[[nodiscard]] bool TryGetStableLiveSubgraphSignature(
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

			if (!SubgraphIdsEqual(middleHigh->currentDefaultSubGraphID, middleHigh->requestedDefaultSubGraphID)) {
				a_reason = "default subgraph request is still pending";
				return false;
			}

			if (!SubgraphIdsEqual(middleHigh->currentWeaponSubGraphID, middleHigh->requestedWeaponSubGraphID)) {
				a_reason = "weapon subgraph request is still pending";
				return false;
			}

			if (!process->IsWeaponSubgraphFinishedLoading(a_player)) {
				a_reason = "weapon subgraph load has not finished";
				return false;
			}

			std::uint64_t hash = 0xCBF29CE484222325ull;
			HashSubgraphIds(hash, middleHigh->currentDefaultSubGraphID);
			HashSubgraphIds(hash, middleHigh->currentWeaponSubGraphID);
			MixHash(hash, middleHigh->subGraphIdleManagerRoots.size());
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
			if (!TryGetStableLiveSubgraphSignature(a_player, liveSubgraphSignature, liveSubgraphReason)) {
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

			std::unordered_set<std::string> graphBones;
			CollectGraphWrittenBoneNames(*holder, graphBones);
			const auto targetStats = InspectGraphTargets(*holder, a_previewRoot);
			if (targetStats.refs == 0) {
				LogDiagnostic("manager discarded: graph target refs are empty for project '" + project + "'");
				return false;
			}
			REX::INFO(
				"Animations: manager created project='{}', targetRoot={:X}, graphRoot={:X}, graphWrittenBones={}, graphTargets={}, directTargets={}, flattenedTargets={}, outsidePreviewRoot={}",
				project,
				reinterpret_cast<std::uintptr_t>(std::addressof(a_previewRoot)),
				reinterpret_cast<std::uintptr_t>(holder->TargetGraphRoot()),
				graphBones.size(),
				targetStats.refs,
				targetStats.directRefs,
				targetStats.flattenedRefs,
				targetStats.outsideExpectedRoot);
			if (targetStats.outsideExpectedRoot != 0) {
				REX::WARN(
					"Animations: preview graph has {} target refs outside preview root",
					targetStats.outsideExpectedRoot);
			}

			g_project = project;
			g_liveSubgraphSignature = liveSubgraphSignature;
			g_holder = std::move(holder);
			return true;
		}
	}

	void Reset()
	{
		Clear();
		g_lastDiagnostic.clear();
	}

	void Update(RE::PlayerCharacter& a_player, RE::NiAVObject& a_previewRoot, const float a_deltaTime)
	{
		if (!EnsureGraph(a_player, a_previewRoot) || !g_holder) {
			return;
		}
		g_holder->SyncLiveEventSource();
		g_holder->ProcessMirroredEvents();

		const auto previewRootLocal = a_previewRoot.GetLocalTransform();
		BSAnimationUpdateData updateData;
		updateData.deltaTime = a_deltaTime;

		const bool updated = g_updateAnimationGraphManager(g_holder.get(), updateData);

		a_previewRoot.SetLocalTransform(previewRootLocal);
		RE::NiUpdateData niUpdateData;
		a_previewRoot.Update(niUpdateData);

		if (!updated) {
			LogDiagnostic("update returned false");
		}
	}
}
