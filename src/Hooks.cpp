#include "Hooks.h"

#include "Config.h"
#include "Previewer.h"
#include "Utils.h"

#include <cstdint>

namespace TF3DHud
{
	namespace
	{
		using RunActorUpdates_t = void(RE::ProcessLists*, float, bool);
		using DoUpdate3DModel_t = void(RE::AIProcess*, RE::Actor*, std::uint16_t);

		REL::Relocation<std::uintptr_t> g_runActorUpdatesCall{ REL::ID(556439), 0x17 };
		REL::Relocation<std::uintptr_t> g_doUpdate3DModelTarget{ REL::ID(114457) };
		RunActorUpdates_t* g_runActorUpdates{ nullptr };
		DoUpdate3DModel_t* g_doUpdate3DModel{ nullptr };
		bool g_loggedSuppressedDoUpdate3D{ false };

		void HookedRunActorUpdates(RE::ProcessLists* a_processLists, float a_deltaTime, bool a_instant)
		{
			if (g_runActorUpdates) {
				g_runActorUpdates(a_processLists, a_deltaTime, a_instant);
			}

			Previewer::Update(a_deltaTime);
		}

		void HookedDoUpdate3DModel(RE::AIProcess* a_process, RE::Actor* a_actor, std::uint16_t a_flags)
		{
			if (Previewer::IsPreviewActor(a_actor) && !Previewer::IsBuildActive()) {
				if (!g_loggedSuppressedDoUpdate3D) {
					REX::WARN(
						"Suppressed AIProcess::DoUpdate3dModel for contained TF3DHud preview actor {:X}",
						reinterpret_cast<std::uintptr_t>(a_actor));
					g_loggedSuppressedDoUpdate3D = true;
				}
				return;
			}

			g_doUpdate3DModel(a_process, a_actor, a_flags);
		}

		void F4SEMessageHandler(F4SE::MessagingInterface::Message* a_message)
		{
			if (!a_message) {
				return;
			}

			switch (a_message->type) {
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
			REX::WARN("TF3DHud V1 could not acquire F4SE messaging interface");
			return;
		}

		if (!messaging->RegisterListener(F4SEMessageHandler)) {
			REX::WARN("TF3DHud V1 failed to register F4SE message listener");
		}
	}

	void InstallHooks()
	{
		auto& trampoline = REL::GetTrampoline();
		g_runActorUpdates = reinterpret_cast<RunActorUpdates_t*>(
			trampoline.write_call<5>(g_runActorUpdatesCall.address(), &HookedRunActorUpdates));
		REX::INFO("Installed TF3DHud V1 frame hook at {:X}", g_runActorUpdatesCall.address());
		g_doUpdate3DModel = CreateBranchGateway5<DoUpdate3DModel_t*>(
			"TF3DHud V1 AIProcess::DoUpdate3dModel containment",
			g_doUpdate3DModelTarget,
			0x6,
			reinterpret_cast<void*>(&HookedDoUpdate3DModel));
		REX::INFO("Installed TF3DHud V1 preview actor containment hook at {:X}", g_doUpdate3DModelTarget.address());
	}
}
