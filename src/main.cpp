#include "Config.h"
#include "Hooks.h"
#include "version.h"

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se, {
		.log = true,
		.logName = "TF3DHUD",
		.trampoline = true,
		.trampolineSize = 128
	});

	TF3DHud::LoadConfig();
	TF3DHud::RegisterMessaging();
	TF3DHud::InstallHooks();

	return true;
}

extern "C"
{
	F4SE_EXPORT bool F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
	{
		a_info->name = Version::PROJECT.data();
		a_info->infoVersion = F4SE::PluginInfo::kVersion;
		a_info->version = Version::MAJOR;

		if (a_f4se->IsEditor()) {
			return false;
		}
		return true;
	}
}
