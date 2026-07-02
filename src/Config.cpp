#include "Config.h"

#include "SimpleIni.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>

namespace TF3DHud
{
	namespace
	{
		Config g_config;
		constexpr std::string_view kLightSectionPrefix{ "Light_" };
		constexpr float kMaxDiffuse = 4.0F;
		constexpr float kMaxDistance = 4096.0F;
		constexpr float kMaxIntensity = 10.0F;

		[[nodiscard]] std::filesystem::path ConfigPath()
		{
			return std::filesystem::path("Data") / "F4SE" / "Plugins" / "TF3DHud.ini";
		}

		[[nodiscard]] float ClampDiffuse(const float a_value)
		{
			return std::clamp(a_value, 0.0F, kMaxDiffuse);
		}

		[[nodiscard]] float ClampDistance(const float a_value)
		{
			return std::clamp(a_value, 0.0F, kMaxDistance);
		}

		[[nodiscard]] float ClampIntensity(const float a_value)
		{
			return std::clamp(a_value, 0.0F, kMaxIntensity);
		}

		[[nodiscard]] float ClampTimeOfDay(const float a_value)
		{
			return std::clamp(a_value, 0.0F, 24.0F);
		}

		void ClampFixedLight(FixedLightSettings& a_settings)
		{
			a_settings.diffuse.r = ClampDiffuse(a_settings.diffuse.r);
			a_settings.diffuse.g = ClampDiffuse(a_settings.diffuse.g);
			a_settings.diffuse.b = ClampDiffuse(a_settings.diffuse.b);
			a_settings.distance = ClampDistance(a_settings.distance);
			a_settings.intensity = ClampIntensity(a_settings.intensity);
		}

		void ClampTimeOfDayLight(TimeOfDayLightSettings& a_settings)
		{
			a_settings.startTimeOfDay = ClampTimeOfDay(a_settings.startTimeOfDay);
			a_settings.endTimeOfDay = ClampTimeOfDay(a_settings.endTimeOfDay);
			ClampFixedLight(a_settings.start);
			ClampFixedLight(a_settings.end);
		}

		[[nodiscard]] const char* TypeName(const LightType a_type)
		{
			switch (a_type) {
			case LightType::kDirectional:
				return "Directional";
			case LightType::kFixed:
				return "Fixed";
			case LightType::kTimeOfDay:
				return "ToD";
			}
			return "Directional";
		}

		void WriteFixedLight(CSimpleIniA& a_ini, const char* a_section, const FixedLightSettings& a_settings, const char* a_prefix)
		{
			const std::string prefix{ a_prefix };
			a_ini.SetDoubleValue(a_section, (prefix + "PositionX").c_str(), a_settings.position.x);
			a_ini.SetDoubleValue(a_section, (prefix + "PositionY").c_str(), a_settings.position.y);
			a_ini.SetDoubleValue(a_section, (prefix + "PositionZ").c_str(), a_settings.position.z);
			a_ini.SetDoubleValue(a_section, (prefix + "DiffuseR").c_str(), a_settings.diffuse.r);
			a_ini.SetDoubleValue(a_section, (prefix + "DiffuseG").c_str(), a_settings.diffuse.g);
			a_ini.SetDoubleValue(a_section, (prefix + "DiffuseB").c_str(), a_settings.diffuse.b);
			a_ini.SetDoubleValue(a_section, (prefix + "Distance").c_str(), a_settings.distance);
			a_ini.SetDoubleValue(a_section, (prefix + "Intensity").c_str(), a_settings.intensity);
		}

		void WriteDefaultLightSections(CSimpleIniA& a_ini)
		{
			for (const auto& light : Lights::DefaultLights()) {
				const auto section = std::string{ kLightSectionPrefix } + light.name;
				a_ini.SetValue(section.c_str(), "Type", TypeName(light.type));
				a_ini.SetBoolValue(section.c_str(), "UseInInterior", light.useInInterior);
				WriteFixedLight(a_ini, section.c_str(), light.fixed, "");
			}
		}

