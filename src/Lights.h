#pragma once

#include <string>
#include <vector>

namespace RE
{
	class NiColor;
	class NiPoint3;
	namespace Interface3D
	{
		class Renderer;
	}
}

namespace TF3DHud
{
	enum class LightType
	{
		kDirectional,
		kFixed,
		kTimeOfDay
	};

	struct LightVectorSettings
	{
		float x{ 0.0F };
		float y{ 0.0F };
		float z{ 0.0F };
	};

	struct LightColorSettings
	{
		float r{ 1.0F };
		float g{ 1.0F };
		float b{ 1.0F };
	};

	struct FixedLightSettings
	{
		LightVectorSettings position{ .x = -300.0F, .y = 600.0F, .z = 460.0F };
		LightColorSettings diffuse{ .r = 0.641F, .g = 0.758F, .b = 0.785F };
		float distance{ 2400.0F };
		float intensity{ 7.0F };
	};

	struct TimeOfDayLightSettings
	{
		float startTimeOfDay{ 18.0F };
		float endTimeOfDay{ 6.0F };
		FixedLightSettings start{
			.position = { .x = -300.0F, .y = 600.0F, .z = 460.0F },
			.diffuse = { .r = 0.641F, .g = 0.758F, .b = 0.785F },
			.distance = 2400.0F,
			.intensity = 7.0F
		};
		FixedLightSettings end{
			.position = { .x = 500.0F, .y = -250.0F, .z = 400.0F },
			.diffuse = { .r = 1.0F, .g = 1.0F, .b = 1.0F },
			.distance = 2000.0F,
			.intensity = 2.25F
		};
	};

	struct LightSettings
	{
		std::string name;
		LightType type{ LightType::kDirectional };
		bool useInInterior{ false };
		FixedLightSettings fixed;
		TimeOfDayLightSettings timeOfDay;
	};

	namespace Lights
	{
		[[nodiscard]] std::vector<LightSettings> DefaultLights();
		void ConfigureOffscreenLights(RE::Interface3D::Renderer& a_renderer, const std::vector<LightSettings>& a_lights);
		void ResetCache();
	}
}
