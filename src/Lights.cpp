#include "Lights.h"

#include "RE/I/Interface3D.h"
#include "RE/N/NiLight.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/S/Sky.h"
#include "RE/S/Sun.h"
#include "RE/T/TESObjectCELL.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>

namespace TF3DHud::Lights
{
	namespace
	{
		constexpr float kDirectionalSunDistance = 1000.0F;
		constexpr float kDirectionalLightDistance = 2000.0F;

		struct ResolvedLight
		{
			RE::NiPoint3 position;
			RE::NiColor diffuse;
			float distance;
			float intensity;
		};

		std::vector<ResolvedLight> g_appliedLights;
		bool g_hasAppliedLights{ false };

		[[nodiscard]] float NormalizeTimeOfDay(const float a_value)
		{
			auto value = std::fmod(a_value, 24.0F);
			if (value < 0.0F) {
				value += 24.0F;
			}
			return value;
		}

		[[nodiscard]] float Lerp(const float a_from, const float a_to, const float a_t)
		{
			return a_from + (a_to - a_from) * a_t;
		}

		[[nodiscard]] RE::NiPoint3 ToPoint(const LightVectorSettings& a_value)
		{
			return { a_value.x, a_value.y, a_value.z };
		}

		[[nodiscard]] RE::NiColor ToColor(const LightColorSettings& a_value)
		{
			return { a_value.r, a_value.g, a_value.b };
		}

		[[nodiscard]] LightVectorSettings LerpVector(
			const LightVectorSettings& a_from,
			const LightVectorSettings& a_to,
			const float a_t)
		{
			return {
				.x = Lerp(a_from.x, a_to.x, a_t),
				.y = Lerp(a_from.y, a_to.y, a_t),
				.z = Lerp(a_from.z, a_to.z, a_t)
			};
		}

		[[nodiscard]] LightColorSettings LerpColor(
			const LightColorSettings& a_from,
			const LightColorSettings& a_to,
			const float a_t)
		{
			return {
				.r = Lerp(a_from.r, a_to.r, a_t),
				.g = Lerp(a_from.g, a_to.g, a_t),
				.b = Lerp(a_from.b, a_to.b, a_t)
			};
		}

		[[nodiscard]] bool NearlyEqual(const float a_lhs, const float a_rhs)
		{
			return std::abs(a_lhs - a_rhs) <= 0.0001F;
		}

		[[nodiscard]] bool SamePoint(const RE::NiPoint3& a_lhs, const RE::NiPoint3& a_rhs)
		{
			return NearlyEqual(a_lhs.x, a_rhs.x) &&
			       NearlyEqual(a_lhs.y, a_rhs.y) &&
			       NearlyEqual(a_lhs.z, a_rhs.z);
		}

		[[nodiscard]] bool SameColor(const RE::NiColor& a_lhs, const RE::NiColor& a_rhs)
		{
			return NearlyEqual(a_lhs.r, a_rhs.r) &&
			       NearlyEqual(a_lhs.g, a_rhs.g) &&
			       NearlyEqual(a_lhs.b, a_rhs.b);
		}

		[[nodiscard]] bool SameLight(const ResolvedLight& a_lhs, const ResolvedLight& a_rhs)
		{
			return SamePoint(a_lhs.position, a_rhs.position) &&
			       SameColor(a_lhs.diffuse, a_rhs.diffuse) &&
			       NearlyEqual(a_lhs.distance, a_rhs.distance) &&
			       NearlyEqual(a_lhs.intensity, a_rhs.intensity);
		}

