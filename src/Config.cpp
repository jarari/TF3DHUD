#include "Config.h"

#include "SimpleIni.h"

#include <algorithm>
#include <filesystem>

namespace TF3DHud
{
	namespace
	{
		Config g_config;
		constexpr float kMaxVanillaInterfaceSpecular = 4096.0F;

		[[nodiscard]] std::filesystem::path ConfigPath()
		{
			return std::filesystem::path("Data") / "F4SE" / "Plugins" / "TF3DHud.ini";
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
			a_ini.SetLongValue("View", "LightingType", std::to_underlying(g_config.lighting));
			a_ini.SetDoubleValue("ClipRect", "Left", g_config.clipRect.left);
			a_ini.SetDoubleValue("ClipRect", "Top", g_config.clipRect.top);
			a_ini.SetDoubleValue("ClipRect", "Right", g_config.clipRect.right);
			a_ini.SetDoubleValue("ClipRect", "Bottom", g_config.clipRect.bottom);
			a_ini.SetBoolValue("Render", "HideInPowerArmor", g_config.hideInPowerArmor);
			a_ini.SetDoubleValue("Lighting", "PositionX", g_config.light.positionX);
			a_ini.SetDoubleValue("Lighting", "PositionY", g_config.light.positionY);
			a_ini.SetDoubleValue("Lighting", "PositionZ", g_config.light.positionZ);
			a_ini.SetDoubleValue("Lighting", "DiffuseR", g_config.light.diffuseR);
			a_ini.SetDoubleValue("Lighting", "DiffuseG", g_config.light.diffuseG);
			a_ini.SetDoubleValue("Lighting", "DiffuseB", g_config.light.diffuseB);
			a_ini.SetDoubleValue("Lighting", "SpecularR", g_config.light.specularR);
			a_ini.SetDoubleValue("Lighting", "SpecularG", g_config.light.specularG);
			a_ini.SetDoubleValue("Lighting", "SpecularB", g_config.light.specularB);
			a_ini.SetDoubleValue("Lighting", "Intensity", g_config.light.intensity);

			a_ini.SetDoubleValue("NightLighting", "PositionX", g_config.nightLight.positionX);
			a_ini.SetDoubleValue("NightLighting", "PositionY", g_config.nightLight.positionY);
			a_ini.SetDoubleValue("NightLighting", "PositionZ", g_config.nightLight.positionZ);
			a_ini.SetDoubleValue("NightLighting", "DiffuseR", g_config.nightLight.diffuseR);
			a_ini.SetDoubleValue("NightLighting", "DiffuseG", g_config.nightLight.diffuseG);
			a_ini.SetDoubleValue("NightLighting", "DiffuseB", g_config.nightLight.diffuseB);
			a_ini.SetDoubleValue("NightLighting", "SpecularR", g_config.nightLight.specularR);
			a_ini.SetDoubleValue("NightLighting", "SpecularG", g_config.nightLight.specularG);
			a_ini.SetDoubleValue("NightLighting", "SpecularB", g_config.nightLight.specularB);
			a_ini.SetDoubleValue("NightLighting", "Intensity", g_config.nightLight.intensity);
		}

