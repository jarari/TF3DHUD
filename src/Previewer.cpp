#include "Previewer.h"
#include "Renderer.h"
#include "Utils.h"

#include "BSSkin.h"
#include "Config.h"

#include "RE/B/BSFadeNode.h"
#include "RE/B/BSFlattenedBoneTree.h"
#include "RE/B/BSGeometry.h"
#include "RE/B/BSLightingShaderProperty.h"
#include "RE/B/BSModelDB.h"
#include "RE/B/BSShaderMaterial.h"
#include "RE/B/BSShaderProperty.h"
#include "RE/B/BipedAnim.h"
#include "RE/N/NiCloningProcess.h"
#include "RE/N/NiNode.h"
#include "RE/N/NiUpdateData.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESNPC.h"
#include "RE/T/TESModel.h"
#include "RE/T/TESObjectARMA.h"
#include "RE/T/TESObjectARMO.h"
#include "RE/T/TESRace.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace RE
{
	class BSFaceGenNiNode :
		public NiNode
	{};
}

namespace TF3DHud
{
	namespace
	{
		constexpr std::uint64_t kNiAVObjectTopFadeNode = 0x4000;
		constexpr std::uint64_t kNiAVObjectFadeDone = 0x8000;

		using CreateHeadForNPC_t = bool(RE::TESNPC*, RE::NiPointer<RE::BSFaceGenNiNode>&, bool, bool, void*);
		using CalculateBodyTintColor_t =
			void(RE::TESNPC*, RE::NiColorA&, RE::BGSCharacterTint::Entry*, bool, bool);
		using UpdateBodyTintColorsOnScene_t = void(RE::NiAVObject*, const RE::NiColorA&);
		using ConvertNodeTree_t = RE::NiAVObject*(RE::NiAVObject*, RE::NiAVObject*);
		using GetActorBodyPart3D_t =
			RE::NiAVObject*(RE::Actor*, RE::NiAVObject*, const std::uint32_t*, bool, bool);
		struct SkinComplexionContext
		{
			RE::BIPED_OBJECT slot{ RE::BIPED_OBJECT::kNone };
			std::uint32_t pad{ 0 };
			RE::BIPOBJECT* objects{ nullptr };
			RE::ObjectRefHandle actorRef{};
			std::uint32_t pad2{ 0 };
		};
		static_assert(offsetof(SkinComplexionContext, objects) == 0x8);
		static_assert(offsetof(SkinComplexionContext, actorRef) == 0x10);
		using DoAdjustSkinComplexion_t = std::uint64_t(SkinComplexionContext*, RE::NiAVObject*);

		REL::Relocation<float*> g_engineTime{ REL::ID{ 599343, 2712485 } };
		REL::Relocation<void (*)(RE::NiAVObject*)> g_createBoneMap{ REL::ID(1131947) };
		REL::Relocation<CreateHeadForNPC_t*> g_createHeadForNPC{ REL::ID(1455012) };
		REL::Relocation<CalculateBodyTintColor_t*> g_calculateBodyTintColor{ REL::ID(134537) };
		REL::Relocation<UpdateBodyTintColorsOnScene_t*> g_updateBodyTintColorsOnScene{ REL::ID(49935) };
		REL::Relocation<DoAdjustSkinComplexion_t*> g_doAdjustSkinComplexion{ REL::ID(1295935) };
		REL::Relocation<ConvertNodeTree_t*> g_convertNodeTree{ REL::ID(633230) };
		REL::Relocation<GetActorBodyPart3D_t*> g_getActorBodyPart3D{ REL::ID(157573) };

		struct PreviewAttachment
		{
			RE::BIPED_OBJECT slot{ RE::BIPED_OBJECT::kNone };
			RE::NiPointer<RE::NiAVObject> object;
			RE::NiNode* parent{ nullptr };
		};

		RE::NiPointer<RE::NiAVObject> g_previewRoot;
		RE::BSFlattenedBoneTree* g_previewFlattenedBoneTree{ nullptr };
		RE::NiPointer<RE::BSFaceGenNiNode> g_previewFaceNode;
		std::vector<PreviewAttachment> g_previewAttachments;
		std::vector<RE::NiPointer<RE::NiAVObject>> g_retiredPreviewObjects;
		std::uint64_t g_bipedSignature{ 0 };
		std::uint64_t g_visualSignature{ 0 };
		std::uint64_t g_failedPreviewSignature{ 0 };
		float g_diagnosticAccumulator{ 0.0F };
		float g_lastEngineTime{ 0.0F };
		std::uint32_t g_frameTicks{ 0 };
		bool g_hasLastEngineTime{ false };
		bool g_looksMenuSuspended{ false };
		std::string g_lastDiagnostic;

		void HashInteger(std::uint64_t& a_hash, const std::uintptr_t a_value)
		{
			HashValue(a_hash, reinterpret_cast<void*>(a_value));
		}

		void HashFloat(std::uint64_t& a_hash, const float a_value)
		{
			std::uint32_t bits = 0;
			std::memcpy(std::addressof(bits), std::addressof(a_value), sizeof(bits));
			HashInteger(a_hash, bits);
		}

		[[nodiscard]] RE::NiAVObject* GetSourceFaceNode(RE::Actor& a_sourceActor)
		{
			if (auto* faceNode = a_sourceActor.GetFaceNodeSkinned()) {
				return reinterpret_cast<RE::NiAVObject*>(faceNode);
			}

			auto* sourceRoot = a_sourceActor.Get3D(false);
			if (!sourceRoot) {
				sourceRoot = a_sourceActor.Get3D();
			}
			return sourceRoot ? sourceRoot->GetObjectByName(RE::BSFixedString("BSFaceGenNiNodeSkinned")) : nullptr;
		}

		[[nodiscard]] std::uint64_t BuildVisualSignature(RE::PlayerCharacter& a_player)
		{
			std::uint64_t hash = 1469598103934665603ull;

			auto* playerBase = a_player.GetObjectReference();
			auto* npc = playerBase ? playerBase->As<RE::TESNPC>() : nullptr;
			HashValue(hash, npc);
			HashValue(hash, npc ? npc->GetRootFaceNPC() : nullptr);
			HashValue(hash, a_player.GetVisualsRace());
			HashValue(hash, a_player.charGenRace);
			HashValue(hash, a_player.complexion);
			HashValue(hash, a_player.tintingData);
			HashValue(hash, GetSourceFaceNode(a_player));
			HashInteger(hash, static_cast<std::uintptr_t>(a_player.GetSex()));

			if (npc) {
				HashValue(hash, npc->headRelatedData);
				HashValue(hash, npc->originalRace);
				HashValue(hash, npc->faceNPC);
				HashValue(hash, npc->headParts);
				HashValue(hash, npc->morphRegionSliderValues);
				HashValue(hash, npc->facialBoneRegionSliderValues);
				HashValue(hash, npc->morphSliderValues);
				HashValue(hash, npc->tintingData);
				HashInteger(hash, static_cast<std::uintptr_t>(npc->GetSex()));
				HashInteger(hash, static_cast<std::uintptr_t>(npc->numHeadParts));
				HashInteger(hash, static_cast<std::uint8_t>(npc->bodyTintColorR));
				HashInteger(hash, static_cast<std::uint8_t>(npc->bodyTintColorG));
				HashInteger(hash, static_cast<std::uint8_t>(npc->bodyTintColorB));
				HashInteger(hash, static_cast<std::uint8_t>(npc->bodyTintColorA));
				for (std::int32_t i = 0; i < npc->numHeadParts && npc->headParts; ++i) {
					HashValue(hash, npc->headParts[i]);
				}
			}

			return hash;
		}

		[[nodiscard]] std::uint64_t BuildBipedSignature(const RE::BipedAnim* a_biped)
		{
			if (!a_biped) {
				return 0;
			}

			std::uint64_t hash = 1469598103934665603ull;
			for (std::int32_t i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
				const auto& object = a_biped->object[i];
				HashValue(hash, object.parent.object);
				HashValue(hash, object.parent.instanceData.get());
				HashValue(hash, object.modExtra);
				HashValue(hash, object.armorAddon);
				HashValue(hash, object.part);
				HashValue(hash, object.skinTexture);
				HashValue(hash, object.partClone.get());
				HashValue(hash, object.objectGraphManager.get());
				HashValue(hash, object.hitEffect.get());
				HashValue(hash, reinterpret_cast<void*>(static_cast<std::uintptr_t>(object.skinned)));
			}

			auto* root = a_biped->GetRoot();
			HashValue(hash, root);
			if (root) {
				std::unordered_set<RE::NiAVObject*> resolvedSlotRoots;
				for (std::int32_t i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
					if (auto* partClone = a_biped->object[i].partClone.get()) {
						resolvedSlotRoots.insert(partClone);
					}
				}

				std::uint32_t unresolvedSkinnedGeometry = 0;
				std::uint32_t unresolvedVisibleGeometry = 0;
				ForEachGeometry(root, [&](RE::BSGeometry& a_geometry) {
					if (!a_geometry.skinInstance) {
						return;
					}

					auto* geometryObject = static_cast<RE::NiAVObject*>(std::addressof(a_geometry));
					for (auto* parent = geometryObject; parent; parent = parent->parent) {
						if (resolvedSlotRoots.contains(parent)) {
							return;
						}
					}

					++unresolvedSkinnedGeometry;
					if (!a_geometry.GetAppCulled()) {
						++unresolvedVisibleGeometry;
					}
					HashValue(hash, geometryObject);
					HashValue(hash, geometryObject->parent);
					HashValue(hash, a_geometry.skinInstance.get());
					HashInteger(hash, a_geometry.GetFlags());
					HashInteger(hash, a_geometry.GetAppCulled() ? 1 : 0);
					HashFloat(hash, a_geometry.fadeAmount);
					HashValue(hash, a_geometry.properties[0].get());
					HashValue(hash, a_geometry.properties[1].get());
				});
				HashInteger(hash, unresolvedSkinnedGeometry);
				HashInteger(hash, unresolvedVisibleGeometry);
			}
			return hash;
		}

		[[nodiscard]] std::uint64_t BuildPreviewSignature(RE::PlayerCharacter& a_player, const RE::BipedAnim* a_biped)
		{
			const auto bipedSignature = BuildBipedSignature(a_biped);
			if (bipedSignature == 0) {
				return 0;
			}

			auto visualSignature = BuildVisualSignature(a_player);
			HashInteger(visualSignature, bipedSignature);
			return visualSignature;
		}