		[[nodiscard]] bool SameLights(
			const std::vector<ResolvedLight>& a_lhs,
			const std::vector<ResolvedLight>& a_rhs)
		{
			if (a_lhs.size() != a_rhs.size()) {
				return false;
			}

			for (std::size_t i = 0; i < a_lhs.size(); ++i) {
				if (!SameLight(a_lhs[i], a_rhs[i])) {
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool IsInterior()
		{
			const auto* player = RE::PlayerCharacter::GetSingleton();
			const auto* parentCell = player ? player->GetParentCell() : nullptr;
			return parentCell && parentCell->IsInterior();
		}

		[[nodiscard]] const RE::NiLight* GetSunLight()
		{
			const auto* sky = RE::Sky::GetSingleton();
			if (!sky || !sky->sun || !sky->sun->light) {
				return nullptr;
			}
			return reinterpret_cast<const RE::NiLight*>(sky->sun->light.get());
		}

		[[nodiscard]] bool GetSunDirection(const RE::NiLight& a_sunLight, RE::NiPoint3& a_direction)
		{
			a_direction = a_sunLight.world.rotate * RE::NiPoint3{ 0.0F, 1.0F, 0.0F };
			return a_direction.Unitize() > 0.0001F;
		}

		[[nodiscard]] std::optional<ResolvedLight> ResolveFixedLight(const FixedLightSettings& a_settings)
		{
			if (a_settings.intensity <= 0.0F || a_settings.distance <= 0.0F) {
				return std::nullopt;
			}

			return ResolvedLight{
				.position = ToPoint(a_settings.position),
				.diffuse = ToColor(a_settings.diffuse),
				.distance = a_settings.distance,
				.intensity = a_settings.intensity
			};
		}

		[[nodiscard]] std::optional<ResolvedLight> ResolveDirectionalLight(const FixedLightSettings& a_settings)
		{
			if (a_settings.intensity <= 0.0F || IsInterior()) {
				return std::nullopt;
			}

			const auto* sunLight = GetSunLight();
			RE::NiPoint3 sunDirection;
			if (!sunLight || !GetSunDirection(*sunLight, sunDirection)) {
				return std::nullopt;
			}

			return ResolvedLight{
				.position = (sunDirection * kDirectionalSunDistance) + ToPoint(a_settings.position),
				.diffuse = sunLight->diff,
				.distance = kDirectionalLightDistance,
				.intensity = sunLight->dimmer * a_settings.intensity
			};
		}

		[[nodiscard]] std::optional<float> TimeOfDayFraction(const TimeOfDayLightSettings& a_settings)
		{
			const auto start = NormalizeTimeOfDay(a_settings.startTimeOfDay);
			const auto end = NormalizeTimeOfDay(a_settings.endTimeOfDay);
			if (NearlyEqual(start, end)) {
				return std::nullopt;
			}

			const auto* sky = RE::Sky::GetSingleton();
			const auto now = NormalizeTimeOfDay(sky ? sky->currentGameHour : start);
			const auto duration = end > start ? end - start : (24.0F - start) + end;
			const auto elapsed = now >= start ? now - start : (24.0F - start) + now;
			if (elapsed < 0.0F || elapsed > duration) {
				return std::nullopt;
			}

			return std::clamp(elapsed / duration, 0.0F, 1.0F);
		}

		[[nodiscard]] std::optional<ResolvedLight> ResolveTimeOfDayLight(const TimeOfDayLightSettings& a_settings)
		{
			const auto fraction = TimeOfDayFraction(a_settings);
			if (!fraction) {
				return std::nullopt;
			}

			const FixedLightSettings fixed{
				.position = LerpVector(a_settings.start.position, a_settings.end.position, *fraction),
				.diffuse = LerpColor(a_settings.start.diffuse, a_settings.end.diffuse, *fraction),
				.distance = Lerp(a_settings.start.distance, a_settings.end.distance, *fraction),
				.intensity = Lerp(a_settings.start.intensity, a_settings.end.intensity, *fraction)
			};
			return ResolveFixedLight(fixed);
		}

		[[nodiscard]] std::vector<ResolvedLight> ResolveLights(const std::vector<LightSettings>& a_lights)
		{
			std::vector<ResolvedLight> resolvedLights;
			resolvedLights.reserve(a_lights.size());
			const bool isInterior = IsInterior();

			for (const auto& light : a_lights) {
				std::optional<ResolvedLight> resolved;
				switch (light.type) {
				case LightType::kDirectional:
					resolved = ResolveDirectionalLight(light.fixed);
					break;
				case LightType::kFixed:
					if (isInterior && !light.useInInterior) {
						break;
					}
					resolved = ResolveFixedLight(light.fixed);
					break;
				case LightType::kTimeOfDay:
					if (isInterior && !light.useInInterior) {
						break;
					}
					resolved = ResolveTimeOfDayLight(light.timeOfDay);
					break;
				}

				if (resolved) {
					resolvedLights.push_back(*resolved);
				}
			}

			return resolvedLights;
		}
	}

	std::vector<LightSettings> DefaultLights()
	{
		return {
			LightSettings{
				.name = "Front",
				.type = LightType::kDirectional,
				.fixed = {
					.position = { .x = 50.0F, .y = 0.0F, .z = 50.0F },
					.diffuse = { .r = 0.841F, .g = 0.758F, .b = 0.785F },
					.distance = 1000.0F,
					.intensity = 3.0F
				}
			}
		};
	}

	void ConfigureOffscreenLights(RE::Interface3D::Renderer& a_renderer, const std::vector<LightSettings>& a_lights)
	{
		auto resolvedLights = ResolveLights(a_lights);
		if (g_hasAppliedLights && SameLights(g_appliedLights, resolvedLights)) {
			return;
		}

		a_renderer.offscreenLights.clear();
		a_renderer.needsLightSetupOffscreen = true;
		for (const auto& light : resolvedLights) {
			a_renderer.Offscreen_AddLight(
				light.position,
				light.diffuse,
				RE::NiColor{ light.distance, 0.0F, 0.0F },
				light.intensity);
		}

		g_appliedLights = std::move(resolvedLights);
		g_hasAppliedLights = true;
		REX::INFO(
			"Interface3D offscreen lights configured: renderer={:X}, offscreenSSN={:X}, lights={}",
			reinterpret_cast<std::uintptr_t>(&a_renderer),
			reinterpret_cast<std::uintptr_t>(a_renderer.offscreenSSN.get()),
			a_renderer.offscreenLights.size());
	}

	void ResetCache()
	{
		g_appliedLights.clear();
		g_hasAppliedLights = false;
	}
}
