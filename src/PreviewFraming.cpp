#include "PreviewFraming.h"

#include "Config.h"
#include "Renderer.h"
#include "Utils.h"

#include "RE/N/NiUpdateData.h"

#include <string_view>

namespace TF3DHud::PreviewFraming
{
	namespace
	{
		[[nodiscard]] const char* TargetName(const CameraFramingTarget a_target)
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

		[[nodiscard]] RE::BSFlattenedBoneTree* ResolveFlattenedTree(
			RE::NiAVObject& a_previewRoot,
			RE::BSFlattenedBoneTree*& a_flattenedCache)
		{
			if (!a_flattenedCache) {
				a_flattenedCache = FindFlattenedBoneTree(std::addressof(a_previewRoot));
			}
			return a_flattenedCache;
		}

		[[nodiscard]] bool TryGetNamedTargetWorld(
			RE::NiAVObject& a_previewRoot,
			RE::BSFlattenedBoneTree*& a_flattenedCache,
			const std::string_view a_name,
			RE::NiPoint3& a_out)
		{
			auto* flattened = ResolveFlattenedTree(a_previewRoot, a_flattenedCache);
			if (flattened) {
				const RE::BSFixedString targetName(a_name.data());
				if (auto* object = flattened->GetObjectByName(targetName)) {
					a_out = object->world.translate;
					return true;
				}

				if (auto* bone = FindFlattenedBoneByName(*flattened, a_name)) {
					a_out = bone->node ? bone->node->world.translate : bone->world.translate;
					return true;
				}
			}

			if (auto* object = a_previewRoot.GetObjectByName(RE::BSFixedString(a_name.data()))) {
				a_out = object->world.translate;
				return true;
			}

			return false;
		}

		[[nodiscard]] bool TryGetFramingTargetWorld(
			RE::NiAVObject& a_previewRoot,
			RE::BSFlattenedBoneTree*& a_flattenedCache,
			RE::NiPoint3& a_out)
		{
			switch (GetConfig().camera.target) {
			case CameraFramingTarget::kHead:
				return TryGetNamedTargetWorld(a_previewRoot, a_flattenedCache, "Head", a_out);
			case CameraFramingTarget::kChest:
				return TryGetNamedTargetWorld(a_previewRoot, a_flattenedCache, "Chest", a_out);
			case CameraFramingTarget::kPelvis:
				return TryGetNamedTargetWorld(a_previewRoot, a_flattenedCache, "Pelvis", a_out);
			case CameraFramingTarget::kRoot:
				a_out = a_previewRoot.world.translate;
				return true;
			}

			return TryGetNamedTargetWorld(a_previewRoot, a_flattenedCache, "Head", a_out);
		}
	}

	bool ApplyTargetCentered(RE::NiAVObject& a_previewRoot, RE::BSFlattenedBoneTree*& a_flattenedCache)
	{
		const auto& config = GetConfig();
		RE::NiTransform transform = RE::NiTransform::IDENTITY;
		transform.scale = config.modelScale;
		transform.rotate.FromEulerAnglesXYZ(0.0F, 0.0F, config.yawDegrees * 3.14159265358979323846F / 180.0F);
		a_previewRoot.SetLocalTransform(transform);

		RE::NiUpdateData updateData;
		a_previewRoot.Update(updateData);

		RE::NiPoint3 targetWorld;
		if (!TryGetFramingTargetWorld(a_previewRoot, a_flattenedCache, targetWorld)) {
			Renderer::ApplyOffscreenFraming(a_previewRoot);
			if (auto* flattened = ResolveFlattenedTree(a_previewRoot, a_flattenedCache)) {
				REX::WARN(
					"target-centered framing fell back to bounds: {} target not found in flattenedTree='{}', bones={}",
					TargetName(config.camera.target),
					flattened->GetName(),
					flattened->boneCount);
			} else {
				REX::WARN(
					"target-centered framing fell back to bounds: {} target not found and no cached BSFlattenedBoneTree",
					TargetName(config.camera.target));
			}
			return false;
		}

		auto centeredTransform = a_previewRoot.GetLocalTransform();
		centeredTransform.translate = {
			-targetWorld.x,
			-targetWorld.y + config.cameraDistance,
			-targetWorld.z
		};
		a_previewRoot.SetLocalTransform(centeredTransform);
		a_previewRoot.Update(updateData);

		return true;
	}

	void ApplyTargetFollowTranslation(RE::NiAVObject& a_previewRoot, RE::BSFlattenedBoneTree*& a_flattenedCache)
	{
		const auto& config = GetConfig();
		if (!config.camera.follow || (!config.camera.followX && !config.camera.followY && !config.camera.followZ)) {
			return;
		}

		RE::NiPoint3 targetWorld;
		if (!TryGetFramingTargetWorld(a_previewRoot, a_flattenedCache, targetWorld)) {
			return;
		}

		auto transform = a_previewRoot.GetLocalTransform();
		if (config.camera.followX) {
			transform.translate.x += -targetWorld.x;
		}
		if (config.camera.followY) {
			transform.translate.y += config.cameraDistance - targetWorld.y;
		}
		if (config.camera.followZ) {
			transform.translate.z += -targetWorld.z;
		}
		a_previewRoot.SetLocalTransform(transform);

		RE::NiUpdateData updateData;
		a_previewRoot.Update(updateData);
	}
}
