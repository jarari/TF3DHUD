#pragma once

#include <string>
#include <string_view>
#include <vector>

struct ImGuiIO;

namespace TF3DHud::Localization
{
	void Initialize();
	void LoadFonts(ImGuiIO& a_io);

	[[nodiscard]] const char* GetText(std::string_view a_key);
	[[nodiscard]] std::string GetString(std::string_view a_key);
	[[nodiscard]] const std::string& GetCurrentLanguage();
	[[nodiscard]] std::vector<std::string> GetSupportedLanguages();

	bool SetCurrentLanguage(std::string_view a_language);
}
