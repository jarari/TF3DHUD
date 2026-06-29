#include "Hooks.h"

#include "Config.h"
#include "Events.h"
#include "Previewer.h"

#include <cstdint>

namespace TF3DHud
{
	namespace
	{
		using RunActorUpdates_t = void(RE::ProcessLists*, float, bool);

		REL::Relocation<std::uintptr_t> g_runActorUpdatesCall{ REL::ID(556439), 0x17 };
		RunActorUpdates_t* g_runActorUpdates{ nullptr };

		void HookedRunActorUpdates(RE::ProcessLists* a_processLists, float a_deltaTime, bool a_instant)
		{
			if (g_runActorUpdates) {
				g_runActorUpdates(a_processLists, a_deltaTime, a_instant);
			}

			Previewer::Update(a_deltaTime);
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
	}
}
