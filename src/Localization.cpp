#include "Localization.h"

#include "Config.h"

#include "RE/S/Setting.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

namespace TF3DHud::Localization
{
	namespace
	{
		using LanguageStrings = std::unordered_map<std::string, std::string>;

		std::unordered_map<std::string, LanguageStrings> g_strings;
		LanguageStrings g_missingStrings;
		std::string g_currentLanguage{ "en" };
		bool g_initialized{ false };

		[[nodiscard]] std::filesystem::path PluginDataPath()
		{
			return std::filesystem::path("Data") / "F4SE" / "Plugins" / "TF3DHUD";
		}

		[[nodiscard]] std::string NormalizeLanguageCode(std::string_view a_language)
		{
			std::string language{ a_language };
			std::ranges::transform(language, language.begin(), [](const unsigned char a_ch) {
				return static_cast<char>(std::tolower(a_ch));
			});
			std::ranges::replace(language, '-', '_');
			std::ranges::replace(language, ' ', '_');

			if (language == "english") {
				return "en";
			}
			if (language == "ja" || language == "japanese") {
				return "jp";
			}
			if (language == "kr" || language == "korean") {
				return "ko";
			}
			if (language == "zh_hans" || language == "zh_cn" || language == "cn" || language == "chinese_simplified") {
				return "zh_cn";
			}
			if (language == "zh_hant" || language == "zh_tw" || language == "tw" || language == "chinese_traditional") {
				return "zh_tw";
			}
			return language.empty() ? "en" : language;
		}