		[[nodiscard]] bool IsHudAvailable(std::string& a_reason)
		{
			const auto ui = RE::UI::GetSingleton();
			if (!ui) {
				a_reason = "UI singleton is null";
				return false;
			}

			const auto hudMenu = ui->GetMenu<RE::HUDMenu>();
			if (!hudMenu) {
				a_reason = "HUDMenu is null";
				return false;
			}

			if (hudMenu->hudShowMenuState != RE::HUDMenu::ShowMenuState::kShown) {
				a_reason = "HUDMenu is hidden by show state " + std::to_string(std::to_underlying(hudMenu->hudShowMenuState.get()));
				return false;
			}

			return true;
		}

		[[nodiscard]] bool IsFirstPerson(std::string& a_reason)
		{
			const auto camera = RE::PlayerCamera::GetSingleton();
			if (!camera) {
				a_reason = "PlayerCamera singleton is null";
				return false;
			}

			if (!camera->QCameraEquals(RE::CameraState::kFirstPerson)) {
				a_reason = "camera is not first person";
				return false;
			}

			return true;
		}

		void LogDiagnostic(std::string a_message, bool a_force = false)
		{
			if (!a_force && a_message == g_lastDiagnostic) {
				return;
			}

			g_lastDiagnostic = std::move(a_message);
			REX::INFO("TF3DHud V1 state: {}", g_lastDiagnostic);
		}

		[[nodiscard]] bool ShouldShow(const RE::PlayerCharacter* a_player, std::string& a_reason)
		{
			const auto& config = GetConfig();
			if (!config.enabled) {
				a_reason = "disabled by INI";
				return false;
			}

			if (!a_player) {
				a_reason = "PlayerCharacter singleton is null";
				return false;
			}

			const auto ui = RE::UI::GetSingleton();
			if (!ui) {
				a_reason = "UI singleton is null";
				return false;
			}

			const auto itemMenuMode = ui->itemMenuMode.load_unchecked();
			if (ui->menuMode != 0 || itemMenuMode != 0 || ui->freezeFrameMenuBG != 0 || ui->freezeFramePause != 0) {
				a_reason =
					"blocked by menu/freeze-frame state menuMode=" + std::to_string(ui->menuMode) +
					", itemMenuMode=" + std::to_string(itemMenuMode) +
					", freezeFrameMenuBG=" + std::to_string(ui->freezeFrameMenuBG) +
					", freezeFramePause=" + std::to_string(ui->freezeFramePause);
				return false;
			}

			if (!IsHudAvailable(a_reason) || !IsFirstPerson(a_reason)) {
				return false;
			}

			if (config.hideInPowerArmor && RE::PowerArmor::ActorInPowerArmor(*a_player)) {
				a_reason = "player is in power armor";
				return false;
			}

			return true;
		}

		[[nodiscard]] bool IsPreviewSourceReady(RE::PlayerCharacter& a_player, const RE::BipedAnim& a_biped, std::string& a_reason)
		{
			if (!a_biped.GetRoot()) {
				a_reason = "third-person biped root is null";
				return false;
			}

			if (!GetSourceFaceNode(a_player)) {
				a_reason = "player face node is not initialized";
				return false;
			}

			return true;
		}

		RE::NiNode* FindPreviewAttachParent(
			RE::NiAVObject& a_sourceObject,
			RE::NiAVObject& a_previewRoot,
			const std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			for (auto* sourceParent = a_sourceObject.parent; sourceParent; sourceParent = sourceParent->parent) {
				auto* previewObject = FindNodeByName(a_previewNodes, sourceParent->GetName());
				auto* previewNode = previewObject ? previewObject->IsNode() : nullptr;
				if (previewNode) {
					return previewNode;
				}
			}

			return a_previewRoot.IsNode();
		}

		void CollectSkinInstances(RE::NiAVObject* a_root, std::unordered_set<RE::BSSkin::Instance*>& a_skins)
		{
			ForEachGeometry(a_root, [&](RE::BSGeometry& a_geometry) {
				if (a_geometry.skinInstance) {
					a_skins.insert(a_geometry.skinInstance.get());
				}
			});
		}

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

			[[nodiscard]] std::size_t Count() const noexcept { return detached_.size(); }

		private:
			struct DetachedController
			{
				RE::NiAVObject* object{ nullptr };
				RE::NiPointer<RE::NiTimeController> controllers;
			};

			std::vector<DetachedController> detached_;
		};

		[[nodiscard]] std::uint32_t SeedSkinCloneMappings(
			RE::NiCloningProcess& a_cloneProcess,
			RE::NiAVObject& a_source,
			RE::NiAVObject* a_previewRoot,
			const std::unordered_map<std::string, RE::NiAVObject*>* a_previewNodes)
		{
			if (!a_previewRoot || !a_previewNodes || a_previewNodes->empty()) {
				return 0;
			}

			std::uint32_t seeded = 0;
			ForEachGeometry(std::addressof(a_source), [&](RE::BSGeometry& a_geometry) {
				auto* skin = a_geometry.skinInstance.get();
				if (!skin || skin->bones.size() > RE::BSSkin::kMaxExpectedBones) {
					return;
				}

				if (skin->rootNode) {
					if (a_cloneProcess.cloneMap.emplace(skin->rootNode, a_previewRoot).second) {
						++seeded;
					}
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

					if (a_cloneProcess.cloneMap.emplace(sourceBone, previewBone).second) {
						++seeded;
					}
				}
			});

			return seeded;
		}

		RE::NiPointer<RE::NiAVObject> ClonePreviewObject(
			RE::NiAVObject& a_source,
			RE::NiAVObject* a_previewRoot = nullptr,
			const std::unordered_map<std::string, RE::NiAVObject*>* a_previewNodes = nullptr,
			std::uint32_t* a_seededSkinMappings = nullptr)
		{
			RE::NiCloningProcess cloneProcess;
			cloneProcess.appendChar = '$';
			cloneProcess.copyType = RE::NiCloningProcess::CopyType::kCopyExact;
			cloneProcess.scale = { 1.0F, 1.0F, 1.0F };

			const auto seeded = SeedSkinCloneMappings(cloneProcess, a_source, a_previewRoot, a_previewNodes);
			if (a_seededSkinMappings) {
				*a_seededSkinMappings = seeded;
			}

			ScopedSourceControllerDetach detachControllers(a_source);
			auto* clone = a_source.CreateClone(cloneProcess);
			a_source.ProcessClone(cloneProcess);
			auto* clonedObject = clone ? static_cast<RE::NiAVObject*>(clone) : nullptr;
			if (detachControllers.Count() > 0) {
				REX::INFO(
					"TF3DHud V1 cloned preview object with source controllers detached during clone: object='{}', detachedControllers={}",
					a_source.GetName(),
					detachControllers.Count());
			}
			return clonedObject;
		}

		[[nodiscard]] const char* GetRaceSkeletonModelPath(RE::TESRace& a_race, const std::uint32_t a_sex)
		{
			const auto sex = std::min<std::uint32_t>(a_sex, 1);
			if (const auto* model = a_race.skeletonModel[sex].GetModel(); model && model[0] != '\0') {
				return model;
			}
			if (const auto* model = a_race.skeletonModel[0].GetModel(); model && model[0] != '\0') {
				return model;
			}
			if (const auto* model = a_race.skeletonModel[1].GetModel(); model && model[0] != '\0') {
				return model;
			}
			return nullptr;
		}

		[[nodiscard]] RE::BSFlattenedBoneTree* FindFlattenedBoneTree(RE::NiAVObject* a_root)
		{
			if (!a_root) {
				return nullptr;
			}
			if (auto* flattened = netimmerse_cast<RE::BSFlattenedBoneTree*>(a_root)) {
				return flattened;
			}

			auto* node = a_root->IsNode();
			if (!node) {
				return nullptr;
			}

			for (auto& child : node->children) {
				if (auto* flattened = FindFlattenedBoneTree(child.get())) {
					return flattened;
				}
			}
			return nullptr;
		}

		[[nodiscard]] bool ConvertPreviewSkeletonToFlattenedTree(RE::PlayerCharacter& a_player, RE::NiAVObject& a_skeletonRoot)
		{
			const auto* beforeFlattened = FindFlattenedBoneTree(std::addressof(a_skeletonRoot));
			constexpr std::uint32_t kFlattenedSkeletonBodyPart = 0x12;
			auto* convertTarget = g_getActorBodyPart3D(
				std::addressof(a_player),
				std::addressof(a_skeletonRoot),
				std::addressof(kFlattenedSkeletonBodyPart),
				false,
				false);
			if (!convertTarget) {
				REX::WARN(
					"TF3DHud V1 preview race skeleton flatten failed: body-part lookup returned null; root={:X}, rootName='{}', bodyPart={}",
					reinterpret_cast<std::uintptr_t>(std::addressof(a_skeletonRoot)),
					a_skeletonRoot.GetName(),
					kFlattenedSkeletonBodyPart);
				return false;
			}

			if (convertTarget != std::addressof(a_skeletonRoot)) {
				auto* converted = g_convertNodeTree(convertTarget, std::addressof(a_skeletonRoot));
				REX::INFO(
					"TF3DHud V1 called BSFlattenedBoneTree::ConvertNodeTree for preview skeleton: target={:X}, root={:X}, converted={:X}, targetName='{}', rootName='{}'",
					reinterpret_cast<std::uintptr_t>(convertTarget),
					reinterpret_cast<std::uintptr_t>(std::addressof(a_skeletonRoot)),
					reinterpret_cast<std::uintptr_t>(converted),
					convertTarget->GetName(),
					a_skeletonRoot.GetName());
			} else {
				REX::INFO(
					"TF3DHud V1 skipped preview skeleton flatten call because body-part target is root: root={:X}, rootName='{}', bodyPart={}",
					reinterpret_cast<std::uintptr_t>(std::addressof(a_skeletonRoot)),
					a_skeletonRoot.GetName(),
					kFlattenedSkeletonBodyPart);
			}

			g_createBoneMap(std::addressof(a_skeletonRoot));

			auto* afterFlattened = FindFlattenedBoneTree(std::addressof(a_skeletonRoot));
			if (!afterFlattened) {
				REX::WARN(
					"TF3DHud V1 preview race skeleton flatten did not produce BSFlattenedBoneTree: target={:X}, root={:X}, targetName='{}', rootName='{}'",
					reinterpret_cast<std::uintptr_t>(convertTarget),
					reinterpret_cast<std::uintptr_t>(std::addressof(a_skeletonRoot)),
					convertTarget->GetName(),
					a_skeletonRoot.GetName());
				return false;
			}

			g_createBoneMap(afterFlattened);
			g_previewFlattenedBoneTree = afterFlattened;
			REX::INFO(
				"TF3DHud V1 converted preview race skeleton to BSFlattenedBoneTree: target={:X}, root={:X}, beforeFlattened={:X}, afterFlattened={:X}, targetName='{}', rootName='{}', boneCount={}",
				reinterpret_cast<std::uintptr_t>(convertTarget),
				reinterpret_cast<std::uintptr_t>(std::addressof(a_skeletonRoot)),
				reinterpret_cast<std::uintptr_t>(beforeFlattened),
				reinterpret_cast<std::uintptr_t>(afterFlattened),
				convertTarget->GetName(),
				a_skeletonRoot.GetName(),
				afterFlattened->boneCount);

			return true;
		}

