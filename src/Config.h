#pragma once

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
		float positionX{ -40.0F };
		float positionY{ -80.0F };
		float positionZ{ 110.0F };
		float diffuseR{ 0.75F };
		float diffuseG{ 0.72F };
		float diffuseB{ 0.66F };
		float specularR{ 0.15F };
		float specularG{ 0.15F };
		float specularB{ 0.15F };
		float intensity{ 0.65F };
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
		LightingType lighting{ LightingType::kWorldDirectional };
		bool hideInPowerArmor{ true };
		float updateHz{ 30.0F };
		LightSettings light;
		LightSettings nightLight{
			.positionX = -20.0F,
			.positionY = -70.0F,
			.positionZ = 95.0F,
			.diffuseR = 0.36F,
			.diffuseG = 0.44F,
			.diffuseB = 0.62F,
			.specularR = 0.08F,
			.specularG = 0.10F,
			.specularB = 0.14F,
			.intensity = 0.42F
		};
	};

	const Config& GetConfig();
	void LoadConfig();
}