		void WriteDefaults(CSimpleIniA& a_ini)
		{
			a_ini.SetBoolValue("General", "Enabled", g_config.enabled);
			a_ini.SetDoubleValue("View", "FOV", g_config.fov);
			a_ini.SetDoubleValue("View", "PlacementX", g_config.placementX);
			a_ini.SetDoubleValue("View", "PlacementY", g_config.placementY);
			a_ini.SetDoubleValue("View", "CameraDistance", g_config.cameraDistance);
			a_ini.SetDoubleValue("View", "ModelScale", g_config.modelScale);
			a_ini.SetDoubleValue("View", "YawDegrees", g_config.yawDegrees);
			a_ini.SetLongValue("View", "Anchor", g_config.anchor);
			a_ini.SetDoubleValue("ClipRect", "Left", g_config.clipRect.left);
			a_ini.SetDoubleValue("ClipRect", "Top", g_config.clipRect.top);
			a_ini.SetDoubleValue("ClipRect", "Right", g_config.clipRect.right);
			a_ini.SetDoubleValue("ClipRect", "Bottom", g_config.clipRect.bottom);
			a_ini.SetBoolValue("Render", "HideInPowerArmor", g_config.hideInPowerArmor);
			WriteDefaultLightSections(a_ini);
			a_ini.SetValue("UI", "MenuKey", "0xDE");
		}

		[[nodiscard]] LightType ReadLightType(CSimpleIniA& a_ini, const char* a_section, const LightType a_default)
		{
			const auto* value = a_ini.GetValue(a_section, "Type", nullptr);
			if (!value || value[0] == '\0') {
				return a_default;
			}

			const std::string_view type{ value };
			if (type == "Directional" || type == "directional" || type == "0") {
				return LightType::kDirectional;
			}
			if (type == "Fixed" || type == "fixed" || type == "1") {
				return LightType::kFixed;
			}
			if (type == "ToD" || type == "TOD" || type == "tod" || type == "TimeOfDay" || type == "2") {
				return LightType::kTimeOfDay;
			}

			REX::WARN("Invalid light type '{}'; using Directional", value);
			return a_default;
		}

		void ReadFixedLight(CSimpleIniA& a_ini, const char* a_section, FixedLightSettings& a_settings, const char* a_prefix)
		{
			const std::string prefix{ a_prefix };
			a_settings.position.x = static_cast<float>(a_ini.GetDoubleValue(a_section, (prefix + "PositionX").c_str(), a_settings.position.x));
			a_settings.position.y = static_cast<float>(a_ini.GetDoubleValue(a_section, (prefix + "PositionY").c_str(), a_settings.position.y));
			a_settings.position.z = static_cast<float>(a_ini.GetDoubleValue(a_section, (prefix + "PositionZ").c_str(), a_settings.position.z));
			a_settings.diffuse.r = static_cast<float>(a_ini.GetDoubleValue(a_section, (prefix + "DiffuseR").c_str(), a_settings.diffuse.r));
			a_settings.diffuse.g = static_cast<float>(a_ini.GetDoubleValue(a_section, (prefix + "DiffuseG").c_str(), a_settings.diffuse.g));
			a_settings.diffuse.b = static_cast<float>(a_ini.GetDoubleValue(a_section, (prefix + "DiffuseB").c_str(), a_settings.diffuse.b));
			a_settings.distance = static_cast<float>(a_ini.GetDoubleValue(a_section, (prefix + "Distance").c_str(), a_settings.distance));
			a_settings.intensity = static_cast<float>(a_ini.GetDoubleValue(a_section, (prefix + "Intensity").c_str(), a_settings.intensity));
			ClampFixedLight(a_settings);
		}

		[[nodiscard]] LightSettings ReadLight(CSimpleIniA& a_ini, const char* a_section)
		{
			LightSettings light;
			light.name = std::string{ a_section }.substr(kLightSectionPrefix.size());
			light.type = ReadLightType(a_ini, a_section, light.type);
			light.useInInterior = a_ini.GetBoolValue(a_section, "UseInInterior", light.useInInterior);
			ReadFixedLight(a_ini, a_section, light.fixed, "");

			light.timeOfDay.startTimeOfDay = static_cast<float>(a_ini.GetDoubleValue(a_section, "StartToD", light.timeOfDay.startTimeOfDay));
			light.timeOfDay.endTimeOfDay = static_cast<float>(a_ini.GetDoubleValue(a_section, "EndToD", light.timeOfDay.endTimeOfDay));
			ReadFixedLight(a_ini, a_section, light.timeOfDay.start, "Start");
			ReadFixedLight(a_ini, a_section, light.timeOfDay.end, "End");
			ClampTimeOfDayLight(light.timeOfDay);
			return light;
		}

		[[nodiscard]] std::vector<LightSettings> ReadLights(CSimpleIniA& a_ini)
		{
			CSimpleIniA::TNamesDepend sections;
			a_ini.GetAllSections(sections);

			std::vector<LightSettings> lights;
			for (const auto& section : sections) {
				const std::string_view name{ section.pItem };
				if (!name.starts_with(kLightSectionPrefix) || name.size() == kLightSectionPrefix.size()) {
					continue;
				}
				lights.push_back(ReadLight(a_ini, section.pItem));
			}

			if (lights.empty()) {
				lights = Lights::DefaultLights();
			}
			return lights;
		}