		[[nodiscard]] RE::BSFlattenedBoneTree::FlattenedBone* FindFlattenedBoneByName(
			RE::BSFlattenedBoneTree& a_tree,
			const std::string_view a_name)
		{
			if (!a_tree.bone || a_tree.boneCount <= 0) {
				return nullptr;
			}

			for (std::int32_t index = 0; index < a_tree.boneCount; ++index) {
				auto& bone = a_tree.bone[index];
				const auto* name = bone.name.c_str();
				if (name && a_name == name) {
					return std::addressof(bone);
				}
			}
			return nullptr;
		}

		bool ApplyHeadCenteredFraming(RE::NiAVObject& a_previewRoot, const bool a_log)
		{
			auto* flattened = g_previewFlattenedBoneTree;
			if (!flattened) {
				flattened = FindFlattenedBoneTree(std::addressof(a_previewRoot));
				g_previewFlattenedBoneTree = flattened;
			}

			if (!flattened) {
				Renderer::ApplyOffscreenFraming(a_previewRoot, a_log);
				if (a_log) {
					REX::WARN("TF3DHud V1 head-centered framing fell back to bounds: no cached BSFlattenedBoneTree");
				}
				return false;
			}

			auto* headObject = flattened->GetObjectByName(RE::BSFixedString("HEAD"));
			auto* head = headObject ? nullptr : FindFlattenedBoneByName(*flattened, "HEAD");
			if (!headObject && !head) {
				Renderer::ApplyOffscreenFraming(a_previewRoot, a_log);
				if (a_log) {
					REX::WARN(
						"TF3DHud V1 head-centered framing fell back to bounds: HEAD bone not found in flattenedTree='{}', bones={}",
						flattened->GetName(),
						flattened->boneCount);
				}
				return false;
			}

			const auto& config = GetConfig();
			RE::NiTransform transform = RE::NiTransform::IDENTITY;
			transform.scale = config.modelScale;
			transform.rotate.FromEulerAnglesXYZ(0.0F, 0.0F, config.yawDegrees * 3.14159265358979323846F / 180.0F);
			a_previewRoot.SetLocalTransform(transform);

			RE::NiUpdateData updateData;
			a_previewRoot.Update(updateData);

			const auto headWorld = headObject ? headObject->world.translate : (head->node ? head->node->world.translate : head->world.translate);
			auto centeredTransform = a_previewRoot.GetLocalTransform();
			centeredTransform.translate = {
				-headWorld.x,
				-headWorld.y + config.cameraDistance,
				-headWorld.z
			};
			a_previewRoot.SetLocalTransform(centeredTransform);
			a_previewRoot.Update(updateData);

			if (a_log) {
				REX::INFO(
					"TF3DHud V1 head-centered preview root: flattenedTree='{}', headNode={:X}, headWorld=({}, {}, {}), localTranslate=({}, {}, {}), scale={}, yawDegrees={}, boundCenter=({}, {}, {}), boundRadius={}",
					flattened->GetName(),
					reinterpret_cast<std::uintptr_t>(headObject ? headObject : head->node.get()),
					headWorld.x,
					headWorld.y,
					headWorld.z,
					a_previewRoot.GetLocalTranslate().x,
					a_previewRoot.GetLocalTranslate().y,
					a_previewRoot.GetLocalTranslate().z,
					centeredTransform.scale,
					config.yawDegrees,
					a_previewRoot.worldBound.center.x,
					a_previewRoot.worldBound.center.y,
					a_previewRoot.worldBound.center.z,
					a_previewRoot.worldBound.fRadius);
			}
			return true;
		}

		RE::NiPointer<RE::NiAVObject> LoadPreviewRaceSkeleton(RE::PlayerCharacter& a_player)
		{
			auto* race = a_player.GetVisualsRace();
			if (!race) {
				if (auto* playerBase = a_player.GetObjectReference()) {
					if (auto* npc = playerBase->As<RE::TESNPC>()) {
						race = npc->GetFormRace();
					}
				}
			}
			if (!race) {
				LogDiagnostic("preview race skeleton load failed: player race is null");
				return nullptr;
			}

			const auto sex = static_cast<std::uint32_t>(a_player.GetSex());
			const auto* skeletonPath = GetRaceSkeletonModelPath(*race, sex);
			if (!skeletonPath) {
				LogDiagnostic("preview race skeleton load failed: race skeleton path is empty");
				return nullptr;
			}

			RE::NiPointer<RE::NiNode> loadedRoot;
			RE::BSModelDB::DBTraits::ArgsType args{};
			args.loadLevel = 3;
			args.prepareAfterLoad = true;
			args.performProcess = true;
			args.createFadeNode = true;
			args.loadTextures = true;
			const auto result = RE::BSModelDB::Demand(skeletonPath, std::addressof(loadedRoot), args);
			if (result != RE::BSResource::ErrorCode::kNone || !loadedRoot) {
				REX::WARN(
					"TF3DHud V1 preview race skeleton load failed: path='{}', result={}",
					skeletonPath,
					std::to_underlying(result));
				LogDiagnostic("preview race skeleton load failed: BSModelDB demand failed");
				return nullptr;
			}

			REX::INFO(
				"TF3DHud V1 loaded race skeleton source model: race={:08X}, sex={}, path='{}', root='{}', objectsBoundRadius={}",
				race->GetFormID(),
				sex,
				skeletonPath,
				loadedRoot->GetName(),
				loadedRoot->worldBound.fRadius);

			auto previewRoot = ClonePreviewObject(*loadedRoot);
			if (!previewRoot) {
				REX::WARN("TF3DHud V1 preview race skeleton clone failed: path='{}'", skeletonPath);
				LogDiagnostic("preview race skeleton clone failed");
				return nullptr;
			}

			REX::INFO(
				"TF3DHud V1 cloned preview-owned race skeleton: source={:X}, clone={:X}, root='{}', objectsBoundRadius={}",
				reinterpret_cast<std::uintptr_t>(loadedRoot.get()),
				reinterpret_cast<std::uintptr_t>(previewRoot.get()),
				previewRoot->GetName(),
				previewRoot->worldBound.fRadius);

			if (!ConvertPreviewSkeletonToFlattenedTree(a_player, *previewRoot)) {
				LogDiagnostic("preview race skeleton flatten failed");
				return nullptr;
			}
			return previewRoot;
		}

		[[nodiscard]] RE::NiPointer<RE::NiAVObject> LoadPreviewModel(const char* a_path)
		{
			if (!a_path || a_path[0] == '\0') {
				return nullptr;
			}

			RE::NiPointer<RE::NiNode> loadedRoot;
			RE::BSModelDB::DBTraits::ArgsType args{};
			args.loadLevel = 3;
			args.prepareAfterLoad = true;
			args.performProcess = true;
			args.createFadeNode = true;
			args.loadTextures = true;
			const auto result = RE::BSModelDB::Demand(a_path, std::addressof(loadedRoot), args);
			if (result != RE::BSResource::ErrorCode::kNone || !loadedRoot) {
				REX::WARN(
					"TF3DHud V1 preview model load failed: path='{}', result={}",
					a_path,
					std::to_underlying(result));
				return nullptr;
			}

			auto previewRoot = ClonePreviewObject(*loadedRoot);
			if (!previewRoot) {
				REX::WARN("TF3DHud V1 preview model clone failed: path='{}'", a_path);
				return nullptr;
			}

			return previewRoot;
		}


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

		[[nodiscard]] bool IsRootedAt(RE::NiAVObject* a_object, RE::NiAVObject& a_root)
		{
			for (auto* current = a_object; current; current = current->parent) {
				if (current == std::addressof(a_root)) {
					return true;
				}
			}
			return false;
		}

		struct ShaderFadeNodeDiagnostics
		{
			std::uint32_t shaderProperties{ 0 };
			std::uint32_t missingFadeNodes{ 0 };
			std::uint32_t externalFadeNodes{ 0 };
			std::string firstExternalGeometry;
		};

		[[nodiscard]] ShaderFadeNodeDiagnostics GetShaderFadeNodeDiagnostics(RE::NiAVObject& a_root)
		{
			ShaderFadeNodeDiagnostics diagnostics;
			ForEachGeometry(std::addressof(a_root), [&](RE::BSGeometry& a_geometry) {
				for (auto& property : a_geometry.properties) {
					auto* shaderProperty = netimmerse_cast<RE::BSShaderProperty*>(property.get());
					if (!shaderProperty) {
						continue;
					}

					++diagnostics.shaderProperties;
					if (!shaderProperty->fadeNode) {
						++diagnostics.missingFadeNodes;
						continue;
					}
					if (!IsRootedAt(shaderProperty->fadeNode, a_root)) {
						++diagnostics.externalFadeNodes;
						if (diagnostics.firstExternalGeometry.empty()) {
							diagnostics.firstExternalGeometry = std::string(a_geometry.GetName());
						}
					}
				}
			});
			return diagnostics;
		}

