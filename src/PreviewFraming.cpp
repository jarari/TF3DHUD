#include "PreviewFraming.h"

#include "Config.h"
#include "Renderer.h"
#include "Utils.h"

#include "RE/N/NiUpdateData.h"

namespace TF3DHud::PreviewFraming
{
	namespace
	{
		[[nodiscard]] RE::BSFlattenedBoneTree* ResolveFlattenedTree(
			RE::NiAVObject& a_previewRoot,
			RE::BSFlattenedBoneTree*& a_flattenedCache)
		{
			if (!a_flattenedCache) {
				a_flattenedCache = FindFlattenedBoneTree(std::addressof(a_previewRoot));
			}
			return a_flattenedCache;
		}

		[[nodiscard]] bool TryGetPreviewHeadWorld(
			RE::NiAVObject& a_previewRoot,
			RE::BSFlattenedBoneTree*& a_flattenedCache,
			RE::NiPoint3& a_out)
		{
			auto* flattened = ResolveFlattenedTree(a_previewRoot, a_flattenedCache);
			if (!flattened) {
				return false;
			}

			if (auto* headObject = flattened->GetObjectByName(RE::BSFixedString("HEAD"))) {
				a_out = headObject->world.translate;
				return true;
			}

			auto* head = FindFlattenedBoneByName(*flattened, "HEAD");
			if (!head) {
				return false;
			}

			a_out = head->node ? head->node->world.translate : head->world.translate;
			return true;
		}
	}

	bool ApplyHeadCentered(RE::NiAVObject& a_previewRoot, RE::BSFlattenedBoneTree*& a_flattenedCache)
	{
		auto* flattened = ResolveFlattenedTree(a_previewRoot, a_flattenedCache);
		if (!flattened) {
			Renderer::ApplyOffscreenFraming(a_previewRoot);
			REX::WARN("head-centered framing fell back to bounds: no cached BSFlattenedBoneTree");
			return false;
		}

		auto* headObject = flattened->GetObjectByName(RE::BSFixedString("HEAD"));
		auto* head = headObject ? nullptr : FindFlattenedBoneByName(*flattened, "HEAD");
		if (!headObject && !head) {
			Renderer::ApplyOffscreenFraming(a_previewRoot);
			REX::WARN(
				"head-centered framing fell back to bounds: HEAD bone not found in flattenedTree='{}', bones={}",
				flattened->GetName(),
				flattened->boneCount);
			return false;
		}

		const auto& config = GetConfig();
		RE::NiTransform transform = RE::NiTransform::IDENTITY;
		transform.scale = config.modelScale;
		transform.rotate.FromEulerAnglesXYZ(0.0F, 0.0F, config.yawDegrees * 3.14159265358979323846F / 180.0F);
		a_previewRoot.SetLocalTransform(transform);

		RE::NiUpdateData updateData;
		a_previewRoot.Update(updateData);

		const auto headWorld =
			headObject ? headObject->world.translate : (head->node ? head->node->world.translate : head->world.translate);
		auto centeredTransform = a_previewRoot.GetLocalTransform();
		centeredTransform.translate = {
			-headWorld.x,
			-headWorld.y + config.cameraDistance,
			-headWorld.z
		};
		a_previewRoot.SetLocalTransform(centeredTransform);
		a_previewRoot.Update(updateData);

		return true;
	}

	void ApplyHeadFollowTranslation(RE::NiAVObject& a_previewRoot, RE::BSFlattenedBoneTree*& a_flattenedCache)
	{
		RE::NiPoint3 headWorld;
		if (!TryGetPreviewHeadWorld(a_previewRoot, a_flattenedCache, headWorld)) {
			return;
		}

		const auto& config = GetConfig();
		auto transform = a_previewRoot.GetLocalTransform();
		transform.translate.x += -headWorld.x;
		transform.translate.y += config.cameraDistance - headWorld.y;
		transform.translate.z += -headWorld.z;
		a_previewRoot.SetLocalTransform(transform);

		RE::NiUpdateData updateData;
		a_previewRoot.Update(updateData);
	}
}
