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
#include "PreviewFraming.h"
#include "RE/B/BSFlattenedBoneTree.h"
#include "RE/B/BSGeometry.h"
#include "RE/B/BGSMod.h"
#include "RE/B/BGSObjectInstanceExtra.h"
#include "RE/B/BSModelDB.h"
#include "RE/B/BipedAnim.h"
#include "RE/N/NiExtraData.h"
#include "RE/N/NiNode.h"
#include "RE/N/NiStringExtraData.h"
#include "RE/N/NiTransform.h"
#include "RE/N/NiUpdateData.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESNPC.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESModel.h"
#include "RE/T/TESObjectARMO.h"
#include "RE/T/TESObjectANIO.h"
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
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace TF3DHud
{
	namespace
	{
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
		auto& g_bakeChargenMorphs = Address::BakeChargenMorphs;
		auto& g_scaleFaceBones = Address::ScaleFaceBones;
		auto& g_tryAttachMod3DRecurse = Address::TryAttachMod3DRecurse;

		using PreviewRenderTree::PrepareForInterface3DOffscreen;
		using PreviewRenderTree::PreparePreviewTree;
		using PreviewRenderTree::RestorePreviewShaderAlpha;
		using PreviewRenderTree::SanitizePreviewRenderTree;
		using PreviewRenderTree::StripControllerChains;

		struct PreviewAttachment
		{
			RE::BIPED_OBJECT slot{ RE::BIPED_OBJECT::kNone };
			RE::NiPointer<RE::NiAVObject> object;
			RE::NiNode* parent{ nullptr };
			bool preserveOnEquipmentSync{ false };
		};

		struct PreviewAnimObjectAttachment
		{
			RE::BSFixedString editorID;
			RE::NiPointer<RE::NiAVObject> object;
			RE::NiNode* parent{ nullptr };
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
		std::vector<PreviewAnimObjectAttachment> g_previewAnimObjectAttachments;
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

		[[nodiscard]] std::string FormatNiTransform(const RE::NiTransform& a_transform)
		{
			const auto& r = a_transform.rotate;
			char buffer[256]{};
			std::snprintf(
				buffer,
				sizeof(buffer),
				"t(%.3f %.3f %.3f) s(%.3f) r[(%.4f %.4f %.4f)(%.4f %.4f %.4f)(%.4f %.4f %.4f)]",
				a_transform.translate.x,
				a_transform.translate.y,
				a_transform.translate.z,
				a_transform.scale,
				r[0].x,
				r[0].y,
				r[0].z,
				r[1].x,
				r[1].y,
				r[1].z,
				r[2].x,
				r[2].y,
				r[2].z);
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

		[[nodiscard]] const char* HeadPartTypeName(const RE::BGSHeadPart::HeadPartType a_type)
		{
			switch (a_type) {
			case RE::BGSHeadPart::HeadPartType::kMisc:
				return "Misc";
			case RE::BGSHeadPart::HeadPartType::kFace:
				return "Face";
			case RE::BGSHeadPart::HeadPartType::kEyes:
				return "Eyes";
			case RE::BGSHeadPart::HeadPartType::kHair:
				return "Hair";
			case RE::BGSHeadPart::HeadPartType::kFacialHair:
				return "FacialHair";
			case RE::BGSHeadPart::HeadPartType::kScar:
				return "Scar";
			case RE::BGSHeadPart::HeadPartType::kEyebrows:
				return "Eyebrows";
			case RE::BGSHeadPart::HeadPartType::kMeatcaps:
				return "Meatcaps";
			case RE::BGSHeadPart::HeadPartType::kTeeth:
				return "Teeth";
			case RE::BGSHeadPart::HeadPartType::kHeadRear:
				return "HeadRear";
			default:
				return "Unknown";
			}
		}

		[[nodiscard]] std::string FixedStringToString(const RE::BSFixedString& a_value)
		{
			const auto* value = a_value.c_str();
			return value ? value : "";
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

		[[nodiscard]] std::vector<Previewer::FaceGenHeadPartDebugInfo> CaptureUsedHeadParts(RE::PlayerCharacter& a_player)
		{
			std::vector<Previewer::FaceGenHeadPartDebugInfo> entries;
			auto* playerBase = a_player.GetObjectReference();
			auto* npc = playerBase ? playerBase->As<RE::TESNPC>() : nullptr;
			if (!npc) {
				return entries;
			}

			RE::BSScrapArray<RE::BGSHeadPart*> headParts;
			g_generateFlattenedHeadPartArray(npc, headParts);
			entries.reserve(headParts.size());
			for (auto* headPart : headParts) {
				if (!headPart) {
					continue;
				}

				entries.push_back({
					.formID = headPart->GetFormID(),
					.ptr = reinterpret_cast<std::uintptr_t>(headPart),
					.editorID = FixedStringToString(headPart->formEditorID),
					.fullName = headPart->GetFullName() ? headPart->GetFullName() : "",
					.type = HeadPartTypeName(headPart->type.get()),
					.model = headPart->GetModel() ? headPart->GetModel() : "",
				});
			}

			return entries;
		}

		[[nodiscard]] std::vector<Previewer::FaceGenGeometryDebugInfo> CapturePreviewFaceGeometries()
		{
			std::vector<Previewer::FaceGenGeometryDebugInfo> entries;
			if (!g_previewFaceNode) {
				return entries;
			}

			ForEachGeometry(g_previewFaceNode.get(), [&](RE::BSGeometry& a_geometry) {
				entries.push_back({
					.ptr = reinterpret_cast<std::uintptr_t>(std::addressof(a_geometry)),
					.name = FixedStringToString(a_geometry.GetName()),
					.parentPtr = reinterpret_cast<std::uintptr_t>(a_geometry.parent),
					.parentName = a_geometry.parent ? FixedStringToString(a_geometry.parent->GetName()) : "",
				});
			});

			std::ranges::sort(entries, [](const auto& a_lhs, const auto& a_rhs) {
				if (a_lhs.parentName != a_rhs.parentName) {
					return a_lhs.parentName < a_rhs.parentName;
				}
				if (a_lhs.name != a_rhs.name) {
					return a_lhs.name < a_rhs.name;
				}
				return a_lhs.ptr < a_rhs.ptr;
			});
			return entries;
		}

		[[nodiscard]] RE::NiAVObject* GetLiveFaceNode(RE::PlayerCharacter& a_player)
		{
			auto* liveRoot = a_player.Get3D(false);
			if (!liveRoot) {
				liveRoot = a_player.Get3D();
			}
			return liveRoot ? liveRoot->GetObjectByName(RE::BSFixedString("BSFaceGenNiNodeSkinned")) : nullptr;
		}

		void CaptureHairSkinBonesForFaceNode(
			std::vector<Previewer::HairSkinBoneDebugInfo>& a_entries,
			const char* a_source,
			RE::NiAVObject& a_faceNode,
			const RE::BSScrapArray<RE::BGSHeadPart*>& a_headParts)
		{
			for (auto* headPart : a_headParts) {
				if (!headPart || *headPart->type != RE::BGSHeadPart::HeadPartType::kHair) {
					continue;
				}

				const auto headPartName = FixedStringToString(headPart->formEditorID);
				if (headPartName.empty()) {
					continue;
				}

				auto* headPartObject = a_faceNode.GetObjectByName(headPart->formEditorID);
				if (!headPartObject) {
					a_entries.push_back({
						.source = a_source ? a_source : "",
						.headPart = headPartName,
						.geometry = "<headpart object missing>",
					});
					continue;
				}

				ForEachGeometry(headPartObject, [&](RE::BSGeometry& a_geometry) {
					auto* skin = a_geometry.skinInstance.get();
					if (!skin) {
						return;
					}

					for (std::uint32_t index = 0; index < skin->bones.size(); ++index) {
						auto* bone = skin->bones[index];
						a_entries.push_back({
							.source = a_source ? a_source : "",
							.headPart = headPartName,
							.geometry = FixedStringToString(a_geometry.GetName()),
							.index = index,
							.boneName = bone ? FixedStringToString(bone->GetName()) : "<null>",
							.bonePtr = reinterpret_cast<std::uintptr_t>(bone),
							.parentName = bone && bone->parent ? FixedStringToString(bone->parent->GetName()) : "",
							.parentPtr = reinterpret_cast<std::uintptr_t>(bone ? bone->parent : nullptr),
							.local = bone ? FormatNiTransform(bone->GetLocalTransform()) : "",
							.world = bone ? FormatNiTransform(bone->GetWorldTransform()) : "",
						});
					}
				});
			}
		}

		[[nodiscard]] std::vector<Previewer::HairSkinBoneDebugInfo> CaptureHairSkinBones(RE::PlayerCharacter& a_player)
		{
			std::vector<Previewer::HairSkinBoneDebugInfo> entries;
			auto* playerBase = a_player.GetObjectReference();
			auto* npc = playerBase ? playerBase->As<RE::TESNPC>() : nullptr;
			if (!npc) {
				return entries;
			}

			RE::BSScrapArray<RE::BGSHeadPart*> headParts;
			g_generateFlattenedHeadPartArray(npc, headParts);

			if (auto* liveFaceNode = GetLiveFaceNode(a_player)) {
				CaptureHairSkinBonesForFaceNode(entries, "live", *liveFaceNode, headParts);
			}
			if (g_previewFaceNode) {
				CaptureHairSkinBonesForFaceNode(entries, "preview", *g_previewFaceNode, headParts);
			}

			std::ranges::sort(entries, [](const auto& a_lhs, const auto& a_rhs) {
				return std::tie(a_lhs.headPart, a_lhs.geometry, a_lhs.index, a_lhs.source) <
				       std::tie(a_rhs.headPart, a_rhs.geometry, a_rhs.index, a_rhs.source);
			});
			return entries;
		}

		void LogHairSkinBoneDiagnosticsImpl(RE::PlayerCharacter& a_player)
		{
			const auto entries = CaptureHairSkinBones(a_player);
			REX::INFO("Hair skin-bone diagnostics begin: entries={}", entries.size());
			for (const auto& entry : entries) {
				REX::INFO(
					"Hair skin-bone: source='{}' headPart='{}' geometry='{}' index={} bone='{}' ptr={:X} parent='{}' parentPtr={:X} local={} world={}",
					entry.source,
					entry.headPart,
					entry.geometry,
					entry.index,
					entry.boneName,
					entry.bonePtr,
					entry.parentName.empty() ? "<null>" : entry.parentName.c_str(),
					entry.parentPtr,
					entry.local,
					entry.world);
			}
			REX::INFO("Hair skin-bone diagnostics end: entries={}", entries.size());
		}

		[[nodiscard]] Previewer::FaceGenDebugSnapshot BuildFaceGenDebugSnapshot(RE::PlayerCharacter& a_player)
		{
			Previewer::FaceGenDebugSnapshot snapshot;
			snapshot.headParts = CaptureUsedHeadParts(a_player);
			snapshot.geometries = CapturePreviewFaceGeometries();
			snapshot.hairSkinBones = CaptureHairSkinBones(a_player);
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

		[[nodiscard]] bool HasGeometryDescendant(RE::NiAVObject& a_root)
		{
			bool hasGeometry = false;
			ForEachGeometry(std::addressof(a_root), [&](RE::BSGeometry&) {
				hasGeometry = true;
			});
			return hasGeometry;
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
			auto* flattened = g_previewFlattenedBoneTree;
			if (!flattened || !IsDescendantOf(*flattened, a_previewRoot)) {
				flattened = FindFlattenedBoneTree(std::addressof(a_previewRoot));
				g_previewFlattenedBoneTree = flattened;
			}

			if (flattened) {
				if (auto* bone = FindFlattenedBoneByName(*flattened, a_name)) {
					if (auto* node = bone->node.get(); node && IsDescendantOf(*node, a_previewRoot)) {
						return node->IsNode();
					}
				}
			}

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

		[[nodiscard]] const char* GetRaceChargenSkeletonModelPath(RE::TESRace& a_race, const std::uint32_t a_sex)
		{
			const auto sex = std::min<std::uint32_t>(a_sex, 1);
			if (const auto* model = a_race.skeletonChargenModel[sex].GetModel(); model && model[0] != '\0') {
				return model;
			}
			// IDA TESRace::GetModel(sex, chargen=true) falls back to normal
			// male skeletonModel[0], not the other sex's chargen skeleton.
			if (const auto* model = a_race.skeletonModel[0].GetModel(); model && model[0] != '\0') {
				return model;
			}
			return nullptr;
		}

		[[nodiscard]] bool ConvertPreviewSkeletonToFlattenedTree(RE::PlayerCharacter& a_player, RE::NiAVObject& a_skeletonRoot)
		{
			constexpr auto kFlattenedSkeletonBodyPart = RE::BGSBodyPartDefs::LIMB_ENUM::kRoot;
			auto* convertTarget = g_getActorBodyPart3D(
				std::addressof(a_player),
				std::addressof(a_skeletonRoot),
				std::addressof(kFlattenedSkeletonBodyPart),
				false);
			if (!convertTarget) {
				REX::WARN(
					"preview race skeleton flatten failed: body-part lookup returned null; root={:X}, rootName='{}', bodyPart={}",
					reinterpret_cast<std::uintptr_t>(std::addressof(a_skeletonRoot)),
					a_skeletonRoot.GetName(),
					std::to_underlying(kFlattenedSkeletonBodyPart));
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

			auto trustedTopologyRoot = PreviewClone::CloneObject(*loadedRoot);
			if (!trustedTopologyRoot) {
				REX::WARN("preview race skeleton trusted-topology clone failed: path='{}'", skeletonPath);
				LogDiagnostic("preview race skeleton trusted-topology clone failed");
				return nullptr;
			}

			if (!ConvertPreviewSkeletonToFlattenedTree(a_player, *previewRoot)) {
				LogDiagnostic("preview race skeleton flatten failed");
				return nullptr;
			}

			Morph::CaptureTrustedSkeleton(*trustedTopologyRoot, nullptr);
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
			RE::NiAVObject& a_previewRoot,
			std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes,
			const RE::BIPED_OBJECT a_ownerSlot = RE::BIPED_OBJECT::kNone,
			const bool a_preserveOnEquipmentSync = false)
		{
			auto* flattened = FindFlattenedBoneTree(std::addressof(a_previewRoot));
			if (!flattened) {
				REX::WARN("attachment skin-bone merge skipped: preview BSFlattenedBoneTree not found");
				return;
			}
			g_previewFlattenedBoneTree = flattened;

			ForEachGeometry(std::addressof(a_attachmentRoot), [&](RE::BSGeometry& a_geometry) {
				auto* skin = a_geometry.skinInstance.get();
				if (!skin || skin->bones.size() > RE::BSSkin::kMaxExpectedBones) {
					return;
				}
				if (!skin->worldTransforms.empty() && skin->worldTransforms.size() != skin->bones.size()) {
					return;
				}

				for (std::uint32_t index = 0; index < skin->bones.size(); ++index) {
					auto* bone = skin->bones[index];
					if (!bone) {
						continue;
					}
					const auto* boneName = bone->GetName().c_str();
					if (!boneName || boneName[0] == '\0') {
						continue;
					}

					const RE::BSFixedString fixedBoneName(boneName);
					auto* previewBone = flattened->GetObjectByName(fixedBoneName);
					if (!previewBone) {
						auto previewClone = PreviewClone::CloneObject(*bone);
						if (!previewClone) {
							continue;
						}

						StripControllerChains(*previewClone);
						StripClonedGeometry(*previewClone);
						RE::bhkWorld::RemoveObjects(previewClone.get(), true, true);
						PreparePreviewTree(*previewClone);

						flattened->AttachChild(previewClone.get(), false);
						g_previewAttachments.push_back({
							.slot = a_ownerSlot,
							.object = previewClone,
							.parent = flattened,
							.preserveOnEquipmentSync = a_preserveOnEquipmentSync,
						});

						g_createBoneMap(flattened);
						CollectPreviewSkinTargetNodes(a_previewRoot, a_previewNodes);
						previewBone = flattened->GetObjectByName(fixedBoneName);
						if (!previewBone) {
							previewBone = previewClone.get();
						}
					}

					if (previewBone && skin->bones[index] != previewBone) {
						skin->bones[index] = previewBone;
						if (!skin->worldTransforms.empty()) {
							skin->worldTransforms[index] = std::addressof(previewBone->world);
						}
					}
				}
			});

			RefreshPreviewBoneLookup(a_previewRoot);
			CollectPreviewSkinTargetNodes(a_previewRoot, a_previewNodes);
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

		void RetirePreviewAnimObjectAttachments(const bool a_detach)
		{
			for (auto& attachment : g_previewAnimObjectAttachments) {
				if (a_detach && attachment.parent && attachment.object) {
					attachment.parent->DetachChild(attachment.object.get());
				}
				if (attachment.object) {
					RetirePreviewObject(std::move(attachment.object));
				}
			}
			g_previewAnimObjectAttachments.clear();
		}

		[[nodiscard]] std::vector<PreviewAnimObjectAttachment>::iterator FindPreviewAnimObjectAttachment(
			const RE::BSFixedString& a_editorID)
		{
			return std::ranges::find_if(
				g_previewAnimObjectAttachments,
				[&](const PreviewAnimObjectAttachment& a_attachment) {
					return a_attachment.editorID == a_editorID;
				});
		}

		void DetachPreviewAnimObject(const RE::BSFixedString& a_editorID)
		{
			if (a_editorID.empty()) {
				RetirePreviewAnimObjectAttachments(true);
				return;
			}

			for (auto it = g_previewAnimObjectAttachments.begin(); it != g_previewAnimObjectAttachments.end();) {
				if (it->editorID != a_editorID) {
					++it;
					continue;
				}

				if (it->parent && it->object) {
					it->parent->DetachChild(it->object.get());
				}
				if (it->object) {
					RetirePreviewObject(std::move(it->object));
				}
				it = g_previewAnimObjectAttachments.erase(it);
			}

			if (g_previewRoot) {
				RE::NiUpdateData updateData;
				g_previewRoot->Update(updateData);
			}
		}

		[[nodiscard]] RE::NiNode* ResolvePreviewAnimObjectParent(
			RE::NiAVObject& a_animObject,
			RE::NiAVObject& a_previewRoot,
			std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			// IDA OG 1.10.163: AnimationObjects::Loaded passes BIPED_OBJECT -1 to
			// BipedAnim::AttachToParent; that path resolves the parent from the
			// model's NiStringExtraData "Prn" and skips weapon-slot fallbacks.
			auto* prn = FindStringExtraDataInTree(a_animObject, RE::BSFixedString("Prn"));
			if (!prn) {
				return nullptr;
			}

			const auto prnValue = prn->GetValue();
			if (prnValue.empty()) {
				return nullptr;
			}

			return FindPreviewNodeByName(a_previewRoot, a_previewNodes, prnValue);
		}

		void UncullPreviewAnimObjectAttachPath(
			RE::NiNode& a_parent,
			RE::NiAVObject& a_previewRoot)
		{
			// IDA OG 1.10.163: BipedAnim::AttachToParent unculls the resolved
			// attach parent and walks upward before attaching animation objects.
			for (auto* object = static_cast<RE::NiAVObject*>(std::addressof(a_parent));
				 object;
				 object = object != std::addressof(a_previewRoot) ? object->parent : nullptr) {
				if (object->GetAppCulled()) {
					object->SetAppCulled(false);
				}
				if (object->fadeAmount <= 0.0F) {
					object->fadeAmount = 1.0F;
				}
				if (object == std::addressof(a_previewRoot)) {
					break;
				}
			}
		}

		void SetPreviewAnimObjectVisible(
			PreviewAnimObjectAttachment& a_attachment,
			const bool a_visible,
			RE::NiAVObject& a_previewRoot)
		{
			if (a_attachment.parent) {
				UncullPreviewAnimObjectAttachPath(*a_attachment.parent, a_previewRoot);
			}

			if (a_attachment.object) {
				a_attachment.object->SetAppCulled(!a_visible);
			}
		}

		void AttachPreviewAnimObject(const RE::BSFixedString& a_editorID, const bool a_initiallyVisible)
		{
			const auto editorID = FixedStringToString(a_editorID);
			if (a_editorID.empty()) {
				return;
			}

			if (!g_previewRoot) {
				return;
			}

			if (auto existing = FindPreviewAnimObjectAttachment(a_editorID);
				existing != g_previewAnimObjectAttachments.end()) {
				SetPreviewAnimObjectVisible(*existing, a_initiallyVisible, *g_previewRoot);
				RE::NiUpdateData updateData;
				if (existing->object) {
					existing->object->Update(updateData);
				}
				g_previewRoot->Update(updateData);
				MarkRenderStateDirty();
				return;
			}

			auto* animObject = RE::TESForm::GetFormByEditorID<RE::TESObjectANIO>(a_editorID);
			if (!animObject) {
				LogDiagnostic(
					std::string{ "anim object attach skipped: ANIO not found for '" } +
					editorID + "'");
				return;
			}

			const auto* modelPath = static_cast<RE::BGSModelMaterialSwap*>(animObject)->GetModel();
			if (!modelPath || modelPath[0] == '\0') {
				LogDiagnostic(
					std::string{ "anim object attach skipped: ANIO has empty model for '" } +
					editorID + "'");
				return;
			}

			auto previewObject = LoadPreviewModel(modelPath);
			if (!previewObject) {
				LogDiagnostic(
					std::string{ "anim object attach skipped: model load failed for '" } +
					editorID + "' path='" + modelPath + "'");
				return;
			}

			std::unordered_map<std::string, RE::NiAVObject*> previewNodes;
			CollectPreviewSkinTargetNodes(*g_previewRoot, previewNodes);
			auto* parent = ResolvePreviewAnimObjectParent(*previewObject, *g_previewRoot, previewNodes);
			if (!parent) {
				LogDiagnostic(
					std::string{ "anim object attach skipped: Prn parent not found for '" } +
					editorID + "' path='" + modelPath + "'");
				RetirePreviewObject(std::move(previewObject));
				return;
			}
			UncullPreviewAnimObjectAttachPath(*parent, *g_previewRoot);

			RE::bhkWorld::RemoveObjects(previewObject.get(), true, true);
			StripControllerChains(*previewObject);
			PreparePreviewTree(*previewObject);
			SanitizePreviewRenderTree(*previewObject);
			PrepareForInterface3DOffscreen(*previewObject);
			previewObject->SetAppCulled(!a_initiallyVisible);

			parent->AttachChild(previewObject.get(), true);
			g_previewAnimObjectAttachments.push_back({
				.editorID = a_editorID,
				.object = previewObject,
				.parent = parent,
			});
			SetPreviewAnimObjectVisible(g_previewAnimObjectAttachments.back(), a_initiallyVisible, *g_previewRoot);

			RE::NiUpdateData updateData;
			previewObject->Update(updateData);
			g_previewRoot->Update(updateData);
			MarkRenderStateDirty();
		}

		void HandleAnimationObjectEventImpl(const RE::BSFixedString& a_tag, const RE::BSFixedString& a_payload)
		{
			// IDA OG 1.10.163: AnimationObjects::Loaded attaches on load and
			// app-culls the root until the animation sends its draw event.
			if (a_tag == std::string_view{ "AnimObjDraw" }) {
				AttachPreviewAnimObject(a_payload, true);
			} else if (a_tag == std::string_view{ "AnimObjLoad" }) {
				AttachPreviewAnimObject(a_payload, false);
			} else if (a_tag == std::string_view{ "AnimObjUnequip" }) {
				DetachPreviewAnimObject(a_payload);
				MarkRenderStateDirty();
			}
		}

		void ResetPreview3DState(const bool a_disableRenderer)
		{
			Renderer::ClearPreviewRoot(a_disableRenderer);
			RetirePreviewAttachments(false);
			RetirePreviewAnimObjectAttachments(false);
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

		void ReplayEngineWeaponPostAttachLayout(
			const RE::BIPED_OBJECT a_slot,
			RE::NiAVObject& a_previewClone,
			RE::NiNode& a_parent,
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
			// BipedAnim::AttachToParent resolves named parents from the current
			// flattened bone tree, so rebuild it after removing old preview parts.
			RefreshPreviewBoneLookup(a_previewRoot);

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

				const auto isWeaponAttach = IsEngineWeaponAttachSlot(a_slot);
				auto* parent = FindPreviewAttachParent(a_slot, a_sourceObject, *sourceClone, a_previewRoot, previewNodes);
				if (!parent) {
					RetirePreviewObject(std::move(previewClone));
					return;
				}

				parent->AttachChild(previewClone.get(), false);
				if (!isWeaponAttach) {
					(void)ReplayObjectInstanceMod3D(a_sourceObject, *previewClone);
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
				// Weapon partClone already contains the engine-assembled weapon
				// hierarchy. Do not clone external weapon bones back into the
				// preview skeleton, or name lookup can select a duplicate Weapon.
				if (!isWeaponAttach) {
					MergeAttachmentSkinBones(
						*previewClone,
						a_previewRoot,
						previewNodes);
				}

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
					a_slot,
					*previewClone,
					*parent,
					a_previewRoot,
					previewNodes);

				g_previewAttachments.push_back({
					.slot = a_slot,
					.object = previewClone,
					.parent = parent,
				});
				CollectNamedNodes(g_previewAttachments.back().object.get(), previewNodes);
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

		void AttachAndRebindHeadToRoot(
			RE::BSFaceGenNiNode& a_faceNode,
			RE::NiNode& a_actorRootNode)
		{
			a_actorRootNode.AttachChild(std::addressof(a_faceNode), false);
			FixPreviewFaceGenSkinInstances(a_faceNode, a_actorRootNode);

			RE::NiUpdateData updateData;
			a_actorRootNode.Update(updateData);
		}

		[[nodiscard]] RE::NiPointer<RE::BSFaceGenNiNode> CreatePreviewHeadWithChargenBake(
			RE::TESNPC& a_npc,
			RE::TESRace& a_previewRace,
			const RE::SEX a_sex)
		{
			RE::NiPointer<RE::BSFaceGenNiNode> finalHead;
			if (!g_createHeadForNPC(std::addressof(a_npc), finalHead, false, true, nullptr) || !finalHead) {
				return nullptr;
			}

			const auto* chargenSkeletonPath =
				GetRaceChargenSkeletonModelPath(a_previewRace, std::to_underlying(a_sex));
			if (!chargenSkeletonPath) {
				REX::WARN("preview head chargen bake skipped: race chargen skeleton path is empty");
				return finalHead;
			}

			auto chargenSkeleton = LoadPreviewModel(chargenSkeletonPath);
			if (!chargenSkeleton) {
				REX::WARN(
					"preview head chargen bake skipped: failed to load race chargen skeleton '{}'",
					chargenSkeletonPath);
				return finalHead;
			}

			RE::NiPointer<RE::BSFaceGenNiNode> tempHead;
			if (!g_createHeadForNPC(std::addressof(a_npc), tempHead, true, false, nullptr) || !tempHead) {
				REX::WARN("preview head chargen bake skipped: temporary chargen head creation failed");
				return finalHead;
			}

			RE::bhkWorld::RemoveObjects(chargenSkeleton.get(), true, true);
			StripControllerChains(*chargenSkeleton);
			PreparePreviewTree(*chargenSkeleton);
			RE::bhkWorld::RemoveObjects(tempHead.get(), true, true);
			PreparePreviewTree(*tempHead);

			auto* chargenRootNode = chargenSkeleton->IsNode();
			if (!chargenRootNode) {
				REX::WARN(
					"preview head chargen bake skipped: chargen skeleton root is not NiNode, path='{}'",
					chargenSkeletonPath);
				return finalHead;
			}

			// IDA 0x14067ADC0: temporary actor loads race->skeletonChargenModel,
			// attaches a temporary head, scales face bones, updates, then bakes
			// that result into the pending final head before final actor attach.
			AttachAndRebindHeadToRoot(*tempHead, *chargenRootNode);
			g_scaleFaceBones(std::addressof(a_npc), chargenSkeleton.get(), true);
			RE::NiUpdateData updateData;
			chargenSkeleton->Update(updateData);
			g_bakeChargenMorphs(
				std::addressof(a_npc),
				static_cast<RE::NiNode*>(finalHead.get()),
				static_cast<RE::NiNode*>(tempHead.get()),
				nullptr);

			return finalHead;
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

			auto* previewRace = ResolvePreviewRace(a_player, *npc);
			if (!previewRace) {
				LogDiagnostic("preview head creation failed: preview race is null");
				return false;
			}

			auto faceNode = CreatePreviewHeadWithChargenBake(*npc, *previewRace, npc->GetSex());
			if (!faceNode) {
				LogDiagnostic("preview head creation failed: CreateHeadForNPC returned null");
				return false;
			}

			RE::bhkWorld::RemoveObjects(faceNode.get(), true, true);
			PreparePreviewTree(*faceNode);

			PreviewHeadParts::ApplyBipedVisibility(*npc, *previewRace, *faceNode, a_player, a_sourceBiped);

			auto* actor3DRoot = a_previewRoot.IsNode();
			if (!actor3DRoot) {
				LogDiagnostic("preview head creation failed: preview actor 3D root is not a NiNode");
				return false;
			}

			AttachAndRebindHeadToRoot(*faceNode, *actor3DRoot);
			g_previewFaceNode = faceNode;
			RefreshPreviewBoneLookup(a_previewRoot);

			std::unordered_map<std::string, RE::NiAVObject*> previewNodes;
			CollectPreviewSkinTargetNodes(a_previewRoot, previewNodes);
			// IDA AttachHeadHelper runs the hair cloth pass with actor3D as the
			// cloth target. Ensure preview-only headpart bones exist there first.
			MergeAttachmentSkinBones(
				*faceNode,
				a_previewRoot,
				previewNodes,
				RE::BIPED_OBJECT::kFaceGenHead,
				true);
			PreviewCloth::InitializeHeadParts(*npc, a_player, *faceNode, a_previewRoot);

			RE::NiUpdateData updateData;
			actor3DRoot->Update(updateData);
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

		bool RebuildPreview(
			RE::PlayerCharacter& a_player,
			const RE::BipedAnim& a_biped,
			std::uint64_t a_bipedSignature,
			std::uint64_t a_visualSignature)
		{
			// Rebuilds run at the Interface3D commit boundary. Keep the active
			// renderer-owned tree intact until a full replacement is ready.
			auto previousAttachments = std::move(g_previewAttachments);
			auto previousAnimObjectAttachments = std::move(g_previewAnimObjectAttachments);
			auto previousFaceNode = std::move(g_previewFaceNode);
			auto* previousFlattenedBoneTree = g_previewFlattenedBoneTree;
			g_previewAttachments.clear();
			g_previewAnimObjectAttachments.clear();
			g_previewFaceNode.reset();
			g_previewFlattenedBoneTree = nullptr;

			auto restorePreviousPreviewMetadata = [&]() {
				g_previewAttachments.clear();
				g_previewAnimObjectAttachments.clear();
				g_previewFaceNode.reset();
				g_previewAttachments = std::move(previousAttachments);
				g_previewAnimObjectAttachments = std::move(previousAnimObjectAttachments);
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
			PreviewFraming::ApplyTargetCentered(*previewRoot, g_previewFlattenedBoneTree);
			PrepareForInterface3DOffscreen(*previewRoot);

			// Do not detach children from the old root here. DrawModel can have
			// stale traversal work after Offscreen_Set3D is updated.
			if (g_previewRoot) {
				RetirePreviewObject(std::move(g_previewRoot));
			}
			g_previewRoot = previewRoot;
			const auto faceCustomizationSignature = PreviewRebuilder::BuildFaceCustomizationSignature(a_player);
			g_rebuilder.CommitSkeletonBuild(
				a_bipedSignature,
				a_visualSignature,
				PreviewRebuilder::BuildMorphGeometrySignature(a_player),
				faceCustomizationSignature);

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
			const std::uint64_t a_morphGeometrySignature)
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
			PreviewFraming::ApplyTargetCentered(*g_previewRoot, g_previewFlattenedBoneTree);
			PrepareForInterface3DOffscreen(*g_previewRoot);

			g_rebuilder.CommitEquipmentLayer(
				a_equipmentSignature,
				a_morphGeometrySignature != 0 ? a_morphGeometrySignature : PreviewRebuilder::BuildMorphGeometrySignature(a_player));
			g_rebuilder.RequestSkeletonAdjustment();
			g_rebuilder.RequestBehaviorGraphRefresh();
			g_pendingRendererAttach = true;
			g_pendingFramingUpdate = false;
			MarkRenderStateDirty();

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
				PreviewFraming::ApplyTargetCentered(*g_previewRoot, g_previewFlattenedBoneTree);
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
							PreviewRebuilder::BuildMorphGeometrySignature(*player))) {
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
							morphGeometrySignature)) {
						return;
					}
					structuralCommandRan = true;
				}
			}

			if (!structuralCommandRan && g_postRebuildAdjustmentHoldFrames == 0) {
				std::uint64_t faceCustomizationSignature = 0;
				if (TryResolveFaceCustomizationSignature(*player, faceCustomizationSignature)) {
					std::uint64_t bipedSignature = 0;
					if (tryRebuildPreview(
							bipedSignature,
							PreviewRebuilder::BuildVisualSignature(*player))) {
						structuralCommandRan = true;
					}
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
			PreviewFraming::ApplyTargetFollowTranslation(*g_previewRoot, g_previewFlattenedBoneTree);

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

		void HandleAnimationObjectEvent(const RE::BSFixedString& a_tag, const RE::BSFixedString& a_payload)
		{
			std::scoped_lock lock(g_stateLock);
			HandleAnimationObjectEventImpl(a_tag, a_payload);
		}

		void ClearAnimationObjects()
		{
			std::scoped_lock lock(g_stateLock);
			RetirePreviewAnimObjectAttachments(true);
			if (g_previewRoot) {
				RE::NiUpdateData updateData;
				g_previewRoot->Update(updateData);
				MarkRenderStateDirty();
			}
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

		FaceGenDebugSnapshot GetFaceGenDebugSnapshot()
		{
			std::scoped_lock lock(g_stateLock);
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return {};
			}

			return BuildFaceGenDebugSnapshot(*player);
		}

		void LogHairSkinBoneDiagnostics()
		{
			std::scoped_lock lock(g_stateLock);
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				REX::WARN("Hair skin-bone diagnostics skipped: player is null");
				return;
			}

			LogHairSkinBoneDiagnosticsImpl(*player);
		}
	}

}
