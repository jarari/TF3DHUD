#include "PreviewRenderTree.h"

#include "Utils.h"

#include "RE/B/BSFadeNode.h"
#include "RE/B/BSGeometry.h"
#include "RE/B/BSLightingShaderProperty.h"
#include "RE/B/BSShaderMaterial.h"
#include "RE/B/BSShaderProperty.h"
#include "RE/N/NiNode.h"
#include "RE/N/NiUpdateData.h"

#include <string_view>
#include <utility>
#include <vector>

namespace TF3DHud::PreviewRenderTree
{
	namespace
	{
		constexpr std::uint64_t kNiAVObjectTopFadeNode = 0x4000;
		constexpr std::uint64_t kNiAVObjectFadeDone = 0x8000;

		void RepairShaderFadeNodes(RE::NiAVObject& a_object, RE::BSFadeNode* a_currentFadeNode);

		void MakePreviewFadeNodeVisible(RE::BSFadeNode& a_fadeNode)
		{
			// IDA: BSLightingShaderProperty::GetRenderPasses clears deferred passes
			// when fadeNode lacks 0x8000 and currentFade <= 0.0. BSFadeNode::OnVisible
			// also fast-paths visible nodes when 0x8000, NiAVObject fade, and currentFade
			// are all set.
			a_fadeNode.flags.flags |= kNiAVObjectFadeDone;
			a_fadeNode.fadeAmount = 1.0F;
			a_fadeNode.currentFade = 1.0F;
			a_fadeNode.currentDecalFade = 1.0F;
			a_fadeNode.previousMaxA = 1.0F;
		}

		void RepairShaderFadeNodes(RE::NiAVObject& a_object, RE::BSFadeNode* a_currentFadeNode)
		{
			if (auto* fadeNode = a_object.IsFadeNode()) {
				a_currentFadeNode = fadeNode;
			}

			if (a_currentFadeNode) {
				if (auto* geometry = a_object.IsGeometry()) {
					for (auto& property : geometry->properties) {
						auto* shaderProperty = netimmerse_cast<RE::BSShaderProperty*>(property.get());
						if (shaderProperty) {
							shaderProperty->fadeNode = a_currentFadeNode;
						}
					}
				}
			}

			auto* node = a_object.IsNode();
			if (!node) {
				return;
			}

			for (auto& child : node->children) {
				if (child) {
					RepairShaderFadeNodes(*child, a_currentFadeNode);
				}
			}
		}

		[[nodiscard]] RE::BSShaderProperty* GetGeometryShaderProperty(RE::BSGeometry& a_geometry)
		{
			auto* shaderProperty0 = netimmerse_cast<RE::BSShaderProperty*>(a_geometry.properties[0].get());
			auto* shaderProperty1 = netimmerse_cast<RE::BSShaderProperty*>(a_geometry.properties[1].get());
			return shaderProperty1 ? shaderProperty1 : shaderProperty0;
		}

		[[nodiscard]] bool IsPreviewNeckGoreGeometry(const std::string_view a_name)
		{
			return a_name == "FemaleNeckGore" || a_name == "MaleNeckGore" || a_name == "NeckGore";
		}
	}

	void PrepareForInterface3DOffscreen(RE::NiAVObject& a_root)
	{
		// The preview tree is assembled from separately loaded/cloned subtrees.
		// Repair shader fade-node ownership once after final attachment so no
		// geometry keeps a stale fade node from the source or attachment root.
		RepairShaderFadeNodes(a_root, nullptr);

		bool updated = false;
		ForEachAVObject(std::addressof(a_root), [&](RE::NiAVObject& a_object) {
			auto* fadeNode = a_object.IsFadeNode();
			if (!fadeNode) {
				return;
			}

			if ((fadeNode->flags.flags & kNiAVObjectFadeDone) == 0 ||
				fadeNode->fadeAmount != 1.0F ||
				fadeNode->currentFade != 1.0F ||
				fadeNode->currentDecalFade != 1.0F) {
				updated = true;
			}
			MakePreviewFadeNodeVisible(*fadeNode);

			if ((a_object.flags.flags & kNiAVObjectTopFadeNode) != 0) {
				a_object.flags.flags &= ~kNiAVObjectTopFadeNode;
				updated = true;
			}
		});

		if (updated) {
			RE::NiUpdateData updateData;
			a_root.Update(updateData);
		}
	}

