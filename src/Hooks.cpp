#include "Hooks.h"

#include "Config.h"
#include "Events.h"
#include "Previewer.h"
#include "Renderer.h"

#include "RE/I/Interface3D.h"

#include <cstdint>

namespace TF3DHud
{
	namespace
	{
		using RunActorUpdates_t = void(RE::ProcessLists*, float, bool);
		using RenderSceneDeferred_t = void(
			RE::NiCamera*,
			RE::BSShaderAccumulator*,
			RE::BSCullingProcess*,
			RE::ShadowSceneNode*,
			std::int32_t,
			std::int32_t,
			bool,
			bool);
		using SetDoTiledLighting_t = void(bool);
		using QTiledLighting_t = bool();

		REL::Relocation<std::uintptr_t> g_runActorUpdatesCall{ REL::ID(556439), 0x17 };
		RunActorUpdates_t* g_runActorUpdates{ nullptr };
		// IDA: Interface3D::Renderer::DrawModel calls BSShaderUtil::RenderSceneDeferred
		// at +0x51A. BSDFCompositeShader's pixel shader ID reads DrawWorld::QTiledLighting()
		// and toggles bit 0x10000, selecting the tiled composite variant that samples t11/t12.
		REL::Relocation<std::uintptr_t> g_interface3DDrawModelRenderSceneDeferredCall{ REL::ID{ 917134, 2222570 }, 0x51A };
		REL::Relocation<SetDoTiledLighting_t*> g_setDoTiledLighting{ REL::ID{ 716351, 2318370 } };
		REL::Relocation<QTiledLighting_t*> g_qTiledLighting{ REL::ID{ 1154650, 2318371 } };
		RenderSceneDeferred_t* g_renderSceneDeferred{ nullptr };

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

			Previewer::Update(a_deltaTime);
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

		void F4SEMessageHandler(F4SE::MessagingInterface::Message* a_message)
		{
			if (!a_message) {
				return;
			}

			switch (a_message->type) {
			case F4SE::MessagingInterface::kGameDataReady:
				Events::Register();
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
		auto& trampoline = REL::GetTrampoline();
		g_runActorUpdates = reinterpret_cast<RunActorUpdates_t*>(
			trampoline.write_call<5>(g_runActorUpdatesCall.address(), &HookedRunActorUpdates));
		REX::INFO("Installed frame hook at {:X}", g_runActorUpdatesCall.address());

		g_renderSceneDeferred = reinterpret_cast<RenderSceneDeferred_t*>(
			trampoline.write_call<5>(g_interface3DDrawModelRenderSceneDeferredCall.address(), &HookedRenderSceneDeferred));
		REX::INFO(
			"Installed Interface3D deferred render hook at {:X}",
			g_interface3DDrawModelRenderSceneDeferredCall.address());
	}
}
