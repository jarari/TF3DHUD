#include "Renderer.h"

#include "Config.h"

#include "RE/B/BSGraphics.h"
#include "RE/B/BSModelDB.h"
#include "RE/N/NiCloningProcess.h"
#include "RE/N/NiLight.h"
#include "RE/N/NiUpdateData.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/S/Sky.h"
#include "RE/S/Sun.h"
#include "RE/T/TESObjectCELL.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace TF3DHud::Renderer
{
	namespace
	{
		constexpr auto kRendererName = "TF3DHudRenderer";
		constexpr auto kDisplayMeshPath = "Interface/GunModMenu/ModMenuRenderMesh.nif";
		constexpr auto kDisplayMeshGeometry = "ModMenuRenderMesh:0";
		constexpr auto kPi = 3.14159265358979323846F;
		constexpr std::int32_t kUseNativeOffscreenDisplayTarget = -1;

		using ForceUpgradeTextures_t = void(RE::NiAVObject*, bool, bool);

		REL::Relocation<ForceUpgradeTextures_t*> g_forceUpgradeTextures{ REL::ID{ 1417022, 2229490 } };
		REL::Relocation<RE::BSGraphics::RenderTargetManager*> g_renderTargetManager{ REL::ID{ 1508457, 2666735 } };

		RE::Interface3D::Renderer* g_renderer{ nullptr };
		RE::NiPointer<RE::NiAVObject> g_displayRoot;
		bool g_visible{ false };
		bool g_rendererConfigured{ false };

		bool EnsureDisplayRoot();

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

		void HideImpl()
		{
			if (g_renderer && g_visible) {
				g_renderer->Disable();
				g_visible = false;
			}
		}


		[[nodiscard]] bool IsDynamicResolutionActive()
		{
			auto& renderTargetManager = *g_renderTargetManager;
			return
				renderTargetManager.dynamicWidthRatio != 1.0F ||
				renderTargetManager.dynamicHeightRatio != 1.0F ||
				renderTargetManager.isDynamicResolutionCurrentlyActivated;
		}

		void UpdatePostAAForDynamicResolutionImpl(const char* a_stage)
		{
			if (!g_renderer) {
				return;
			}

			const bool dynamicResolutionActive = IsDynamicResolutionActive();
			if (g_renderer->postAA == dynamicResolutionActive) {
				return;
			}

			g_renderer->postAA = dynamicResolutionActive;
			auto& renderTargetManager = *g_renderTargetManager;
			REX::INFO(
				"TF3DHud V1 updated RenderAll pass selector [{}]: postAA={}, dynamicRatio=({}, {}), dynamicActive={}",
				a_stage,
				g_renderer->postAA,
				renderTargetManager.dynamicWidthRatio,
				renderTargetManager.dynamicHeightRatio,
				renderTargetManager.isDynamicResolutionCurrentlyActivated);
		}

		void ConfigureImpl()
		{
			const auto name = RE::BSFixedString(kRendererName);
			const auto& config = GetConfig();
			g_renderer = RE::Interface3D::Renderer::GetByName(name);
			const bool reusedRenderer = g_renderer != nullptr;
			if (!g_renderer) {
				g_renderer = RE::Interface3D::Renderer::Create(
					name,
					RE::UI_DEPTH_PRIORITY::kStandard3DModel,
					config.fov,
					false);
			}

			if (!g_renderer) {
				REX::WARN("TF3DHud V1 Interface3D renderer creation returned null");
				return;
			}

			REX::INFO(
				"TF3DHud V1 renderer acquired via Interface3D::Renderer::{}('{}'): renderer={:X}",
				reusedRenderer ? "GetByName" : "Create",
				kRendererName,
				reinterpret_cast<std::uintptr_t>(g_renderer));

			g_renderer->MainScreen_SetBackgroundMode(RE::Interface3D::BackgroundMode::kLive);
			g_renderer->MainScreen_SetHideScreenWhenDisabled(false);
			g_renderer->MainScreen_SetMenuBlendMode(RE::Interface3D::OffscreenMenuBlendMode::kAlpha);
			g_renderer->MainScreen_SetMenuIntensity(1.0F, 1.0F);
			g_renderer->MainScreen_SetOpacityAlpha(1.0F);
			g_renderer->usePremultAlpha = true;
			g_renderer->useFullPremultAlpha = true;
			g_renderer->alwaysRenderWhenEnabled = false;
			// IDA: WorkbenchItem3D leaves Renderer+0x53 at Reset's default false.
			// Keep it true only when dynamic resolution/upscaler routing needs that pass.
			g_renderer->postAA = false;
			UpdatePostAAForDynamicResolutionImpl("configured");
			g_renderer->defRenderMainScreen = false;
			g_renderer->depth = RE::UI_DEPTH_PRIORITY::kStandard3DModel;
			g_renderer->Offscreen_Enable3D(true);
			g_renderer->Offscreen_SetUseLongRangeCamera(true);
			g_renderer->Offscreen_SetBackgroundColor(RE::NiColorA{ 0.0F, 0.0F, 0.0F, 0.0F });
			g_renderer->Offscreen_SetClearRenderTarget(true);
			g_renderer->MainScreen_SetClearDepthStencil(true);
			g_renderer->Offscreen_SetClearDepthStencil(true);
			g_renderer->Offscreen_SetRenderTargetSize(RE::Interface3D::OffscreenMenuSize::kFullFrame);
			g_renderer->Offscreen_SetDisplayMode(
				RE::Interface3D::ScreenMode::kScreenAttached,
				kDisplayMeshGeometry,
				nullptr);
			g_renderer->Offscreen_SetPostEffect(RE::Interface3D::PostEffect::kModMenu);
			// IDA: WorkbenchItem3D leaves customRT/customSwap at -1. RenderPrepassesAndMenus
			// draws the model into RT52, then resolves it to the display-sampled target
			// selected by InitialOffscreenTarget() before SetupMaterial binds dword_146721BD4.
			g_renderer->customRenderTarget = kUseNativeOffscreenDisplayTarget;
			g_renderer->customSwapTarget = kUseNativeOffscreenDisplayTarget;

			if (EnsureDisplayRoot()) {
				g_renderer->MainScreen_SetScreenAttached3D(g_displayRoot.get());
				g_renderer->MainScreen_RegisterGeometryRequiringFullViewport(g_displayRoot.get());
			}

			g_rendererConfigured = true;
			REX::INFO("TF3DHud V1 renderer configured");
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

		void ConfigureLightingImpl()
		{
			if (!g_renderer) {
				return;
			}

			g_renderer->MainScreen_ClearLights();
			g_renderer->Offscreen_ClearLights();
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
					const auto& sunColor = sky->skyColor[4];
					appliedDiffuse = { sunColor.r, sunColor.g, sunColor.b };
					appliedSpecular = sky->sunSpecularColor;
					appliedIntensity = sunLight ? sunLight->dimmer : config.light.intensity;
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

			AddFakePointLight(appliedPosition, appliedDiffuse, appliedSpecular, appliedIntensity);

			REX::INFO(
				"TF3DHud V1 configured presentation lighting: requestedMode={}, effectiveMode={}, interiorForcedFakePoint={}, timeOfDay={}, position=({}, {}, {}), diffuse=({}, {}, {}), specular=({}, {}, {}), intensity={}, mainLights={}, offscreenLights={}, needsLightSetupOffscreen={}",
				std::to_underlying(config.lighting),
				std::to_underlying(effectiveLighting),
				interiorForcedFakePoint,
				sky ? sky->currentGameHour : -1.0F,
				appliedPosition.x,
				appliedPosition.y,
				appliedPosition.z,
				appliedDiffuse.r,
				appliedDiffuse.g,
				appliedDiffuse.b,
				appliedSpecular.r,
				appliedSpecular.g,
				appliedSpecular.b,
				appliedIntensity,
				g_renderer->mainLights.size(),
				g_renderer->offscreenLights.size(),
				g_renderer->needsLightSetupOffscreen);
		}

		void ApplyOffscreenFramingImpl(RE::NiAVObject& a_object, bool a_log)
		{
			const auto& config = GetConfig();
			RE::NiTransform transform = RE::NiTransform::IDENTITY;
			transform.scale = config.modelScale;
			transform.rotate.FromEulerAnglesXYZ(0.0F, 0.0F, config.yawDegrees * kPi / 180.0F);
			a_object.SetLocalTransform(transform);

			RE::NiUpdateData updateData;
			a_object.Update(updateData);

			const auto center = a_object.worldBound.center;
			const auto effectivePlacementY = config.placementY + center.z;
			auto centeredTransform = a_object.GetLocalTransform();
			centeredTransform.translate = {
				-center.x + config.placementX,
				-center.y + config.cameraDistance,
				-center.z + effectivePlacementY
			};
			a_object.SetLocalTransform(centeredTransform);
			a_object.Update(updateData);

			if (a_log) {
				REX::INFO(
					"TF3DHud V1 offscreen framed preview root: sourceCenter=({}, {}, {}), placement=({}, {}, +{} cameraY), effectivePlacementY={}, localTranslate=({}, {}, {}), scale={}, yawDegrees={}, boundCenter=({}, {}, {}), boundRadius={}",
					center.x,
					center.y,
					center.z,
					config.placementX,
					config.placementY,
					config.cameraDistance,
					effectivePlacementY,
					a_object.GetLocalTranslate().x,
					a_object.GetLocalTranslate().y,
					a_object.GetLocalTranslate().z,
					centeredTransform.scale,
					config.yawDegrees,
					a_object.worldBound.center.x,
					a_object.worldBound.center.y,
					a_object.worldBound.center.z,
					a_object.worldBound.fRadius);
			}
		}

		RE::NiPointer<RE::NiAVObject> LoadDisplayRoot()
		{
			RE::BSModelDB::DBTraits::ArgsType args{};
			args.prepareAfterLoad = true;
			args.useErrorMarker = true;
			args.performProcess = true;
			args.createFadeNode = true;
			args.loadTextures = true;

			RE::NiPointer<RE::NiNode> loadedRoot;
			const auto result = RE::BSModelDB::Demand(kDisplayMeshPath, std::addressof(loadedRoot), args);
			if (!loadedRoot) {
				REX::WARN(
					"TF3DHud V1 failed to load display mesh '{}'; BSModelDB result={}",
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
			displayRoot->SetLocalTranslate({ 0.0F, 375.0F, 0.0F });
			RE::NiUpdateData updateData;
			displayRoot->Update(updateData);
			REX::INFO(
				"TF3DHud V1 loaded workbench-style display mesh '{}'; root='{}', flags={:016X}, boundRadius={}",
				kDisplayMeshPath,
				displayRoot->GetName(),
				displayRoot->GetFlags(),
				displayRoot->worldBound.fRadius);
			return displayRoot;
		}

		bool EnsureDisplayRoot()
		{
			if (g_displayRoot) {
				return true;
			}

			g_displayRoot = LoadDisplayRoot();
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

	void ApplyOffscreenFraming(RE::NiAVObject& a_object, bool a_log)
	{
		ApplyOffscreenFramingImpl(a_object, a_log);
	}

	void UpdatePostAAForDynamicResolution(const char* a_stage)
	{
		UpdatePostAAForDynamicResolutionImpl(a_stage);
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
	}

	void ClearPreviewRoot()
	{
		if (g_renderer) {
			g_renderer->Offscreen_Set3D(nullptr);
		}
	}

	void AttachPreviewRoot(RE::NiAVObject& a_previewRoot)
	{
		if (!g_renderer) {
			return;
		}

		if (EnsureDisplayRoot()) {
			g_renderer->MainScreen_SetScreenAttached3D(g_displayRoot.get());
			g_renderer->MainScreen_RegisterGeometryRequiringFullViewport(g_displayRoot.get());
		}
		g_forceUpgradeTextures(std::addressof(a_previewRoot), false, false);
		g_renderer->Offscreen_Set3D(std::addressof(a_previewRoot));
		ConfigureLightingImpl();
	}
}