		void PrepareForInterface3DOffscreen(RE::NiAVObject& a_root)
		{
			std::uint32_t fadeNodes{ 0 };
			std::uint32_t clearedTopFadeNodes{ 0 };
			std::uint32_t forcedVisibleFadeNodes{ 0 };
			const auto beforeFlags = a_root.GetFlags();
			const auto beforeRadius = a_root.worldBound.fRadius;
			const auto beforeFadeDiagnostics = GetShaderFadeNodeDiagnostics(a_root);

			// The preview tree is assembled from separately loaded/cloned subtrees.
			// Repair shader fade-node ownership once after final attachment so no
			// geometry keeps a stale fade node from the source or attachment root.
			RepairShaderFadeNodes(a_root, nullptr);

			ForEachAVObject(std::addressof(a_root), [&](RE::NiAVObject& a_object) {
				auto* fadeNode = a_object.IsFadeNode();
				if (!fadeNode) {
					return;
				}

				++fadeNodes;
				if ((fadeNode->flags.flags & kNiAVObjectFadeDone) == 0 ||
					fadeNode->fadeAmount != 1.0F ||
					fadeNode->currentFade != 1.0F ||
					fadeNode->currentDecalFade != 1.0F) {
					++forcedVisibleFadeNodes;
				}
				MakePreviewFadeNodeVisible(*fadeNode);

				if ((a_object.flags.flags & kNiAVObjectTopFadeNode) != 0) {
					a_object.flags.flags &= ~kNiAVObjectTopFadeNode;
					++clearedTopFadeNodes;
				}
			});

			const auto afterFadeDiagnostics = GetShaderFadeNodeDiagnostics(a_root);
			if (clearedTopFadeNodes > 0 ||
				forcedVisibleFadeNodes > 0 ||
				beforeFadeDiagnostics.externalFadeNodes != afterFadeDiagnostics.externalFadeNodes ||
				beforeFadeDiagnostics.missingFadeNodes != afterFadeDiagnostics.missingFadeNodes) {
				RE::NiUpdateData updateData;
				a_root.Update(updateData);
			}

			REX::INFO(
				"TF3DHud V1 prepared preview root for Interface3D offscreen: fadeNodes={}, forcedVisibleFadeNodes={}, clearedTopFadeNodes={}, rootFlags {:016X}->{:016X}, boundRadius {}->{}, shaderFadeProps={}, missingFadeNodes {}->{}, externalFadeNodes {}->{}, firstExternalFade='{}'",
				fadeNodes,
				forcedVisibleFadeNodes,
				clearedTopFadeNodes,
				beforeFlags,
				a_root.GetFlags(),
				beforeRadius,
				a_root.worldBound.fRadius,
				afterFadeDiagnostics.shaderProperties,
				beforeFadeDiagnostics.missingFadeNodes,
				afterFadeDiagnostics.missingFadeNodes,
				beforeFadeDiagnostics.externalFadeNodes,
				afterFadeDiagnostics.externalFadeNodes,
				beforeFadeDiagnostics.firstExternalGeometry);
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

		void PreparePreviewTree(RE::NiAVObject& a_previewRoot)
		{
			std::uint32_t objects = 0;
			std::uint32_t appCulledObjects = 0;

			ForEachAVObject(std::addressof(a_previewRoot), [&](RE::NiAVObject& a_object) {
				++objects;
				if (a_object.GetAppCulled()) {
					++appCulledObjects;
					a_object.SetAppCulled(false);
				}
				a_object.fadeAmount = 1.0F;
				if (auto* fadeNode = a_object.IsFadeNode()) {
					MakePreviewFadeNodeVisible(*fadeNode);
				}
			});

			RepairShaderFadeNodes(a_previewRoot, nullptr);

			REX::INFO(
				"TF3DHud V1 prepared preview tree: objects={}, clearedAppCulled={}",
				objects,
				appCulledObjects);
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

		void LogPreviewRenderGeometryDetails(RE::NiAVObject& a_previewRoot, const char* a_stage)
		{
			std::uint32_t index = 0;
			ForEachGeometry(std::addressof(a_previewRoot), [&](RE::BSGeometry& a_geometry) {
				const auto geometryName = std::string(a_geometry.GetName());
				auto* shaderProperty = GetGeometryShaderProperty(a_geometry);
				auto* lightingProperty = netimmerse_cast<RE::BSLightingShaderProperty*>(shaderProperty);
				auto* material = shaderProperty ? shaderProperty->material : nullptr;
				const auto* skin = a_geometry.skinInstance.get();
				std::uint32_t nullBones = 0;
				if (skin) {
					for (auto* bone : skin->bones) {
						if (!bone) {
							++nullBones;
						}
					}
				}
				const bool missingShaderOrMaterial = !shaderProperty || !material;
				const bool neckGore = IsPreviewNeckGoreGeometry(geometryName);
				const char* decision = neckGore ? "cull-neck-gore" : (missingShaderOrMaterial ? "cull-missing-shader-material" : "render");

				REX::INFO(
					"TF3DHud V1 render geometry detail [{}] #{} decision={} name='{}' geom={:X} parent='{}' parent={:X} parentRooted={} flags={:016X} appCulled={} fade={} localT=({}, {}, {}) localScale={} worldT=({}, {}, {}) worldScale={} worldBound=({}, {}, {})/{} modelBound=({}, {}, {})/{} shader={:X} shaderFlags={:016X} shaderAlpha={} shaderRoot='{}' shaderFadeNode={:X} shaderFadeRooted={} material={:X} materialFeature={} materialType={} materialHash={:08X} materialUnique={:08X} lighting={} baseTechnique={} skin={:X} skinRoot='{}' skinRoot={:X} skinRooted={} skinBones={} nullBones={} worldTransforms={} paletteStamp={} rendererData={:X} geomType={} registered={}",
					a_stage,
					index++,
					decision,
					geometryName,
					reinterpret_cast<std::uintptr_t>(std::addressof(a_geometry)),
					a_geometry.parent ? a_geometry.parent->GetName() : "",
					reinterpret_cast<std::uintptr_t>(a_geometry.parent),
					IsRootedAt(std::addressof(a_geometry), a_previewRoot),
					a_geometry.GetFlags(),
					a_geometry.GetAppCulled(),
					a_geometry.fadeAmount,
					a_geometry.local.translate.x,
					a_geometry.local.translate.y,
					a_geometry.local.translate.z,
					a_geometry.local.scale,
					a_geometry.world.translate.x,
					a_geometry.world.translate.y,
					a_geometry.world.translate.z,
					a_geometry.world.scale,
					a_geometry.worldBound.center.x,
					a_geometry.worldBound.center.y,
					a_geometry.worldBound.center.z,
					a_geometry.worldBound.fRadius,
					a_geometry.modelBound.center.x,
					a_geometry.modelBound.center.y,
					a_geometry.modelBound.center.z,
					a_geometry.modelBound.fRadius,
					reinterpret_cast<std::uintptr_t>(shaderProperty),
					shaderProperty ? shaderProperty->flags.underlying() : 0,
					shaderProperty ? shaderProperty->alpha : 0.0F,
					lightingProperty ? lightingProperty->rootName.c_str() : "",
					lightingProperty ? reinterpret_cast<std::uintptr_t>(lightingProperty->fadeNode) : 0,
					lightingProperty && lightingProperty->fadeNode ? IsRootedAt(lightingProperty->fadeNode, a_previewRoot) : false,
					reinterpret_cast<std::uintptr_t>(material),
					material ? std::to_underlying(material->GetFeature()) : -1,
					material ? std::to_underlying(material->GetType()) : -1,
					material ? material->hashKey : 0,
					material ? material->uniqueCode : 0,
					lightingProperty != nullptr,
					lightingProperty ? lightingProperty->baseTechniqueID : 0,
					reinterpret_cast<std::uintptr_t>(skin),
					skin && skin->rootNode ? skin->rootNode->GetName() : "",
					skin ? reinterpret_cast<std::uintptr_t>(skin->rootNode) : 0,
					skin && skin->rootNode ? IsRootedAt(skin->rootNode, a_previewRoot) : false,
					skin ? skin->bones.size() : 0,
					nullBones,
					skin ? skin->worldTransforms.size() : 0,
					skin ? skin->paletteStamp : 0,
					reinterpret_cast<std::uintptr_t>(a_geometry.rendererData),
					a_geometry.type,
					a_geometry.registered);
			});
		}

		void SanitizePreviewRenderTree(RE::NiAVObject& a_previewRoot, const char* a_stage)
		{
			std::uint32_t geometries = 0;
			std::uint32_t culledMissingShaderOrMaterial = 0;
			std::uint32_t culledNeckGore = 0;
			std::string firstCulledMissingShaderOrMaterial;
			std::string firstNeckGore;

			ForEachGeometry(std::addressof(a_previewRoot), [&](RE::BSGeometry& a_geometry) {
				++geometries;

				const auto geometryName = std::string(a_geometry.GetName());
				const auto* shaderProperty = GetGeometryShaderProperty(a_geometry);
				const bool missingShaderPath = !shaderProperty || !shaderProperty->material;
				const bool neckGore = IsPreviewNeckGoreGeometry(geometryName);

				if (missingShaderPath) {
					a_geometry.SetAppCulled(true);
					a_geometry.fadeAmount = 0.0F;
					++culledMissingShaderOrMaterial;
					if (firstCulledMissingShaderOrMaterial.empty()) {
						firstCulledMissingShaderOrMaterial = geometryName;
					}
				}
				if (neckGore) {
					a_geometry.SetAppCulled(true);
					a_geometry.fadeAmount = 0.0F;
					++culledNeckGore;
					if (firstNeckGore.empty()) {
						firstNeckGore = geometryName;
					}
				}
			});

			if (culledMissingShaderOrMaterial == 0 && culledNeckGore == 0) {
				return;
			}

			RE::NiUpdateData updateData;
			a_previewRoot.Update(updateData);

			REX::INFO(
				"TF3DHud V1 sanitized preview render tree [{}]: geometries={}, culledMissingShaderOrMaterial={} first='{}', culledNeckGore={} first='{}', boundRadius={}",
				a_stage,
				geometries,
				culledMissingShaderOrMaterial,
				firstCulledMissingShaderOrMaterial,
				culledNeckGore,
				firstNeckGore,
				a_previewRoot.worldBound.fRadius);
		}

		[[nodiscard]] bool ValidatePreviewRenderMaterials(RE::NiAVObject& a_previewRoot)
		{
			std::uint32_t lightingProperties = 0;
			std::uint32_t missingMaterials = 0;
			std::string firstMissingGeometry;

			ForEachGeometry(std::addressof(a_previewRoot), [&](RE::BSGeometry& a_geometry) {
				for (auto& property : a_geometry.properties) {
					auto* lightingProperty = netimmerse_cast<RE::BSLightingShaderProperty*>(property.get());
					if (!lightingProperty) {
						continue;
					}

					++lightingProperties;
					if (!lightingProperty->material) {
						++missingMaterials;
						if (firstMissingGeometry.empty()) {
							firstMissingGeometry = std::string(a_geometry.GetName());
						}
					}
				}
			});

			if (missingMaterials > 0) {
				REX::WARN(
					"TF3DHud V1 preview render material warning: lighting shader material is null on '{}' ({}/{})",
					firstMissingGeometry,
					missingMaterials,
					lightingProperties);
			}

			return true;
		}

		void RestorePreviewShaderAlpha(RE::NiAVObject& a_previewRoot, const char* a_stage)
		{
			std::uint32_t lightingProperties = 0;
			std::uint32_t restoredAlpha = 0;
			std::string firstRestoredGeometry;

			ForEachGeometry(std::addressof(a_previewRoot), [&](RE::BSGeometry& a_geometry) {
				for (auto& property : a_geometry.properties) {
					auto* lightingProperty = netimmerse_cast<RE::BSLightingShaderProperty*>(property.get());
					if (!lightingProperty) {
						continue;
					}

					++lightingProperties;
					if (lightingProperty->alpha <= 0.0F) {
						lightingProperty->alpha = 1.0F;
						++restoredAlpha;
						if (firstRestoredGeometry.empty()) {
							firstRestoredGeometry = std::string(a_geometry.GetName());
						}
					}
				}
			});

			if (restoredAlpha > 0) {
				REX::INFO(
					"TF3DHud V1 restored preview shader alpha after {}: restored={}/{} first='{}'",
					a_stage,
					restoredAlpha,
					lightingProperties,
					firstRestoredGeometry);
			}
		}

		void StripClonedGeometry(RE::NiAVObject& a_previewRoot)
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
					g_retiredPreviewObjects.emplace_back(std::move(object));
				}
			}

