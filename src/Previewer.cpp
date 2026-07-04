#include "Previewer.h"
#include "Address.h"
#include "Animations.h"
#include "Morph.h"
#include "PreviewHeadParts.h"
#include "PreviewRebuilder.h"
#include "PreviewRenderTree.h"
#include "PreviewVisibility.h"
#include "Renderer.h"
#include "Utils.h"

#include "Config.h"
#include "RE/BSFaceGenAnimationData.h"
#include "RE/BSSkin.h"

#include "PreviewCloth.h"
#include "PreviewClone.h"
#include "PreviewFaceBoneDiagnostics.h"
#include "PreviewFraming.h"
#include "RE/B/BSFlattenedBoneTree.h"
#include "RE/B/BSGeometry.h"
#include "RE/B/BGSMod.h"
#include "RE/B/BGSObjectInstance.h"
#include "RE/B/BGSObjectInstanceExtra.h"
#include "RE/B/BSModelDB.h"
#include "RE/B/BipedAnim.h"
#include "RE/M/MemoryManager.h"
#include "RE/N/NiExtraData.h"
#include "RE/N/NiNode.h"
#include "RE/N/NiStringExtraData.h"
#include "RE/N/NiUpdateData.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESNPC.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESModel.h"
#include "RE/T/TESObjectARMA.h"
#include "RE/T/TESObjectARMO.h"
#include "RE/T/TESRace.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace TF3DHud
{
	namespace
	{
		using BorrowedBipedPointer = Address::BorrowedBipedPointer;
		using SkinComplexionContext = Address::SkinComplexionContext;

		auto& g_createBoneMap = Address::CreateBoneMap;
		auto& g_createHeadForNPC = Address::CreateHeadForNPC;
		auto& g_calculateBodyTintColor = Address::CalculateBodyTintColor;
		auto& g_updateBodyTintColorsOnScene = Address::UpdateBodyTintColorsOnScene;
		auto& g_doAdjustSkinComplexion = Address::DoAdjustSkinComplexion;
		auto& g_convertNodeTree = Address::ConvertNodeTree;
		auto& g_getActorBodyPart3D = Address::GetActorBodyPart3D;
		auto& g_fixFaceGenHeadSkinInstances = Address::FixFaceGenHeadSkinInstances;
		auto& g_resetFaceGenCurrentMorphs = Address::ResetFaceGenCurrentMorphs;
		auto& g_generateFlattenedHeadPartArray = Address::GenerateFlattenedHeadPartArray;
		auto& g_applyAllCustomizationMorphs = Address::ApplyAllCustomizationMorphs;
		auto& g_applyWeightFaceMorph = Address::ApplyWeightFaceMorph;
		auto& g_prepareHeadForShaders = Address::PrepareHeadForShaders;
		auto& g_scaleFaceSkinBones = Address::ScaleFaceSkinBones;
		auto& g_updateAllChildrenMorphData = Address::UpdateAllChildrenMorphData;
		auto& g_tryAttachMod3DRecurse = Address::TryAttachMod3DRecurse;
		auto& g_bipedAnimCtor = Address::BipedAnimCtor;
		auto& g_bipedAnimDtor = Address::BipedAnimDtor;
		auto& g_getSkin = Address::GetSkin;
		auto& g_addArmorToBiped = Address::AddArmorToBiped;
		auto& g_initWornObject = Address::InitWornObject;
		auto& g_getChargenModelName = Address::GetChargenModelName;

		using PreviewRenderTree::PrepareForInterface3DOffscreen;
		using PreviewRenderTree::PreparePreviewTree;
		using PreviewRenderTree::RestorePreviewShaderAlpha;
		using PreviewRenderTree::SanitizePreviewRenderTree;
		using PreviewRenderTree::StripControllerChains;
		using PreviewFaceBoneDiagnostics::FaceBoneAttachStats;
		using PreviewFaceBoneDiagnostics::FaceBoneCandidateDetail;

		struct PreviewAttachment
		{
			RE::BIPED_OBJECT slot{ RE::BIPED_OBJECT::kNone };
			RE::NiPointer<RE::NiAVObject> object;
			RE::NiNode* parent{ nullptr };
			bool preserveOnEquipmentSync{ false };
		};

		enum class EquipmentSyncResult
		{
			kNoChange,
			kSynced,
			kSyncedWithMergedBones,
			kNeedsSkeletonTopologyRebuild,
			kFailedTransient
		};

		[[nodiscard]] bool EquipmentSyncSucceeded(const EquipmentSyncResult a_result)
		{
			return a_result == EquipmentSyncResult::kNoChange ||
			       a_result == EquipmentSyncResult::kSynced ||
			       a_result == EquipmentSyncResult::kSyncedWithMergedBones;
		}

		RE::NiPointer<RE::NiAVObject> g_previewRoot;
		RE::BSFlattenedBoneTree* g_previewFlattenedBoneTree{ nullptr };
		RE::NiPointer<RE::BSFaceGenNiNode> g_previewFaceNode;
		std::vector<Previewer::FaceGenSliderDebugInfo> g_lastAppliedFaceGenEntries;
		std::vector<PreviewAttachment> g_previewAttachments;
		std::vector<RE::NiPointer<RE::NiAVObject>> g_retiredPreviewObjects;
		std::vector<RE::NiPointer<RE::NiAVObject>> g_retiredPreviewObjectsPendingRelease;
		constexpr std::uint32_t kPostRebuildAdjustmentHoldFrames = 6;
		PreviewRebuilder::Controller g_rebuilder;
		bool g_pendingRendererAttach{ false };
		bool g_pendingFramingUpdate{ false };
		bool g_renderStateDirty{ false };
		bool g_committingRenderState{ false };
		std::uint32_t g_postRebuildAdjustmentHoldFrames{ 0 };
		float g_pendingRenderDelta{ 0.0F };
		bool g_looksMenuSuspended{ false };
		std::string g_lastDiagnostic;
		std::recursive_mutex g_stateLock;

		void RefreshPreviewBoneLookup(RE::NiAVObject& a_previewRoot);
		void CollectPreviewSkinTargetNodes(
			RE::NiAVObject& a_previewRoot,
			std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes);

		[[nodiscard]] std::string FormatFloat(const float a_value)
		{
			char buffer[32]{};
			std::snprintf(buffer, sizeof(buffer), "%.4f", a_value);
			return buffer;
		}

		[[nodiscard]] std::string FormatTransform(const RE::BGSCharacterMorph::Transform& a_transform)
		{
			char buffer[192]{};
			std::snprintf(
				buffer,
				sizeof(buffer),
				"p(%.3f %.3f %.3f) r(%.3f %.3f %.3f) s(%.3f %.3f %.3f)",
				a_transform.position.x,
				a_transform.position.y,
				a_transform.position.z,
				a_transform.rotation.x,
				a_transform.rotation.y,
				a_transform.rotation.z,
				a_transform.scale.x,
				a_transform.scale.y,
				a_transform.scale.z);
			return buffer;
		}

		[[nodiscard]] const char* BodyMorphRegionName(const std::uint32_t a_index)
		{
			switch (static_cast<RE::BGSCharacterMorph::BODY_MORPH_REGION>(a_index)) {
			case RE::BGSCharacterMorph::BODY_MORPH_REGION::kHead:
				return "morphRegion/head";
			case RE::BGSCharacterMorph::BODY_MORPH_REGION::kUpperTorso:
				return "morphRegion/upperTorso";
			case RE::BGSCharacterMorph::BODY_MORPH_REGION::kArms:
				return "morphRegion/arms";
			case RE::BGSCharacterMorph::BODY_MORPH_REGION::kLowerTorso:
				return "morphRegion/lowerTorso";
			case RE::BGSCharacterMorph::BODY_MORPH_REGION::kLegs:
				return "morphRegion/legs";
			default:
				return "morphRegion/unknown";
			}
		}

		void AddFaceGenDebugValue(
			std::vector<Previewer::FaceGenSliderDebugInfo>& a_entries,
			std::string a_category,
			const std::uint32_t a_id,
			std::string a_value)
		{
			a_entries.push_back({
				.category = std::move(a_category),
				.id = a_id,
				.liveValue = std::move(a_value),
				.hasLive = true,
			});
		}

		[[nodiscard]] std::string ResolveMorphSliderCategory(
			const RE::TESRace* a_race,
			const RE::SEX a_sex,
			const std::uint32_t a_sliderId)
		{
			const auto sex = std::to_underlying(a_sex);
			const auto* faceData = a_race && sex < 2 ? a_race->faceRelatedData[sex] : nullptr;
			const auto* groups = faceData ? faceData->morphGroups : nullptr;
			if (groups) {
				for (const auto* group : *groups) {
					if (!group) {
						continue;
					}
					for (const auto sliderId : group->sliders) {
						if (sliderId == a_sliderId) {
							return group->name.empty() ? "morphSlider/group" : group->name.c_str();
						}
					}
				}
			}

			if (a_race) {
				if (const auto slider = a_race->morphSliders.find(a_sliderId); slider != a_race->morphSliders.end()) {
					const auto* sliderData = slider->second;
					if (sliderData && !sliderData->morphNames[0].empty()) {
						return std::string("morphSlider/") + sliderData->morphNames[0].c_str();
					}
				}
			}

			return "morphSlider";
		}

		[[nodiscard]] std::string ResolveFacialBoneRegionCategory(
			const RE::TESRace* a_race,
			const RE::SEX a_sex,
			const std::uint32_t a_regionId)
		{
			const auto sex = std::to_underlying(a_sex);
			const auto* faceData = a_race && sex < 2 ? a_race->faceRelatedData[sex] : nullptr;
			const auto* regions = faceData ? faceData->facialBoneRegions : nullptr;
			if (regions) {
				for (const auto* region : *regions) {
					if (region && region->id == a_regionId) {
						return region->name.empty() ? "facialBoneRegion" : region->name.c_str();
					}
				}
			}

			return "facialBoneRegion";
		}

		void CaptureFaceGenEntriesFromNPC(
			RE::PlayerCharacter& a_player,
			RE::TESNPC& a_npc,
			std::vector<Previewer::FaceGenSliderDebugInfo>& a_entries)
		{
			auto* race = a_player.charGenRace ? a_player.charGenRace : a_player.GetVisualsRace();
			const auto sex = a_npc.GetSex();

			if (a_npc.morphRegionSliderValues) {
				for (std::uint32_t index = 0; index < a_npc.morphRegionSliderValues->size(); ++index) {
					AddFaceGenDebugValue(
						a_entries,
						BodyMorphRegionName(index),
						index,
						FormatFloat((*a_npc.morphRegionSliderValues)[index]));
				}
			}

			if (a_npc.morphSliderValues) {
				for (const auto& entry : *a_npc.morphSliderValues) {
					AddFaceGenDebugValue(
						a_entries,
						ResolveMorphSliderCategory(race, sex, entry.first),
						entry.first,
						FormatFloat(entry.second));
				}
			}

			if (a_npc.facialBoneRegionSliderValues) {
				for (const auto& entry : *a_npc.facialBoneRegionSliderValues) {
					AddFaceGenDebugValue(
						a_entries,
						ResolveFacialBoneRegionCategory(race, sex, entry.first),
						entry.first,
						FormatTransform(entry.second));
				}
			}

			if (a_npc.tintingData) {
				for (auto* entry : a_npc.tintingData->entriesA) {
					if (!entry) {
						continue;
					}

					std::string value = std::string("type=") + std::to_string(std::to_underlying(entry->GetType())) +
					                    " amount=" + std::to_string(entry->tingingValue);
					if (entry->GetType() == RE::BGSCharacterTint::EntryType::kPalette) {
						const auto* palette = static_cast<const RE::BGSCharacterTint::PaletteEntry*>(entry);
						char color[64]{};
						std::snprintf(color, sizeof(color), " color=%08X swatch=%u", palette->tintingColor, palette->swatchID);
						value += color;
					}

					AddFaceGenDebugValue(a_entries, "tint", entry->idLink, std::move(value));
				}
			}
		}

		[[nodiscard]] std::vector<Previewer::FaceGenSliderDebugInfo> CaptureLiveFaceGenEntries(RE::PlayerCharacter& a_player)
		{
			std::vector<Previewer::FaceGenSliderDebugInfo> entries;
			auto* playerBase = a_player.GetObjectReference();
			auto* npc = playerBase ? playerBase->As<RE::TESNPC>() : nullptr;
			if (!npc) {
				return entries;
			}

			CaptureFaceGenEntriesFromNPC(a_player, *npc, entries);
			std::ranges::sort(entries, [](const auto& a_lhs, const auto& a_rhs) {
				if (a_lhs.category != a_rhs.category) {
					return a_lhs.category < a_rhs.category;
				}
				return a_lhs.id < a_rhs.id;
			});
			return entries;
		}

		[[nodiscard]] Previewer::FaceGenDebugSnapshot BuildFaceGenDebugSnapshot(RE::PlayerCharacter& a_player)
		{
			Previewer::FaceGenDebugSnapshot snapshot;
			snapshot.sliders = CaptureLiveFaceGenEntries(a_player);

			for (auto& liveEntry : snapshot.sliders) {
				const auto previewIt = std::ranges::find_if(g_lastAppliedFaceGenEntries, [&](const auto& a_previewEntry) {
					return a_previewEntry.category == liveEntry.category && a_previewEntry.id == liveEntry.id;
				});
				if (previewIt != g_lastAppliedFaceGenEntries.end()) {
					liveEntry.previewValue = previewIt->liveValue;
					liveEntry.hasPreview = true;
				}
			}

			for (const auto& previewEntry : g_lastAppliedFaceGenEntries) {
				const auto liveIt = std::ranges::find_if(snapshot.sliders, [&](const auto& a_liveEntry) {
					return a_liveEntry.category == previewEntry.category && a_liveEntry.id == previewEntry.id;
				});
				if (liveIt != snapshot.sliders.end()) {
					continue;
				}

				snapshot.sliders.push_back({
					.category = previewEntry.category,
					.id = previewEntry.id,
					.previewValue = previewEntry.liveValue,
					.hasPreview = true,
				});
			}

			return snapshot;
		}

		[[nodiscard]] RE::BSFaceGenAnimationData* GetLiveFaceAnimationData(const RE::PlayerCharacter& a_player)
		{
			auto* process = a_player.currentProcess;
			auto* middleHigh = process ? process->middleHigh : nullptr;
			return middleHigh ? middleHigh->faceAnimationData : nullptr;
		}

		void SyncPreviewFacialExpression(const RE::PlayerCharacter& a_player)
		{
			if (!g_previewFaceNode || !g_previewFaceNode->animationData) {
				return;
			}

			auto* liveData = GetLiveFaceAnimationData(a_player);
			auto* previewData = g_previewFaceNode->animationData;
			if (!liveData || liveData == previewData) {
				return;
			}

			// MiddleHigh owns emotional-idle timers and handles. Mirror only the
			// expression payload consumed by the preview head.
			previewData->currentExpression = liveData->currentExpression;
			previewData->modifierExpression = liveData->modifierExpression;
			previewData->baseExpression = liveData->baseExpression;
			previewData->morphsDirty = true;
			previewData->forceMorphUpdate = true;
			g_previewFaceNode->faceGenFlags &= static_cast<std::uint16_t>(~0x4u);
		}

		void LogDiagnostic(std::string a_message)
		{
			if (a_message == g_lastDiagnostic) {
				return;
			}

			g_lastDiagnostic = std::move(a_message);
			REX::INFO("state: {}", g_lastDiagnostic);
		}

		void HideRendererAndResetAnimation()
		{
			Renderer::Hide();
			Animations::Reset();
		}

		void MarkRenderStateDirty(const float a_deltaTime = 0.0F)
		{
			g_renderStateDirty = true;
			g_pendingRenderDelta += a_deltaTime;
		}

		void RetirePreviewObject(RE::NiPointer<RE::NiAVObject>&& a_object)
		{
			if (a_object) {
				g_retiredPreviewObjects.emplace_back(std::move(a_object));
			}
		}

		void StripClonedGeometry(RE::NiAVObject& a_previewRoot)
		{
			for (auto& object : PreviewRenderTree::StripClonedGeometry(a_previewRoot)) {
				RetirePreviewObject(std::move(object));
			}
		}

		void AdvanceRetiredPreviewObjects()
		{
			// Keep retired roots across a render boundary; then release them so
			// rapid equipment rebuilds cannot accumulate old cloth/scene trees.
			g_retiredPreviewObjectsPendingRelease.clear();
			if (g_retiredPreviewObjects.empty()) {
				return;
			}

			g_retiredPreviewObjectsPendingRelease.reserve(g_retiredPreviewObjects.size());
			for (auto& object : g_retiredPreviewObjects) {
				g_retiredPreviewObjectsPendingRelease.emplace_back(std::move(object));
			}
			g_retiredPreviewObjects.clear();
		}

		[[nodiscard]] RE::TESRace* ResolvePreviewRace(RE::PlayerCharacter& a_player, RE::TESNPC& a_npc);

		[[nodiscard]] bool IsEngineWeaponAttachSlot(const RE::BIPED_OBJECT a_slot)
		{
			const auto slot = std::to_underlying(a_slot);
			return slot >= 32 && (slot <= 39 || (slot >= 41 && slot <= 43));
		}

		[[nodiscard]] RE::NiStringExtraData* FindStringExtraDataInTree(
			RE::NiAVObject& a_root,
			const RE::BSFixedString& a_name)
		{
			RE::NiStringExtraData* found = nullptr;
			ForEachAVObject(std::addressof(a_root), [&](RE::NiAVObject& a_object) {
				if (found) {
					return;
				}
				found = netimmerse_cast<RE::NiStringExtraData*>(a_object.GetExtraData(a_name));
			});
			return found;
		}

		[[nodiscard]] RE::NiNode* FindPreviewNodeByName(
			RE::NiAVObject& a_previewRoot,
			const std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes,
			const std::string_view a_name)
		{
			auto* previewObject = FindNodeByName(a_previewNodes, a_name);
			if (!previewObject) {
				previewObject = a_previewRoot.GetObjectByName(RE::BSFixedString(a_name.data()));
			}

			return previewObject ? previewObject->IsNode() : nullptr;
		}

		[[nodiscard]] RE::NiNode* ResolveFixedPreviewAttachParent(
			const RE::BIPED_OBJECT a_slot,
			const RE::BIPOBJECT& a_sourceObject,
			RE::NiAVObject& a_previewRoot,
			const std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			const auto* sourceForm = a_sourceObject.parent.object;
			if (a_slot == RE::BIPED_OBJECT::kWeaponHand &&
				sourceForm &&
				sourceForm->Is(RE::ENUM_FORM_ID::kLIGH)) {
				return FindPreviewNodeByName(a_previewRoot, a_previewNodes, "Weapon");
			}

			if (a_slot == RE::BIPED_OBJECT::kWeaponStaff) {
				return FindPreviewNodeByName(a_previewRoot, a_previewNodes, "Weapon");
			}

			if (a_slot == RE::BIPED_OBJECT::kShield &&
				sourceForm &&
				sourceForm->Is(RE::ENUM_FORM_ID::kWEAP)) {
				return FindPreviewNodeByName(a_previewRoot, a_previewNodes, "WeaponLeft");
			}

			return nullptr;
		}

		[[nodiscard]] RE::NiNode* ResolveWeaponBonePreviewAttachParent(
			const RE::BIPED_OBJECT a_slot,
			const RE::BIPOBJECT& a_sourceObject,
			RE::NiAVObject& a_previewRoot,
			const std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			const auto* sourceForm = a_sourceObject.parent.object;
			if (!IsEngineWeaponAttachSlot(a_slot) || !sourceForm || !sourceForm->Is(RE::ENUM_FORM_ID::kWEAP)) {
				return nullptr;
			}

			// IDA OG 1.10.163: when no fixed parent is found and no Prn is
			// present, AttachToParent falls back to WeaponUtils::GetWeaponBoneName.
			// The FO4 right-hand weapon bone is named "Weapon"; shield remains
			// handled by the fixed WeaponLeft path above.
			return FindPreviewNodeByName(a_previewRoot, a_previewNodes, "Weapon");
		}

		RE::NiNode* FindPreviewAttachParent(
			const RE::BIPED_OBJECT a_slot,
			const RE::BIPOBJECT& a_sourceBipedObject,
			RE::NiAVObject& a_sourceClone,
			RE::NiAVObject& a_previewRoot,
			const std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			// IDA OG 1.10.163: BipedAnim::AttachToParent resolves fixed
			// Weapon/WeaponLeft parents before falling back to NiStringExtraData
			// "Prn". Keep the live-parent chain as the final copied-biped fallback.
			if (auto* fixedParent = ResolveFixedPreviewAttachParent(a_slot, a_sourceBipedObject, a_previewRoot, a_previewNodes)) {
				return fixedParent;
			}

			auto* prn = FindStringExtraDataInTree(a_sourceClone, RE::BSFixedString("Prn"));
			if (prn && !prn->GetValue().empty()) {
				if (auto* previewNode = FindPreviewNodeByName(a_previewRoot, a_previewNodes, prn->GetValue())) {
					return previewNode;
				}
			}

			if (auto* weaponBoneParent = ResolveWeaponBonePreviewAttachParent(a_slot, a_sourceBipedObject, a_previewRoot, a_previewNodes)) {
				return weaponBoneParent;
			}

			for (auto* sourceParent = a_sourceClone.parent; sourceParent; sourceParent = sourceParent->parent) {
				auto* previewObject = FindNodeByName(a_previewNodes, sourceParent->GetName());
				auto* previewNode = previewObject ? previewObject->IsNode() : nullptr;
				if (previewNode) {
					return previewNode;
				}
			}

			const auto* sourceForm = a_sourceBipedObject.parent.object;
			REX::WARN(
				"Preview attach parent fallback failed: slot={}, sourceForm='{}' ({:08X}), sourceClone='{}', sourceClonePtr={:X}; using preview root",
				std::to_underlying(a_slot),
				sourceForm ? sourceForm->GetFormTypeString() : "<null>",
				sourceForm ? sourceForm->GetFormID() : 0,
				a_sourceClone.GetName(),
				reinterpret_cast<std::uintptr_t>(std::addressof(a_sourceClone)));
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

		void RefreshPreviewHeadPartVisibility(RE::PlayerCharacter& a_player, const RE::BipedAnim& a_sourceBiped)
		{
			if (!g_previewFaceNode) {
				return;
			}

			auto* playerBase = a_player.GetObjectReference();
			auto* npc = playerBase ? playerBase->As<RE::TESNPC>() : nullptr;
			if (!npc) {
				return;
			}

			auto* previewRace = ResolvePreviewRace(a_player, *npc);
			if (!previewRace) {
				return;
			}

			PreviewHeadParts::ApplyBipedVisibility(*npc, *previewRace, *g_previewFaceNode, a_player, a_sourceBiped);
		}

		void CollectSourceBipedSkinInstances(
			const RE::BipedAnim& a_biped,
			std::unordered_set<RE::BSSkin::Instance*>& a_skins)
		{
			for (std::int32_t i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
				CollectSkinInstances(a_biped.object[i].partClone.get(), a_skins);
			}
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

		struct FaceBoneModelPath
		{
			std::string source;
			std::string path;
		};

		void AddFaceBoneModelPath(std::vector<FaceBoneModelPath>& a_paths, std::string a_source, const char* a_path)
		{
			if (!a_path || a_path[0] == '\0') {
				return;
			}

			for (const auto& path : a_paths) {
				if (path.path == a_path) {
					return;
				}
			}

			a_paths.push_back({
				.source = std::move(a_source),
				.path = a_path,
			});
		}

		[[nodiscard]] std::vector<FaceBoneModelPath> GetRaceSkeletonFaceBoneModelPaths(
			RE::TESRace& a_race,
			const std::uint32_t a_sex)
		{
			std::vector<FaceBoneModelPath> paths;
			const auto sex = std::min<std::uint32_t>(a_sex, 1);
			AddFaceBoneModelPath(paths, "race skeletonChargenModel[sex]", a_race.skeletonChargenModel[sex].GetModel());
			AddFaceBoneModelPath(paths, "race skeletonChargenModel[0]", a_race.skeletonChargenModel[0].GetModel());
			AddFaceBoneModelPath(paths, "race skeletonChargenModel[1]", a_race.skeletonChargenModel[1].GetModel());

			if (const auto* skeletonPath = GetRaceSkeletonModelPath(a_race, sex)) {
				std::array<char, 260> chargenPath{};
				g_getChargenModelName(chargenPath.data(), static_cast<std::uint32_t>(chargenPath.size()), skeletonPath, false);
				AddFaceBoneModelPath(paths, "race skeleton derived facebones", chargenPath.data());

				if (sex == std::to_underlying(RE::SEX::kFemale)) {
					chargenPath.fill('\0');
					g_getChargenModelName(chargenPath.data(), static_cast<std::uint32_t>(chargenPath.size()), skeletonPath, true);
					AddFaceBoneModelPath(paths, "race skeleton derived female facebones", chargenPath.data());
				}
			}

			return paths;
		}

		[[nodiscard]] bool ConvertPreviewSkeletonToFlattenedTree(RE::PlayerCharacter& a_player, RE::NiAVObject& a_skeletonRoot)
		{
			constexpr std::uint32_t kFlattenedSkeletonBodyPart = 0x12;
			auto* convertTarget = g_getActorBodyPart3D(
				std::addressof(a_player),
				std::addressof(a_skeletonRoot),
				std::addressof(kFlattenedSkeletonBodyPart),
				false,
				false);
			if (!convertTarget) {
				REX::WARN(
					"preview race skeleton flatten failed: body-part lookup returned null; root={:X}, rootName='{}', bodyPart={}",
					reinterpret_cast<std::uintptr_t>(std::addressof(a_skeletonRoot)),
					a_skeletonRoot.GetName(),
					kFlattenedSkeletonBodyPart);
				return false;
			}

			if (convertTarget != std::addressof(a_skeletonRoot)) {
				g_convertNodeTree(convertTarget);
			}

			g_createBoneMap(std::addressof(a_skeletonRoot));

			auto* afterFlattened = FindFlattenedBoneTree(std::addressof(a_skeletonRoot));
			if (!afterFlattened) {
				REX::WARN(
					"preview race skeleton flatten did not produce BSFlattenedBoneTree: target={:X}, root={:X}, targetName='{}', rootName='{}'",
					reinterpret_cast<std::uintptr_t>(convertTarget),
					reinterpret_cast<std::uintptr_t>(std::addressof(a_skeletonRoot)),
					convertTarget->GetName(),
					a_skeletonRoot.GetName());
				return false;
			}

			g_createBoneMap(afterFlattened);
			g_previewFlattenedBoneTree = afterFlattened;

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
					"preview race skeleton load failed: path='{}', result={}",
					skeletonPath,
					std::to_underlying(result));
				LogDiagnostic("preview race skeleton load failed: BSModelDB demand failed");
				return nullptr;
			}

			auto previewRoot = PreviewClone::CloneObject(*loadedRoot);
			if (!previewRoot) {
				REX::WARN("preview race skeleton clone failed: path='{}'", skeletonPath);
				LogDiagnostic("preview race skeleton clone failed");
				return nullptr;
			}

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
					"preview model load failed: path='{}', result={}",
					a_path,
					std::to_underlying(result));
				return nullptr;
			}

			auto previewRoot = PreviewClone::CloneObject(*loadedRoot);
			if (!previewRoot) {
				REX::WARN("preview model clone failed: path='{}'", a_path);
				return nullptr;
			}

			return previewRoot;
		}


		void PrepareAttachmentSkinComplexion(
			RE::NiAVObject& a_attachmentRoot,
			const RE::BIPED_OBJECT a_slot,
			const RE::BipedAnim& a_sourceBiped)
		{
			if (a_slot == RE::BIPED_OBJECT::kNone) {
				return;
			}

			SkinComplexionContext context{
				.slot = a_slot,
				.objects = const_cast<RE::BIPOBJECT*>(a_sourceBiped.object),
				.actorRef = a_sourceBiped.GetRequester(),
			};

			ForEachGeometry(std::addressof(a_attachmentRoot), [&](RE::BSGeometry& a_geometry) {
				g_doAdjustSkinComplexion(std::addressof(context), std::addressof(a_geometry));
			});
		}

		struct Mod3DReplayResult
		{
			std::uint32_t attached{ 0 };
		};

		[[nodiscard]] Mod3DReplayResult ReplayObjectInstanceMod3D(
			const RE::BIPOBJECT& a_sourceObject,
			RE::NiAVObject& a_previewAttachment)
		{
			Mod3DReplayResult result;
			auto* modExtra = a_sourceObject.modExtra;
			if (!modExtra || !modExtra->values) {
				return result;
			}

			auto* targetNode = a_previewAttachment.IsNode();
			if (!targetNode) {
				return result;
			}

			const auto indexData = modExtra->GetIndexData();
			if (indexData.empty()) {
				return result;
			}

			auto* instanceData = a_sourceObject.parent.instanceData.get();
			for (const auto& entry : indexData) {
				if (entry.disabled) {
					continue;
				}

				auto* mod = RE::TESForm::GetFormByID<RE::BGSMod::Attachment::Mod>(entry.objectID);
				if (!mod) {
					continue;
				}

				// Ghidra OG 1.10.163: PowerArmor::SyncFurnitureVisualsToInventory
				// replays object-instance OMOD visuals through
				// BGSMod::Attachment::Mod::TryAttach3DRecurse, which loads the
				// mod model and connects it by BSConnectPoint.
				if (g_tryAttachMod3DRecurse(mod, targetNode, nullptr, instanceData)) {
					++result.attached;
				}
			}

			return result;
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

		[[nodiscard]] RE::TESRace* ResolvePreviewRace(RE::PlayerCharacter& a_player, RE::TESNPC& a_npc)
		{
			if (auto* race = a_player.GetVisualsRace()) {
				return race;
			}
			if (auto* race = a_npc.GetFormRace()) {
				return race;
			}
			return a_npc.originalRace;
		}

		class PreviewBipedData
		{
		public:
			PreviewBipedData(RE::TESObjectREFR& a_reference, RE::NiNode& a_actorRoot)
			{
				biped_ = RE::malloc<RE::BipedAnim>();
				if (!biped_) {
					return;
				}

				g_bipedAnimCtor(biped_, std::addressof(a_reference), false);
				// This is a borrowed native smart-pointer payload for population
				// helpers only. Keep a non-zero refcount so any transient native
				// smart-pointer copies cannot delete this preview-owned object.
				biped_->refCount = 1;
				biped_->root = std::addressof(a_actorRoot);
				borrowed_.biped = biped_;
			}

			~PreviewBipedData()
			{
				if (!biped_) {
					return;
				}

				g_bipedAnimDtor(biped_);
				RE::free(biped_);
			}

			PreviewBipedData(const PreviewBipedData&) = delete;
			PreviewBipedData(PreviewBipedData&&) = delete;
			PreviewBipedData& operator=(const PreviewBipedData&) = delete;
			PreviewBipedData& operator=(PreviewBipedData&&) = delete;

			[[nodiscard]] explicit operator bool() const { return biped_ != nullptr; }
			[[nodiscard]] RE::BipedAnim& Get() const { return *biped_; }
			[[nodiscard]] const BorrowedBipedPointer* Borrowed() const { return std::addressof(borrowed_); }

		private:
			RE::BipedAnim* biped_{ nullptr };
			BorrowedBipedPointer borrowed_;
		};

		struct PreviewBipedPopulationStats
		{
			bool skinAdded{ false };
			std::uint32_t wornObjects{ 0 };
			std::uint32_t objectAddons{ 0 };
			std::uint32_t bufferedAddons{ 0 };
		};

		[[nodiscard]] std::uint32_t CountBipedArmorAddons(const RE::BIPOBJECT* a_objects)
		{
			std::uint32_t count = 0;
			for (std::int32_t i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
				if (a_objects[i].armorAddon) {
					++count;
				}
			}
			return count;
		}

		PreviewBipedPopulationStats PopulatePreviewBipedFromLiveNPC(
			RE::PlayerCharacter& a_player,
			RE::TESNPC& a_npc,
			const RE::BipedAnim& a_sourceBiped,
			PreviewBipedData& a_previewBiped)
		{
			PreviewBipedPopulationStats stats;
			auto* race = ResolvePreviewRace(a_player, a_npc);
			if (!race) {
				return stats;
			}

			auto* skin = g_getSkin(std::addressof(a_npc));
			if (skin) {
				RE::BGSObjectInstance skinInstance(skin, nullptr);
				g_addArmorToBiped(
					std::addressof(skinInstance),
					race,
					a_previewBiped.Borrowed(),
					a_npc.GetSex(),
					skin->armorData.index);
				stats.skinAdded = true;
			}

			for (std::int32_t i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
				const auto& sourceObject = a_sourceBiped.object[i];
				if (!sourceObject.parent.object || sourceObject.parent.object == skin) {
					continue;
				}

				if (g_initWornObject(
						std::addressof(a_npc),
						a_previewBiped.Borrowed(),
						std::addressof(sourceObject.parent))) {
					++stats.wornObjects;
				}
			}

			stats.objectAddons = CountBipedArmorAddons(a_previewBiped.Get().object);
			stats.bufferedAddons = CountBipedArmorAddons(a_previewBiped.Get().bufferedObjects);
			return stats;
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

		void RefreshPreviewBoneLookup(RE::NiAVObject& a_previewRoot)
		{
			g_createBoneMap(std::addressof(a_previewRoot));

			auto* flattened = FindFlattenedBoneTree(std::addressof(a_previewRoot));
			if (!flattened) {
				return;
			}

			g_createBoneMap(flattened);
			g_previewFlattenedBoneTree = flattened;
			g_createBoneMap(std::addressof(a_previewRoot));
		}

		[[nodiscard]] RE::NiNode* GetPreviewActor3DRootNode(RE::NiAVObject& a_previewRoot)
		{
			auto* flattened = g_previewFlattenedBoneTree;
			if (!flattened || !IsDescendantOf(*flattened, a_previewRoot)) {
				flattened = FindFlattenedBoneTree(std::addressof(a_previewRoot));
				g_previewFlattenedBoneTree = flattened;
			}

			if (flattened) {
				return flattened->IsNode();
			}

			return a_previewRoot.IsNode();
		}

		void CollectPreviewSkinTargetNodes(
			RE::NiAVObject& a_previewRoot,
			std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			a_previewNodes.clear();

			if (auto* flattened = FindFlattenedBoneTree(std::addressof(a_previewRoot))) {
				CollectFlattenedBoneNodes(flattened, a_previewNodes);
			}

			// Keep flattened skeleton bones authoritative for duplicate names such
			// as Head_skin; add non-flattened attachment/helper nodes only when a
			// skeleton target with the same name does not already exist.
			CollectNamedNodes(std::addressof(a_previewRoot), a_previewNodes);
		}

		void ResolveNullSkinBonesFromFlattenedTree(
			RE::BSSkin::Instance& a_skin,
			RE::NiAVObject& a_attachmentRoot)
		{
			if (a_skin.bones.empty() || a_skin.bones.size() > RE::BSSkin::kMaxExpectedBones) {
				return;
			}
			if (a_skin.worldTransforms.empty() || a_skin.worldTransforms.size() != a_skin.bones.size()) {
				return;
			}

			bool hasNullBones = false;
			for (auto* bone : a_skin.bones) {
				if (!bone) {
					hasNullBones = true;
					break;
				}
			}
			if (!hasNullBones) {
				return;
			}

			auto* flattened = FindFlattenedBoneTree(a_skin.rootNode);
			if (!flattened) {
				flattened = FindFlattenedBoneTree(GetTopRoot(std::addressof(a_attachmentRoot)));
			}
			if (!flattened) {
				return;
			}

			const std::array<RE::NiAVObject*, 4> searchRoots{
				a_skin.rootNode,
				std::addressof(a_attachmentRoot),
				GetTopRoot(a_skin.rootNode),
				GetTopRoot(std::addressof(a_attachmentRoot)),
			};

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
			}
		}

		void MergeAttachmentSkinBones(
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
			std::unordered_set<std::string> queuedMissingBoneNames;
			ForEachGeometry(std::addressof(a_attachmentRoot), [&](RE::BSGeometry& a_geometry) {
				auto* skin = a_geometry.skinInstance.get();
				if (!skin || skin->bones.size() > RE::BSSkin::kMaxExpectedBones) {
					return;
				}

				for (auto* bone : skin->bones) {
					if (!bone || !seenBones.insert(bone).second) {
						continue;
					}
					const auto* boneName = bone->GetName().c_str();
					if (!boneName || boneName[0] == '\0') {
						continue;
					}
					if (FindNodeByName(a_previewNodes, boneName)) {
						continue;
					}
					if (!queuedMissingBoneNames.emplace(boneName).second) {
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

				auto previewClone = PreviewClone::CloneObject(*candidate, std::addressof(a_previewRoot), std::addressof(a_previewNodes));
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
				CollectNamedNodes(previewClone.get(), a_previewNodes);
				++mergedBones;
			}

			for (const auto& external : externalBones) {
				if (!external.sourceBone || FindNodeByName(a_previewNodes, external.sourceBone->GetName())) {
					continue;
				}

				auto previewClone = PreviewClone::CloneObject(*external.sourceBone, std::addressof(a_previewRoot), std::addressof(a_previewNodes));
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
				CollectNamedNodes(previewClone.get(), a_previewNodes);
				++externalClonedBones;
			}

			if (mergedBones > 0 || externalClonedBones > 0) {
				RefreshPreviewBoneLookup(a_previewRoot);
				CollectPreviewSkinTargetNodes(a_previewRoot, a_previewNodes);
			}
		}

		std::uint32_t MergeDuplicateNodeChildrenIntoKnownNode(
			RE::NiNode& a_duplicateNode,
			RE::NiNode& a_knownNode,
			std::unordered_map<std::string, RE::NiAVObject*>& a_knownNodes,
			std::uint32_t& a_movedUnknownNodes)
		{
			std::vector<RE::NiPointer<RE::NiAVObject>> children;
			children.reserve(a_duplicateNode.children.size());
			for (auto& child : a_duplicateNode.children) {
				if (child) {
					children.push_back(child);
				}
			}

			std::uint32_t pruned = 0;
			for (auto& child : children) {
				const auto* childName = child->GetName().c_str();
				auto* knownObject = childName && childName[0] != '\0' ? FindNodeByName(a_knownNodes, childName) : nullptr;
				if (knownObject) {
					if (auto* duplicateChildNode = child->IsNode()) {
						if (auto* knownChildNode = knownObject->IsNode()) {
							pruned += MergeDuplicateNodeChildrenIntoKnownNode(
								*duplicateChildNode,
								*knownChildNode,
								a_knownNodes,
								a_movedUnknownNodes);
						}
					}

					a_duplicateNode.DetachChild(child.get());
					RetirePreviewObject(std::move(child));
					++pruned;
					continue;
				}

				a_duplicateNode.DetachChild(child.get());
				a_knownNode.AttachChild(child.get(), false);
				CollectNamedNodes(child.get(), a_knownNodes);
				++a_movedUnknownNodes;
			}

			return pruned;
		}

		std::uint32_t PruneKnownNamedChildSubtrees(
			RE::NiAVObject& a_root,
			std::unordered_map<std::string, RE::NiAVObject*>& a_knownNodes,
			std::uint32_t& a_movedUnknownNodes)
		{
			auto* node = a_root.IsNode();
			if (!node) {
				return 0;
			}

			std::vector<RE::NiPointer<RE::NiAVObject>> children;
			children.reserve(node->children.size());
			for (auto& child : node->children) {
				if (child) {
					children.push_back(child);
				}
			}

			std::uint32_t pruned = 0;
			for (auto& child : children) {
				const auto* childName = child->GetName().c_str();
				auto* knownObject = childName && childName[0] != '\0' ? FindNodeByName(a_knownNodes, childName) : nullptr;
				if (knownObject) {
					if (auto* duplicateChildNode = child->IsNode()) {
						if (auto* knownChildNode = knownObject->IsNode()) {
							pruned += MergeDuplicateNodeChildrenIntoKnownNode(
								*duplicateChildNode,
								*knownChildNode,
								a_knownNodes,
								a_movedUnknownNodes);
						}
					}

					node->DetachChild(child.get());
					RetirePreviewObject(std::move(child));
					++pruned;
					continue;
				}

				pruned += PruneKnownNamedChildSubtrees(*child, a_knownNodes, a_movedUnknownNodes);
			}

			return pruned;
		}

		bool ContainsUnknownNamedNode(
			RE::NiAVObject& a_root,
			const std::unordered_map<std::string, RE::NiAVObject*>& a_knownNodes)
		{
			const auto* rootName = a_root.GetName().c_str();
			if (rootName && rootName[0] != '\0' && !FindNodeByName(a_knownNodes, rootName)) {
				return true;
			}

			auto* node = a_root.IsNode();
			if (!node) {
				return false;
			}

			for (auto& child : node->children) {
				if (child && ContainsUnknownNamedNode(*child, a_knownNodes)) {
					return true;
				}
			}
			return false;
		}

		bool ContainsUnknownNamedDescendant(
			RE::NiAVObject& a_root,
			const std::unordered_map<std::string, RE::NiAVObject*>& a_knownNodes)
		{
			auto* node = a_root.IsNode();
			if (!node) {
				return false;
			}

			for (auto& child : node->children) {
				if (child && ContainsUnknownNamedNode(*child, a_knownNodes)) {
					return true;
				}
			}
			return false;
		}

		void RetirePreviewAttachments(const bool a_detach, const bool a_preserveFaceAttachments = false)
		{
			std::vector<PreviewAttachment> preservedAttachments;
			for (auto& attachment : g_previewAttachments) {
				if (a_preserveFaceAttachments && attachment.preserveOnEquipmentSync) {
					preservedAttachments.push_back(std::move(attachment));
					continue;
				}

				if (a_detach && attachment.parent && attachment.object) {
					attachment.parent->DetachChild(attachment.object.get());
				}
				if (attachment.object) {
					RetirePreviewObject(std::move(attachment.object));
				}
			}
			g_previewAttachments = std::move(preservedAttachments);
		}

		void ResetPreview3DState(const bool a_disableRenderer)
		{
			Renderer::ClearPreviewRoot(a_disableRenderer);
			RetirePreviewAttachments(false);
			g_previewRoot.reset();
			g_previewFlattenedBoneTree = nullptr;
			g_previewFaceNode.reset();
			g_pendingRendererAttach = false;
			g_pendingFramingUpdate = false;
			g_renderStateDirty = false;
			g_pendingRenderDelta = 0.0F;
		}

		void ReleasePreview3DState()
		{
			ResetPreview3DState(true);
			g_retiredPreviewObjects.clear();
			g_retiredPreviewObjectsPendingRelease.clear();
		}

		void ClearPreviewRebuildState()
		{
			g_rebuilder.Reset();
			g_lastAppliedFaceGenEntries.clear();
			g_pendingRendererAttach = false;
			g_pendingFramingUpdate = false;
			g_renderStateDirty = false;
			g_postRebuildAdjustmentHoldFrames = 0;
			g_pendingRenderDelta = 0.0F;
			g_lastDiagnostic.clear();
		}

		void BeginChangeAuditsIfRequested()
		{
			g_rebuilder.BeginRequestedAudits();
		}

		[[nodiscard]] bool TryBuildBipedSignature(const RE::BipedAnim& a_biped, std::uint64_t& a_signature)
		{
			a_signature = PreviewRebuilder::BuildEquipmentSignature(std::addressof(a_biped));
			if (a_signature == 0) {
				LogDiagnostic("third-person biped has empty signature");
				return false;
			}
			return true;
		}

		[[nodiscard]] bool TryResolveAuditedEquipmentSignature(const RE::BipedAnim& a_biped, std::uint64_t& a_signature)
		{
			std::int32_t pendingSlot = -1;
			std::uint64_t currentSignature = 0;
			if (!TryBuildBipedSignature(a_biped, currentSignature)) {
				return false;
			}

			if (const auto accepted = g_rebuilder.TryResolveEquipmentSignature(
					currentSignature,
					PreviewVisibility::HasPendingBipedModelHandles(a_biped, pendingSlot))) {
				a_signature = *accepted;
				return true;
			}

			return false;
		}

		[[nodiscard]] bool TryResolveAuditedMorphGeometrySignature(RE::PlayerCharacter& a_player, std::uint64_t& a_signature)
		{
			const auto currentSignature = PreviewRebuilder::BuildMorphGeometrySignature(a_player);
			if (const auto accepted = g_rebuilder.TryResolveMorphGeometrySignature(
					currentSignature)) {
				a_signature = *accepted;
				return true;
			}

			return false;
		}

		[[nodiscard]] bool TryResolveFaceCustomizationSignature(RE::PlayerCharacter& a_player, std::uint64_t& a_signature)
		{
			const auto currentSignature = PreviewRebuilder::BuildFaceCustomizationSignature(a_player);
			if (const auto accepted = g_rebuilder.TryResolveFaceCustomizationSignature(currentSignature)) {
				a_signature = *accepted;
				return true;
			}

			return false;
		}

		void RebindSkinInstance(
			RE::BSSkin::Instance& a_skin,
			RE::NiAVObject& a_previewRoot,
			const std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			if (a_skin.bones.empty()) {
				return;
			}

			if (a_skin.bones.size() > RE::BSSkin::kMaxExpectedBones) {
				return;
			}

			if (!a_skin.worldTransforms.empty() && a_skin.worldTransforms.size() != a_skin.bones.size()) {
				return;
			}

			for (std::uint32_t i = 0; i < a_skin.bones.size(); ++i) {
				auto* originalBone = a_skin.bones[i];
				if (!originalBone) {
					continue;
				}

				auto* previewBone = FindNodeByName(a_previewNodes, originalBone->GetName());
				if (!previewBone) {
					continue;
				}

				if (a_skin.bones[i] != previewBone) {
					a_skin.bones[i] = previewBone;
				}
				if (!a_skin.worldTransforms.empty()) {
					a_skin.worldTransforms[i] = std::addressof(previewBone->world);
				}
			}

			a_skin.rootNode = std::addressof(a_previewRoot);
			a_skin.paletteStamp = 0;
		}

		void FixPreviewFaceGenSkinInstances(RE::BSFaceGenNiNode& a_faceNode, RE::NiAVObject& a_attachRoot)
		{
			// IDA: BSFaceGenUtils::AttachHeadHelper attaches the face node to
			// actor3D->IsNode(), then calls the BSFaceGenNiNode vfunc at 0x218.
			// The vfunc iterates direct headpart geometries and runs the same
			// internal FixSkinInstances path used by AddHeadPartOnActor.
			// CreateHeadForNPC clears faceGenFlags 0x4/0x10; AttachHeadHelper
			// sets them again immediately before this call.
			a_faceNode.faceGenFlags |= 0x14;
			g_fixFaceGenHeadSkinInstances(std::addressof(a_faceNode), std::addressof(a_attachRoot), true);
			if (a_faceNode.animationData) {
				g_resetFaceGenCurrentMorphs(a_faceNode.animationData, 0.0F);
			}
		}

		void RebindPreviewSkinInstances(
			RE::NiAVObject& a_previewRoot,
			const RE::BipedAnim& a_sourceBiped)
		{
			RefreshPreviewBoneLookup(a_previewRoot);

			std::unordered_map<std::string, RE::NiAVObject*> previewNodes;
			CollectPreviewSkinTargetNodes(a_previewRoot, previewNodes);
			if (previewNodes.empty()) {
				return;
			}

			std::unordered_set<RE::BSSkin::Instance*> sourceSkins;
			CollectSourceBipedSkinInstances(a_sourceBiped, sourceSkins);

			std::uint32_t rebound = 0;
			ForEachGeometry(std::addressof(a_previewRoot), [&](RE::BSGeometry& a_geometry) {
				if (g_previewFaceNode && IsDescendantOf(a_geometry, *g_previewFaceNode)) {
					return;
				}

				auto* skin = a_geometry.skinInstance.get();
				if (!skin || sourceSkins.contains(skin)) {
					return;
				}

				RebindSkinInstance(*skin, a_previewRoot, previewNodes);
				++rebound;
			});

			if (rebound == 0) {
				return;
			}

			RE::NiUpdateData updateData;
			a_previewRoot.Update(updateData);
		}

		[[nodiscard]] RE::NiAVObject* FindNamedObjectOutside(
			RE::NiAVObject& a_root,
			const std::string_view a_name,
			RE::NiAVObject& a_excludedRoot)
		{
			RE::NiAVObject* found = nullptr;
			ForEachAVObject(std::addressof(a_root), [&](RE::NiAVObject& a_object) {
				if (found || IsDescendantOf(a_object, a_excludedRoot)) {
					return;
				}
				if (NamesEqual(a_object.GetName(), a_name)) {
					found = std::addressof(a_object);
				}
			});
			return found;
		}

		void AttachPreviewChildLikeEngine(RE::NiAVObject& a_child, RE::NiNode& a_parent)
		{
			if (a_child.parent == std::addressof(a_parent)) {
				return;
			}

			RE::NiPointer<RE::NiAVObject> keepAlive(std::addressof(a_child));
			if (auto* oldParent = a_child.parent) {
				oldParent->DetachChild(std::addressof(a_child));
			}
			a_parent.AttachChild(keepAlive.get(), true);
		}

		void TrackReparentedPreviewAttachment(
			const RE::BIPED_OBJECT a_slot,
			RE::NiAVObject& a_object,
			RE::NiNode& a_parent,
			std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			g_previewAttachments.push_back({
				.slot = a_slot,
				.object = RE::NiPointer<RE::NiAVObject>(std::addressof(a_object)),
				.parent = std::addressof(a_parent),
			});
			CollectNamedNodes(std::addressof(a_object), a_previewNodes);
		}

		void PrepareClonedWeaponLayoutObject(
			RE::PlayerCharacter& a_player,
			RE::NiAVObject& a_attachment,
			RE::NiNode& a_previewRootNode,
			RE::NiAVObject& a_previewRoot,
			const RE::BIPED_OBJECT a_slot,
			const RE::BIPOBJECT& a_sourceObject,
			const RE::BipedAnim& a_sourceBiped,
			std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			RE::bhkWorld::RemoveObjects(std::addressof(a_attachment), true, true);
			StripControllerChains(a_attachment);
			PreparePreviewTree(a_attachment);
			(void)PreviewCloth::Initialize(
				a_player,
				a_attachment,
				a_previewRoot,
				a_sourceObject.part ? a_sourceObject.part->GetModel() : nullptr);

			ForEachGeometry(std::addressof(a_attachment), [&](RE::BSGeometry& a_geometry) {
				if (auto* skin = a_geometry.skinInstance.get()) {
					ResolveNullSkinBonesFromFlattenedTree(*skin, a_attachment);
				}
			});

			MergeAttachmentSkinBones(a_attachment, a_previewRootNode, a_previewRoot, a_previewNodes);

			ForEachGeometry(std::addressof(a_attachment), [&](RE::BSGeometry& a_geometry) {
				auto* skin = a_geometry.skinInstance.get();
				if (!skin) {
					return;
				}
				RebindSkinInstance(*skin, a_previewRoot, a_previewNodes);
			});

			PrepareAttachmentSkinComplexion(a_attachment, a_slot, a_sourceBiped);
		}

		void ReplayEngineWeaponPostAttachLayout(
			RE::PlayerCharacter& a_player,
			const RE::BIPED_OBJECT a_slot,
			const RE::BIPOBJECT& a_sourceObject,
			const RE::BipedAnim& a_sourceBiped,
			RE::NiAVObject& a_sourceClone,
			RE::NiAVObject& a_previewClone,
			RE::NiNode& a_parent,
			RE::NiNode& a_previewRootNode,
			RE::NiAVObject& a_previewRoot,
			std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			if (!IsEngineWeaponAttachSlot(a_slot)) {
				return;
			}

			// IDA OG 1.10.163: BipedAnim::AttachToParent+0x4A4 moves "Scb"
			// under the resolved parent bone after the weapon root is attached.
			constexpr std::string_view scbName{ "Scb" };
			auto* previewScb = a_previewClone.GetObjectByName(RE::BSFixedString(scbName.data()));
			if (previewScb && previewScb != std::addressof(a_previewClone)) {
				AttachPreviewChildLikeEngine(*previewScb, a_parent);
				TrackReparentedPreviewAttachment(a_slot, *previewScb, a_parent, a_previewNodes);
			} else if (!FindNamedObjectOutside(a_parent, scbName, a_previewClone)) {
				RE::NiAVObject* sourceScb = nullptr;
				if (auto* sourceParent = a_sourceClone.parent) {
					sourceScb = FindNamedObjectOutside(*sourceParent, scbName, a_sourceClone);
				}

				if (sourceScb) {
					auto scbClone = PreviewClone::CloneObject(*sourceScb, std::addressof(a_previewRoot), std::addressof(a_previewNodes));
					if (scbClone) {
						StripControllerChains(*scbClone);
						a_parent.AttachChild(scbClone.get(), true);
						PrepareClonedWeaponLayoutObject(
							a_player,
							*scbClone,
							a_previewRootNode,
							a_previewRoot,
							a_slot,
							a_sourceObject,
							a_sourceBiped,
							a_previewNodes);
						g_previewAttachments.push_back({
							.slot = a_slot,
							.object = scbClone,
							.parent = std::addressof(a_parent),
						});
						CollectNamedNodes(g_previewAttachments.back().object.get(), a_previewNodes);
					}
				}
			}

			auto* backpack = a_previewClone.GetObjectByName(RE::BSFixedString("Backpack"));
			if (!backpack || backpack == std::addressof(a_previewClone)) {
				return;
			}

			auto* upb = netimmerse_cast<RE::NiStringExtraData*>(backpack->GetExtraData(RE::BSFixedString("UPB")));
			if (!upb || upb->GetValue().empty()) {
				return;
			}

			auto* target = FindNodeByName(a_previewNodes, upb->GetValue());
			if (!target) {
				target = a_previewRoot.GetObjectByName(upb->GetValue());
			}

			auto* targetNode = target ? target->IsNode() : nullptr;
			if (!targetNode) {
				return;
			}

			AttachPreviewChildLikeEngine(*backpack, *targetNode);
			TrackReparentedPreviewAttachment(a_slot, *backpack, *targetNode, a_previewNodes);
		}

		[[nodiscard]] const char* DebugNodeName(const RE::NiAVObject& a_object)
		{
			const auto name = a_object.GetName();
			return name.empty() ? "<unnamed>" : name.c_str();
		}

		void LogBoneNodeHierarchy(
			RE::NiAVObject& a_object,
			const std::string& a_prefix,
			const bool a_isLast,
			const bool a_isRoot,
			std::unordered_set<RE::NiAVObject*>& a_seen)
		{
			if (!a_seen.insert(std::addressof(a_object)).second) {
				REX::WARN(
					"Preview right-hand bone hierarchy: {}{}'{}' ptr={:X} <cycle>",
					a_prefix,
					a_isRoot ? "" : (a_isLast ? "`-- " : "|-- "),
					DebugNodeName(a_object),
					reinterpret_cast<std::uintptr_t>(std::addressof(a_object)));
				return;
			}

			const auto& local = a_object.GetLocalTransform();
			const auto& world = a_object.GetWorldTransform();
			const auto& r = local.rotate;
			REX::INFO(
				"Preview right-hand bone hierarchy: {}{}'{}' ptr={:X} parent='{}' parentPtr={:X} localT=({:.3f},{:.3f},{:.3f}) localS={:.3f} localR=[({:.4f},{:.4f},{:.4f}),({:.4f},{:.4f},{:.4f}),({:.4f},{:.4f},{:.4f})] worldT=({:.3f},{:.3f},{:.3f}) worldS={:.3f}",
				a_prefix,
				a_isRoot ? "" : (a_isLast ? "`-- " : "|-- "),
				DebugNodeName(a_object),
				reinterpret_cast<std::uintptr_t>(std::addressof(a_object)),
				a_object.parent ? DebugNodeName(*a_object.parent) : "<null>",
				reinterpret_cast<std::uintptr_t>(a_object.parent),
				local.translate.x,
				local.translate.y,
				local.translate.z,
				local.scale,
				r[0].x,
				r[0].y,
				r[0].z,
				r[1].x,
				r[1].y,
				r[1].z,
				r[2].x,
				r[2].y,
				r[2].z,
				world.translate.x,
				world.translate.y,
				world.translate.z,
				world.scale);

			auto* node = a_object.IsNode();
			if (!node) {
				return;
			}

			std::vector<RE::NiAVObject*> childNodes;
			childNodes.reserve(node->children.size());
			for (auto& child : node->children) {
				if (child && child->IsNode()) {
					childNodes.push_back(child.get());
				}
			}

			const auto childPrefix = a_prefix + (a_isRoot ? "" : (a_isLast ? "    " : "|   "));
			for (std::size_t i = 0; i < childNodes.size(); ++i) {
				auto* child = childNodes[i];
				if (!child) {
					continue;
				}
				LogBoneNodeHierarchy(*child, childPrefix, i + 1 == childNodes.size(), false, a_seen);
			}
		}

		void LogPreviewRightHandBoneHierarchy()
		{
			if (!g_previewRoot) {
				REX::WARN("Preview right-hand bone hierarchy skipped: preview root is null");
				return;
			}

			RefreshPreviewBoneLookup(*g_previewRoot);
			std::unordered_map<std::string, RE::NiAVObject*> previewNodes;
			CollectPreviewSkinTargetNodes(*g_previewRoot, previewNodes);

			auto* rightHand = FindPreviewNodeByName(*g_previewRoot, previewNodes, "RArm_Hand");
			if (!rightHand) {
				REX::WARN(
					"Preview right-hand bone hierarchy skipped: RArm_Hand not found, root='{}', rootPtr={:X}, namedNodes={}",
					g_previewRoot->GetName(),
					reinterpret_cast<std::uintptr_t>(g_previewRoot.get()),
					previewNodes.size());
				return;
			}

			REX::INFO(
				"Preview right-hand bone hierarchy begin: root='{}', rootPtr={:X}, rightHandPtr={:X}, namedNodes={}",
				g_previewRoot->GetName(),
				reinterpret_cast<std::uintptr_t>(g_previewRoot.get()),
				reinterpret_cast<std::uintptr_t>(rightHand),
				previewNodes.size());
			std::unordered_set<RE::NiAVObject*> seen;
			LogBoneNodeHierarchy(*rightHand, "", true, true, seen);
			REX::INFO("Preview right-hand bone hierarchy end: nodes={}", seen.size());
		}

		[[nodiscard]] std::uint32_t CountPreviewAttachmentsForSlot(const RE::BIPED_OBJECT a_slot)
		{
			return static_cast<std::uint32_t>(std::ranges::count_if(
				g_previewAttachments,
				[&](const PreviewAttachment& a_attachment) {
					return a_attachment.slot == a_slot;
				}));
		}

		[[nodiscard]] EquipmentSyncResult SyncEquipmentsFromBiped(
			RE::PlayerCharacter& a_player,
			RE::NiAVObject& a_previewRoot,
			const RE::BipedAnim& a_sourceBiped,
			const bool a_preserveFaceAttachments)
		{
			auto* previewRootNode = a_previewRoot.IsNode();
			if (!previewRootNode) {
				LogDiagnostic("equipment sync skipped: preview root is not a NiNode");
				return EquipmentSyncResult::kFailedTransient;
			}

			RetirePreviewAttachments(true, a_preserveFaceAttachments);

			std::unordered_map<std::string, RE::NiAVObject*> previewNodes;
			CollectPreviewSkinTargetNodes(a_previewRoot, previewNodes);
			if (previewNodes.empty()) {
				LogDiagnostic("equipment sync skipped: preview root has no named nodes");
				return EquipmentSyncResult::kFailedTransient;
			}

			std::unordered_set<RE::NiAVObject*> seenSourceObjects;
			std::uint32_t attachedObjects = 0;
			const auto externalBoneAttachmentsBefore = CountPreviewAttachmentsForSlot(RE::BIPED_OBJECT::kNone);

			auto mirrorObject = [&](const RE::BIPED_OBJECT a_slot, const RE::BIPOBJECT& a_sourceObject) {
				auto* sourceClone = a_sourceObject.partClone.get();
				if (!sourceClone || !seenSourceObjects.insert(sourceClone).second) {
					return;
				}

				std::unordered_set<RE::BSSkin::Instance*> sourceSkins;
				CollectSkinInstances(sourceClone, sourceSkins);

				auto previewClone = PreviewClone::CloneObject(*sourceClone, std::addressof(a_previewRoot), std::addressof(previewNodes));
				if (!previewClone) {
					return;
				}
				StripControllerChains(*previewClone);

				auto* parent = FindPreviewAttachParent(a_slot, a_sourceObject, *sourceClone, a_previewRoot, previewNodes);
				if (!parent) {
					RetirePreviewObject(std::move(previewClone));
					return;
				}

				parent->AttachChild(previewClone.get(), false);
				Mod3DReplayResult modReplay;
				if (!IsEngineWeaponAttachSlot(a_slot)) {
					modReplay = ReplayObjectInstanceMod3D(a_sourceObject, *previewClone);
				}

				RE::bhkWorld::RemoveObjects(previewClone.get(), true, true);
				StripControllerChains(*previewClone);
				PreparePreviewTree(*previewClone);
				(void)PreviewCloth::Initialize(
					a_player,
					*previewClone,
					a_previewRoot,
					a_sourceObject.part ? a_sourceObject.part->GetModel() : nullptr);
				ForEachGeometry(previewClone.get(), [&](RE::BSGeometry& a_geometry) {
					if (auto* skin = a_geometry.skinInstance.get()) {
						ResolveNullSkinBonesFromFlattenedTree(
							*skin,
							*previewClone);
					}
				});
				MergeAttachmentSkinBones(
					*previewClone,
					*previewRootNode,
					a_previewRoot,
					previewNodes);

				ForEachGeometry(previewClone.get(), [&](RE::BSGeometry& a_geometry) {
					auto* skin = a_geometry.skinInstance.get();
					if (!skin) {
						return;
					}
					if (sourceSkins.contains(skin)) {
						return;
					}
					RebindSkinInstance(*skin, a_previewRoot, previewNodes);
				});
				PrepareAttachmentSkinComplexion(
					*previewClone,
					a_slot,
					a_sourceBiped);
				ReplayEngineWeaponPostAttachLayout(
					a_player,
					a_slot,
					a_sourceObject,
					a_sourceBiped,
					*sourceClone,
					*previewClone,
					*parent,
					*previewRootNode,
					a_previewRoot,
					previewNodes);

				g_previewAttachments.push_back({
					.slot = a_slot,
					.object = previewClone,
					.parent = parent,
				});
				CollectNamedNodes(g_previewAttachments.back().object.get(), previewNodes);
				if (modReplay.attached > 0) {
					REX::INFO(
						"Preview equipment replayed object-instance mod visuals: slot={}, attached={}",
						std::to_underlying(a_slot),
						modReplay.attached);
				}
				++attachedObjects;
			};

			for (std::int32_t i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
				const auto slot = static_cast<RE::BIPED_OBJECT>(i);
				const auto& sourceObject = a_sourceBiped.object[i];
				mirrorObject(slot, sourceObject);
			}

			RefreshPreviewBoneLookup(a_previewRoot);

			RE::NiUpdateData updateData;
			a_previewRoot.Update(updateData);

			if (attachedObjects == 0) {
				return EquipmentSyncResult::kNoChange;
			}

			const auto externalBoneAttachmentsAfter = CountPreviewAttachmentsForSlot(RE::BIPED_OBJECT::kNone);
			if (externalBoneAttachmentsAfter > externalBoneAttachmentsBefore) {
				return EquipmentSyncResult::kSyncedWithMergedBones;
			}

			return EquipmentSyncResult::kSynced;
		}

		bool AttachPreviewFaceBonesFromBiped(
			const RE::BipedAnim& a_sourceBiped,
			RE::NiAVObject& a_previewRoot,
			RE::TESRace& a_race,
			const RE::SEX a_sex)
		{
			auto* actorRootNode = GetPreviewActor3DRootNode(a_previewRoot);
			if (!actorRootNode) {
				return false;
			}

			std::unordered_map<std::string, RE::NiAVObject*> previewNodes;
			CollectPreviewSkinTargetNodes(a_previewRoot, previewNodes);
			if (previewNodes.empty()) {
				return false;
			}

			FaceBoneAttachStats stats;
			std::vector<FaceBoneCandidateDetail> details;
			std::unordered_set<std::string> loadedPaths;

			enum class FaceBoneModelAttachResult
			{
				kAttached,
				kLoadFailed,
				kPrunedEmpty
			};

			auto attachFaceBoneModel = [&](const char* a_modelPath) {
				auto faceBones = LoadPreviewModel(a_modelPath);
				if (!faceBones) {
					return FaceBoneModelAttachResult::kLoadFailed;
				}

				StripControllerChains(*faceBones);
				RE::bhkWorld::RemoveObjects(faceBones.get(), true, true);
				PreparePreviewTree(*faceBones);
				StripClonedGeometry(*faceBones);
				std::uint32_t movedUnknownNodes = 0;
				const auto pruned = PruneKnownNamedChildSubtrees(*faceBones, previewNodes, movedUnknownNodes);
				stats.prunedDuplicates += pruned;
				stats.movedUnknownNodes += movedUnknownNodes;
				const bool hasRemainingUnknownNodes = ContainsUnknownNamedDescendant(*faceBones, previewNodes);
				if (!hasRemainingUnknownNodes && movedUnknownNodes == 0) {
					return FaceBoneModelAttachResult::kPrunedEmpty;
				}

				if (hasRemainingUnknownNodes) {
					actorRootNode->AttachChild(faceBones.get(), false);
					if (auto* convertedFaceBones = g_convertNodeTree(faceBones.get())) {
						if (convertedFaceBones != faceBones.get()) {
							faceBones.reset(convertedFaceBones);
						}
						++stats.converted;
					}
					if (!ContainsObject(*actorRootNode, *faceBones)) {
						actorRootNode->AttachChild(faceBones.get(), false);
						++stats.reattached;
					}
					g_previewAttachments.push_back({
						.slot = RE::BIPED_OBJECT::kFaceGenHead,
						.object = faceBones,
						.parent = actorRootNode,
						.preserveOnEquipmentSync = true,
					});
					CollectNamedNodes(faceBones.get(), previewNodes);
				} else {
					g_convertNodeTree(actorRootNode);
					++stats.converted;
				}

				++stats.attached;
				return FaceBoneModelAttachResult::kAttached;
			};

			for (const auto& candidate : GetRaceSkeletonFaceBoneModelPaths(a_race, std::to_underlying(a_sex))) {
				++stats.skeletonCandidates;
				if (!loadedPaths.insert(candidate.path).second) {
					continue;
				}

				const auto result = attachFaceBoneModel(candidate.path.c_str());
				if (result == FaceBoneModelAttachResult::kAttached) {
					++stats.skeletonAttached;
					REX::INFO(
						"Preview race skeleton facebones attached: source='{}', path='{}'",
						candidate.source,
						candidate.path);
					break;
				}

				if (result == FaceBoneModelAttachResult::kLoadFailed) {
					++stats.skeletonLoadFailed;
				} else {
					++stats.skeletonPrunedEmpty;
				}
			}

			auto attachFromObjects = [&](const RE::BIPOBJECT* a_objects, const char* a_sourceName) {
				for (std::int32_t i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
					const auto& sourceObject = a_objects[i];
					if (!sourceObject.armorAddon) {
						continue;
					}

					auto* model = SelectArmorAddonFacebonesModel(*sourceObject.armorAddon, a_sex);
					const auto* modelPath = model ? model->GetModel() : nullptr;
					++stats.candidates;
					if (!modelPath || modelPath[0] == '\0') {
						++stats.noFacebonesModel;
						details.push_back(PreviewFaceBoneDiagnostics::MakeCandidateDetail(
							a_sourceName,
							i,
							sourceObject,
							modelPath,
							"no facebones model",
							0));
						continue;
					}
					if (!loadedPaths.insert(modelPath).second) {
						++stats.duplicatePath;
						details.push_back(PreviewFaceBoneDiagnostics::MakeCandidateDetail(
							a_sourceName,
							i,
							sourceObject,
							modelPath,
							"duplicate facebones path",
							0));
						continue;
					}

					const auto result = attachFaceBoneModel(modelPath);
					if (result != FaceBoneModelAttachResult::kAttached) {
						if (result == FaceBoneModelAttachResult::kLoadFailed) {
							++stats.loadFailed;
						} else {
							++stats.prunedEmpty;
						}
						details.push_back(PreviewFaceBoneDiagnostics::MakeCandidateDetail(
							a_sourceName,
							i,
							sourceObject,
							modelPath,
							result == FaceBoneModelAttachResult::kLoadFailed ? "model load failed" : "no unknown named facebone descendants after pruning",
							0));
						continue;
					}
				}
			};

			attachFromObjects(a_sourceBiped.object, "object");
			attachFromObjects(a_sourceBiped.bufferedObjects, "buffered");

			if (stats.attached > 0) {
				RefreshPreviewBoneLookup(a_previewRoot);
				REX::INFO(
					"Headpart facebone models attached: models={}, converted={}, prunedDuplicates={}",
					stats.attached,
					stats.converted,
					stats.prunedDuplicates);
			} else {
				PreviewFaceBoneDiagnostics::LogAttachFailure(
					a_sourceBiped,
					*actorRootNode,
					a_sex,
					previewNodes.size(),
					stats,
					details);
			}

			return stats.attached > 0;
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
			PreviewCloth::InitializeHeadParts(*npc, a_player, *faceNode, a_previewRoot);

			auto* previewRace = ResolvePreviewRace(a_player, *npc);
			if (!previewRace) {
				LogDiagnostic("preview head creation failed: preview race is null");
				return false;
			}

			PreviewHeadParts::ApplyBipedVisibility(*npc, *previewRace, *faceNode, a_player, a_sourceBiped);

			auto* actorRootNode = GetPreviewActor3DRootNode(a_previewRoot);
			if (!actorRootNode) {
				LogDiagnostic("preview head creation failed: preview actor root is not a NiNode");
				return false;
			}

			PreviewBipedData previewBiped(a_player, *actorRootNode);
			if (!previewBiped) {
				LogDiagnostic("preview head creation failed: preview biped allocation failed");
				return false;
			}

			const auto previewBipedStats =
				PopulatePreviewBipedFromLiveNPC(a_player, *npc, a_sourceBiped, previewBiped);
			if (!AttachPreviewFaceBonesFromBiped(previewBiped.Get(), a_previewRoot, *previewRace, npc->GetSex())) {
				REX::WARN(
					"preview head creation: no preview facebone model was attached; skinAdded={}, wornObjects={}, objectAddons={}, bufferedAddons={}",
					previewBipedStats.skinAdded,
					previewBipedStats.wornObjects,
					previewBipedStats.objectAddons,
					previewBipedStats.bufferedAddons);
			}

			actorRootNode->AttachChild(faceNode.get(), false);
			g_previewFaceNode = faceNode;
			RefreshPreviewBoneLookup(a_previewRoot);
			FixPreviewFaceGenSkinInstances(*faceNode, *actorRootNode);
			RE::NiUpdateData updateData;
			actorRootNode->Update(updateData);
			RefreshPreviewBoneLookup(a_previewRoot);
			return true;
		}

		void ApplyPreviewBodyTint(RE::TESNPC& a_npc, RE::NiAVObject& a_previewRoot)
		{
			RE::NiColorA bodyTint{ 0.0F, 0.0F, 0.0F, 0.0F };
			g_calculateBodyTintColor(std::addressof(a_npc), bodyTint, nullptr, true, false);
			bodyTint.a = 1.0F;
			g_updateBodyTintColorsOnScene(std::addressof(a_previewRoot), bodyTint);
			RestorePreviewShaderAlpha(a_previewRoot);
		}

		void ApplyPreviewFaceBoneRegionScales(RE::TESNPC& a_npc, RE::BSFaceGenNiNode& a_faceNode)
		{
			// IDA: CreateHeadForNPC applies facial bone regions by iterating the
			// created head geometries and calling TESNPC::ScaleFaceBones on each
			// BSSkin::Instance, not by traversing the face node transform tree.
			ForEachGeometry(std::addressof(a_faceNode), [&](RE::BSGeometry& a_geometry) {
				if (auto* skin = a_geometry.skinInstance.get(); skin) {
					g_scaleFaceSkinBones(std::addressof(a_npc), skin);
				}
			});
		}

		bool ApplyPreviewFaceCustomization(RE::PlayerCharacter& a_player, const std::uint64_t a_signature)
		{
			if (!g_previewRoot || !g_previewFaceNode) {
				return false;
			}

			auto* playerBase = a_player.GetObjectReference();
			auto* npc = playerBase ? playerBase->As<RE::TESNPC>() : nullptr;
			if (!npc) {
				return false;
			}

			RE::BSScrapArray<RE::BGSHeadPart*> headParts;
			g_generateFlattenedHeadPartArray(npc, headParts);

			// IDA: CreateHeadForNPC runs these after headpart creation. This
			// pass deliberately skips creating/replacing headparts and reapplies
			// only the current TESNPC FaceGen customization payload.
			g_applyWeightFaceMorph(npc, g_previewFaceNode.get());
			ApplyPreviewFaceBoneRegionScales(*npc, *g_previewFaceNode);
			g_applyAllCustomizationMorphs(npc, headParts, g_previewFaceNode.get());
			g_updateAllChildrenMorphData(g_previewFaceNode.get(), true);
			g_prepareHeadForShaders(headParts, g_previewFaceNode.get(), npc, nullptr);
			ApplyPreviewBodyTint(*npc, *g_previewRoot);
			g_lastAppliedFaceGenEntries = CaptureLiveFaceGenEntries(a_player);

			if (auto* animationData = g_previewFaceNode->animationData; animationData) {
				animationData->morphsDirty = true;
				animationData->forceMorphUpdate = true;
			}
			g_previewFaceNode->faceGenFlags &= static_cast<std::uint16_t>(~0x4u);

			RE::NiUpdateData updateData;
			g_previewFaceNode->Update(updateData);
			g_rebuilder.CommitFaceCustomization(a_signature);
			return true;
		}

		bool RebuildPreview(
			RE::PlayerCharacter& a_player,
			const RE::BipedAnim& a_biped,
			std::uint64_t a_bipedSignature,
			std::uint64_t a_visualSignature)
		{
			// Rebuilds run at the Interface3D commit boundary. Keep the active
			// renderer-owned tree intact until a full replacement is ready.
			auto previousAttachments = std::move(g_previewAttachments);
			auto previousFaceNode = std::move(g_previewFaceNode);
			auto* previousFlattenedBoneTree = g_previewFlattenedBoneTree;
			g_previewAttachments.clear();
			g_previewFaceNode.reset();
			g_previewFlattenedBoneTree = nullptr;

			auto restorePreviousPreviewMetadata = [&]() {
				g_previewAttachments.clear();
				g_previewFaceNode.reset();
				g_previewAttachments = std::move(previousAttachments);
				g_previewFaceNode = std::move(previousFaceNode);
				g_previewFlattenedBoneTree = previousFlattenedBoneTree;
				if (!g_previewRoot) {
					g_rebuilder.ClearCommittedSignatures();
				}
			};

			RE::NiPointer<RE::NiAVObject> previewRoot = LoadPreviewRaceSkeleton(a_player);
			if (!previewRoot) {
				restorePreviousPreviewMetadata();
				LogDiagnostic("preview race skeleton load returned null");
				return false;
			}

			RE::bhkWorld::RemoveObjects(previewRoot.get(), true, true);
			StripControllerChains(*previewRoot);
			PreparePreviewTree(*previewRoot);
			if (!EnsurePreviewHead(a_player, *previewRoot, a_biped)) {
				restorePreviousPreviewMetadata();
				return false;
			}
			if (!EquipmentSyncSucceeded(SyncEquipmentsFromBiped(a_player, *previewRoot, a_biped, true))) {
				restorePreviousPreviewMetadata();
				return false;
			}
			RefreshPreviewHeadPartVisibility(a_player, a_biped);
			if (auto* playerBase = a_player.GetObjectReference(); playerBase) {
				if (auto* npc = playerBase->As<RE::TESNPC>(); npc) {
					ApplyPreviewBodyTint(*npc, *previewRoot);
				}
			}
			RebindPreviewSkinInstances(*previewRoot, a_biped);

			SanitizePreviewRenderTree(*previewRoot);
			PreviewFraming::ApplyHeadCentered(*previewRoot, g_previewFlattenedBoneTree);
			PrepareForInterface3DOffscreen(*previewRoot);

			// Do not detach children from the old root here. DrawModel can have
			// stale traversal work after Offscreen_Set3D is updated.
			if (g_previewRoot) {
				RetirePreviewObject(std::move(g_previewRoot));
			}
			g_previewRoot = previewRoot;
			const auto faceCustomizationSignature = PreviewRebuilder::BuildFaceCustomizationSignature(a_player);
			const bool faceCustomizationApplied =
				ApplyPreviewFaceCustomization(a_player, faceCustomizationSignature);
			g_rebuilder.CommitSkeletonBuild(
				a_bipedSignature,
				a_visualSignature,
				PreviewRebuilder::BuildMorphGeometrySignature(a_player),
				faceCustomizationApplied ? faceCustomizationSignature : 0);

			g_pendingRendererAttach = true;
			g_pendingFramingUpdate = false;
			g_postRebuildAdjustmentHoldFrames = kPostRebuildAdjustmentHoldFrames;
			MarkRenderStateDirty();
			Animations::ResetInitialState();
			g_rebuilder.RequestSkeletonAdjustment();
			g_rebuilder.RequestBehaviorGraphRefresh();

			return true;
		}

		[[nodiscard]] bool SyncPreviewEquipmentLayer(
			RE::PlayerCharacter& a_player,
			const RE::BipedAnim& a_biped,
			const std::uint64_t a_equipmentSignature,
			const std::uint64_t a_morphGeometrySignature,
			const char* a_reason)
		{
			if (!g_previewRoot) {
				return false;
			}

			std::int32_t pendingSlot = -1;
			if (PreviewVisibility::HasPendingBipedModelHandles(a_biped, pendingSlot)) {
				return false;
			}

			const auto result = SyncEquipmentsFromBiped(a_player, *g_previewRoot, a_biped, true);
			if (!EquipmentSyncSucceeded(result)) {
				return false;
			}
			RefreshPreviewHeadPartVisibility(a_player, a_biped);

			if (auto* playerBase = a_player.GetObjectReference(); playerBase) {
				if (auto* npc = playerBase->As<RE::TESNPC>(); npc) {
					ApplyPreviewBodyTint(*npc, *g_previewRoot);
				}
			}

			RebindPreviewSkinInstances(*g_previewRoot, a_biped);
			SanitizePreviewRenderTree(*g_previewRoot);
			PreviewFraming::ApplyHeadCentered(*g_previewRoot, g_previewFlattenedBoneTree);
			PrepareForInterface3DOffscreen(*g_previewRoot);

			g_rebuilder.CommitEquipmentLayer(
				a_equipmentSignature,
				a_morphGeometrySignature != 0 ? a_morphGeometrySignature : PreviewRebuilder::BuildMorphGeometrySignature(a_player));
			g_rebuilder.RequestSkeletonAdjustment();
			g_rebuilder.RequestBehaviorGraphRefresh();
			g_pendingRendererAttach = true;
			g_pendingFramingUpdate = false;
			MarkRenderStateDirty();

			REX::INFO(
				"Preview equipment layer synced: reason={}, result={}",
				a_reason ? a_reason : "unknown",
				static_cast<std::uint32_t>(result));
			return true;
		}

		void Tick(const float a_deltaTime)
		{
			if (g_looksMenuSuspended) {
				LogDiagnostic("halted: LooksMenu is open");
				HideRendererAndResetAnimation();
				return;
			}

			auto player = RE::PlayerCharacter::GetSingleton();
			std::string reason;
			if (!PreviewVisibility::ShouldShow(player, reason)) {
				HideRendererAndResetAnimation();
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

			const auto& biped = player->GetBiped(false);
			if (!biped) {
				LogDiagnostic("third-person biped is null");
				HideRendererAndResetAnimation();
				return;
			}

			std::string sourceReason;
			if (!PreviewVisibility::IsPreviewSourceReady(*player, *biped, sourceReason)) {
				LogDiagnostic("preview render postponed: " + sourceReason);
				HideRendererAndResetAnimation();
				return;
			}

			MarkRenderStateDirty(a_deltaTime);

			if (Renderer::Enable()) {
				LogDiagnostic("renderer enabled");
			}
		}

		void CommitRenderStateImpl()
		{
			if (g_committingRenderState || (!g_renderStateDirty && !g_pendingRendererAttach)) {
				return;
			}
			g_committingRenderState = true;

			const REX::TScopeExit clearCommitGuard{ [&]() noexcept {
				g_committingRenderState = false;
			} };
			AdvanceRetiredPreviewObjects();

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				g_renderStateDirty = false;
				g_pendingRenderDelta = 0.0F;
				return;
			}

			std::string reason;
			if (!PreviewVisibility::ShouldShow(player, reason)) {
				HideRendererAndResetAnimation();
				g_renderStateDirty = false;
				g_pendingRenderDelta = 0.0F;
				return;
			}

			const auto& biped = player->GetBiped(false);
			if (!biped) {
				LogDiagnostic("third-person biped is null");
				HideRendererAndResetAnimation();
				g_renderStateDirty = false;
				g_pendingRenderDelta = 0.0F;
				return;
			}

			std::string sourceReason;
			if (!PreviewVisibility::IsPreviewSourceReady(*player, *biped, sourceReason)) {
				LogDiagnostic("preview render postponed: " + sourceReason);
				HideRendererAndResetAnimation();
				g_renderStateDirty = false;
				g_pendingRenderDelta = 0.0F;
				return;
			}

			const auto renderDelta = g_pendingRenderDelta;
			g_pendingRenderDelta = 0.0F;
			g_renderStateDirty = false;

			BeginChangeAuditsIfRequested();

			auto tryRebuildPreview = [&](std::uint64_t a_bipedSignature, std::uint64_t a_visualSignature) {
				std::int32_t pendingSlot = -1;
				if (PreviewVisibility::HasPendingBipedModelHandles(*biped, pendingSlot)) {
					return false;
				}

				if (a_bipedSignature == 0 && !TryBuildBipedSignature(*biped, a_bipedSignature)) {
					if (!g_previewRoot) {
						HideRendererAndResetAnimation();
					}
					return false;
				}

				if (!RebuildPreview(*player, *biped, a_bipedSignature, a_visualSignature)) {
					if (!g_previewRoot) {
						HideRendererAndResetAnimation();
					}
					return false;
				}

				return true;
			};

			const auto visualSignature = PreviewRebuilder::BuildVisualSignature(*player);
			const bool forceRebuild = g_rebuilder.NeedsSkeletonBuild(visualSignature, static_cast<bool>(g_previewRoot));
			const bool deferRebuildForAdjustment = g_previewRoot && g_postRebuildAdjustmentHoldFrames != 0;
			bool structuralCommandRan = false;
			if (forceRebuild && !deferRebuildForAdjustment) {
				std::uint64_t bipedSignature = 0;
				if (tryRebuildPreview(
						bipedSignature,
						visualSignature)) {
					structuralCommandRan = true;
				} else if (!g_previewRoot) {
					return;
				}
			}

			if (!g_previewRoot) {
				return;
			}

			if (g_pendingFramingUpdate) {
				PreviewFraming::ApplyHeadCentered(*g_previewRoot, g_previewFlattenedBoneTree);
				g_pendingFramingUpdate = false;
			}

			if (g_postRebuildAdjustmentHoldFrames != 0) {
				--g_postRebuildAdjustmentHoldFrames;
			}

			if (!structuralCommandRan && g_postRebuildAdjustmentHoldFrames == 0) {
				std::uint64_t equipmentSignature = 0;
				std::uint64_t morphGeometrySignature = 0;
				if (TryResolveAuditedEquipmentSignature(*biped, equipmentSignature)) {
					if (!SyncPreviewEquipmentLayer(
							*player,
							*biped,
							equipmentSignature,
							PreviewRebuilder::BuildMorphGeometrySignature(*player),
							"equipment")) {
						return;
					}
					structuralCommandRan = true;
				} else if (TryResolveAuditedMorphGeometrySignature(*player, morphGeometrySignature)) {
					std::uint64_t currentEquipmentSignature = 0;
					if (!TryBuildBipedSignature(*biped, currentEquipmentSignature) ||
						!SyncPreviewEquipmentLayer(
							*player,
							*biped,
							currentEquipmentSignature,
							morphGeometrySignature,
							"morph geometry")) {
						return;
					}
					structuralCommandRan = true;
				}
			}

			if (!structuralCommandRan && g_postRebuildAdjustmentHoldFrames == 0) {
				std::uint64_t faceCustomizationSignature = 0;
				if (TryResolveFaceCustomizationSignature(*player, faceCustomizationSignature)) {
					(void)ApplyPreviewFaceCustomization(*player, faceCustomizationSignature);
				}
			}

			if (!g_previewRoot) {
				return;
			}

			if (g_postRebuildAdjustmentHoldFrames == 0 && g_rebuilder.ConsumeSkeletonAdjustmentRequest()) {
				Morph::MarkSecondaryDirty();
			}
			(void)Morph::Update(*player, *g_previewRoot, g_previewFlattenedBoneTree);

			if (g_rebuilder.ConsumeBehaviorGraphRefreshRequest()) {
				Animations::ResetGraph();
			}
			Animations::Update(*player, *g_previewRoot, renderDelta);
			SyncPreviewFacialExpression(*player);
			PreviewFraming::ApplyHeadFollowTranslation(*g_previewRoot, g_previewFlattenedBoneTree);

			if (g_pendingRendererAttach) {
				Renderer::AttachPreviewRoot(*g_previewRoot);
				g_pendingRendererAttach = false;
			}
		}
	}

	namespace Previewer
	{
		void Update(float a_deltaTime)
		{
			std::scoped_lock lock(g_stateLock);
			Tick(a_deltaTime);
		}

		void CommitRenderState()
		{
			std::scoped_lock lock(g_stateLock);
			CommitRenderStateImpl();
		}

		void Reset()
		{
			std::scoped_lock lock(g_stateLock);
			g_looksMenuSuspended = false;
			Renderer::Hide();
			Renderer::Reset();
			ReleasePreview3DState();
			g_previewRoot.reset();
			g_previewFaceNode.reset();
			ClearPreviewRebuildState();
			Morph::Reset();
			Animations::Reset();
		}

		void MarkEquipmentDirty()
		{
			std::scoped_lock lock(g_stateLock);
			g_rebuilder.ObserveEquipment();
			MarkRenderStateDirty();
		}

		void ObserveUpdate3DModel(const std::uint16_t a_updateFlags, const bool a_updateEditorDeadModel)
		{
			std::scoped_lock lock(g_stateLock);
			g_rebuilder.ObserveUpdate3DModel(a_updateFlags, a_updateEditorDeadModel);
			MarkRenderStateDirty();
		}

		void ApplyConfigChanges()
		{
			std::scoped_lock lock(g_stateLock);
			if (!GetConfig().enabled) {
				HideRendererAndResetAnimation();
				return;
			}

			if (g_previewRoot) {
				g_pendingFramingUpdate = true;
				MarkRenderStateDirty();
			}

			if (!Renderer::Get()) {
				return;
			}

			Renderer::ApplyDisplayLayout();
			Renderer::ConfigureLighting();
		}

		void ReloadConfig()
		{
			std::scoped_lock lock(g_stateLock);
			LoadConfig();
			g_lastDiagnostic.clear();

			if (!GetConfig().enabled) {
				HideRendererAndResetAnimation();
				return;
			}

			if (g_previewRoot) {
				g_pendingFramingUpdate = true;
				g_pendingRendererAttach = true;
				MarkRenderStateDirty();
			}

			if (!Renderer::Get()) {
				return;
			}

			Renderer::Configure();
			Renderer::ConfigureLighting();
		}

		void SuspendForLooksMenu()
		{
			std::scoped_lock lock(g_stateLock);
			if (g_looksMenuSuspended) {
				return;
			}

			g_looksMenuSuspended = true;
			Renderer::Hide();
			ReleasePreview3DState();
			ClearPreviewRebuildState();
			Morph::Reset();
			Animations::Reset();
		}

		void ResumeAfterLooksMenu()
		{
			std::scoped_lock lock(g_stateLock);
			if (!g_looksMenuSuspended) {
				return;
			}

			g_looksMenuSuspended = false;
			ClearPreviewRebuildState();
		}

		void LogRightHandBoneHierarchy()
		{
			std::scoped_lock lock(g_stateLock);
			LogPreviewRightHandBoneHierarchy();
		}

		FaceGenDebugSnapshot GetFaceGenDebugSnapshot()
		{
			std::scoped_lock lock(g_stateLock);
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return {};
			}

			return BuildFaceGenDebugSnapshot(*player);
		}
	}

}