		void AddBuiltInEnglish()
		{
			auto& en = g_strings["en"];
			const std::pair<const char*, const char*> values[] = {
				{ "menu.file", "File" },
				{ "menu.save", "Save" },
				{ "menu.reload", "Reload" },
				{ "menu.language", "Language" },
				{ "modal.save_changes.title", "Save changes?" },
				{ "modal.save_changes.body", "Save changes to TF3DHud.ini?" },
				{ "button.save", "Save" },
				{ "button.close", "Close" },
				{ "button.cancel", "Cancel" },
				{ "button.edit", "E" },
				{ "button.add", "Add" },
				{ "button.delete", "Delete" },
				{ "button.previous", "<" },
				{ "button.next", ">" },
				{ "button.dump_hair_skin_bones", "Dump hair skin bones" },
				{ "tab.layout", "Layout" },
				{ "tab.animation", "Animation" },
				{ "tab.equipment", "Equipment" },
				{ "tab.light", "Light" },
				{ "tab.debug", "Debug" },
				{ "section.anchor", "Anchor" },
				{ "section.anchor.hint", "Select which screen point the preview placement offsets are measured from." },
				{ "section.view", "View" },
				{ "section.view.hint", "Adjust the offscreen camera and model placement used by the HUD preview." },
				{ "section.camera", "Camera" },
				{ "section.camera.hint", "Choose the actor bone used for framing and whether the preview follows it at runtime." },
				{ "section.follow_axis", "Follow Axis" },
				{ "section.follow_axis.hint", "Limit camera follow correction to selected local axes." },
				{ "section.clip_rect", "ClipRect" },
				{ "section.clip_rect.hint", "Crop the display mesh used to present the offscreen preview." },
				{ "section.general", "General" },
				{ "section.general.hint", "Toggle the preview, power-armor suppression, and anti-aliasing behavior." },
				{ "section.live_animation", "Live Animation" },
				{ "section.live_animation.hint", "Use the live player's animation graph as the source for the preview." },
				{ "section.mirror_events", "Mirror Events / Graph Variables" },
				{ "section.mirror_events.hint", "Choose which live animation events and graph variables are mirrored into the preview graph." },
				{ "section.idle_animation", "Idle Animation" },
				{ "section.idle_animation.hint", "Pick a dynamic activation idle when live animation is disabled." },
				{ "section.equipment_slots", "Equipment Slots" },
				{ "section.equipment_slots.hint", "Choose which equipped armor slots are mirrored onto the preview actor." },
				{ "section.equipped_armor", "Equipped Armor" },
				{ "section.equipped_armor.hint", "Shows the player's currently equipped armor after slot filtering." },
				{ "section.lights", "Lights" },
				{ "section.lights.hint", "Configure lights attached to the offscreen renderer." },
				{ "section.debug_graph", "Graph Debug" },
				{ "section.debug_graph.hint", "Diagnostic animation graph state captured from the live and preview graphs." },
				{ "section.debug_facegen", "FaceGen Debug" },
				{ "section.debug_facegen.hint", "Diagnostic headpart, geometry, skin-bone, slider, and tint state from preview rebuilds." },
				{ "anchor.bottom_left", "bottom left" },
				{ "anchor.bottom_center", "bottom center" },
				{ "anchor.bottom_right", "bottom right" },
				{ "anchor.middle_left", "middle left" },
				{ "anchor.middle_center", "middle center" },
				{ "anchor.middle_right", "middle right" },
				{ "anchor.top_left", "top left" },
				{ "anchor.top_center", "top center" },
				{ "anchor.top_right", "top right" },
				{ "anchor.unknown", "unknown" },
				{ "field.fov", "FOV" },
				{ "field.placement_x", "PlacementX" },
				{ "field.placement_y", "PlacementY" },
				{ "field.camera_distance", "CameraDistance" },
				{ "field.model_scale", "ModelScale" },
				{ "field.yaw_degrees", "YawDegrees" },
				{ "field.target", "Target" },
				{ "field.follow", "Follow" },
				{ "field.x", "X" },
				{ "field.y", "Y" },
				{ "field.z", "Z" },
				{ "field.left", "Left" },
				{ "field.right", "Right" },
				{ "field.top", "Top" },
				{ "field.bottom", "Bottom" },
				{ "field.enabled", "Enabled" },
				{ "field.hide_in_power_armor", "HideInPowerArmor" },
				{ "field.anti_aliasing", "Anti-Aliasing" },
				{ "field.use_live_animation", "Use Live Animation" },
				{ "field.locomotion", "Locomotion" },
				{ "field.sneak", "Sneak" },
				{ "field.jump", "Jump" },
				{ "field.weapon_fire", "Weapon Fire" },
				{ "field.weapon_reload", "Weapon Reload" },
				{ "field.melee", "Melee" },
				{ "field.throw", "Throw" },
				{ "field.file", "File" },
				{ "field.idle_animation", "Idle Animation" },
				{ "field.hide_weapon", "Hide Weapon" },
				{ "field.sheathe_weapon", "Sheathe Weapon" },
				{ "field.name", "Name" },
				{ "field.type", "Type" },
				{ "field.use_in_interior", "UseInInterior" },
				{ "field.position_x", "PositionX" },
				{ "field.position_y", "PositionY" },
				{ "field.position_z", "PositionZ" },
				{ "field.diffuse", "Diffuse" },
				{ "field.distance", "Distance" },
				{ "field.intensity", "Intensity" },
				{ "field.start_tod", "StartToD" },
				{ "field.end_tod", "EndToD" },
				{ "field.start", "Start" },
				{ "field.end", "End" },
				{ "camera.head", "Head" },
				{ "camera.chest", "Chest" },
				{ "camera.pelvis", "Pelvis" },
				{ "camera.root", "Root" },
				{ "light.directional", "Directional" },
				{ "light.fixed", "Fixed" },
				{ "light.tod", "ToD" },
				{ "state.none", "(none)" },
				{ "state.null", "(null)" },
				{ "state.empty", "(empty)" },
				{ "state.unnamed", "(unnamed)" },
				{ "state.no_editor_id", "<no editor id>" },
				{ "state.no_data", "-" },
				{ "state.data_unavailable", "data unavailable" },
				{ "state.true", "true" },
				{ "state.false", "false" },
				{ "state.yes", "yes" },
				{ "state.no", "no" },
				{ "equipment.player_unavailable", "Player unavailable" },
				{ "equipment.no_equipped_armor", "No equipped armor" },
				{ "debug.tab.graph_info", "Graph Info" },
				{ "debug.tab.subgraphs", "Subgraphs" },
				{ "debug.tab.active_nodes", "Active Nodes" },
				{ "debug.tab.facegen", "FaceGen" },
				{ "debug.active_clip.none", "Active clip: none" },
				{ "debug.active_clip", "Active clip: %s" },
				{ "debug.subgraph_slot", "Subgraph: slot %u root=%p" },
				{ "debug.subgraph_root", "Subgraph: root graph root=%p" },
				{ "debug.path", "Path: %s" },
				{ "debug.time_local", "Time (+0x140 localTime): %.4f / %.4f" },
				{ "debug.control_time", "Control +0x10: %.4f" },
				{ "debug.mode_fraction", "Mode (+0xBE): %u  User fraction (+0xB8): %.4f" },
				{ "debug.time_unavailable", "Time: unavailable" },
				{ "debug.list_count", "%s count=%u shown=%u:" },
				{ "debug.list_count_plain", "%s count=%u shown=%u" },
				{ "debug.no_graph_swap_data", "No graph swap data attached." },
				{ "debug.default_handles", "default handles" },
				{ "debug.default_ids", "default ids" },
				{ "debug.weapon_handles", "weapon handles" },
				{ "debug.weapon_ids", "weapon ids" },
				{ "debug.file_arrays", "file arrays" },
				{ "debug.slot", "slot %u" },
				{ "debug.data_160", "data+0x160" },
				{ "debug.data_178", "data+0x178" },
				{ "debug.last_diagnostic", "Last diagnostic: %s" },
				{ "debug.project", "Project: %s" },
				{ "debug.no_facegen", "No FaceGen debug data." },
				{ "debug.used_headparts", "Used headparts" },
				{ "debug.no_hdpt", "No used HDPT data." },
				{ "debug.preview_face_geometries", "Preview face geometries" },
				{ "debug.no_face_geometry", "No preview face geometry data." },
				{ "debug.hair_skin_bones", "Hair skin bones" },
				{ "debug.no_hair_skin_bones", "No hair skin-bone data." },
				{ "debug.sliders_tints", "Sliders and tints" },
				{ "debug.no_facegen_slider", "No FaceGen slider data." },
				{ "debug.clip", "clip" },
				{ "debug.node", "node" },
				{ "table.slot", "Slot" },
				{ "table.index", "#" },
				{ "table.state", "State" },
				{ "table.handle", "Handle" },
				{ "table.shared", "Shared" },
				{ "table.root", "Root" },
				{ "table.root_name", "Root Name" },
				{ "table.use", "Use" },
				{ "table.remove", "Remove" },
				{ "table.data_160", "data+160" },
				{ "table.data_178", "data+178" },
				{ "table.hdpt", "HDPT" },
				{ "table.form", "Form" },
				{ "table.ptr", "Ptr" },
				{ "table.name", "Name" },
				{ "table.model", "Model" },
				{ "table.geometry", "Geometry" },
				{ "table.geometry_ptr", "Geometry Ptr" },
				{ "table.parent", "Parent" },
				{ "table.parent_ptr", "Parent Ptr" },
				{ "table.src", "Src" },
				{ "table.bone", "Bone" },
				{ "table.bone_ptr", "Bone Ptr" },
				{ "table.local", "Local" },
				{ "table.world", "World" },
				{ "table.category", "Category" },
				{ "table.live", "Live" },
				{ "table.preview", "Preview" },
				{ "table.entry", "Entry" },
				{ "table.node", "Node" },
				{ "table.type", "Type" },
				{ "table.sg", "SG" },
				{ "table.clip_path", "Clip Path" },
				{ "table.time", "Time" },
				{ "table.ctrl", "Ctrl" },
				{ "table.mode", "Mode" },
				{ "table.frac", "Frac" },
				{ "table.behavior", "Behavior" }
			};

			for (const auto& [key, text] : values) {
				en.insert_or_assign(key, text);
			}
		}

