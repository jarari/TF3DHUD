#include "PreviewClone.h"

#include "Utils.h"

#include "RE/BSSkin.h"
#include "RE/N/NiCloningProcess.h"
#include "RE/N/NiTimeController.h"

#include <vector>

namespace TF3DHud::PreviewClone
{
	namespace
	{
		class ScopedSourceControllerDetach
		{
		public:
			explicit ScopedSourceControllerDetach(RE::NiAVObject& a_root)
			{
				ForEachAVObject(std::addressof(a_root), [&](RE::NiAVObject& a_object) {
					if (!a_object.controllers) {
						return;
					}

					detached_.push_back({
						.object = std::addressof(a_object),
						.controllers = a_object.controllers,
					});
					a_object.controllers.reset();
				});
			}

			~ScopedSourceControllerDetach()
			{
				for (auto& detached : detached_) {
					if (detached.object) {
						detached.object->controllers = detached.controllers;
					}
				}
			}

			ScopedSourceControllerDetach(const ScopedSourceControllerDetach&) = delete;
			ScopedSourceControllerDetach(ScopedSourceControllerDetach&&) = delete;
			ScopedSourceControllerDetach& operator=(const ScopedSourceControllerDetach&) = delete;
			ScopedSourceControllerDetach& operator=(ScopedSourceControllerDetach&&) = delete;

		private:
			struct DetachedController
			{
				RE::NiAVObject* object{ nullptr };
				RE::NiPointer<RE::NiTimeController> controllers;
			};

			std::vector<DetachedController> detached_;
		};

		void SeedSkinCloneMappings(
			RE::NiCloningProcess& a_cloneProcess,
			RE::NiAVObject& a_source,
			RE::NiAVObject* a_previewRoot,
			const std::unordered_map<std::string, RE::NiAVObject*>* a_previewNodes)
		{
			if (!a_previewRoot || !a_previewNodes || a_previewNodes->empty()) {
				return;
			}

			ForEachGeometry(std::addressof(a_source), [&](RE::BSGeometry& a_geometry) {
				auto* skin = a_geometry.skinInstance.get();
				if (!skin || skin->bones.size() > RE::BSSkin::kMaxExpectedBones) {
					return;
				}

				if (skin->rootNode) {
					a_cloneProcess.cloneMap.emplace(skin->rootNode, a_previewRoot);
				}

				for (std::uint32_t index = 0; index < skin->bones.size(); ++index) {
					auto* sourceBone = skin->bones[index];
					if (!sourceBone) {
						continue;
					}

					auto* previewBone = FindNodeByName(*a_previewNodes, sourceBone->GetName());
					if (!previewBone) {
						continue;
					}

					a_cloneProcess.cloneMap.emplace(sourceBone, previewBone);
				}
			});
		}
	}

	RE::NiPointer<RE::NiAVObject> CloneObject(
		RE::NiAVObject& a_source,
		RE::NiAVObject* a_previewRoot,
		const std::unordered_map<std::string, RE::NiAVObject*>* a_previewNodes)
	{
		RE::NiCloningProcess cloneProcess;
		cloneProcess.appendChar = '$';
		cloneProcess.copyType = RE::NiCloningProcess::CopyType::kCopyExact;
		cloneProcess.scale = { 1.0F, 1.0F, 1.0F };

		SeedSkinCloneMappings(cloneProcess, a_source, a_previewRoot, a_previewNodes);

		ScopedSourceControllerDetach detachControllers(a_source);
		auto* clone = a_source.CreateClone(cloneProcess);
		a_source.ProcessClone(cloneProcess);
		auto* clonedObject = clone ? static_cast<RE::NiAVObject*>(clone) : nullptr;
		return clonedObject;
	}
}