		[[nodiscard]] std::uint32_t ReadVirtualKey(CSimpleIniA& a_ini, const char* a_section, const char* a_key, const std::uint32_t a_default)
		{
			const auto* value = a_ini.GetValue(a_section, a_key, nullptr);
			if (!value || value[0] == '\0') {
				return a_default;
			}

			try {
				const auto parsed = std::stoul(value, nullptr, 0);
				if (parsed <= 0xFF) {
					return static_cast<std::uint32_t>(parsed);
				}
			} catch (...) {
			}

			REX::WARN("Invalid virtual key {}.{}='{}'; using 0x{:02X}", a_section, a_key, value, a_default);
			return a_default;
		}
	}

	const Config& GetConfig()
	{
		return g_config;
	}

	void LoadConfig()
	{
		g_config = Config{};

		CSimpleIniA ini;
		ini.SetUnicode();

		const auto path = ConfigPath();
		if (!std::filesystem::exists(path)) {
			std::filesystem::create_directories(path.parent_path());
			WriteDefaults(ini);
			ini.SaveFile(path.string().c_str());
			REX::INFO("Created default config at {}", path.string());
			return;
		}

		if (ini.LoadFile(path.string().c_str()) < 0) {
			REX::WARN("Failed to load {}; using built-in defaults", path.string());
			return;
		}

		if (!ini.GetValue("UI", "MenuKey", nullptr)) {
			ini.SetValue("UI", "MenuKey", "0xDE");
			ini.SaveFile(path.string().c_str());
		}

		g_config.enabled = ini.GetBoolValue("General", "Enabled", g_config.enabled);
		g_config.fov = static_cast<float>(ini.GetDoubleValue("View", "FOV", g_config.fov));
		g_config.placementX = static_cast<float>(ini.GetDoubleValue("View", "PlacementX", g_config.placementX));
		g_config.placementY = static_cast<float>(ini.GetDoubleValue("View", "PlacementY", g_config.placementY));
		g_config.cameraDistance = static_cast<float>(ini.GetDoubleValue("View", "CameraDistance", g_config.cameraDistance));
		g_config.modelScale = static_cast<float>(ini.GetDoubleValue("View", "ModelScale", g_config.modelScale));
		g_config.yawDegrees = static_cast<float>(ini.GetDoubleValue("View", "YawDegrees", g_config.yawDegrees));
		g_config.anchor = static_cast<std::int32_t>(ini.GetLongValue("View", "Anchor", g_config.anchor));
		g_config.clipRect.left = static_cast<float>(ini.GetDoubleValue("ClipRect", "Left", g_config.clipRect.left));
		g_config.clipRect.top = static_cast<float>(ini.GetDoubleValue("ClipRect", "Top", g_config.clipRect.top));
		g_config.clipRect.right = static_cast<float>(ini.GetDoubleValue("ClipRect", "Right", g_config.clipRect.right));
		g_config.clipRect.bottom = static_cast<float>(ini.GetDoubleValue("ClipRect", "Bottom", g_config.clipRect.bottom));
		g_config.hideInPowerArmor = ini.GetBoolValue("Render", "HideInPowerArmor", g_config.hideInPowerArmor);
		g_config.uiMenuKey = ReadVirtualKey(ini, "UI", "MenuKey", g_config.uiMenuKey);
		g_config.lights = ReadLights(ini);
		g_config.fov = std::clamp(g_config.fov, 10.0F, 120.0F);
		g_config.modelScale = std::clamp(g_config.modelScale, 0.01F, 10.0F);
		g_config.anchor = std::clamp(g_config.anchor, 1, 9);

		REX::INFO(
			"Loaded config: enabled={}, fov={}, placement=({}, {}), cameraDistance={}, modelScale={}, yawDegrees={}, anchor={}, clipRect=({}, {}, {}, {}), hideInPowerArmor={}, uiMenuKey=0x{:02X}, lights={}",
			g_config.enabled,
			g_config.fov,
			g_config.placementX,
			g_config.placementY,
			g_config.cameraDistance,
			g_config.modelScale,
			g_config.yawDegrees,
			g_config.anchor,
			g_config.clipRect.left,
			g_config.clipRect.top,
			g_config.clipRect.right,
			g_config.clipRect.bottom,
			g_config.hideInPowerArmor,
			g_config.uiMenuKey,
			g_config.lights.size());
	}
}
