#include "Config.h"

#include "Equipment.h"
#include "SimpleIni.h"

#include "RE/S/Setting.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
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

		[[nodiscard]] std::string ResolveDefaultLanguage()
		{
			if (const auto setting = RE::GetINISetting("sLanguage:General")) {
				if (const auto value = setting->GetString(); !value.empty()) {
					return NormalizeLanguageCode(value);
				}
			}
			return "en";
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

		[[nodiscard]] const char* FramingTargetName(const CameraFramingTarget a_target)
		{
			switch (a_target) {
			case CameraFramingTarget::kHead:
				return "Head";
			case CameraFramingTarget::kChest:
				return "Chest";
			case CameraFramingTarget::kPelvis:
				return "Pelvis";
			case CameraFramingTarget::kRoot:
				return "Root";
			}
			return "Head";
		}

		[[nodiscard]] std::string EquipmentSlotKey(const std::uint32_t a_slotIndex)
		{
			return std::format("Slot{}", Equipment::kEditorSlotBase + a_slotIndex);
		}

		void WriteEquipmentSlots(CSimpleIniA& a_ini, const EquipmentSettings& a_settings)
		{
			for (std::uint32_t index = 0; index < Equipment::kEditorSlotCount; ++index) {
				const auto key = EquipmentSlotKey(index);
				a_ini.SetBoolValue(
					"Equipment",
					key.c_str(),
					(a_settings.syncSlotMask & (1u << index)) != 0);
			}
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

		void WriteLightSections(CSimpleIniA& a_ini, const std::vector<LightSettings>& a_lights)
		{
			for (const auto& light : a_lights) {
				if (light.name.empty()) {
					continue;
				}

				const auto section = std::string{ kLightSectionPrefix } + light.name;
				a_ini.SetValue(section.c_str(), "Type", TypeName(light.type));
				a_ini.SetBoolValue(section.c_str(), "UseInInterior", light.useInInterior);
				WriteFixedLight(a_ini, section.c_str(), light.fixed, "");
				if (light.type == LightType::kTimeOfDay) {
					a_ini.SetDoubleValue(section.c_str(), "StartToD", light.timeOfDay.startTimeOfDay);
					a_ini.SetDoubleValue(section.c_str(), "EndToD", light.timeOfDay.endTimeOfDay);
					WriteFixedLight(a_ini, section.c_str(), light.timeOfDay.start, "Start");
					WriteFixedLight(a_ini, section.c_str(), light.timeOfDay.end, "End");
				}
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
			a_ini.SetValue("Camera", "Target", FramingTargetName(g_config.camera.target));
			a_ini.SetBoolValue("Camera", "Follow", g_config.camera.follow);
			a_ini.SetBoolValue("Camera", "FollowX", g_config.camera.followX);
			a_ini.SetBoolValue("Camera", "FollowY", g_config.camera.followY);
			a_ini.SetBoolValue("Camera", "FollowZ", g_config.camera.followZ);
			a_ini.SetBoolValue("Animation", "UseLiveAnimation", g_config.animation.useLiveAnimation);
			a_ini.SetBoolValue(
				"Animation",
				"HideWeaponDuringIdleAnimation",
				g_config.animation.hideWeaponDuringIdleAnimation);
			a_ini.SetBoolValue(
				"Animation",
				"SheatheWeaponDuringIdleAnimation",
				g_config.animation.sheatheWeaponDuringIdleAnimation);
			a_ini.SetValue("Animation", "DynamicActivationIdle", g_config.animation.dynamicActivationIdle.c_str());
			a_ini.SetBoolValue("Animation", "MirrorLocomotion", g_config.animation.mirrorEvents.locomotion);
			a_ini.SetBoolValue("Animation", "MirrorSneak", g_config.animation.mirrorEvents.sneak);
			a_ini.SetBoolValue("Animation", "MirrorJump", g_config.animation.mirrorEvents.jump);
			a_ini.SetBoolValue("Animation", "MirrorWeaponFire", g_config.animation.mirrorEvents.weaponFire);
			a_ini.SetBoolValue("Animation", "MirrorWeaponReload", g_config.animation.mirrorEvents.weaponReload);
			a_ini.SetBoolValue("Animation", "MirrorMelee", g_config.animation.mirrorEvents.melee);
			a_ini.SetBoolValue("Animation", "MirrorThrow", g_config.animation.mirrorEvents.throwable);
			a_ini.SetDoubleValue("ClipRect", "Left", g_config.clipRect.left);
			a_ini.SetDoubleValue("ClipRect", "Top", g_config.clipRect.top);
			a_ini.SetDoubleValue("ClipRect", "Right", g_config.clipRect.right);
			a_ini.SetDoubleValue("ClipRect", "Bottom", g_config.clipRect.bottom);
			a_ini.SetBoolValue("Render", "HideInPowerArmor", g_config.hideInPowerArmor);
			WriteEquipmentSlots(a_ini, g_config.equipment);
			WriteDefaultLightSections(a_ini);
			a_ini.SetValue("UI", "MenuKey", "0xDE");
			a_ini.SetValue("UI", "Language", g_config.language.c_str());
		}

		void WriteConfig(CSimpleIniA& a_ini, const Config& a_config)
		{
			a_ini.SetBoolValue("General", "Enabled", a_config.enabled);
			a_ini.SetDoubleValue("View", "FOV", a_config.fov);
			a_ini.SetDoubleValue("View", "PlacementX", a_config.placementX);
			a_ini.SetDoubleValue("View", "PlacementY", a_config.placementY);
			a_ini.SetDoubleValue("View", "CameraDistance", a_config.cameraDistance);
			a_ini.SetDoubleValue("View", "ModelScale", a_config.modelScale);
			a_ini.SetDoubleValue("View", "YawDegrees", a_config.yawDegrees);
			a_ini.SetLongValue("View", "Anchor", a_config.anchor);
			a_ini.SetValue("Camera", "Target", FramingTargetName(a_config.camera.target));
			a_ini.SetBoolValue("Camera", "Follow", a_config.camera.follow);
			a_ini.SetBoolValue("Camera", "FollowX", a_config.camera.followX);
			a_ini.SetBoolValue("Camera", "FollowY", a_config.camera.followY);
			a_ini.SetBoolValue("Camera", "FollowZ", a_config.camera.followZ);
			a_ini.SetBoolValue("Animation", "UseLiveAnimation", a_config.animation.useLiveAnimation);
			a_ini.SetBoolValue(
				"Animation",
				"HideWeaponDuringIdleAnimation",
				a_config.animation.hideWeaponDuringIdleAnimation);
			a_ini.SetBoolValue(
				"Animation",
				"SheatheWeaponDuringIdleAnimation",
				a_config.animation.sheatheWeaponDuringIdleAnimation);
			a_ini.SetValue("Animation", "DynamicActivationIdle", a_config.animation.dynamicActivationIdle.c_str());
			a_ini.SetBoolValue("Animation", "MirrorLocomotion", a_config.animation.mirrorEvents.locomotion);
			a_ini.SetBoolValue("Animation", "MirrorSneak", a_config.animation.mirrorEvents.sneak);
			a_ini.SetBoolValue("Animation", "MirrorJump", a_config.animation.mirrorEvents.jump);
			a_ini.SetBoolValue("Animation", "MirrorWeaponFire", a_config.animation.mirrorEvents.weaponFire);
			a_ini.SetBoolValue("Animation", "MirrorWeaponReload", a_config.animation.mirrorEvents.weaponReload);
			a_ini.SetBoolValue("Animation", "MirrorMelee", a_config.animation.mirrorEvents.melee);
			a_ini.SetBoolValue("Animation", "MirrorThrow", a_config.animation.mirrorEvents.throwable);
			a_ini.SetDoubleValue("ClipRect", "Left", a_config.clipRect.left);
			a_ini.SetDoubleValue("ClipRect", "Right", a_config.clipRect.right);
			a_ini.SetDoubleValue("ClipRect", "Top", a_config.clipRect.top);
			a_ini.SetDoubleValue("ClipRect", "Bottom", a_config.clipRect.bottom);
			a_ini.SetBoolValue("Render", "HideInPowerArmor", a_config.hideInPowerArmor);
			WriteEquipmentSlots(a_ini, a_config.equipment);
			WriteLightSections(a_ini, a_config.lights.empty() ? Lights::DefaultLights() : a_config.lights);
			const auto menuKey = std::format("0x{:02X}", a_config.uiMenuKey);
			a_ini.SetValue("UI", "MenuKey", menuKey.c_str());
			a_ini.SetValue("UI", "Language", a_config.language.c_str());
		}

		void ClampConfig(Config& a_config)
		{
			a_config.fov = std::clamp(a_config.fov, 10.0F, 120.0F);
			a_config.modelScale = std::clamp(a_config.modelScale, 0.01F, 10.0F);
			a_config.anchor = std::clamp(a_config.anchor, 1, 9);
			a_config.equipment.syncSlotMask &= Equipment::kAllEditorSlotsMask;
			a_config.language = NormalizeLanguageCode(a_config.language);
			for (auto& light : a_config.lights) {
				ClampFixedLight(light.fixed);
				ClampTimeOfDayLight(light.timeOfDay);
			}
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

		[[nodiscard]] CameraFramingTarget ReadFramingTarget(
			CSimpleIniA& a_ini,
			const char* a_section,
			const char* a_key,
			const CameraFramingTarget a_default)
		{
			const auto* value = a_ini.GetValue(a_section, a_key, nullptr);
			if (!value || value[0] == '\0') {
				return a_default;
			}

			const std::string_view target{ value };
			if (target == "Head" || target == "head" || target == "0") {
				return CameraFramingTarget::kHead;
			}
			if (target == "Chest" || target == "chest" || target == "1") {
				return CameraFramingTarget::kChest;
			}
			if (target == "Pelvis" || target == "pelvis" || target == "2") {
				return CameraFramingTarget::kPelvis;
			}
			if (target == "Root" || target == "root" || target == "3") {
				return CameraFramingTarget::kRoot;
			}

			REX::WARN("Invalid camera framing target '{}'; using {}", value, FramingTargetName(a_default));
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

		[[nodiscard]] std::uint32_t ReadEquipmentSlotMask(CSimpleIniA& a_ini)
		{
			std::uint32_t mask = 0;
			for (std::uint32_t index = 0; index < Equipment::kEditorSlotCount; ++index) {
				const auto key = EquipmentSlotKey(index);
				if (a_ini.GetBoolValue("Equipment", key.c_str(), true)) {
					mask |= (1u << index);
				}
			}
			return mask;
		}
	}

	const Config& GetConfig()
	{
		return g_config;
	}

	Config& GetMutableConfig()
	{
		return g_config;
	}

	void LoadConfig()
	{
		g_config = Config{};
		g_config.language = ResolveDefaultLanguage();

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

		bool writeMissingUI = false;
		if (!ini.GetValue("UI", "MenuKey", nullptr)) {
			ini.SetValue("UI", "MenuKey", "0xDE");
			writeMissingUI = true;
		}
		if (const auto* language = ini.GetValue("UI", "Language", nullptr); language && language[0] != '\0') {
			g_config.language = NormalizeLanguageCode(language);
		} else {
			ini.SetValue("UI", "Language", g_config.language.c_str());
			writeMissingUI = true;
		}
		if (writeMissingUI) {
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
		g_config.camera.target = ReadFramingTarget(ini, "Camera", "Target", g_config.camera.target);
		g_config.camera.follow = ini.GetBoolValue("Camera", "Follow", g_config.camera.follow);
		g_config.camera.followX = ini.GetBoolValue("Camera", "FollowX", g_config.camera.followX);
		g_config.camera.followY = ini.GetBoolValue("Camera", "FollowY", g_config.camera.followY);
		g_config.camera.followZ = ini.GetBoolValue("Camera", "FollowZ", g_config.camera.followZ);
		g_config.animation.useLiveAnimation =
			ini.GetBoolValue("Animation", "UseLiveAnimation", g_config.animation.useLiveAnimation);
		g_config.animation.hideWeaponDuringIdleAnimation = ini.GetBoolValue(
			"Animation",
			"HideWeaponDuringIdleAnimation",
			g_config.animation.hideWeaponDuringIdleAnimation);
		g_config.animation.sheatheWeaponDuringIdleAnimation = ini.GetBoolValue(
			"Animation",
			"SheatheWeaponDuringIdleAnimation",
			g_config.animation.sheatheWeaponDuringIdleAnimation);
		g_config.animation.dynamicActivationIdle =
			ini.GetValue("Animation", "DynamicActivationIdle", g_config.animation.dynamicActivationIdle.c_str());
		g_config.animation.mirrorEvents.locomotion =
			ini.GetBoolValue("Animation", "MirrorLocomotion", g_config.animation.mirrorEvents.locomotion);
		g_config.animation.mirrorEvents.sneak =
			ini.GetBoolValue("Animation", "MirrorSneak", g_config.animation.mirrorEvents.sneak);
		g_config.animation.mirrorEvents.jump =
			ini.GetBoolValue("Animation", "MirrorJump", g_config.animation.mirrorEvents.jump);
		g_config.animation.mirrorEvents.weaponFire =
			ini.GetBoolValue("Animation", "MirrorWeaponFire", g_config.animation.mirrorEvents.weaponFire);
		g_config.animation.mirrorEvents.weaponReload =
			ini.GetBoolValue("Animation", "MirrorWeaponReload", g_config.animation.mirrorEvents.weaponReload);
		g_config.animation.mirrorEvents.melee =
			ini.GetBoolValue("Animation", "MirrorMelee", g_config.animation.mirrorEvents.melee);
		g_config.animation.mirrorEvents.throwable =
			ini.GetBoolValue("Animation", "MirrorThrow", g_config.animation.mirrorEvents.throwable);
		g_config.clipRect.left = static_cast<float>(ini.GetDoubleValue("ClipRect", "Left", g_config.clipRect.left));
		g_config.clipRect.top = static_cast<float>(ini.GetDoubleValue("ClipRect", "Top", g_config.clipRect.top));
		g_config.clipRect.right = static_cast<float>(ini.GetDoubleValue("ClipRect", "Right", g_config.clipRect.right));
		g_config.clipRect.bottom = static_cast<float>(ini.GetDoubleValue("ClipRect", "Bottom", g_config.clipRect.bottom));
		g_config.hideInPowerArmor = ini.GetBoolValue("Render", "HideInPowerArmor", g_config.hideInPowerArmor);
		g_config.equipment.syncSlotMask = ReadEquipmentSlotMask(ini);
		g_config.uiMenuKey = ReadVirtualKey(ini, "UI", "MenuKey", g_config.uiMenuKey);
		g_config.lights = ReadLights(ini);
		ClampConfig(g_config);

		REX::INFO(
			"Loaded config: enabled={}, fov={}, placement=({}, {}), cameraDistance={}, modelScale={}, yawDegrees={}, anchor={}, cameraTarget={}, cameraFollow={} axes=({}, {}, {}), clipRect=({}, {}, {}, {}), hideInPowerArmor={}, uiMenuKey=0x{:02X}, language={}, lights={}",
			g_config.enabled,
			g_config.fov,
			g_config.placementX,
			g_config.placementY,
			g_config.cameraDistance,
			g_config.modelScale,
			g_config.yawDegrees,
			g_config.anchor,
			FramingTargetName(g_config.camera.target),
			g_config.camera.follow,
			g_config.camera.followX,
			g_config.camera.followY,
			g_config.camera.followZ,
			g_config.clipRect.left,
			g_config.clipRect.top,
			g_config.clipRect.right,
			g_config.clipRect.bottom,
			g_config.hideInPowerArmor,
			g_config.uiMenuKey,
			g_config.language,
			g_config.lights.size());
	}

	bool SaveConfig()
	{
		ClampConfig(g_config);

		CSimpleIniA ini;
		ini.SetUnicode();
		WriteConfig(ini, g_config);

		const auto path = ConfigPath();
		std::filesystem::create_directories(path.parent_path());
		if (ini.SaveFile(path.string().c_str()) < 0) {
			REX::WARN("Failed to save config to {}", path.string());
			return false;
		}

		REX::INFO("Saved config to {}", path.string());
		return true;
	}

	bool SaveLanguageConfig(std::string_view a_language)
	{
		g_config.language = NormalizeLanguageCode(a_language);

		CSimpleIniA ini;
		ini.SetUnicode();

		const auto path = ConfigPath();
		if (std::filesystem::exists(path) && ini.LoadFile(path.string().c_str()) < 0) {
			REX::WARN("Failed to load {}; rewriting current config while saving language", path.string());
			WriteConfig(ini, g_config);
		}

		ini.SetValue("UI", "Language", g_config.language.c_str());
		std::filesystem::create_directories(path.parent_path());
		if (ini.SaveFile(path.string().c_str()) < 0) {
			REX::WARN("Failed to save language config to {}", path.string());
			return false;
		}

		REX::INFO("Saved language '{}' to {}", g_config.language, path.string());
		return true;
	}
}