	void PreparePreviewTree(RE::NiAVObject& a_previewRoot)
	{
		ForEachAVObject(std::addressof(a_previewRoot), [&](RE::NiAVObject& a_object) {
			if (a_object.GetAppCulled()) {
				a_object.SetAppCulled(false);
			}
			a_object.fadeAmount = 1.0F;
			if (auto* fadeNode = a_object.IsFadeNode()) {
				MakePreviewFadeNodeVisible(*fadeNode);
			}
		});

		RepairShaderFadeNodes(a_previewRoot, nullptr);
	}

	std::uint32_t StripControllerChains(RE::NiAVObject& a_root)
	{
		std::uint32_t strippedControllers = 0;
		ForEachAVObject(std::addressof(a_root), [&](RE::NiAVObject& a_object) {
			if (a_object.controllers) {
				a_object.controllers.reset();
				++strippedControllers;
			}
		});

		return strippedControllers;
	}

	void SanitizePreviewRenderTree(RE::NiAVObject& a_previewRoot)
	{
		bool updated = false;
		ForEachGeometry(std::addressof(a_previewRoot), [&](RE::BSGeometry& a_geometry) {
			const std::string_view geometryName(a_geometry.GetName());
			const auto* shaderProperty = GetGeometryShaderProperty(a_geometry);
			const bool missingShaderPath = !shaderProperty || !shaderProperty->material;
			const bool neckGore = IsPreviewNeckGoreGeometry(geometryName);

			if (missingShaderPath) {
				a_geometry.SetAppCulled(true);
				a_geometry.fadeAmount = 0.0F;
				updated = true;
			}
			if (neckGore) {
				a_geometry.SetAppCulled(true);
				a_geometry.fadeAmount = 0.0F;
				updated = true;
			}
		});

		if (!updated) {
			return;
		}

		RE::NiUpdateData updateData;
		a_previewRoot.Update(updateData);
	}

	void RestorePreviewShaderAlpha(RE::NiAVObject& a_previewRoot)
	{
		ForEachGeometry(std::addressof(a_previewRoot), [&](RE::BSGeometry& a_geometry) {
			for (auto& property : a_geometry.properties) {
				auto* lightingProperty = netimmerse_cast<RE::BSLightingShaderProperty*>(property.get());
				if (!lightingProperty) {
					continue;
				}

				if (lightingProperty->alpha <= 0.0F) {
					lightingProperty->alpha = 1.0F;
				}
			}
		});
	}

	std::vector<RE::NiPointer<RE::NiAVObject>> StripClonedGeometry(RE::NiAVObject& a_previewRoot)
	{
		std::vector<std::pair<RE::NiNode*, RE::NiPointer<RE::NiAVObject>>> detachedObjects;

		ForEachAVObject(std::addressof(a_previewRoot), [&](RE::NiAVObject& a_object) {
			if (!a_object.IsGeometry()) {
				return;
			}

			auto* parent = a_object.parent;
			if (!parent) {
				return;
			}

			detachedObjects.emplace_back(parent, RE::NiPointer<RE::NiAVObject>(std::addressof(a_object)));
		});

		for (auto& [parent, object] : detachedObjects) {
			if (parent && object) {
				parent->DetachChild(object.get());
			}
		}

		std::vector<RE::NiPointer<RE::NiAVObject>> objects;
		objects.reserve(detachedObjects.size());
		for (auto& [parent, object] : detachedObjects) {
			(void)parent;
			if (object) {
				objects.push_back(std::move(object));
			}
		}
		return objects;
	}
}
