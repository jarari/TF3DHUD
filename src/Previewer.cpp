#include "Previewer.h"
#include "Renderer.h"
#include "Utils.h"

#include "BSSkin.h"
#include "Config.h"

#include "RE/B/BSFadeNode.h"
#include "RE/B/BSGeometry.h"
#include "RE/B/BSLightingShaderProperty.h"
#include "RE/B/BSShaderMaterial.h"
#include "RE/B/BSShaderProperty.h"
#include "RE/N/NiCloningProcess.h"
#include "RE/N/NiNode.h"
#include "RE/N/NiUpdateData.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESNPC.h"
#include "RE/T/TLS.h"

#include <algorithm>
#include <cstdint>
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

		using ActorCtor_t = RE::Actor*(RE::Actor*, bool);
		using CreateHeadForNPC_t = void(RE::TESNPC*, RE::NiPointer<RE::BSFaceGenNiNode>&, bool, bool, void*);
		using AttachHeadHelper_t = void(const RE::TESNPC*, RE::BSFaceGenNiNode*, RE::TESObjectREFR*, RE::Actor*);
		using CalculateBodyTintColor_t =
			void(RE::TESNPC*, RE::NiColorA&, RE::BGSCharacterTint::Entry*, bool, bool);
		using UpdateBodyTintColorsOnScene_t = void(RE::NiAVObject*, const RE::NiColorA&);

		REL::Relocation<float*> g_engineTime{ REL::ID{ 599343, 2712485 } };
		REL::Relocation<void (*)(RE::NiAVObject*)> g_createBoneMap{ REL::ID(1131947) };
		REL::Relocation<ActorCtor_t*> g_actorCtor{ REL::ID(1027500) };
		REL::Relocation<CreateHeadForNPC_t*> g_createHeadForNPC{ REL::ID(1455012) };
		REL::Relocation<AttachHeadHelper_t*> g_attachHeadHelper{ REL::ID(224610) };
		REL::Relocation<CalculateBodyTintColor_t*> g_calculateBodyTintColor{ REL::ID(134537) };
		REL::Relocation<UpdateBodyTintColorsOnScene_t*> g_updateBodyTintColorsOnScene{ REL::ID(49935) };

		class ScopedFormRegistrationSuppression
		{
		public:
			ScopedFormRegistrationSuppression()
			{
				if (auto* tls = RE::TLS::GetSingleton()) {
					flag_ = reinterpret_cast<std::uint8_t*>(tls) + 0x6FA;
					previous_ = *flag_;
					*flag_ = 1;
				}
			}

			~ScopedFormRegistrationSuppression()
			{
				if (flag_) {
					*flag_ = previous_;
				}
			}

			ScopedFormRegistrationSuppression(const ScopedFormRegistrationSuppression&) = delete;
			ScopedFormRegistrationSuppression(ScopedFormRegistrationSuppression&&) = delete;
			ScopedFormRegistrationSuppression& operator=(const ScopedFormRegistrationSuppression&) = delete;
			ScopedFormRegistrationSuppression& operator=(ScopedFormRegistrationSuppression&&) = delete;

		private:
			std::uint8_t* flag_{ nullptr };
			std::uint8_t previous_{ 0 };
		};

		struct PreviewAttachment
		{
			RE::BIPED_OBJECT slot{ RE::BIPED_OBJECT::kNone };
			RE::NiPointer<RE::NiAVObject> object;
			RE::NiNode* parent{ nullptr };
		};

		RE::NiPointer<RE::NiAVObject> g_previewRoot;
		RE::NiPointer<RE::BSFaceGenNiNode> g_previewFaceNode;
		RE::Actor* g_previewActor{ nullptr };
		std::vector<PreviewAttachment> g_previewAttachments;
		std::vector<RE::NiPointer<RE::NiAVObject>> g_retiredPreviewObjects;
		std::unordered_set<RE::Actor*> g_previewActors;
		std::uint64_t g_bipedSignature{ 0 };
		std::uint64_t g_failedPreviewSignature{ 0 };
		float g_updateAccumulator{ 0.0F };
		float g_diagnosticAccumulator{ 0.0F };
		float g_lastEngineTime{ 0.0F };
		std::uint32_t g_frameTicks{ 0 };
		bool g_hasLastEngineTime{ false };
		bool g_loggedSuppressedDoUpdate3D{ false };
		std::uint32_t g_previewBuildDepth{ 0 };
		std::string g_lastDiagnostic;

		struct ScopedPreviewBuild
		{
			ScopedPreviewBuild() { ++g_previewBuildDepth; }
			~ScopedPreviewBuild() { --g_previewBuildDepth; }
		};

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

			HashValue(hash, a_biped->GetRoot());
			return hash;
		}

		[[nodiscard]] bool IsTrackedPreviewActor(const RE::Actor* a_actor)
		{
			return a_actor && g_previewActors.contains(const_cast<RE::Actor*>(a_actor));
		}

		[[nodiscard]] RE::NiPoint3A GetHiddenPreviewLocation(const RE::PlayerCharacter& a_player)
		{
			RE::NiPoint3A location{ a_player.GetPosition().x, a_player.GetPosition().y, a_player.GetPosition().z };
			location.z -= 30000.0F;
			return location;
		}

		[[nodiscard]] RE::Actor* CreatePreviewActor(RE::PlayerCharacter& a_player)
		{
			const auto playerBase = a_player.GetObjectReference();
			if (!playerBase) {
				REX::WARN("TF3DHud V1 preview actor creation failed: player object reference is null");
				return nullptr;
			}

			if (g_previewActor) {
				g_previewActor->SetObjectReference(playerBase);
				g_previewActor->data.location = GetHiddenPreviewLocation(a_player);
				g_previewActor->data.angle = { 0.0F, 0.0F, 0.0F };
				g_previewActors.insert(g_previewActor);
				return g_previewActor;
			}

			auto& memoryManager = RE::MemoryManager::GetSingleton();
			const auto memory = memoryManager.Allocate(sizeof(RE::Actor), alignof(RE::Actor), true);
			if (!memory) {
				REX::WARN("TF3DHud V1 preview actor creation failed: MemoryManager allocation returned null");
				return nullptr;
			}

			ScopedFormRegistrationSuppression suppressFormRegistration;
			auto* createdActor = g_actorCtor(reinterpret_cast<RE::Actor*>(memory), false);
			if (!createdActor) {
				REX::WARN("TF3DHud V1 preview actor creation failed: Actor constructor returned null");
				return nullptr;
			}

			createdActor->SetObjectReference(playerBase);
			createdActor->data.location = GetHiddenPreviewLocation(a_player);
			createdActor->data.angle = { 0.0F, 0.0F, 0.0F };

			g_previewActor = createdActor;
			g_previewActors.insert(createdActor);
			g_loggedSuppressedDoUpdate3D = false;

			REX::INFO(
				"Created contained TF3DHud V1 preview actor from base {:08X}; actor={:X}, formID={:08X}",
				playerBase->GetFormID(),
				reinterpret_cast<std::uintptr_t>(createdActor),
				createdActor->GetFormID());
			return createdActor;
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

			auto* clone = a_source.CreateClone(cloneProcess);
			a_source.ProcessClone(cloneProcess);
			auto* clonedObject = clone ? static_cast<RE::NiAVObject*>(clone) : nullptr;
			return clonedObject;
		}


		void PrepareForInterface3DOffscreen(RE::NiAVObject& a_root)
		{
			std::uint32_t fadeNodes{ 0 };
			std::uint32_t clearedTopFadeNodes{ 0 };
			const auto beforeFlags = a_root.GetFlags();
			const auto beforeRadius = a_root.worldBound.fRadius;

			ForEachAVObject(std::addressof(a_root), [&](RE::NiAVObject& a_object) {
				if (!a_object.IsFadeNode()) {
					return;
				}

				++fadeNodes;
				if ((a_object.flags.flags & kNiAVObjectTopFadeNode) != 0) {
					a_object.flags.flags &= ~kNiAVObjectTopFadeNode;
					++clearedTopFadeNodes;
				}
			});

			if (clearedTopFadeNodes > 0) {
				RE::NiUpdateData updateData;
				a_root.Update(updateData);
			}

			REX::INFO(
				"TF3DHud V1 prepared preview root for Interface3D offscreen: fadeNodes={}, clearedTopFadeNodes={}, rootFlags {:016X}->{:016X}, boundRadius {}->{}",
				fadeNodes,
				clearedTopFadeNodes,
				beforeFlags,
				a_root.GetFlags(),
				beforeRadius,
				a_root.worldBound.fRadius);
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
			});

			RepairShaderFadeNodes(a_previewRoot, nullptr);

			REX::INFO(
				"TF3DHud V1 prepared preview tree: objects={}, clearedAppCulled={}",
				objects,
				appCulledObjects);
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

		void SanitizePreviewRenderTree(RE::NiAVObject& a_previewRoot, const char* a_stage)
		{
			std::uint32_t geometries = 0;
			std::uint32_t culledNonRenderable = 0;
			std::uint32_t culledNeckGore = 0;
			std::string firstNonRenderable;
			std::string firstNeckGore;

			ForEachGeometry(std::addressof(a_previewRoot), [&](RE::BSGeometry& a_geometry) {
				++geometries;

				const auto geometryName = std::string(a_geometry.GetName());
				const auto* shaderProperty = GetGeometryShaderProperty(a_geometry);
				const bool missingShaderPath = !shaderProperty || !shaderProperty->material;
				const bool neckGore = IsPreviewNeckGoreGeometry(geometryName);
				if (!missingShaderPath && !neckGore) {
					return;
				}

				a_geometry.SetAppCulled(true);
				a_geometry.fadeAmount = 0.0F;

				if (missingShaderPath) {
					++culledNonRenderable;
					if (firstNonRenderable.empty()) {
						firstNonRenderable = geometryName;
					}
				}
				if (neckGore) {
					++culledNeckGore;
					if (firstNeckGore.empty()) {
						firstNeckGore = geometryName;
					}
				}
			});

			if (culledNonRenderable == 0 && culledNeckGore == 0) {
				return;
			}

			RE::NiUpdateData updateData;
			a_previewRoot.Update(updateData);

			REX::INFO(
				"TF3DHud V1 sanitized preview render tree [{}]: geometries={}, culledNonRenderable={} first='{}', culledNeckGore={} first='{}', boundRadius={}",
				a_stage,
				geometries,
				culledNonRenderable,
				firstNonRenderable,
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
				LogDiagnostic(
					"preview render postponed: lighting shader material is null on " + firstMissingGeometry +
					" (" + std::to_string(missingMaterials) + "/" + std::to_string(lightingProperties) + ")");
				return false;
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

		[[nodiscard]] bool RebindSkinInstance(
			RE::BSSkin::Instance& a_skin,
			RE::NiAVObject& a_previewRoot,
			const std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes,
			std::uint32_t& a_reboundBones,
			std::uint32_t& a_missingBones)
		{
			if (a_skin.bones.empty()) {
				return true;
			}

			if (a_skin.bones.size() > RE::BSSkin::kMaxExpectedBones) {
				return false;
			}

			if (!a_skin.worldTransforms.empty() && a_skin.worldTransforms.size() != a_skin.bones.size()) {
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
			ForEachGeometry(std::addressof(a_previewRoot), [&](RE::BSGeometry& a_geometry) {
				auto* skin = a_geometry.skinInstance.get();
				if (!skin) {
					return;
				}
				++skinnedGeometries;
				if (!RebindSkinInstance(*skin, a_previewRoot, previewNodes, reboundBones, missingBones)) {
					++failedSkins;
				}
			});

			g_createBoneMap(std::addressof(a_previewRoot));

			REX::INFO(
				"TF3DHud V1 rebound cloned root skinning: skinned={}, reboundBones={}, missingBones={}, failedSkins={}",
				skinnedGeometries,
				reboundBones,
				missingBones,
				failedSkins);
			return failedSkins == 0;
		}

		[[nodiscard]] bool SyncEquipmentsFromBiped(RE::Actor&, RE::NiAVObject& a_previewRoot, const RE::BipedAnim& a_sourceBiped)
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
			std::uint32_t failedClones = 0;
			std::uint32_t failedSkins = 0;

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

				auto* parent = FindPreviewAttachParent(*sourceClone, a_previewRoot, previewNodes);
				if (!parent) {
					g_retiredPreviewObjects.emplace_back(std::move(previewClone));
					++failedClones;
					return;
				}

				RE::bhkWorld::RemoveObjects(previewClone.get(), true, true);
				PreparePreviewTree(*previewClone);

				ForEachGeometry(previewClone.get(), [&](RE::BSGeometry& a_geometry) {
					auto* skin = a_geometry.skinInstance.get();
					if (!skin) {
						return;
					}
					++skinnedGeometries;
					if (sourceSkins.contains(skin)) {
						++sharedSkinInstances;
						++failedSkins;
						return;
					}
					if (!RebindSkinInstance(*skin, a_previewRoot, previewNodes, reboundBones, missingBones)) {
						++failedSkins;
					}
				});

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
				const auto& sourceObject = a_sourceBiped.object[i];
				mirrorObject(slot, sourceObject.partClone.get());
			}

			// FaceGen uses BSDynamicTriShape render resources that are unsafe in the
			// current Interface3D clone path. V1 keeps face rendering disabled until
			// the face render-resource ownership path is IDA-verified.

			g_createBoneMap(std::addressof(a_previewRoot));

			RE::NiUpdateData updateData;
			a_previewRoot.Update(updateData);

			REX::INFO(
				"TF3DHud V1 mirrored biped/face geometry: cloned={}, attached={}, skinned={}, seededSkinMappings={}, sharedSkinInstances={}, reboundBones={}, missingBones={}, failedClones={}, failedSkins={}",
				clonedObjects,
				attachedObjects,
				skinnedGeometries,
				seededSkinMappings,
				sharedSkinInstances,
				reboundBones,
				missingBones,
				failedClones,
				failedSkins);
			return attachedObjects > 0;
		}

		bool EnsurePreviewHead(RE::PlayerCharacter& a_player, RE::Actor& a_previewActor, RE::NiAVObject& a_previewRoot)
		{
			if (a_previewRoot.GetObjectByName(RE::BSFixedString("BSFaceGenNiNodeSkinned"))) {
				return true;
			}

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
			{
				ScopedPreviewBuild buildGuard;
				g_createHeadForNPC(npc, faceNode, true, true, nullptr);
			}

			if (!faceNode) {
				LogDiagnostic("preview head creation failed: CreateHeadForNPC returned null");
				return false;
			}

			g_attachHeadHelper(rootFaceNPC, faceNode.get(), std::addressof(a_previewActor), nullptr);
			RE::bhkWorld::RemoveObjects(faceNode.get(), true, true);
			PreparePreviewTree(*faceNode);

			g_previewFaceNode = faceNode;
			g_createBoneMap(std::addressof(a_previewRoot));

			REX::INFO(
				"TF3DHud V1 created preview facegen head from player NPC {:08X}; root face NPC {:08X}",
				npc->GetFormID(),
				rootFaceNPC->GetFormID());
			return true;
		}

		void ApplyPreviewBodyTint(RE::TESNPC& a_npc, RE::NiAVObject& a_previewRoot)
		{
			RE::NiColorA bodyTint{ 0.0F, 0.0F, 0.0F, 0.0F };
			g_calculateBodyTintColor(std::addressof(a_npc), bodyTint, nullptr, true, false);
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
				"TF3DHud V1 applied preview body tint rgb=({}, {}, {}), calculatedAlpha={}, appliedAlpha=1 from NPC {:08X}; geometries={}, lightingProperties={}, skinTintMaterials={} firstSkinTint='{}', alphaZeroShaders={} firstAlphaZero='{}'",
				bodyTint.r,
				bodyTint.g,
				bodyTint.b,
				calculatedAlpha,
				a_npc.GetFormID(),
				geometries,
				lightingProperties,
				skinTintMaterials,
				firstSkinTintGeometry,
				alphaZeroShaders,
				firstAlphaZeroGeometry);
		}

		bool RebuildPreview(RE::PlayerCharacter& a_player, const RE::BipedAnim&, std::uint64_t a_signature)
		{
			Renderer::ClearPreviewRoot();

			auto* previewActor = CreatePreviewActor(a_player);
			if (!previewActor) {
				g_previewRoot.reset();
				g_bipedSignature = 0;
				return false;
			}

			previewActor->data.location = GetHiddenPreviewLocation(a_player);

			RE::NiPointer<RE::NiAVObject> previewRoot{ previewActor->Get3D(false) };
			if (!previewRoot) {
				ScopedPreviewBuild buildGuard;
				previewRoot = previewActor->Load3D(false);
			}
			if (!previewRoot) {
 				g_previewRoot.reset();
 				g_bipedSignature = 0;
 				LogDiagnostic("contained preview actor Load3D returned null");
 				return false;
			}

			RE::bhkWorld::RemoveObjects(previewRoot.get(), true, true);
			PreparePreviewTree(*previewRoot);
			EnsurePreviewHead(a_player, *previewActor, *previewRoot);
			if (auto* playerBase = a_player.GetObjectReference(); playerBase) {
				if (auto* npc = playerBase->As<RE::TESNPC>(); npc) {
					ApplyPreviewBodyTint(*npc, *previewRoot);
				}
			}

			SanitizePreviewRenderTree(*previewRoot, "before-framing");
			Renderer::ApplyOffscreenFraming(*previewRoot, true);
			PrepareForInterface3DOffscreen(*previewRoot);
			if (!ValidatePreviewRenderMaterials(*previewRoot)) {
				g_previewRoot.reset();
				g_bipedSignature = 0;
				return false;
			}

			g_previewRoot = previewRoot;
			g_bipedSignature = a_signature;
			g_failedPreviewSignature = 0;

			Renderer::AttachPreviewRoot(*g_previewRoot);

			REX::INFO(
				"Rebuilt TF3DHud V1 preview from contained fake actor root; actor={:X}",
				reinterpret_cast<std::uintptr_t>(previewActor));
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

			const auto& config = GetConfig();
			if (config.updateHz > 0.0F) {
				if (a_engineDeltaTime > 0.0F) {
					g_updateAccumulator += a_engineDeltaTime;
				}
				const auto step = 1.0F / config.updateHz;
				if (g_updateAccumulator < step) {
					return;
				}
				g_updateAccumulator = std::min(g_updateAccumulator - step, step);
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
			const auto signature = BuildBipedSignature(biped.get());
			if (!g_previewRoot || signature == 0 || signature != g_bipedSignature) {
				if (signature == 0) {
					LogDiagnostic(biped ? "third-person biped has empty signature" : "third-person biped is null");
				}
				if (!biped || !RebuildPreview(*player, *biped, signature)) {
					Renderer::Hide();
					return;
				}
			} else {
				Renderer::ApplyOffscreenFraming(*g_previewRoot, false);
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
			Renderer::Hide();
			Renderer::Reset();
			RetirePreviewAttachments(false);
			g_previewRoot.reset();
			g_previewFaceNode.reset();
			g_bipedSignature = 0;
			g_failedPreviewSignature = 0;
			g_updateAccumulator = 0.0F;
			REX::INFO(
				"Reset TF3DHud V1 transient preview state; retained preview actor={:X}, trackedPreviewActors={}",
				reinterpret_cast<std::uintptr_t>(g_previewActor),
				g_previewActors.size());
		}

		bool IsPreviewActor(const RE::Actor* a_actor)
		{
			return IsTrackedPreviewActor(a_actor);
		}

		bool IsBuildActive()
		{
			return g_previewBuildDepth != 0;
		}
	}

}
