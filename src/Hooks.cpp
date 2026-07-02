#include "Hooks.h"

#include "Animations.h"
#include "Config.h"
#include "Events.h"
#include "ImguiMenu.h"
#include "Morph.h"
#include "Previewer.h"
#include "Renderer.h"
#include "Utils.h"

#include "RE/A/AIProcess.h"
#include "RE/A/Actor.h"
#include "RE/B/BSAnimationGraphManager.h"
#include "RE/B/BSFixedString.h"
#include "RE/B/BSTimer.h"
#include "RE/B/BSTEvent.h"
#include "RE/I/Interface3D.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESEquipEvent.h"
#include "RE/T/TESForm.h"

#include <cstdint>

namespace RE
{
	class EquipEventSource :
		public BSTEventSource<TESEquipEvent>
	{
	public:
		[[nodiscard]] static EquipEventSource* GetSingleton()
		{
			REL::Relocation<EquipEventSource*> singleton{ REL::ID{ 485633, 2691240, 4798533 } };
			return singleton.get();
		}
	};
}

namespace TF3DHud
{
	namespace
	{
		using RunActorUpdates_t = void(RE::ProcessLists*, float, bool);
		using ProcessGraphEvent_t = std::uint32_t(RE::BSAnimationGraphManager*, const RE::BSFixedString&);
		using Update3DModel_t = void(RE::AIProcess*, RE::Actor*, bool);
		using GetAll3DUpdateFlags_t = std::uint16_t(RE::AIProcess*);
		using QUpdateEditorDeadActorModel_t = bool(RE::AIProcess*);
		using RenderSceneDeferred_t = void(
			RE::NiCamera*,
			RE::BSShaderAccumulator*,
			RE::BSCullingProcess*,
			RE::ShadowSceneNode*,
			std::int32_t,
			std::int32_t,
			bool,
			bool);
		using RenderPrepassesAndMenus_t = void(RE::Interface3D::Renderer*);
		using SetDoTiledLighting_t = void(bool);
		using QTiledLighting_t = bool();

		REL::Relocation<std::uintptr_t> g_runActorUpdatesCall{ REL::ID(556439), 0x17 };
		REL::Relocation<std::uintptr_t> g_processGraphEventTarget{ REL::ID(1199489) };
		// BodyShapeManager hooks AIProcess::Update3dModel at this function entry and
		// preserves the first 5-byte instruction (`mov [rsp+0x18], rbp`). Use the same
		// post-original signal point so skeleton-adjustment/morph follow-up work has
		// entered the engine's 3D update path before our 300-frame stable audit starts.
		REL::Relocation<std::uintptr_t> g_update3DModelTarget{ REL::ID{ 986782, 2231882 } };
		REL::Relocation<GetAll3DUpdateFlags_t*> g_getAll3DUpdateFlags{ REL::ID{ 582098, 2232393 } };
		REL::Relocation<QUpdateEditorDeadActorModel_t*> g_qUpdateEditorDeadActorModel{ REL::ID{ 16281, 2231571 } };
		RunActorUpdates_t* g_runActorUpdates{ nullptr };
		ProcessGraphEvent_t* g_processGraphEvent{ nullptr };
		Update3DModel_t* g_update3DModel{ nullptr };
		// IDA: Interface3D::Renderer::DrawModel calls BSShaderUtil::RenderSceneDeferred
		// at +0x51A. BSDFCompositeShader's pixel shader ID reads DrawWorld::QTiledLighting()
		// and toggles bit 0x10000, selecting the tiled composite variant that samples t11/t12.
		REL::Relocation<std::uintptr_t> g_interface3DDrawModelRenderSceneDeferredCall{ REL::ID{ 917134, 2222570 }, 0x51A };
		// IDA/Ghidra: Interface3D::Renderer::RenderAll calls RenderPrepassesAndMenus
		// immediately before RenderMain for the selected renderer. Hook this entry so
		// preview scenegraph commits happen at the DrawWorld/Interface3D boundary.
		REL::Relocation<std::uintptr_t> g_renderPrepassesAndMenusTarget{ REL::ID(1189309) };
		REL::Relocation<SetDoTiledLighting_t*> g_setDoTiledLighting{ REL::ID{ 716351, 2318370 } };
		REL::Relocation<QTiledLighting_t*> g_qTiledLighting{ REL::ID{ 1154650, 2318371 } };
		RenderSceneDeferred_t* g_renderSceneDeferred{ nullptr };
		RenderPrepassesAndMenus_t* g_renderPrepassesAndMenus{ nullptr };
		bool g_equipWatcherRegistered{ false };