			REX::INFO("TF3DHud V1 stripped cloned root geometry: detached={}", detachedObjects.size());
		}

		void LogPreviewGeometrySummary(RE::NiAVObject& a_root, const std::string_view a_label)
		{
			std::uint32_t geometries = 0;
			std::uint32_t visible = 0;
			std::uint32_t skinned = 0;
			std::uint32_t shaderProperties = 0;
			std::uint32_t lightingProperties = 0;
			std::uint32_t missingShader = 0;
			std::uint32_t missingMaterial = 0;
			std::uint32_t skinTintMaterials = 0;
			std::string summary;

			ForEachGeometry(std::addressof(a_root), [&](RE::BSGeometry& a_geometry) {
				++geometries;
				if (!a_geometry.GetAppCulled() && a_geometry.fadeAmount > 0.0F) {
					++visible;
				}
				if (a_geometry.skinInstance) {
					++skinned;
				}

				auto* shaderProperty = GetGeometryShaderProperty(a_geometry);
				if (shaderProperty) {
					++shaderProperties;
					if (auto* lightingProperty = netimmerse_cast<RE::BSLightingShaderProperty*>(shaderProperty)) {
						++lightingProperties;
						if (auto* material = lightingProperty->material; material) {
							if (material->GetFeature() == RE::BSShaderMaterial::Feature::kSkinTint) {
								++skinTintMaterials;
							}
						} else {
							++missingMaterial;
						}
					}
				} else {
					++missingShader;
				}

				if (summary.size() < 512) {
					if (!summary.empty()) {
						summary += ", ";
					}
					summary += "'";
					summary += std::string(a_geometry.GetName());
					summary += "'";
					summary += a_geometry.GetAppCulled() ? "/culled" : "/visible";
					summary += shaderProperty ? "/shader" : "/noShader";
					if (auto* lightingProperty = netimmerse_cast<RE::BSLightingShaderProperty*>(shaderProperty)) {
						summary += lightingProperty->material ? "/material" : "/noMaterial";
					}
				}
			});

			REX::INFO(
				"TF3DHud V1 preview geometry summary [{}]: root='{}', geometries={}, visible={}, skinned={}, shaderProps={}, lightingProps={}, missingShader={}, missingMaterial={}, skinTintMaterials={}, summary={}",
				a_label,
				a_root.GetName(),
				geometries,
				visible,
				skinned,
				shaderProperties,
				lightingProperties,
				missingShader,
				missingMaterial,
				skinTintMaterials,
				summary);
		}

		[[nodiscard]] std::uint32_t PrepareAttachmentSkinComplexion(
			RE::NiAVObject& a_attachmentRoot,
			const RE::BIPED_OBJECT a_slot,
			const RE::BipedAnim& a_sourceBiped,
			const std::string_view a_label)
		{
			if (a_slot == RE::BIPED_OBJECT::kNone) {
				return 0;
			}

			SkinComplexionContext context{
				.slot = a_slot,
				.objects = const_cast<RE::BIPOBJECT*>(a_sourceBiped.object),
				.actorRef = a_sourceBiped.GetRequester(),
			};

			std::uint32_t geometries = 0;
			std::uint32_t skinTintCandidates = 0;
			std::uint32_t prepared = 0;
			std::string firstSkinTintGeometry;
			ForEachGeometry(std::addressof(a_attachmentRoot), [&](RE::BSGeometry& a_geometry) {
				++geometries;

				auto* lightingProperty = netimmerse_cast<RE::BSLightingShaderProperty*>(GetGeometryShaderProperty(a_geometry));
				auto* material = lightingProperty ? lightingProperty->material : nullptr;
				if (material && material->GetFeature() == RE::BSShaderMaterial::Feature::kSkinTint) {
					++skinTintCandidates;
					if (firstSkinTintGeometry.empty()) {
						firstSkinTintGeometry = std::string(a_geometry.GetName());
					}
				}

				g_doAdjustSkinComplexion(std::addressof(context), std::addressof(a_geometry));
				++prepared;
			});

			if (skinTintCandidates > 0) {
				const auto& sourceObject = a_sourceBiped.object[std::to_underlying(a_slot)];
				REX::INFO(
					"TF3DHud V1 prepared attachment skin complexion: slot={}, label='{}', geometries={}, calls={}, skinTintCandidates={}, firstSkinTint='{}', sourceSkinTexture={:X}",
					std::to_underlying(a_slot),
					a_label,
					geometries,
					prepared,
					skinTintCandidates,
					firstSkinTintGeometry,
					reinterpret_cast<std::uintptr_t>(sourceObject.skinTexture));
			}

			return prepared;
		}

		[[nodiscard]] RE::BGSModelMaterialSwap* SelectArmorAddonFacebonesModel(RE::TESObjectARMA& a_addon, const RE::SEX a_sex)
		{
			const auto sex = a_sex == RE::SEX::kFemale ? 1u : 0u;
			auto* model = std::addressof(a_addon.bipedModelFacebones[sex]);
			if (model->GetModel() && model->GetModel()[0] != '\0') {
				return model;
			}

			model = std::addressof(a_addon.bipedModelFacebones[0]);
			return model->GetModel() && model->GetModel()[0] != '\0' ? model : nullptr;
		}

		bool IsDescendantOf(RE::NiAVObject& a_object, RE::NiAVObject& a_potentialAncestor)
		{
			for (auto* current = std::addressof(a_object); current; current = current->parent) {
				if (current == std::addressof(a_potentialAncestor)) {
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] std::uint32_t CountNullSkinBones(RE::BSSkin::Instance& a_skin)
		{
			std::uint32_t count = 0;
			for (auto* bone : a_skin.bones) {
				if (!bone) {
					++count;
				}
			}
			return count;
		}

		[[nodiscard]] RE::NiAVObject* GetTopRoot(RE::NiAVObject* a_object)
		{
			auto* current = a_object;
			while (current && current->parent) {
				current = current->parent;
			}
			return current;
		}

		[[nodiscard]] const RE::BSFlattenedBoneTree::FlattenedBone* FindFlattenedBoneByWorldTransform(
			RE::BSFlattenedBoneTree& a_tree,
			const RE::NiTransform* a_worldTransform)
		{
			if (!a_tree.bone || !a_worldTransform || a_tree.boneCount <= 0 ||
				a_tree.boneCount > static_cast<std::int32_t>(RE::BSSkin::kMaxExpectedBones)) {
				return nullptr;
			}

			for (std::int32_t index = 0; index < a_tree.boneCount; ++index) {
				const auto& entry = a_tree.bone[index];
				if (std::addressof(entry.world) == a_worldTransform) {
					return std::addressof(entry);
				}
			}
			return nullptr;
		}

		[[nodiscard]] RE::NiNode* FindExistingNodeForFlattenedBone(
			const RE::BSFlattenedBoneTree::FlattenedBone& a_entry,
			const std::array<RE::NiAVObject*, 4>& a_searchRoots)
		{
			if (auto* existing = a_entry.node.get()) {
				return existing;
			}

			const std::string_view name(a_entry.name);
			if (name.empty()) {
				return nullptr;
			}

			std::unordered_set<RE::NiAVObject*> visitedRoots;
			for (auto* root : a_searchRoots) {
				if (!root || !visitedRoots.insert(root).second) {
					continue;
				}
				if (auto* found = root->GetObjectByName(a_entry.name)) {
					return found->IsNode();
				}
			}
			return nullptr;
		}

		void RefreshPreviewBoneLookup(RE::NiAVObject& a_previewRoot, const char* a_reason)
		{
			g_createBoneMap(std::addressof(a_previewRoot));

			auto* flattened = FindFlattenedBoneTree(std::addressof(a_previewRoot));
			if (!flattened) {
				REX::INFO("TF3DHud V1 refreshed preview BSBoneMap for {}; no BSFlattenedBoneTree found", a_reason);
				return;
			}

			const auto beforeBoneCount = flattened->boneCount;
			const std::string treeName(flattened->GetName());
			g_createBoneMap(flattened);
			g_previewFlattenedBoneTree = flattened;
			g_createBoneMap(std::addressof(a_previewRoot));
			REX::INFO(
				"TF3DHud V1 refreshed preview BSBoneMap for {}: flattenedTree='{}', bones={}",
				a_reason,
				treeName,
				beforeBoneCount);
		}

		void ResolveNullSkinBonesFromFlattenedTree(
			RE::BSSkin::Instance& a_skin,
			RE::NiAVObject& a_attachmentRoot,
			const std::string_view a_geometryName,
			std::uint32_t& a_resolvedNullBones,
			std::uint32_t& a_unresolvedNullBones)
		{
			if (a_skin.bones.empty() || a_skin.bones.size() > RE::BSSkin::kMaxExpectedBones) {
				return;
			}
			if (a_skin.worldTransforms.empty() || a_skin.worldTransforms.size() != a_skin.bones.size()) {
				return;
			}

			const auto beforeNullBones = CountNullSkinBones(a_skin);
			if (beforeNullBones == 0) {
				return;
			}

			auto* flattened = FindFlattenedBoneTree(a_skin.rootNode);
			if (!flattened) {
				flattened = FindFlattenedBoneTree(GetTopRoot(std::addressof(a_attachmentRoot)));
			}
			if (!flattened) {
				a_unresolvedNullBones += beforeNullBones;
				return;
			}

			const std::array<RE::NiAVObject*, 4> searchRoots{
				a_skin.rootNode,
				std::addressof(a_attachmentRoot),
				GetTopRoot(a_skin.rootNode),
				GetTopRoot(std::addressof(a_attachmentRoot)),
			};

			std::uint32_t resolved = 0;
			for (std::uint32_t index = 0; index < a_skin.bones.size(); ++index) {
				if (a_skin.bones[index]) {
					continue;
				}

				const auto* entry = FindFlattenedBoneByWorldTransform(*flattened, a_skin.worldTransforms[index]);
				auto* node = entry ? FindExistingNodeForFlattenedBone(*entry, searchRoots) : nullptr;
				if (!node) {
					continue;
				}

				a_skin.bones[index] = node;
				a_skin.worldTransforms[index] = std::addressof(node->world);
				++resolved;
			}

			const auto afterNullBones = CountNullSkinBones(a_skin);
			a_resolvedNullBones += resolved;
			a_unresolvedNullBones += afterNullBones;
			if (resolved > 0 || afterNullBones > 0) {
				REX::INFO(
					"TF3DHud V1 resolved null skin bones from flattened tree: geometry='{}', nullBefore={}, resolved={}, nullAfter={}",
					a_geometryName,
					beforeNullBones,
					resolved,
					afterNullBones);
			}
		}

		[[nodiscard]] std::uint32_t MergeAttachmentSkinBones(
			RE::NiAVObject& a_attachmentRoot,
			RE::NiNode& a_previewRootNode,
			RE::NiAVObject& a_previewRoot,
			std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			std::vector<RE::NiPointer<RE::NiAVObject>> candidateBones;
			struct ExternalBoneClone
			{
				RE::NiAVObject* sourceBone{ nullptr };
				RE::NiNode* previewParent{ nullptr };
			};
			std::vector<ExternalBoneClone> externalBones;
			std::unordered_set<RE::NiAVObject*> seenBones;
			std::unordered_set<RE::NiAVObject*> seenExternalRoots;
			ForEachGeometry(std::addressof(a_attachmentRoot), [&](RE::BSGeometry& a_geometry) {
				auto* skin = a_geometry.skinInstance.get();
				if (!skin || skin->bones.size() > RE::BSSkin::kMaxExpectedBones) {
					return;
				}

				for (auto* bone : skin->bones) {
					if (!bone || !seenBones.insert(bone).second) {
						continue;
					}
					if (FindNodeByName(a_previewNodes, bone->GetName())) {
						continue;
					}

					if (IsDescendantOf(*bone, a_attachmentRoot)) {
						candidateBones.emplace_back(bone);
						continue;
					}

					auto* cloneRoot = bone;
					auto* previewParent = std::addressof(a_previewRootNode);
					bool matchedPreviewParent = false;
					for (auto* parent = bone->parent; parent; parent = parent->parent) {
						if (auto* matchedParent = FindNodeByName(a_previewNodes, parent->GetName())) {
							if (auto* matchedParentNode = matchedParent->IsNode()) {
								previewParent = matchedParentNode;
								matchedPreviewParent = true;
								break;
							}
						}
						cloneRoot = parent;
					}
					if (!matchedPreviewParent) {
						cloneRoot = bone;
					}

					if (cloneRoot && seenExternalRoots.insert(cloneRoot).second) {
						externalBones.push_back({
							.sourceBone = cloneRoot,
							.previewParent = previewParent,
						});
					}
				}
			});

			std::uint32_t mergedBones = 0;
			std::uint32_t externalClonedBones = 0;
			for (auto& candidate : candidateBones) {
				if (!candidate || !candidate->parent || FindNodeByName(a_previewNodes, candidate->GetName())) {
					continue;
				}

				bool hasMissingAncestor = false;
				for (auto& other : candidateBones) {
					if (other && other.get() != candidate.get() && IsDescendantOf(*candidate, *other)) {
						hasMissingAncestor = true;
						break;
					}
				}
				if (hasMissingAncestor) {
					continue;
				}

				auto previewClone = ClonePreviewObject(*candidate, std::addressof(a_previewRoot), std::addressof(a_previewNodes), nullptr);
				if (!previewClone) {
					continue;
				}

				StripControllerChains(*previewClone);
				StripClonedGeometry(*previewClone);
				RE::bhkWorld::RemoveObjects(previewClone.get(), true, true);
				PreparePreviewTree(*previewClone);

				a_previewRootNode.AttachChild(previewClone.get(), false);
				g_previewAttachments.push_back({
					.slot = RE::BIPED_OBJECT::kNone,
					.object = previewClone,
					.parent = std::addressof(a_previewRootNode),
				});
				++mergedBones;
			}

			for (const auto& external : externalBones) {
				if (!external.sourceBone || FindNodeByName(a_previewNodes, external.sourceBone->GetName())) {
					continue;
				}

				auto previewClone = ClonePreviewObject(*external.sourceBone, std::addressof(a_previewRoot), std::addressof(a_previewNodes), nullptr);
				if (!previewClone) {
					continue;
				}

				StripControllerChains(*previewClone);
				StripClonedGeometry(*previewClone);
				RE::bhkWorld::RemoveObjects(previewClone.get(), true, true);
				PreparePreviewTree(*previewClone);

				auto* previewParent = external.previewParent ? external.previewParent : std::addressof(a_previewRootNode);
				previewParent->AttachChild(previewClone.get(), false);
				g_previewAttachments.push_back({
					.slot = RE::BIPED_OBJECT::kNone,
					.object = previewClone,
					.parent = previewParent,
				});
				++externalClonedBones;
			}

			if (mergedBones > 0 || externalClonedBones > 0) {
				RefreshPreviewBoneLookup(a_previewRoot, "attachment bone merge");
				a_previewNodes.clear();
				CollectNamedNodes(std::addressof(a_previewRoot), a_previewNodes);
			}
			if (externalClonedBones > 0) {
				REX::INFO(
					"TF3DHud V1 cloned external attachment skin bone roots: cloned={}, attachment='{}'",
					externalClonedBones,
					a_attachmentRoot.GetName());
			}

			return mergedBones + externalClonedBones;
		}

		void RetirePreviewAttachments(const bool a_detach)
		{
			for (auto& attachment : g_previewAttachments) {
				if (a_detach && attachment.parent && attachment.object) {
					attachment.parent->DetachChild(attachment.object.get());
				}
				if (attachment.object) {
					g_retiredPreviewObjects.emplace_back(std::move(attachment.object));
				}
			}
			g_previewAttachments.clear();
		}

		void ResetPreview3DState(const char* a_reason, const bool a_disableRenderer)
		{
			Renderer::ClearPreviewRoot(a_disableRenderer);
			RetirePreviewAttachments(true);
			g_previewRoot.reset();
			g_previewFlattenedBoneTree = nullptr;
			g_previewFaceNode.reset();
			REX::INFO("TF3DHud V1 cleared preview 3D state for {}", a_reason);
		}

		void ReleasePreview3DState(const char* a_reason)
		{
			ResetPreview3DState(a_reason, true);
			g_retiredPreviewObjects.clear();
		}

		void ClearPreviewRebuildState()
		{
			g_bipedSignature = 0;
			g_visualSignature = 0;
			g_failedPreviewSignature = 0;
			g_lastDiagnostic.clear();
		}

		[[nodiscard]] bool RebindSkinInstance(
			RE::BSSkin::Instance& a_skin,
			RE::NiAVObject& a_previewRoot,
			const std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes,
			std::uint32_t& a_reboundBones,
			std::uint32_t& a_missingBones,
			std::string* a_firstFailedGeometry = nullptr,
			std::string* a_firstMissingBone = nullptr,
			const std::string_view a_geometryName = {})
		{
			if (a_skin.bones.empty()) {
				return true;
			}

			if (a_skin.bones.size() > RE::BSSkin::kMaxExpectedBones) {
				if (a_firstFailedGeometry && a_firstFailedGeometry->empty()) {
					*a_firstFailedGeometry = std::string(a_geometryName);
				}
				return false;
			}

			if (!a_skin.worldTransforms.empty() && a_skin.worldTransforms.size() != a_skin.bones.size()) {
				if (a_firstFailedGeometry && a_firstFailedGeometry->empty()) {
					*a_firstFailedGeometry = std::string(a_geometryName);
				}
				return false;
			}

			bool fullyBound = true;
			for (std::uint32_t i = 0; i < a_skin.bones.size(); ++i) {
				auto* originalBone = a_skin.bones[i];
				if (!originalBone) {
					++a_missingBones;
					fullyBound = false;
					continue;
				}

				auto* previewBone = FindNodeByName(a_previewNodes, originalBone->GetName());
				if (!previewBone) {
					++a_missingBones;
					if (a_firstFailedGeometry && a_firstFailedGeometry->empty()) {
						*a_firstFailedGeometry = std::string(a_geometryName);
					}
					if (a_firstMissingBone && a_firstMissingBone->empty()) {
						*a_firstMissingBone = std::string(originalBone->GetName());
					}
					fullyBound = false;
					continue;
				}

				if (a_skin.bones[i] != previewBone) {
					a_skin.bones[i] = previewBone;
					++a_reboundBones;
				}
				if (!a_skin.worldTransforms.empty()) {
					a_skin.worldTransforms[i] = std::addressof(previewBone->world);
				}
			}

			a_skin.rootNode = std::addressof(a_previewRoot);
			a_skin.paletteStamp = 0;
			return fullyBound;
		}

		[[nodiscard]] bool RebindAllSkinInstances(RE::NiAVObject& a_previewRoot)
		{
			std::unordered_map<std::string, RE::NiAVObject*> previewNodes;
			CollectNamedNodes(std::addressof(a_previewRoot), previewNodes);
			if (previewNodes.empty()) {
				LogDiagnostic("preview root clone has no named nodes");
				return false;
			}

			std::uint32_t skinnedGeometries = 0;
			std::uint32_t reboundBones = 0;
			std::uint32_t missingBones = 0;
			std::uint32_t failedSkins = 0;
			std::string firstFailedGeometry;
			std::string firstMissingBone;
			ForEachGeometry(std::addressof(a_previewRoot), [&](RE::BSGeometry& a_geometry) {
				auto* skin = a_geometry.skinInstance.get();
				if (!skin) {
					return;
				}
				++skinnedGeometries;
				if (!RebindSkinInstance(
						*skin,
						a_previewRoot,
						previewNodes,
						reboundBones,
						missingBones,
						std::addressof(firstFailedGeometry),
						std::addressof(firstMissingBone),
						a_geometry.GetName())) {
					++failedSkins;
				}
			});

			RefreshPreviewBoneLookup(a_previewRoot, "skin rebind");

			REX::INFO(
				"TF3DHud V1 rebound cloned root skinning: skinned={}, reboundBones={}, missingBones={}, failedSkins={}, firstFailedGeometry='{}', firstMissingBone='{}'",
				skinnedGeometries,
				reboundBones,
				missingBones,
				failedSkins,
				firstFailedGeometry,
				firstMissingBone);
			return failedSkins == 0;
		}

		[[nodiscard]] bool SyncEquipmentsFromBiped(RE::NiAVObject& a_previewRoot, const RE::BipedAnim& a_sourceBiped)
		{
			auto* previewRootNode = a_previewRoot.IsNode();
			if (!previewRootNode) {
				LogDiagnostic("equipment sync skipped: preview root is not a NiNode");
				return false;
			}

			RetirePreviewAttachments(true);

			std::unordered_map<std::string, RE::NiAVObject*> previewNodes;
			CollectNamedNodes(std::addressof(a_previewRoot), previewNodes);
			if (previewNodes.empty()) {
				LogDiagnostic("equipment sync skipped: preview root has no named nodes");
				return false;
			}

			std::unordered_set<RE::NiAVObject*> seenSourceObjects;
			std::uint32_t clonedObjects = 0;
			std::uint32_t attachedObjects = 0;
			std::uint32_t skinnedGeometries = 0;
			std::uint32_t reboundBones = 0;
			std::uint32_t missingBones = 0;
			std::uint32_t seededSkinMappings = 0;
			std::uint32_t sharedSkinInstances = 0;
			std::uint32_t strippedControllers = 0;
			std::uint32_t failedClones = 0;
			std::uint32_t failedSkins = 0;
			std::uint32_t mergedAttachmentBones = 0;
			std::uint32_t skinComplexionPrepared = 0;
			std::uint32_t resolvedNullSkinBones = 0;
			std::uint32_t unresolvedNullSkinBones = 0;
			std::string firstFailedGeometry;
			std::string firstMissingBone;

			auto mirrorObject = [&](const RE::BIPED_OBJECT a_slot, RE::NiAVObject* a_sourceClone) {
				auto* sourceClone = a_sourceClone;
				if (!sourceClone || !seenSourceObjects.insert(sourceClone).second) {
					return;
				}

				std::unordered_set<RE::BSSkin::Instance*> sourceSkins;
				CollectSkinInstances(sourceClone, sourceSkins);

				std::uint32_t cloneSeededSkinMappings = 0;
				auto previewClone = ClonePreviewObject(*sourceClone, std::addressof(a_previewRoot), std::addressof(previewNodes), std::addressof(cloneSeededSkinMappings));
				seededSkinMappings += cloneSeededSkinMappings;
				if (!previewClone) {
					++failedClones;
					return;
				}
				++clonedObjects;
				strippedControllers += StripControllerChains(*previewClone);

				auto* parent = FindPreviewAttachParent(*sourceClone, a_previewRoot, previewNodes);
				if (!parent) {
					g_retiredPreviewObjects.emplace_back(std::move(previewClone));
					++failedClones;
					return;
				}

				RE::bhkWorld::RemoveObjects(previewClone.get(), true, true);
				PreparePreviewTree(*previewClone);
				ForEachGeometry(previewClone.get(), [&](RE::BSGeometry& a_geometry) {
					if (auto* skin = a_geometry.skinInstance.get()) {
						ResolveNullSkinBonesFromFlattenedTree(
							*skin,
							*previewClone,
							a_geometry.GetName(),
							resolvedNullSkinBones,
							unresolvedNullSkinBones);
					}
				});
				mergedAttachmentBones += MergeAttachmentSkinBones(
					*previewClone,
					*previewRootNode,
					a_previewRoot,
					previewNodes);

				ForEachGeometry(previewClone.get(), [&](RE::BSGeometry& a_geometry) {
					auto* skin = a_geometry.skinInstance.get();
					if (!skin) {
						return;
					}
					++skinnedGeometries;
					if (sourceSkins.contains(skin)) {
						++sharedSkinInstances;
						++failedSkins;
						if (firstFailedGeometry.empty()) {
							firstFailedGeometry = std::string(a_geometry.GetName());
						}
						return;
					}
					if (!RebindSkinInstance(
							*skin,
							a_previewRoot,
							previewNodes,
							reboundBones,
							missingBones,
							std::addressof(firstFailedGeometry),
							std::addressof(firstMissingBone),
							a_geometry.GetName())) {
						++failedSkins;
					}
				});
				skinComplexionPrepared += PrepareAttachmentSkinComplexion(
					*previewClone,
					a_slot,
					a_sourceBiped,
					sourceClone->GetName());

				parent->AttachChild(previewClone.get(), false);
				g_previewAttachments.push_back({
					.slot = a_slot,
					.object = previewClone,
					.parent = parent,
				});
				++attachedObjects;
			};

			for (std::int32_t i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
				const auto slot = static_cast<RE::BIPED_OBJECT>(i);
				switch (slot) {
				case RE::BIPED_OBJECT::kFaceGenHead:
				case RE::BIPED_OBJECT::kEyes:
				case RE::BIPED_OBJECT::kBeard:
				case RE::BIPED_OBJECT::kMouth:
				case RE::BIPED_OBJECT::kScalp:
					continue;
				default:
					break;
				}
				const auto& sourceObject = a_sourceBiped.object[i];
				mirrorObject(slot, sourceObject.partClone.get());
			}

			// FaceGen/head-part preview is temporarily disabled while isolating
			// LooksMenu/F4EE native FaceGen crashes.

			RefreshPreviewBoneLookup(a_previewRoot, "equipment sync");

			RE::NiUpdateData updateData;
			a_previewRoot.Update(updateData);

			REX::INFO(
				"TF3DHud V1 mirrored resolved biped/face geometry: cloned={}, attached={}, skinned={}, seededSkinMappings={}, sharedSkinInstances={}, strippedControllers={}, mergedAttachmentBones={}, skinComplexionPrepared={}, nullSkinBones=resolved={}/unresolved={}, reboundBones={}, missingBones={}, failedClones={}, failedSkins={}, firstFailedGeometry='{}', firstMissingBone='{}'",
				clonedObjects,
				attachedObjects,
				skinnedGeometries,
				seededSkinMappings,
				sharedSkinInstances,
				strippedControllers,
				mergedAttachmentBones,
				skinComplexionPrepared,
				resolvedNullSkinBones,
				unresolvedNullSkinBones,
				reboundBones,
				missingBones,
				failedClones,
				failedSkins,
				firstFailedGeometry,
				firstMissingBone);
			return attachedObjects > 0;
		}

		struct FaceBoneAttachmentStats
		{
			std::uint32_t models{ 0 };
			std::uint32_t attached{ 0 };
			std::uint32_t failed{ 0 };
			std::uint32_t strippedControllers{ 0 };
		};

		FaceBoneAttachmentStats AttachPreviewFaceBonesFromBiped(
			const RE::BipedAnim& a_sourceBiped,
			RE::NiAVObject& a_previewRoot,
			const RE::SEX a_sex)
		{
			FaceBoneAttachmentStats stats;

			auto* previewRootNode = a_previewRoot.IsNode();
			if (!previewRootNode) {
				return stats;
			}

			std::unordered_set<std::string> loadedPaths;
			for (std::int32_t i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
				const auto& sourceObject = a_sourceBiped.object[i];
				if (!sourceObject.armorAddon || !sourceObject.part || (sourceObject.part->flags & 1) == 0) {
					continue;
				}

				auto* model = SelectArmorAddonFacebonesModel(*sourceObject.armorAddon, a_sex);
				const auto* modelPath = model ? model->GetModel() : nullptr;
				if (!modelPath || modelPath[0] == '\0') {
					continue;
				}
				if (!loadedPaths.insert(modelPath).second) {
					continue;
				}

				++stats.models;
				auto faceBones = LoadPreviewModel(modelPath);
				if (!faceBones) {
					++stats.failed;
					continue;
				}

				stats.strippedControllers += StripControllerChains(*faceBones);
				RE::bhkWorld::RemoveObjects(faceBones.get(), true, true);
				PreparePreviewTree(*faceBones);
				LogPreviewGeometrySummary(*faceBones, modelPath);
				StripClonedGeometry(*faceBones);

				previewRootNode->AttachChild(faceBones.get(), false);
				g_previewAttachments.push_back({
					.slot = RE::BIPED_OBJECT::kFaceGenHead,
					.object = faceBones,
					.parent = previewRootNode,
				});
				++stats.attached;
			}

			if (stats.attached > 0) {
				RefreshPreviewBoneLookup(a_previewRoot, "facebones model attach");
			}

			REX::INFO(
				"TF3DHud V1 attached preview-owned facebones from biped data: models={}, attached={}, failed={}, strippedControllers={}",
				stats.models,
				stats.attached,
				stats.failed,
				stats.strippedControllers);
			return stats;
		}

		bool EnsurePreviewHead(RE::PlayerCharacter& a_player, RE::NiAVObject& a_previewRoot, const RE::BipedAnim& a_sourceBiped)
		{
			auto* playerBase = a_player.GetObjectReference();
			auto* npc = playerBase ? playerBase->As<RE::TESNPC>() : nullptr;
			if (!npc) {
				LogDiagnostic("preview head creation skipped: player base is not TESNPC");
				return false;
			}

			auto* rootFaceNPC = npc->GetRootFaceNPC();
			if (!rootFaceNPC) {
				LogDiagnostic("preview head creation skipped: root face NPC is null");
				return false;
			}

			RE::NiPointer<RE::BSFaceGenNiNode> faceNode;
			const bool createdHead = g_createHeadForNPC(npc, faceNode, true, true, nullptr);

			if (!createdHead || !faceNode) {
				LogDiagnostic("preview head creation failed: CreateHeadForNPC returned null");
				return false;
			}

			RE::bhkWorld::RemoveObjects(faceNode.get(), true, true);
			PreparePreviewTree(*faceNode);

			auto* sourceFaceNode = GetSourceFaceNode(a_player);
			std::unordered_map<std::string, RE::NiAVObject*> previewNodes;
			CollectNamedNodes(std::addressof(a_previewRoot), previewNodes);
			auto* parent = sourceFaceNode ? FindPreviewAttachParent(*sourceFaceNode, a_previewRoot, previewNodes) : a_previewRoot.IsNode();
			if (!parent) {
				LogDiagnostic("preview head creation failed: no preview head attach parent");
				return false;
			}
			auto* previewRootNode = a_previewRoot.IsNode();
			if (!previewRootNode) {
				LogDiagnostic("preview head creation failed: preview root is not a NiNode");
				return false;
			}

			auto faceBoneStats = AttachPreviewFaceBonesFromBiped(a_sourceBiped, a_previewRoot, npc->GetSex());

			previewNodes.clear();
			CollectNamedNodes(std::addressof(a_previewRoot), previewNodes);
			const auto mergedFaceBones = MergeAttachmentSkinBones(
				*faceNode,
				*previewRootNode,
				a_previewRoot,
				previewNodes);

			std::uint32_t reboundBones = 0;
			std::uint32_t missingBones = 0;
			std::uint32_t failedSkins = 0;
			std::string firstFailedGeometry;
			std::string firstMissingBone;
			ForEachGeometry(faceNode.get(), [&](RE::BSGeometry& a_geometry) {
				auto* skin = a_geometry.skinInstance.get();
				if (!skin) {
					return;
				}
				if (!RebindSkinInstance(
						*skin,
						a_previewRoot,
						previewNodes,
						reboundBones,
						missingBones,
						std::addressof(firstFailedGeometry),
						std::addressof(firstMissingBone),
						a_geometry.GetName())) {
					++failedSkins;
				}
			});

			parent->AttachChild(faceNode.get(), false);
			g_previewFaceNode = faceNode;
			RefreshPreviewBoneLookup(a_previewRoot, "facegen head attach");

			REX::INFO(
				"TF3DHud V1 created preview facegen head from player NPC {:08X}; root face NPC {:08X}, parent='{}', faceBoneModels={}, attachedFaceBones={}, failedFaceBones={}, strippedFaceBoneControllers={}, mergedFaceBones={}, reboundBones={}, missingBones={}, failedSkins={}, firstFailedGeometry='{}', firstMissingBone='{}'",
				npc->GetFormID(),
				rootFaceNPC->GetFormID(),
				parent->GetName(),
				faceBoneStats.models,
				faceBoneStats.attached,
				faceBoneStats.failed,
				faceBoneStats.strippedControllers,
				mergedFaceBones,
				reboundBones,
				missingBones,
				failedSkins,
				firstFailedGeometry,
				firstMissingBone);
			return true;
		}

		void ApplyPreviewBodyTint(RE::TESNPC& a_npc, RE::NiAVObject& a_previewRoot)
		{
			RE::NiColorA bodyTint{ 0.0F, 0.0F, 0.0F, 0.0F };
			g_calculateBodyTintColor(std::addressof(a_npc), bodyTint, nullptr, true, false);
			const auto calculatedRed = bodyTint.r;
			const auto calculatedGreen = bodyTint.g;
			const auto calculatedBlue = bodyTint.b;
			const auto calculatedAlpha = bodyTint.a;
			bodyTint.a = 1.0F;
			g_updateBodyTintColorsOnScene(std::addressof(a_previewRoot), bodyTint);
			RestorePreviewShaderAlpha(a_previewRoot, "body tint");

			std::uint32_t geometries = 0;
			std::uint32_t lightingProperties = 0;
			std::uint32_t skinTintMaterials = 0;
			std::uint32_t alphaZeroShaders = 0;
			std::string firstSkinTintGeometry;
			std::string firstAlphaZeroGeometry;
			ForEachGeometry(std::addressof(a_previewRoot), [&](RE::BSGeometry& a_geometry) {
				++geometries;
				for (auto& property : a_geometry.properties) {
					auto* lightingProperty = netimmerse_cast<RE::BSLightingShaderProperty*>(property.get());
					if (!lightingProperty) {
						continue;
					}

					++lightingProperties;
					if (lightingProperty->alpha <= 0.0F) {
						++alphaZeroShaders;
						if (firstAlphaZeroGeometry.empty()) {
							firstAlphaZeroGeometry = std::string(a_geometry.GetName());
						}
					}

					auto* material = lightingProperty->material;
					if (material && material->GetFeature() == RE::BSShaderMaterial::Feature::kSkinTint) {
						++skinTintMaterials;
						if (firstSkinTintGeometry.empty()) {
							firstSkinTintGeometry = std::string(a_geometry.GetName());
						}
					}
				}
			});

			REX::INFO(
				"TF3DHud V1 applied preview body tint calculatedRgb=({}, {}, {}), appliedRgb=({}, {}, {}), calculatedAlpha={}, appliedAlpha=1, npcTintBytes=({}, {}, {}, {}) from NPC {:08X}; geometries={}, lightingProperties={}, skinTintMaterials={} firstSkinTint='{}', alphaZeroShaders={} firstAlphaZero='{}'",
				calculatedRed,
				calculatedGreen,
				calculatedBlue,
				bodyTint.r,
				bodyTint.g,
				bodyTint.b,
				calculatedAlpha,
				static_cast<std::uint8_t>(a_npc.bodyTintColorR),
				static_cast<std::uint8_t>(a_npc.bodyTintColorG),
				static_cast<std::uint8_t>(a_npc.bodyTintColorB),
				static_cast<std::uint8_t>(a_npc.bodyTintColorA),
				a_npc.GetFormID(),
				geometries,
				lightingProperties,
				skinTintMaterials,
				firstSkinTintGeometry,
				alphaZeroShaders,
				firstAlphaZeroGeometry);
		}

		bool RebuildPreview(
			RE::PlayerCharacter& a_player,
			const RE::BipedAnim& a_biped,
			std::uint64_t a_signature,
			std::uint64_t a_visualSignature)
		{
			// IDA: Inventory3DManager::AddLoadedModel swaps Offscreen_Set3D roots
			// without disabling the renderer. Keep that shape for in-frame preview
			// rebuilds; true hides still go through ReleasePreview3DState/Hide.
			ResetPreview3DState(g_visualSignature != a_visualSignature ? "player visual change" : "preview rebuild", false);

			RE::NiPointer<RE::NiAVObject> previewRoot = LoadPreviewRaceSkeleton(a_player);
			if (!previewRoot) {
				g_previewRoot.reset();
				g_previewFlattenedBoneTree = nullptr;
				g_bipedSignature = 0;
				g_visualSignature = 0;
				LogDiagnostic("preview race skeleton load returned null");
				return false;
			}

			RE::bhkWorld::RemoveObjects(previewRoot.get(), true, true);
			const auto strippedSkeletonControllers = StripControllerChains(*previewRoot);
			PreparePreviewTree(*previewRoot);
			if (!SyncEquipmentsFromBiped(*previewRoot, a_biped)) {
				g_previewRoot.reset();
				g_previewFlattenedBoneTree = nullptr;
				g_bipedSignature = 0;
				g_visualSignature = 0;
				return false;
			}
			if (!EnsurePreviewHead(a_player, *previewRoot, a_biped)) {
				g_previewRoot.reset();
				g_previewFlattenedBoneTree = nullptr;
				g_bipedSignature = 0;
				g_visualSignature = 0;
				return false;
			}
			if (auto* playerBase = a_player.GetObjectReference(); playerBase) {
				if (auto* npc = playerBase->As<RE::TESNPC>(); npc) {
					ApplyPreviewBodyTint(*npc, *previewRoot);
				}
			}

			LogPreviewRenderGeometryDetails(*previewRoot, "before-sanitize");
			SanitizePreviewRenderTree(*previewRoot, "before-framing");
			ApplyHeadCenteredFraming(*previewRoot, true);
			PrepareForInterface3DOffscreen(*previewRoot);
			if (!ValidatePreviewRenderMaterials(*previewRoot)) {
				g_previewRoot.reset();
				g_previewFlattenedBoneTree = nullptr;
				g_bipedSignature = 0;
				g_visualSignature = 0;
				return false;
			}

			g_previewRoot = previewRoot;
			g_bipedSignature = a_signature;
			g_visualSignature = a_visualSignature;
			g_failedPreviewSignature = 0;

			Renderer::AttachPreviewRoot(*g_previewRoot);

			REX::INFO(
				"Rebuilt TF3DHud V1 preview from preview-owned race skeleton plus resolved biped visuals; visualSignature={:016X}, strippedSkeletonControllers={}",
				g_visualSignature,
				strippedSkeletonControllers);
			return true;
		}

		float CalculateEngineDelta()
		{
			const auto currentEngineTime = *g_engineTime;
			if (!g_hasLastEngineTime) {
				g_lastEngineTime = currentEngineTime;
				g_hasLastEngineTime = true;
				return 0.0F;
			}

			const auto delta = currentEngineTime - g_lastEngineTime;
			g_lastEngineTime = currentEngineTime;
			return delta;
		}

		void Tick(float a_hookDeltaTime, float a_engineDeltaTime)
		{
			++g_frameTicks;
			if (a_engineDeltaTime > 0.0F) {
				g_diagnosticAccumulator += a_engineDeltaTime;
			}
			if (g_frameTicks == 1) {
				REX::INFO(
					"TF3DHud V1 state: frame hook is ticking; hook delta={}, engine time={}, engine delta={}",
					a_hookDeltaTime,
					g_lastEngineTime,
					a_engineDeltaTime);
			}

			if (g_looksMenuSuspended) {
				if (g_diagnosticAccumulator >= 5.0F) {
					LogDiagnostic("halted: LooksMenu is open", true);
					g_diagnosticAccumulator = 0.0F;
				} else {
					LogDiagnostic("halted: LooksMenu is open");
				}
				Renderer::Hide();
				return;
			}

			auto player = RE::PlayerCharacter::GetSingleton();
			std::string reason;
			if (!ShouldShow(player, reason)) {
				if (g_diagnosticAccumulator >= 5.0F) {
					LogDiagnostic("hidden: " + reason, true);
					g_diagnosticAccumulator = 0.0F;
				} else {
					LogDiagnostic("hidden: " + reason);
				}
				Renderer::Hide();
				return;
			}

			if (!Renderer::IsConfigured() || !Renderer::Get()) {
				Renderer::Configure();
				Renderer::ConfigureLighting();
			}

			if (!Renderer::Get()) {
				LogDiagnostic("renderer unavailable after configuration");
				return;
			}
			Renderer::UpdatePostAAForDynamicResolution("tick");

			const auto& biped = player->GetBiped(false);
			if (!biped) {
				LogDiagnostic("third-person biped is null");
				Renderer::Hide();
				return;
			}

			std::string sourceReason;
			if (!IsPreviewSourceReady(*player, *biped, sourceReason)) {
				LogDiagnostic("preview render postponed: " + sourceReason);
				Renderer::Hide();
				return;
			}

			const auto visualSignature = BuildVisualSignature(*player);
			const auto signature = BuildPreviewSignature(*player, biped.get());
			if (!g_previewRoot || signature == 0 || signature != g_bipedSignature) {
				if (signature == 0) {
					LogDiagnostic("third-person biped has empty signature");
				}
				if (!RebuildPreview(*player, *biped, signature, visualSignature)) {
					Renderer::Hide();
					return;
				}
			}

			if (Renderer::Enable()) {
				LogDiagnostic("renderer enabled");
			}
		}
	}

	namespace Previewer
	{
		void Update(float a_deltaTime)
		{
			Tick(a_deltaTime, CalculateEngineDelta());
		}

		void Reset()
		{
			g_looksMenuSuspended = false;
			Renderer::Hide();
			Renderer::Reset();
			ReleasePreview3DState("plugin reset");
			g_previewRoot.reset();
			g_previewFaceNode.reset();
			ClearPreviewRebuildState();
			REX::INFO("Reset TF3DHud V1 transient preview state");
		}

		void SuspendForLooksMenu()
		{
			if (g_looksMenuSuspended) {
				return;
			}

			g_looksMenuSuspended = true;
			Renderer::Hide();
			ReleasePreview3DState("LooksMenu open");
			ClearPreviewRebuildState();
			REX::INFO("TF3DHud V1 suspended preview updates while LooksMenu is open");
		}

		void ResumeAfterLooksMenu()
		{
			if (!g_looksMenuSuspended) {
				return;
			}

			g_looksMenuSuspended = false;
			ClearPreviewRebuildState();
			REX::INFO("TF3DHud V1 resumed preview updates after LooksMenu close; preview will rebuild on next eligible frame");
		}
	}

}
