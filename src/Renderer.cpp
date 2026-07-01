#include "Renderer.h"

#include "Config.h"

#include "RE/B/BSGraphics.h"
#include "RE/B/BSGeometry.h"
#include "RE/B/BSModelDB.h"
#include "RE/B/BSTriShape.h"
#include "RE/N/NiCloningProcess.h"
#include "RE/N/NiCamera.h"
#include "RE/N/NiLight.h"
#include "RE/N/NiUpdateData.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/S/Sky.h"
#include "RE/S/Sun.h"
#include "RE/T/TESObjectCELL.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace TF3DHud::Renderer
{
	namespace
	{
		constexpr auto kRendererName = "TF3DHudRenderer";
		constexpr auto kDisplayMeshPath = "Interface/GunModMenu/ModMenuRenderMesh.nif";
		constexpr auto kDisplayMeshGeometry = "ModMenuRenderMesh:0";
		constexpr auto kPi = 3.14159265358979323846F;
		constexpr auto kPreviewCameraAspect = 16.0F / 9.0F;
		constexpr float kDisplayRootY = 375.0F;
		constexpr float kVanillaDisplayLeft = -148.125F;
		constexpr float kVanillaDisplayTop = 79.875F;
		constexpr float kVanillaDisplayRight = 148.125F;
		constexpr float kVanillaDisplayBottom = -79.875F;
		constexpr std::int32_t kFullFrameDisplayRenderTarget = 63;

		using ForceUpgradeTextures_t = void(RE::NiAVObject*, bool, bool);

		REL::Relocation<ForceUpgradeTextures_t*> g_forceUpgradeTextures{ REL::ID{ 1417022, 2229490 } };

		RE::Interface3D::Renderer* g_renderer{ nullptr };
		RE::NiPointer<RE::NiAVObject> g_displayRoot;
		bool g_visible{ false };
		bool g_rendererConfigured{ false };

		struct AppliedLightState
		{
			RE::NiPoint3 position;
			RE::NiColor diffuse;
			RE::NiColor specular;
			float intensity;
		};

		bool g_hasAppliedLightState{ false };
		AppliedLightState g_appliedLightState{};

		bool EnsureDisplayRoot();

		struct DisplayBounds
		{
			float left;
			float top;
			float right;
			float bottom;
		};

		[[nodiscard]] DisplayBounds GetDisplayBounds()
		{
			const auto& clipRect = GetConfig().clipRect;
			const bool hasCustomBounds =
				clipRect.left > 0.0F ||
				clipRect.top > 0.0F ||
				clipRect.right > 0.0F ||
				clipRect.bottom > 0.0F;

			if (hasCustomBounds) {
				const DisplayBounds centeredBounds{
					-std::max(clipRect.left, 0.0F),
					std::max(clipRect.top, 0.0F),
					std::max(clipRect.right, 0.0F),
					-std::max(clipRect.bottom, 0.0F)
				};

				if (centeredBounds.right > centeredBounds.left && centeredBounds.top > centeredBounds.bottom) {
					return centeredBounds;
				}

				REX::WARN(
					"ignored invalid ClipRect centered extents: left={}, top={}, right={}, bottom={}",
					clipRect.left,
					clipRect.top,
					clipRect.right,
					clipRect.bottom);
			}

			return {
				kVanillaDisplayLeft,
				kVanillaDisplayTop,
				kVanillaDisplayRight,
				kVanillaDisplayBottom
			};
		}

		struct ScreenPlaneBounds
		{
			float left;
			float top;
			float right;
			float bottom;
		};

		[[nodiscard]] RE::NiCamera* GetDisplayPlacementCamera()
		{
			if (!g_renderer) {
				return nullptr;
			}

			if (g_renderer->nativeAspect) {
				return g_renderer->nativeAspect.get();
			}
			if (g_renderer->nativeAspectLongRange) {
				return g_renderer->nativeAspectLongRange.get();
			}
			return g_renderer->pipboyAspect.get();
		}

		[[nodiscard]] ScreenPlaneBounds GetScreenPlaneBounds()
		{
			if (const auto* camera = GetDisplayPlacementCamera()) {
				return {
					camera->viewFrustum.left * kDisplayRootY,
					camera->viewFrustum.top * kDisplayRootY,
					camera->viewFrustum.right * kDisplayRootY,
					camera->viewFrustum.bottom * kDisplayRootY
				};
			}

			return {
				kVanillaDisplayLeft,
				kVanillaDisplayTop,
				kVanillaDisplayRight,
				kVanillaDisplayBottom
			};
		}

		[[nodiscard]] std::uint16_t FloatToHalf(const float a_value)
		{
			std::uint32_t bits;
			std::memcpy(std::addressof(bits), std::addressof(a_value), sizeof(bits));

			const auto sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000);
			auto exponent = static_cast<std::int32_t>((bits >> 23) & 0xFF);
			auto mantissa = bits & 0x7FFFFF;

			if (exponent == 0xFF) {
				if (mantissa != 0) {
					return static_cast<std::uint16_t>(sign | 0x7E00);
				}
				return static_cast<std::uint16_t>(sign | 0x7C00);
			}

			exponent = exponent - 127 + 15;
			if (exponent >= 0x1F) {
				return static_cast<std::uint16_t>(sign | 0x7C00);
			}
			if (exponent <= 0) {
				if (exponent < -10) {
					return sign;
				}
				mantissa |= 0x800000;
				const auto shift = static_cast<std::uint32_t>(14 - exponent);
				auto halfMantissa = static_cast<std::uint16_t>(mantissa >> shift);
				if ((mantissa >> (shift - 1)) & 1) {
					++halfMantissa;
				}
				return static_cast<std::uint16_t>(sign | halfMantissa);
			}

			auto half = static_cast<std::uint16_t>(sign | (static_cast<std::uint16_t>(exponent) << 10) | (mantissa >> 13));
			if (mantissa & 0x1000) {
				++half;
			}
			return half;
		}

		void WriteHalf(std::byte* a_target, const float a_value)
		{
			const auto half = FloatToHalf(a_value);
			std::memcpy(a_target, std::addressof(half), sizeof(half));
		}

		[[nodiscard]] float NormalizeDisplayX(const float a_x)
		{
			return (a_x - kVanillaDisplayLeft) / (kVanillaDisplayRight - kVanillaDisplayLeft);
		}

		[[nodiscard]] float NormalizeDisplayZ(const float a_z)
		{
			return (a_z - kVanillaDisplayTop) / (kVanillaDisplayBottom - kVanillaDisplayTop);
		}

		RE::NiPointer<RE::NiAVObject> CloneDisplayObject(RE::NiAVObject& a_source)
		{
			RE::NiCloningProcess cloneProcess;
			cloneProcess.appendChar = '$';
			cloneProcess.copyType = RE::NiCloningProcess::CopyType::kCopyExact;
			cloneProcess.scale = { 1.0F, 1.0F, 1.0F };

			auto* clone = a_source.CreateClone(cloneProcess);
			a_source.ProcessClone(cloneProcess);
			auto* clonedObject = clone ? static_cast<RE::NiAVObject*>(clone) : nullptr;
			return clonedObject;
		}

		[[nodiscard]] RE::BSResource::ErrorCode DemandDisplayRoot(
			const char* a_path,
			RE::NiPointer<RE::NiNode>& a_loadedRoot)
		{
			RE::BSModelDB::DBTraits::ArgsType args{};
			args.prepareAfterLoad = true;
			args.useErrorMarker = true;
			args.performProcess = true;
			args.createFadeNode = true;
			args.loadTextures = true;

			return RE::BSModelDB::Demand(a_path, std::addressof(a_loadedRoot), args);
		}

		void HideImpl()
		{
			if (g_renderer && (g_visible || g_renderer->enabled)) {
				g_renderer->Disable();
				g_visible = false;
			}
		}

		[[nodiscard]] std::uintptr_t Ptr(const void* a_ptr)
		{
			return reinterpret_cast<std::uintptr_t>(a_ptr);
		}

		void ApplyCameraFOV(RE::NiCamera* a_camera, const float a_configFOV)
		{
			if (!a_camera) {
				return;
			}

			const auto currentHeight = a_camera->viewFrustum.top - a_camera->viewFrustum.bottom;
			if (std::abs(currentHeight) <= 0.000001F) {
				return;
			}

			const auto top = std::tan((a_configFOV * kPi / 180.0F) * 0.15F);
			const auto right = top * kPreviewCameraAspect;

			a_camera->viewFrustum.left = -right;
			a_camera->viewFrustum.right = right;
			a_camera->viewFrustum.top = top;
			a_camera->viewFrustum.bottom = -top;
			a_camera->viewFrustum.ortho = false;
			if (a_camera->viewFrustum.near > 0.0F) {
				a_camera->maxFarNearRatio = a_camera->viewFrustum.far / a_camera->viewFrustum.near;
			}
		}

		void ApplyRendererFOV(const float a_configFOV)
		{
			if (!g_renderer) {
				return;
			}

			// IDA: Interface3D::Renderer::SetupCamera computes top = tan(DEG_TO_RAD * fov * 0.15).
			// Renderer::Create only applies this while constructing new cameras, so reused named renderers
			// need the frustum refreshed when config is reloaded.
			ApplyCameraFOV(g_renderer->pipboyAspect.get(), a_configFOV);
			ApplyCameraFOV(g_renderer->nativeAspect.get(), a_configFOV);
			ApplyCameraFOV(g_renderer->nativeAspectLongRange.get(), a_configFOV);
		}

		void ApplyDisplayClipRect()
		{
			if (!g_displayRoot) {
				return;
			}

			const auto bounds = GetDisplayBounds();

			auto* object = g_displayRoot->GetObjectByName(RE::BSFixedString(kDisplayMeshGeometry));
			auto* triShape = object ? object->IsTriShape() : nullptr;
			auto* geometry = triShape ? static_cast<RE::BSGeometry*>(triShape) : nullptr;
			auto* rendererData = geometry ? static_cast<RE::BSGraphics::TriShape*>(geometry->rendererData) : nullptr;
			auto* vertexBuffer = rendererData ? rendererData->vertexBuffer : nullptr;
			if (!triShape || !rendererData || !vertexBuffer || !vertexBuffer->data) {
				REX::WARN(
					"skipped display clip rect: display geometry unavailable object={:X}, rendererData={:X}, vertexBuffer={:X}",
					reinterpret_cast<std::uintptr_t>(object),
					reinterpret_cast<std::uintptr_t>(rendererData),
					reinterpret_cast<std::uintptr_t>(vertexBuffer));
				return;
			}

			const auto desc = rendererData->vertexDesc.desc;
			const auto stride = static_cast<std::uint32_t>((desc & 0xF) * 4);
			const auto positionOffset = static_cast<std::uint32_t>((desc >> 2) & 0x3C);
			const auto uvOffset = static_cast<std::uint32_t>((desc >> 6) & 0x3C);
			const auto fullPrecision = ((desc >> 44) & RE::BSGraphics::Vertex::VF_FULLPREC) != 0;
			if (triShape->numVertices != 4 || stride != 20 || positionOffset != 0 || uvOffset != 8 || fullPrecision) {
				REX::WARN(
					"skipped display clip rect: unsupported '{}' layout vertices={}, stride={}, posOffset={}, uvOffset={}, fullPrecision={}, desc={:X}",
					kDisplayMeshGeometry,
					triShape->numVertices,
					stride,
					positionOffset,
					uvOffset,
					fullPrecision,
					desc);
				return;
			}

			constexpr float planeY = -0.0000013113022F;
			const auto vertexBytes = static_cast<std::size_t>(triShape->numVertices) * stride;
			constexpr std::size_t kUploadPadding = 32;
			std::array<std::byte, 128> uploadData{};
			if (vertexBytes + kUploadPadding > uploadData.size()) {
				REX::WARN(
					"skipped display clip rect: upload buffer too small vertices={}, stride={}, bytes={}",
					triShape->numVertices,
					stride,
					vertexBytes);
				return;
			}

			const std::array<std::array<float, 3>, 4> positions{ {
				{ bounds.left, planeY, bounds.bottom },
				{ bounds.right, planeY, bounds.bottom },
				{ bounds.right, planeY, bounds.top },
				{ bounds.left, planeY, bounds.top }
			} };
			const auto uvLeft = std::clamp(NormalizeDisplayX(bounds.left), 0.0F, 1.0F);
			// Vanilla ModMenuRenderMesh.nif uses z=-79.875 -> V=1 and z=+79.875 -> V=0.
			const auto uvTop = std::clamp(NormalizeDisplayZ(bounds.top), 0.0F, 1.0F);
			const auto uvRight = std::clamp(NormalizeDisplayX(bounds.right), 0.0F, 1.0F);
			const auto uvBottom = std::clamp(NormalizeDisplayZ(bounds.bottom), 0.0F, 1.0F);
			const std::array<std::array<float, 2>, 4> uvs{ {
				{ uvLeft, uvBottom },
				{ uvRight, uvBottom },
				{ uvRight, uvTop },
				{ uvLeft, uvTop }
			} };

			auto* vertexData = static_cast<std::byte*>(vertexBuffer->data);
			for (std::uint16_t i = 0; i < triShape->numVertices; ++i) {
				auto* position = vertexData + (static_cast<std::size_t>(i) * stride) + positionOffset;
				WriteHalf(position, positions[i][0]);
				WriteHalf(position + 2, positions[i][1]);
				WriteHalf(position + 4, positions[i][2]);
				WriteHalf(position + 6, 1.0F);

				auto* uv = vertexData + (static_cast<std::size_t>(i) * stride) + uvOffset;
				WriteHalf(uv, uvs[i][0]);
				WriteHalf(uv + 2, uvs[i][1]);
			}
			std::memcpy(uploadData.data(), vertexData, vertexBytes);

			if (auto* rendererDataSingleton = RE::BSGraphics::GetRendererData();
				rendererDataSingleton && rendererDataSingleton->context && vertexBuffer->buffer) {
				const REX::W32::D3D11_BOX updateBox{
					.left = 0,
					.top = 0,
					.front = 0,
					.right = static_cast<std::uint32_t>(vertexBytes),
					.bottom = 1,
					.back = 1
				};
				rendererDataSingleton->context->UpdateSubresource(
					vertexBuffer->buffer,
					0,
					std::addressof(updateBox),
					uploadData.data(),
					static_cast<std::uint32_t>(vertexBytes),
					static_cast<std::uint32_t>(vertexBytes));
			} else {
				vertexBuffer->pendingCopy = true;
			}

			const auto centerX = (bounds.left + bounds.right) * 0.5F;
			const auto centerZ = (bounds.top + bounds.bottom) * 0.5F;
			const auto halfX = (bounds.right - bounds.left) * 0.5F;
			const auto halfZ = std::abs(bounds.top - bounds.bottom) * 0.5F;
			const auto radius = std::sqrt((halfX * halfX) + (halfZ * halfZ));
			geometry->modelBound.center = { centerX, planeY, centerZ };
			geometry->modelBound.fRadius = radius;

			RE::NiUpdateData updateData;
			g_displayRoot->Update(updateData);
		}

		void ApplyDisplayPlacement()
		{
			if (!g_displayRoot) {
				return;
			}

			const auto& config = GetConfig();
			const auto screenBounds = GetScreenPlaneBounds();
			const auto centerX = (screenBounds.left + screenBounds.right) * 0.5F;
			const auto centerZ = (screenBounds.top + screenBounds.bottom) * 0.5F;

			float anchorX = centerX;
			float anchorZ = centerZ;
			switch (config.anchor) {
			case 1:
				anchorX = screenBounds.left;
				anchorZ = screenBounds.bottom;
				break;
			case 2:
				anchorX = centerX;
				anchorZ = screenBounds.bottom;
				break;
			case 3:
				anchorX = screenBounds.right;
				anchorZ = screenBounds.bottom;
				break;
			case 4:
				anchorX = screenBounds.left;
				anchorZ = centerZ;
				break;
			case 6:
				anchorX = screenBounds.right;
				anchorZ = centerZ;
				break;
			case 7:
				anchorX = screenBounds.left;
				anchorZ = screenBounds.top;
				break;
			case 8:
				anchorX = centerX;
				anchorZ = screenBounds.top;
				break;
			case 9:
				anchorX = screenBounds.right;
				anchorZ = screenBounds.top;
				break;
			case 5:
			default:
				break;
			}

			g_displayRoot->SetLocalTranslate({
				anchorX + config.placementX,
				kDisplayRootY,
				anchorZ + config.placementY
			});

			RE::NiUpdateData updateData;
			g_displayRoot->Update(updateData);
		}

		void ConfigureImpl()
		{
			const auto name = RE::BSFixedString(kRendererName);
			const auto& config = GetConfig();
			g_renderer = RE::Interface3D::Renderer::GetByName(name);
			bool createdRenderer = false;
			if (!g_renderer) {
				g_renderer = RE::Interface3D::Renderer::Create(
					name,
					RE::UI_DEPTH_PRIORITY::kStandard3DModel,
					config.fov,
					false);
				createdRenderer = true;
			}

			if (!g_renderer) {
				REX::WARN("Interface3D renderer creation returned null");
				return;
			}

			REX::INFO(
				"Interface3D renderer {}: renderer={:X}",
				createdRenderer ? "created" : "reused",
				Ptr(g_renderer));

			g_renderer->MainScreen_SetBackgroundMode(RE::Interface3D::BackgroundMode::kLive);
			g_renderer->useFullPremultAlpha = true;
			ApplyRendererFOV(config.fov);
			g_renderer->postAA = true;
			g_renderer->Offscreen_Enable3D(true);
			g_renderer->Offscreen_SetUseLongRangeCamera(true);
			g_renderer->Offscreen_SetRenderTargetSize(RE::Interface3D::OffscreenMenuSize::kFullFrame);
			g_renderer->Offscreen_SetDisplayMode(
				RE::Interface3D::ScreenMode::kScreenAttached,
				kDisplayMeshGeometry,
				nullptr);
			g_renderer->MainScreen_EnableScreenAttached3DMasking(nullptr, nullptr);
			g_renderer->Offscreen_SetPostEffect(RE::Interface3D::PostEffect::kModMenu);
			// IDA: for kModMenu + kFullFrame + kScreenAttached, vanilla display sampling
			// selects RT63 while DrawPostFX otherwise derives RT64. Pin the native custom
			// render target so both paths use the same full-frame display target.
			g_renderer->customRenderTarget = kFullFrameDisplayRenderTarget;
			g_renderer->customSwapTarget = -1;

			if (EnsureDisplayRoot()) {
				ApplyDisplayClipRect();
				ApplyDisplayPlacement();
				g_renderer->MainScreen_SetScreenAttached3D(g_displayRoot.get());
				g_renderer->MainScreen_RegisterGeometryRequiringFullViewport(g_displayRoot.get());
			}

			REX::INFO(
				"Interface3D renderer roots: renderer={:X}, screenSSN={:X}, offscreenSSN={:X}, screenRoot={:X}, offscreenRoot={:X}, displayRoot={:X}",
				Ptr(g_renderer),
				Ptr(g_renderer->screenSSN.get()),
				Ptr(g_renderer->offscreenSSN.get()),
				Ptr(g_renderer->screenAttachedElementRoot.get()),
				Ptr(g_renderer->offscreenElement.get()),
				Ptr(g_displayRoot.get()));

			g_rendererConfigured = true;
		}

		[[nodiscard]] float Lerp(const float a_from, const float a_to, const float a_t)
		{
			return a_from + (a_to - a_from) * a_t;
		}

		[[nodiscard]] RE::NiPoint3 LerpPoint(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, const float a_t)
		{
			return {
				Lerp(a_from.x, a_to.x, a_t),
				Lerp(a_from.y, a_to.y, a_t),
				Lerp(a_from.z, a_to.z, a_t)
			};
		}

		[[nodiscard]] RE::NiColor LerpColor(const RE::NiColor& a_from, const RE::NiColor& a_to, const float a_t)
		{
			return {
				Lerp(a_from.r, a_to.r, a_t),
				Lerp(a_from.g, a_to.g, a_t),
				Lerp(a_from.b, a_to.b, a_t)
			};
		}

		[[nodiscard]] RE::NiPoint3 LightPosition(const LightSettings& a_settings)
		{
			return { a_settings.positionX, a_settings.positionY, a_settings.positionZ };
		}

		[[nodiscard]] RE::NiColor LightDiffuse(const LightSettings& a_settings)
		{
			return { a_settings.diffuseR, a_settings.diffuseG, a_settings.diffuseB };
		}

		[[nodiscard]] RE::NiColor LightSpecular(const LightSettings& a_settings)
		{
			return { a_settings.specularR, a_settings.specularG, a_settings.specularB };
		}

		[[nodiscard]] float NightLightingBlend(const float a_timeOfDay)
		{
			const auto hour = std::fmod(std::fmod(a_timeOfDay, 24.0F) + 24.0F, 24.0F);
			if (hour < 6.0F) {
				return 1.0F;
			}
			if (hour < 8.0F) {
				return 1.0F - ((hour - 6.0F) / 2.0F);
			}
			if (hour < 18.0F) {
				return 0.0F;
			}
			if (hour < 20.0F) {
				return (hour - 18.0F) / 2.0F;
			}
			return 1.0F;
		}

		[[nodiscard]] bool IsInteriorWithLightingTemplate()
		{
			const auto* player = RE::PlayerCharacter::GetSingleton();
			const auto* parentCell = player ? player->GetParentCell() : nullptr;
			return parentCell && parentCell->IsInterior() && parentCell->lightingTemplate;
		}

		[[nodiscard]] bool GetSunDirection(const RE::NiLight& a_sunLight, RE::NiPoint3& a_direction)
		{
			a_direction = a_sunLight.world.rotate * RE::NiPoint3{ 0.0F, 1.0F, 0.0F };
			if (a_direction.Unitize() <= 0.0001F) {
				return false;
			}
			return true;
		}

		[[nodiscard]] float LightPositionRadius(const LightSettings& a_settings)
		{
			const auto position = LightPosition(a_settings);
			return std::max(position.Length(), 1.0F);
		}

		void AddFakePointLight(const RE::NiPoint3& a_position, const RE::NiColor& a_diffuse, const RE::NiColor& a_specular, const float a_intensity)
		{
			g_renderer->Offscreen_AddLight(a_position, a_diffuse, a_specular, a_intensity);
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

		[[nodiscard]] bool SameLightState(const AppliedLightState& a_lhs, const AppliedLightState& a_rhs)
		{
			return SamePoint(a_lhs.position, a_rhs.position) &&
			       SameColor(a_lhs.diffuse, a_rhs.diffuse) &&
			       SameColor(a_lhs.specular, a_rhs.specular) &&
			       NearlyEqual(a_lhs.intensity, a_rhs.intensity);
		}

		void ClearOffscreenLightParamsOnly()
		{
			// IDA: Interface3D::Renderer::Offscreen_ClearLights also calls
			// ShadowSceneNode::RemoveAllLights(). For normal preview light refresh,
			// only reset the renderer-owned light-param array and let DrawModel's
			// UpdateLights rebuild the isolated offscreen SSN when needed.
			g_renderer->offscreenLights.clear();
			g_renderer->needsLightSetupOffscreen = true;
		}

		void ConfigureLightingImpl()
		{
			if (!g_renderer) {
				return;
			}

			const auto& config = GetConfig();

			auto effectiveLighting = config.lighting;
			const auto* sky = RE::Sky::GetSingleton();
			const auto interiorForcedFakePoint = IsInteriorWithLightingTemplate();
			if (interiorForcedFakePoint) {
				effectiveLighting = LightingType::kFakePoint;
			}

			RE::NiPoint3 appliedPosition = LightPosition(config.light);
			RE::NiColor appliedDiffuse = LightDiffuse(config.light);
			RE::NiColor appliedSpecular = LightSpecular(config.light);
			float appliedIntensity = config.light.intensity;

			switch (effectiveLighting) {
			case LightingType::kWorldDirectional:
				if (sky && sky->sun && sky->sun->light) {
					const auto* sunLight = reinterpret_cast<const RE::NiLight*>(sky->sun->light.get());
					RE::NiPoint3 sunDirection;
					if (sunLight && GetSunDirection(*sunLight, sunDirection)) {
						appliedPosition = sunDirection * LightPositionRadius(config.light);
					}
				}
				break;
			case LightingType::kFakePointAdaptiveTime:
			{
				const auto blend = sky ? NightLightingBlend(sky->currentGameHour) : 0.0F;
				appliedPosition = LerpPoint(LightPosition(config.light), LightPosition(config.nightLight), blend);
				appliedDiffuse = LerpColor(LightDiffuse(config.light), LightDiffuse(config.nightLight), blend);
				appliedSpecular = LerpColor(LightSpecular(config.light), LightSpecular(config.nightLight), blend);
				appliedIntensity = Lerp(config.light.intensity, config.nightLight.intensity, blend);
				break;
			}
			case LightingType::kFakePoint:
			default:
				break;
			}

			const AppliedLightState state{
				.position = appliedPosition,
				.diffuse = appliedDiffuse,
				.specular = appliedSpecular,
				.intensity = appliedIntensity
			};
			if (g_hasAppliedLightState && SameLightState(g_appliedLightState, state)) {
				return;
			}

			ClearOffscreenLightParamsOnly();
			AddFakePointLight(state.position, state.diffuse, state.specular, state.intensity);
			g_appliedLightState = state;
			g_hasAppliedLightState = true;
			REX::INFO(
				"Interface3D offscreen light configured: renderer={:X}, offscreenSSN={:X}, lights={}, pos=({}, {}, {}), intensity={}",
				Ptr(g_renderer),
				Ptr(g_renderer->offscreenSSN.get()),
				g_renderer->offscreenLights.size(),
				state.position.x,
				state.position.y,
				state.position.z,
				state.intensity);
		}

		void ApplyOffscreenFramingImpl(RE::NiAVObject& a_object)
		{
			const auto& config = GetConfig();
			RE::NiTransform transform = RE::NiTransform::IDENTITY;
			transform.scale = config.modelScale;
			transform.rotate.FromEulerAnglesXYZ(0.0F, 0.0F, config.yawDegrees * kPi / 180.0F);
			a_object.SetLocalTransform(transform);

			RE::NiUpdateData updateData;
			a_object.Update(updateData);

			const auto center = a_object.worldBound.center;
			auto centeredTransform = a_object.GetLocalTransform();
			centeredTransform.translate = {
				0.0F,
				-center.y + config.cameraDistance,
				0.0F
			};
			a_object.SetLocalTransform(centeredTransform);
			a_object.Update(updateData);
		}

		RE::NiPointer<RE::NiAVObject> LoadDisplayRoot()
		{
			RE::NiPointer<RE::NiNode> loadedRoot;
			const auto result = DemandDisplayRoot(kDisplayMeshPath, loadedRoot);
			if (!loadedRoot) {
				REX::WARN(
					"failed to load Interface3D Pass #2 display mesh '{}'; BSModelDB result={}",
					kDisplayMeshPath,
					static_cast<std::uint32_t>(result));
				return nullptr;
			}

			auto displayRoot = CloneDisplayObject(*loadedRoot);
			if (!displayRoot) {
				displayRoot = loadedRoot.get();
			}

			// IDA: WorkbenchMenuBase::UpdateMenu writes local translate {0, 375, 0}
			// to ModMenuRenderMesh before NiAVObject::Update.
			displayRoot->SetLocalTranslate({ 0.0F, kDisplayRootY, 0.0F });
			RE::NiUpdateData updateData;
			displayRoot->Update(updateData);
			return displayRoot;
		}

		bool EnsureDisplayRoot()
		{
			if (g_displayRoot) {
				return true;
			}

			g_displayRoot = LoadDisplayRoot();
			ApplyDisplayClipRect();
			ApplyDisplayPlacement();
			return static_cast<bool>(g_displayRoot);
		}
	}

	RE::Interface3D::Renderer* Get()
	{
		return g_renderer;
	}

	bool IsConfigured()
	{
		return g_rendererConfigured;
	}

	bool Enable()
	{
		if (!g_renderer || g_visible) {
			return false;
		}

		g_renderer->Enable(false);
		g_visible = true;
		return true;
	}

	void Hide()
	{
		HideImpl();
	}

	void Configure()
	{
		ConfigureImpl();
	}

	void ConfigureLighting()
	{
		ConfigureLightingImpl();
	}

	void ApplyOffscreenFraming(RE::NiAVObject& a_object)
	{
		ApplyOffscreenFramingImpl(a_object);
	}

	void Reset()
	{
		ClearPreviewRoot();
		if (g_renderer) {
			g_renderer->MainScreen_SetScreenAttached3D(nullptr);
		}
		g_displayRoot.reset();
		g_rendererConfigured = false;
		g_visible = false;
		g_hasAppliedLightState = false;
	}

	void ClearPreviewRoot(const bool a_disableRenderer)
	{
		if (g_renderer) {
			if (a_disableRenderer) {
				HideImpl();
			}
			g_renderer->Offscreen_Set3D(nullptr);
		}
		if (a_disableRenderer) {
			g_visible = false;
		}
	}

	void AttachPreviewRoot(RE::NiAVObject& a_previewRoot)
	{
		if (!g_renderer) {
			return;
		}

		if (EnsureDisplayRoot()) {
			ApplyDisplayClipRect();
			ApplyDisplayPlacement();
			g_renderer->MainScreen_SetScreenAttached3D(g_displayRoot.get());
			g_renderer->MainScreen_RegisterGeometryRequiringFullViewport(g_displayRoot.get());
		}
		g_forceUpgradeTextures(std::addressof(a_previewRoot), false, false);
		g_renderer->Offscreen_Set3D(std::addressof(a_previewRoot));
		ConfigureLightingImpl();
	}
}
