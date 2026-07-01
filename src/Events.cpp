#include "Events.h"

#include "Previewer.h"

#include "RE/B/BSTEvent.h"
#include "RE/M/MenuOpenCloseEvent.h"
#include "RE/U/UI.h"

#include <string_view>

namespace TF3DHud::Events
{
	namespace
	{
		constexpr std::string_view kLooksMenuName{ "LooksMenu" };
		bool g_registered{ false };

		[[nodiscard]] bool IsMenu(const RE::BSFixedString& a_menuName, const std::string_view a_expected)
		{
			const auto* name = a_menuName.c_str();
			return name && std::string_view{ name } == a_expected;
		}

		class MenuOpenCloseSink final :
			public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent& a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!IsMenu(a_event.menuName, kLooksMenuName)) {
					return RE::BSEventNotifyControl::kContinue;
				}

				if (a_event.opening) {
					Previewer::SuspendForLooksMenu();
				} else {
					Previewer::ResumeAfterLooksMenu();
				}

				return RE::BSEventNotifyControl::kContinue;
			}
		};

		MenuOpenCloseSink g_menuOpenCloseSink;
	}

	void Register()
	{
		if (g_registered) {
			return;
		}

		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			REX::WARN("could not acquire UI singleton for MenuOpenCloseEvent listener");
			return;
		}

		ui->RegisterSink<RE::MenuOpenCloseEvent>(&g_menuOpenCloseSink);
		g_registered = true;
		REX::INFO("Registered MenuOpenCloseEvent listener for LooksMenu");
	}
}
