#pragma once

#include "Lights.h"

#include <cstdint>
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
		ClipRectSettings clipRect;
		bool hideInPowerArmor{ true };
		std::uint32_t uiMenuKey{ 0xDE };
		std::vector<LightSettings> lights;
	};

	const Config& GetConfig();
	Config& GetMutableConfig();
	void LoadConfig();
	bool SaveConfig();
}
