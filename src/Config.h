#pragma once

#include "Lights.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace TF3DHud
{
	struct ClipRectSettings
	{
		float left{ 0.0F };
		float top{ 30.0F };
		float right{ 0.0F };
		float bottom{ 60.0F };
	};

	enum class CameraFramingTarget : std::uint32_t
	{
		kHead,
		kChest,
		kPelvis,
		kRoot
	};

	struct CameraFramingSettings
	{
		CameraFramingTarget target{ CameraFramingTarget::kHead };
		bool follow{ true };
		bool followX{ true };
		bool followY{ true };
		bool followZ{ true };
	};

	struct AnimationMirrorEventSettings
	{
		bool locomotion{ true };
		bool sneak{ true };
		bool jump{ true };
		bool weaponFire{ true };
		bool weaponReload{ true };
		bool melee{ true };
		bool throwable{ true };
	};

	struct AnimationSettings
	{
		bool useLiveAnimation{ true };
		bool hideWeaponDuringIdleAnimation{ false };
		bool sheatheWeaponDuringIdleAnimation{ false };
		std::string dynamicActivationIdle;
		AnimationMirrorEventSettings mirrorEvents;
	};

	struct EquipmentSettings
	{
		std::uint32_t syncSlotMask{ 0xFFFF'FFFFu };
	};

	struct Config
	{
		bool enabled{ true };
		float fov{ 70.0F };
		float placementX{ 30.0F };
		float placementY{ 50.0F };
		float cameraDistance{ 200.0F };
		float modelScale{ 0.45F };
		float yawDegrees{ 160.0F };
		std::int32_t anchor{ 1 };
		CameraFramingSettings camera;
		AnimationSettings animation;
		EquipmentSettings equipment;
		ClipRectSettings clipRect;
		bool hideInPowerArmor{ true };
		bool antiAliasing{ false };
		std::uint32_t uiMenuKey{ 0xDE };
		std::string language{ "en" };
		std::vector<LightSettings> lights;
	};

	const Config& GetConfig();
	Config& GetMutableConfig();
	void LoadConfig();
	bool SaveConfig();
	bool SaveLanguageConfig(std::string_view a_language);
}