		class EquipWatcher :
			public RE::BSTEventSink<RE::TESEquipEvent>
		{
		public:
			static EquipWatcher* GetSingleton()
			{
				static EquipWatcher singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESEquipEvent& a_event,
				RE::BSTEventSource<RE::TESEquipEvent>* a_source) override
			{
				(void)a_source;

				const auto* player = RE::PlayerCharacter::GetSingleton();
				if (!player || a_event.actor.get() != player) {
					return RE::BSEventNotifyControl::kContinue;
				}

				const auto* item = RE::TESForm::GetFormByID(a_event.baseObject);
				if (!item || item->IsNot(RE::ENUM_FORM_ID::kARMO, RE::ENUM_FORM_ID::kWEAP)) {
					return RE::BSEventNotifyControl::kContinue;
				}

				Previewer::MarkEquipmentDirty();
				Morph::MarkPrimaryDirty();
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		void RegisterEquipWatcher()
		{
			if (g_equipWatcherRegistered) {
				return;
			}

			auto* source = RE::EquipEventSource::GetSingleton();
			if (!source) {
				REX::WARN("could not acquire TESEquipEvent source");
				return;
			}

			source->RegisterSink(EquipWatcher::GetSingleton());
			g_equipWatcherRegistered = true;
			REX::INFO("Registered TESEquipEvent watcher");
		}

		[[nodiscard]] bool IsTF3DHudOffscreenRender(RE::ShadowSceneNode* a_shadowSceneNode)
		{
			const auto* renderer = Renderer::Get();
			return renderer && a_shadowSceneNode && a_shadowSceneNode == renderer->offscreenSSN.get();
		}

		void HookedRunActorUpdates(RE::ProcessLists* a_processLists, float a_deltaTime, bool a_instant)
		{
			if (g_runActorUpdates) {
				g_runActorUpdates(a_processLists, a_deltaTime, a_instant);
			}

			const auto* const timer = RE::BSTimer::GetSingleton();
			Previewer::Update(timer ? timer->delta : a_deltaTime);
		}

		std::uint32_t HookedProcessGraphEvent(RE::BSAnimationGraphManager* a_manager, const RE::BSFixedString& a_eventName)
		{
			const auto result = g_processGraphEvent ? g_processGraphEvent(a_manager, a_eventName) : 0;
			Animations::ObserveGraphRequest(a_manager, a_eventName.c_str(), result);
			return result;
		}

		void HookedUpdate3DModel(RE::AIProcess* a_process, RE::Actor* a_actor, bool a_queued)
		{
			const auto updateFlags = a_process ? g_getAll3DUpdateFlags(a_process) : 0;
			const bool updateEditorDeadModel = a_process && g_qUpdateEditorDeadActorModel(a_process);

			if (g_update3DModel) {
				g_update3DModel(a_process, a_actor, a_queued);
			}

			if (a_actor && a_actor->IsPlayerRef() && (updateFlags != 0 || updateEditorDeadModel)) {
				Morph::MarkSecondaryDirty();
			}
		}

		void HookedRenderSceneDeferred(
			RE::NiCamera* a_camera,
			RE::BSShaderAccumulator* a_accumulator,
			RE::BSCullingProcess* a_cullingProcess,
			RE::ShadowSceneNode* a_shadowSceneNode,
			std::int32_t a_target,
			std::int32_t a_depth,
			bool a_arg7,
			bool a_arg8)
		{
			if (!g_renderSceneDeferred) {
				return;
			}

			if (IsTF3DHudOffscreenRender(a_shadowSceneNode)) {
				const bool oldTiledLighting = g_qTiledLighting();
				g_setDoTiledLighting(false);
				g_renderSceneDeferred(
					a_camera,
					a_accumulator,
					a_cullingProcess,
					a_shadowSceneNode,
					a_target,
					a_depth,
					a_arg7,
					a_arg8);
				g_setDoTiledLighting(oldTiledLighting);
				return;
			}

			g_renderSceneDeferred(
				a_camera,
				a_accumulator,
				a_cullingProcess,
				a_shadowSceneNode,
				a_target,
				a_depth,
				a_arg7,
				a_arg8);
		}

		void HookedRenderPrepassesAndMenus(RE::Interface3D::Renderer* a_renderer)
		{
			if (a_renderer && a_renderer == Renderer::Get()) {
				Previewer::CommitRenderState();
			}

			if (g_renderPrepassesAndMenus) {
				g_renderPrepassesAndMenus(a_renderer);
			}
		}

		void F4SEMessageHandler(F4SE::MessagingInterface::Message* a_message)
		{
			if (!a_message) {
				return;
			}

			switch (a_message->type) {
			case F4SE::MessagingInterface::kGameDataReady:
				Events::Register();
				RegisterEquipWatcher();
				break;
			case F4SE::MessagingInterface::kPreLoadGame:
				Previewer::Reset();
				break;
			case F4SE::MessagingInterface::kPostLoadGame:
			case F4SE::MessagingInterface::kNewGame:
				LoadConfig();
				Previewer::Reset();
				break;
			default:
				break;
			}
		}
	}

	void RegisterMessaging()
	{
		const auto messaging = F4SE::GetMessagingInterface();
		if (!messaging) {
			REX::WARN("could not acquire F4SE messaging interface");
			return;
		}

		if (!messaging->RegisterListener(F4SEMessageHandler)) {
			REX::WARN("failed to register F4SE message listener");
		}
	}

	void InstallHooks()
	{
		Imgui::InstallHooks();

		auto& trampoline = REL::GetTrampoline();
		g_runActorUpdates = reinterpret_cast<RunActorUpdates_t*>(
			trampoline.write_call<5>(g_runActorUpdatesCall.address(), &HookedRunActorUpdates));
		REX::INFO("Installed frame hook at {:X}", g_runActorUpdatesCall.address());

		g_processGraphEvent = CreateBranchGateway5<ProcessGraphEvent_t*>(
			"BSAnimationGraphManager::ProcessGraphEvent",
			g_processGraphEventTarget,
			5,
			reinterpret_cast<void*>(&HookedProcessGraphEvent));

		g_update3DModel = CreateBranchGateway5<Update3DModel_t*>(
			"AIProcess::Update3dModel",
			g_update3DModelTarget,
			5,
			reinterpret_cast<void*>(&HookedUpdate3DModel));

		g_renderPrepassesAndMenus = CreateBranchGateway5<RenderPrepassesAndMenus_t*>(
			"Interface3D::Renderer::RenderPrepassesAndMenus",
			g_renderPrepassesAndMenusTarget,
			5,
			reinterpret_cast<void*>(&HookedRenderPrepassesAndMenus));

		g_renderSceneDeferred = reinterpret_cast<RenderSceneDeferred_t*>(
			trampoline.write_call<5>(g_interface3DDrawModelRenderSceneDeferredCall.address(), &HookedRenderSceneDeferred));
		REX::INFO(
			"Installed Interface3D deferred render hook at {:X}",
			g_interface3DDrawModelRenderSceneDeferredCall.address());
	}
}