		void LoadTranslations()
		{
			const auto path = PluginDataPath() / "translations";
			if (!std::filesystem::exists(path)) {
				REX::INFO("No translation directory at {}", path.string());
				return;
			}

			for (const auto& entry : std::filesystem::directory_iterator(path)) {
				if (!entry.is_regular_file() || entry.path().extension() != ".json") {
					continue;
				}

				try {
					std::ifstream reader(entry.path());
					nlohmann::json data;
					reader >> data;
					for (auto langIt = data.begin(); langIt != data.end(); ++langIt) {
						if (!langIt.value().is_object()) {
							continue;
						}

						auto& strings = g_strings[NormalizeLanguageCode(langIt.key())];
						for (auto textIt = langIt.value().begin(); textIt != langIt.value().end(); ++textIt) {
							if (textIt.value().is_string()) {
								strings.insert_or_assign(textIt.key(), textIt.value().get<std::string>());
							}
						}
					}
				} catch (const std::exception& e) {
					REX::WARN("Failed to load translation {}: {}", entry.path().string(), e.what());
				}
			}
		}

		[[nodiscard]] const char* LookupText(std::string_view a_language, std::string_view a_key)
		{
			if (const auto langIt = g_strings.find(std::string{ a_language }); langIt != g_strings.end()) {
				if (const auto textIt = langIt->second.find(std::string{ a_key }); textIt != langIt->second.end()) {
					return textIt->second.c_str();
				}
			}

			if (const auto enIt = g_strings.find("en"); enIt != g_strings.end()) {
				if (const auto textIt = enIt->second.find(std::string{ a_key }); textIt != enIt->second.end()) {
					return textIt->second.c_str();
				}
			}

			auto [it, inserted] = g_missingStrings.emplace(std::string{ a_key }, std::string{ a_key });
			return it->second.c_str();
		}

