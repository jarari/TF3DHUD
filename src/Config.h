#pragma once

#include <cstdint>

namespace TF3DHud
{
	enum class LightingType
	{
		kWorldDirectional = 0,
		kFakePoint = 1,
		kFakePointAdaptiveTime = 2
	};

	struct LightSettings
	{
		float positionX{ -300.0F };
		float positionY{ 600.0F };
		float positionZ{ 460.0F };
		float diffuseR{ 0.641F };
		float diffuseG{ 0.758F };
		float diffuseB{ 0.785F };
		float specularR{ 2400.0F };
		float specularG{ 0.0F };
		float specularB{ 0.0F };
		float intensity{ 7.0F };
	};

	struct ClipRectSettings
	{
		float left{ 0.0F };
		float top{ 0.0F };
		float right{ 0.0F };
		float bottom{ 0.0F };
	};

	struct Config
	{
		bool enabled{ true };
		float fov{ 50.0F };
		float placementX{ 0.0F };
		float placementY{ 0.0F };
		float cameraDistance{ 400.0F };
		float modelScale{ 0.11F };
		float yawDegrees{ 180.0F };
		std::int32_t anchor{ 5 };
		LightingType lighting{ LightingType::kWorldDirectional };
		ClipRectSettings clipRect;
		bool hideInPowerArmor{ true };
		std::uint32_t uiMenuKey{ 0xDE };
		LightSettings light;
		LightSettings nightLight{
			.positionX = 500.0F,
			.positionY = -250.0F,
			.positionZ = 400.0F,
			.diffuseR = 1.0F,
			.diffuseG = 1.0F,
			.diffuseB = 1.0F,
			.specularR = 2000.0F,
			.specularG = 0.0F,
			.specularB = 0.0F,
			.intensity = 2.25F
		};
	};

	const Config& GetConfig();
	void LoadConfig();
}