		void ReadLightSettings(CSimpleIniA& a_ini, const char* a_section, LightSettings& a_settings)
		{
			a_settings.positionX = static_cast<float>(a_ini.GetDoubleValue(a_section, "PositionX", a_settings.positionX));
			a_settings.positionY = static_cast<float>(a_ini.GetDoubleValue(a_section, "PositionY", a_settings.positionY));
			a_settings.positionZ = static_cast<float>(a_ini.GetDoubleValue(a_section, "PositionZ", a_settings.positionZ));
			a_settings.diffuseR = static_cast<float>(a_ini.GetDoubleValue(a_section, "DiffuseR", a_settings.diffuseR));
			a_settings.diffuseG = static_cast<float>(a_ini.GetDoubleValue(a_section, "DiffuseG", a_settings.diffuseG));
			a_settings.diffuseB = static_cast<float>(a_ini.GetDoubleValue(a_section, "DiffuseB", a_settings.diffuseB));
			a_settings.specularR = static_cast<float>(a_ini.GetDoubleValue(a_section, "SpecularR", a_settings.specularR));
			a_settings.specularG = static_cast<float>(a_ini.GetDoubleValue(a_section, "SpecularG", a_settings.specularG));
			a_settings.specularB = static_cast<float>(a_ini.GetDoubleValue(a_section, "SpecularB", a_settings.specularB));
			a_settings.intensity = static_cast<float>(a_ini.GetDoubleValue(a_section, "Intensity", a_settings.intensity));

			a_settings.diffuseR = std::clamp(a_settings.diffuseR, 0.0F, 4.0F);
			a_settings.diffuseG = std::clamp(a_settings.diffuseG, 0.0F, 4.0F);
			a_settings.diffuseB = std::clamp(a_settings.diffuseB, 0.0F, 4.0F);
			a_settings.specularR = std::clamp(a_settings.specularR, 0.0F, kMaxVanillaInterfaceSpecular);
			a_settings.specularG = std::clamp(a_settings.specularG, 0.0F, kMaxVanillaInterfaceSpecular);
			a_settings.specularB = std::clamp(a_settings.specularB, 0.0F, kMaxVanillaInterfaceSpecular);
			a_settings.intensity = std::clamp(a_settings.intensity, 0.0F, 10.0F);
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

		g_config.enabled = ini.GetBoolValue("General", "Enabled", g_config.enabled);
		g_config.fov = static_cast<float>(ini.GetDoubleValue("View", "FOV", g_config.fov));
		g_config.placementX = static_cast<float>(ini.GetDoubleValue("View", "PlacementX", g_config.placementX));
		g_config.placementY = static_cast<float>(ini.GetDoubleValue("View", "PlacementY", g_config.placementY));
		g_config.cameraDistance = static_cast<float>(ini.GetDoubleValue("View", "CameraDistance", g_config.cameraDistance));
		g_config.modelScale = static_cast<float>(ini.GetDoubleValue("View", "ModelScale", g_config.modelScale));
		g_config.yawDegrees = static_cast<float>(ini.GetDoubleValue("View", "YawDegrees", g_config.yawDegrees));
		g_config.anchor = static_cast<std::int32_t>(ini.GetLongValue("View", "Anchor", g_config.anchor));
		g_config.lighting = static_cast<LightingType>(std::clamp<long>(
			ini.GetLongValue("View", "LightingType", std::to_underlying(g_config.lighting)),
			std::to_underlying(LightingType::kWorldDirectional),
			std::to_underlying(LightingType::kFakePointAdaptiveTime)));
		g_config.clipRect.left = static_cast<float>(ini.GetDoubleValue("ClipRect", "Left", g_config.clipRect.left));
		g_config.clipRect.top = static_cast<float>(ini.GetDoubleValue("ClipRect", "Top", g_config.clipRect.top));
		g_config.clipRect.right = static_cast<float>(ini.GetDoubleValue("ClipRect", "Right", g_config.clipRect.right));
		g_config.clipRect.bottom = static_cast<float>(ini.GetDoubleValue("ClipRect", "Bottom", g_config.clipRect.bottom));
		g_config.hideInPowerArmor = ini.GetBoolValue("Render", "HideInPowerArmor", g_config.hideInPowerArmor);
		ReadLightSettings(ini, "Lighting", g_config.light);
		ReadLightSettings(ini, "NightLighting", g_config.nightLight);
		g_config.fov = std::clamp(g_config.fov, 10.0F, 120.0F);
		g_config.modelScale = std::clamp(g_config.modelScale, 0.01F, 10.0F);
		g_config.anchor = std::clamp(g_config.anchor, 1, 9);

		REX::INFO(
			"Loaded config: enabled={}, fov={}, placement=({}, {}), cameraDistance={}, modelScale={}, yawDegrees={}, anchor={}, lighting={}, clipRect=({}, {}, {}, {}), hideInPowerArmor={}, lightPos=({}, {}, {}), lightColor=({}, {}, {}), lightSpec=({}, {}, {}), lightIntensity={}, nightLightPos=({}, {}, {}), nightLightColor=({}, {}, {}), nightLightSpec=({}, {}, {}), nightLightIntensity={}",
			g_config.enabled,
			g_config.fov,
			g_config.placementX,
			g_config.placementY,
			g_config.cameraDistance,
			g_config.modelScale,
			g_config.yawDegrees,
			g_config.anchor,
			std::to_underlying(g_config.lighting),
			g_config.clipRect.left,
			g_config.clipRect.top,
			g_config.clipRect.right,
			g_config.clipRect.bottom,
			g_config.hideInPowerArmor,
			g_config.light.positionX,
			g_config.light.positionY,
			g_config.light.positionZ,
			g_config.light.diffuseR,
			g_config.light.diffuseG,
			g_config.light.diffuseB,
			g_config.light.specularR,
			g_config.light.specularG,
			g_config.light.specularB,
			g_config.light.intensity,
			g_config.nightLight.positionX,
			g_config.nightLight.positionY,
			g_config.nightLight.positionZ,
			g_config.nightLight.diffuseR,
			g_config.nightLight.diffuseG,
			g_config.nightLight.diffuseB,
			g_config.nightLight.specularR,
			g_config.nightLight.specularG,
			g_config.nightLight.specularB,
			g_config.nightLight.intensity);
	}
}