		void AddFontIfPresent(ImGuiIO& a_io, const std::filesystem::path& a_path, const float a_size, ImFontConfig* a_config, const ImWchar* a_ranges)
		{
			if (!std::filesystem::exists(a_path)) {
				REX::WARN("ImGui font not found: {}", a_path.string());
				return;
			}

			if (!a_io.Fonts->AddFontFromFileTTF(a_path.string().c_str(), a_size, a_config, a_ranges)) {
				REX::WARN("Failed to load ImGui font: {}", a_path.string());
			}
		}
	}

	void Initialize()
	{
		if (g_initialized) {
			return;
		}

		AddBuiltInEnglish();
		LoadTranslations();
		g_currentLanguage = NormalizeLanguageCode(GetConfig().language);
		if (!g_strings.contains(g_currentLanguage)) {
			g_currentLanguage = g_strings.contains("en") ? "en" : g_strings.begin()->first;
			GetMutableConfig().language = g_currentLanguage;
		}
		g_initialized = true;
	}

	void LoadFonts(ImGuiIO& a_io)
	{
		ImVector<ImWchar> ranges;
		ImFontGlyphRangesBuilder builder;
		builder.AddRanges(a_io.Fonts->GetGlyphRangesKorean());
		builder.AddRanges(a_io.Fonts->GetGlyphRangesDefault());
		builder.AddRanges(a_io.Fonts->GetGlyphRangesGreek());
		builder.AddRanges(a_io.Fonts->GetGlyphRangesCyrillic());
		builder.BuildRanges(&ranges);

		const auto fontPath = PluginDataPath() / "fonts";
		ImFontConfig mergeConfig;
		mergeConfig.MergeMode = true;

		AddFontIfPresent(a_io, fontPath / "NotoSansKR-Light.ttf", 20.0F, nullptr, ranges.Data);
		if (a_io.Fonts->Fonts.empty()) {
			a_io.Fonts->AddFontDefault();
		}
		AddFontIfPresent(a_io, fontPath / "NotoSansJP-Light.ttf", 20.0F, std::addressof(mergeConfig), a_io.Fonts->GetGlyphRangesJapanese());
		AddFontIfPresent(a_io, fontPath / "NotoSansSC-Light.ttf", 20.0F, std::addressof(mergeConfig), a_io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
		AddFontIfPresent(a_io, fontPath / "NotoSansTC-Light.ttf", 20.0F, std::addressof(mergeConfig), a_io.Fonts->GetGlyphRangesChineseFull());
		a_io.Fonts->Build();
	}

	const char* GetText(std::string_view a_key)
	{
		Initialize();
		return LookupText(g_currentLanguage, a_key);
	}

	std::string GetString(std::string_view a_key)
	{
		return GetText(a_key);
	}

	const std::string& GetCurrentLanguage()
	{
		Initialize();
		return g_currentLanguage;
	}

	std::vector<std::string> GetSupportedLanguages()
	{
		Initialize();
		std::vector<std::string> languages;
		languages.reserve(g_strings.size());
		for (const auto& [language, strings] : g_strings) {
			if (!strings.empty()) {
				languages.push_back(language);
			}
		}
		std::ranges::sort(languages);
		return languages;
	}

	bool SetCurrentLanguage(std::string_view a_language)
	{
		Initialize();
		const auto normalized = NormalizeLanguageCode(a_language);
		if (!g_strings.contains(normalized)) {
			return false;
		}

		g_currentLanguage = normalized;
		GetMutableConfig().language = normalized;
		return SaveLanguageConfig(normalized);
	}
}
