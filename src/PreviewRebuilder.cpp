#include "Address.h"
#include "PreviewRebuilder.h"

#include "Utils.h"

#include "RE/B/BipedAnim.h"
#include "RE/B/BSFixedString.h"
#include "RE/B/BSGeometry.h"
#include "RE/N/NiAVObject.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESNPC.h"
#include "RE/T/TESObjectARMO.h"

#include <algorithm>
#include <bit>
#include <utility>
#include <vector>

namespace TF3DHud::PreviewRebuilder
{
	namespace
	{
		constexpr std::uint32_t kEquipmentAuditFrames = 300;
		constexpr std::uint32_t kEquipmentQuietFrames = 15;
		constexpr std::uint32_t kMorphGeometryAuditFrames = 300;
		constexpr std::uint32_t kMorphGeometryQuietFrames = 15;
		constexpr std::uint32_t kEquipmentSignatureStableFrames = 3;
		constexpr std::uint32_t kMorphSignatureStableFrames = 3;
		constexpr std::uint32_t kFaceCustomizationQuietFrames = 3;
		constexpr std::uint32_t kFaceCustomizationSignatureStableFrames = 3;

		constexpr std::uint16_t kUpdateFlagHeadModel = 0x000C;
		constexpr std::uint16_t kUpdateFlagScaleSkinBones = 0x0010;
		constexpr std::uint16_t kUpdateFlagFull3D = 0x0020;

		[[nodiscard]] constexpr std::uint32_t ToMask(const DirtyFlag a_flag)
		{
			return static_cast<std::uint32_t>(a_flag);
		}

		auto& g_getSkin = Address::GetSkin;

		void HashInteger(std::uint64_t& a_hash, const std::uintptr_t a_value)
		{
			HashValue(a_hash, reinterpret_cast<void*>(a_value));
		}

		void HashFloat(std::uint64_t& a_hash, const float a_value)
		{
			HashInteger(a_hash, std::bit_cast<std::uint32_t>(a_value));
		}

		void HashTransform(std::uint64_t& a_hash, const RE::BGSCharacterMorph::Transform& a_transform)
		{
			HashFloat(a_hash, a_transform.position.x);
			HashFloat(a_hash, a_transform.position.y);
			HashFloat(a_hash, a_transform.position.z);
			HashFloat(a_hash, a_transform.rotation.x);
			HashFloat(a_hash, a_transform.rotation.y);
			HashFloat(a_hash, a_transform.rotation.z);
			HashFloat(a_hash, a_transform.scale.x);
			HashFloat(a_hash, a_transform.scale.y);
			HashFloat(a_hash, a_transform.scale.z);
		}

		void HashMorphRegionSliderValues(std::uint64_t& a_hash, const RE::BSTArray<float>* a_values)
		{
			HashValue(a_hash, a_values);
			if (!a_values) {
				return;
			}

			HashInteger(a_hash, a_values->size());
			for (const float value : *a_values) {
				HashFloat(a_hash, value);
			}
		}

		void HashFacialBoneRegionSliderValues(
			std::uint64_t& a_hash,
			const RE::BSTHashMap<std::uint32_t, RE::BGSCharacterMorph::Transform>* a_values)
		{
			HashValue(a_hash, a_values);
			if (!a_values) {
				return;
			}

			std::vector<std::pair<std::uint32_t, RE::BGSCharacterMorph::Transform>> entries;
			entries.reserve(a_values->size());
			for (const auto& entry : *a_values) {
				entries.emplace_back(entry.first, entry.second);
			}
			std::ranges::sort(entries, {}, &std::pair<std::uint32_t, RE::BGSCharacterMorph::Transform>::first);

			HashInteger(a_hash, entries.size());
			for (const auto& [key, transform] : entries) {
				HashInteger(a_hash, key);
				HashTransform(a_hash, transform);
			}
		}

		void HashMorphSliderValues(
			std::uint64_t& a_hash,
			const RE::BSTHashMap<std::uint32_t, float>* a_values)
		{
			HashValue(a_hash, a_values);
			if (!a_values) {
				return;
			}

			std::vector<std::pair<std::uint32_t, float>> entries;
			entries.reserve(a_values->size());
			for (const auto& entry : *a_values) {
				entries.emplace_back(entry.first, entry.second);
			}
			std::ranges::sort(entries, {}, &std::pair<std::uint32_t, float>::first);

			HashInteger(a_hash, entries.size());
			for (const auto& [key, value] : entries) {
				HashInteger(a_hash, key);
				HashFloat(a_hash, value);
			}
		}

		void HashTintingData(std::uint64_t& a_hash, const RE::BGSCharacterTint::Entries* a_tintingData)
		{
			HashValue(a_hash, a_tintingData);
			if (!a_tintingData) {
				return;
			}

			HashInteger(a_hash, a_tintingData->entriesA.size());
			for (auto* entry : a_tintingData->entriesA) {
				HashValue(a_hash, entry);
				if (!entry) {
					continue;
				}

				HashValue(a_hash, entry->templateEntry);
				HashInteger(a_hash, entry->idLink);
				HashInteger(a_hash, entry->tingingValue);

				const auto type = entry->GetType();
				HashInteger(a_hash, static_cast<std::uintptr_t>(type));
				if (type == RE::BGSCharacterTint::EntryType::kPalette) {
					const auto* paletteEntry = static_cast<const RE::BGSCharacterTint::PaletteEntry*>(entry);
					HashInteger(a_hash, paletteEntry->tintingColor);
					HashInteger(a_hash, paletteEntry->swatchID);
				}
			}
		}

		void HashNPCFaceCustomization(std::uint64_t& a_hash, const RE::TESNPC& a_npc)
		{
			HashValue(a_hash, std::addressof(a_npc));
			HashInteger(a_hash, static_cast<std::uint8_t>(a_npc.bodyTintColorR));
			HashInteger(a_hash, static_cast<std::uint8_t>(a_npc.bodyTintColorG));
			HashInteger(a_hash, static_cast<std::uint8_t>(a_npc.bodyTintColorB));
			HashInteger(a_hash, static_cast<std::uint8_t>(a_npc.bodyTintColorA));
			HashFloat(a_hash, a_npc.morphWeight.x);
			HashFloat(a_hash, a_npc.morphWeight.y);
			HashFloat(a_hash, a_npc.morphWeight.z);
			HashMorphRegionSliderValues(a_hash, a_npc.morphRegionSliderValues);
			HashMorphSliderValues(a_hash, a_npc.morphSliderValues);
			HashTintingData(a_hash, a_npc.tintingData);
		}

		void HashNPCFacialBoneRegions(std::uint64_t& a_hash, const RE::TESNPC& a_npc)
		{
			HashValue(a_hash, std::addressof(a_npc));
			HashFacialBoneRegionSliderValues(a_hash, a_npc.facialBoneRegionSliderValues);
		}

		struct GeometryDataView
		{
			struct VertexData
			{
				void* d3d11Buffer;
				void* vertexBlock;
			};

			std::uint64_t vertexDesc;
			VertexData* vertexData;
			void* triangleData;
		};
	}

	RE::NiAVObject* GetSourceFaceNode(RE::Actor& a_sourceActor)
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

	std::uint64_t BuildVisualSignature(RE::PlayerCharacter& a_player)
	{
		std::uint64_t hash = 1469598103934665603ull;

		auto* playerBase = a_player.GetObjectReference();
		auto* npc = playerBase ? playerBase->As<RE::TESNPC>() : nullptr;
		HashValue(hash, npc);
		HashValue(hash, npc ? npc->GetRootFaceNPC() : nullptr);
		HashValue(hash, a_player.GetVisualsRace());
		HashValue(hash, a_player.charGenRace);
		HashValue(hash, GetSourceFaceNode(a_player));
		HashInteger(hash, static_cast<std::uintptr_t>(a_player.GetSex()));

		if (npc) {
			HashValue(hash, npc->headRelatedData);
			HashValue(hash, npc->originalRace);
			HashValue(hash, npc->faceNPC);
			HashValue(hash, g_getSkin(npc));
			HashValue(hash, npc->headParts);
			HashNPCFacialBoneRegions(hash, *npc);
			if (auto* rootFaceNPC = npc->GetRootFaceNPC(); rootFaceNPC && rootFaceNPC != npc) {
				HashNPCFacialBoneRegions(hash, *rootFaceNPC);
			}
			HashInteger(hash, static_cast<std::uintptr_t>(npc->GetSex()));
			HashInteger(hash, static_cast<std::uintptr_t>(npc->numHeadParts));
			for (std::int32_t i = 0; i < npc->numHeadParts && npc->headParts; ++i) {
				HashValue(hash, npc->headParts[i]);
			}
		}

		return hash;
	}

	std::uint64_t BuildFaceCustomizationSignature(RE::PlayerCharacter& a_player)
	{
		std::uint64_t hash = 1469598103934665603ull;

		auto* playerBase = a_player.GetObjectReference();
		auto* npc = playerBase ? playerBase->As<RE::TESNPC>() : nullptr;
		HashValue(hash, npc);
		HashValue(hash, npc ? npc->GetRootFaceNPC() : nullptr);
		HashValue(hash, a_player.complexion);
		HashTintingData(hash, a_player.tintingData);

		if (npc) {
			HashNPCFaceCustomization(hash, *npc);
			if (auto* rootFaceNPC = npc->GetRootFaceNPC(); rootFaceNPC && rootFaceNPC != npc) {
				HashNPCFaceCustomization(hash, *rootFaceNPC);
			}
		}

		return hash;
	}

	std::uint64_t BuildEquipmentSignature(const RE::BipedAnim* a_biped)
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
			HashValue(hash, reinterpret_cast<void*>(static_cast<std::uintptr_t>(object.skinned)));
		}

		return hash;
	}

	std::uint64_t BuildMorphGeometrySignature(RE::PlayerCharacter& a_player)
	{
		auto* root = a_player.Get3D(false);
		if (!root) {
			return 0;
		}

		std::uint64_t hash = 1469598103934665603ull;
		std::uint32_t geometryCount = 0;
		ForEachGeometry(root, [&](RE::BSGeometry& a_geometry) {
			++geometryCount;
			HashValue(hash, std::addressof(a_geometry));
			HashValue(hash, a_geometry.parent);
			HashValue(hash, a_geometry.skinInstance.get());
			HashInteger(hash, a_geometry.vertexDesc.desc);
			HashInteger(hash, a_geometry.type);

			if (a_geometry.IsTriShape()) {
				auto* geometryData = static_cast<GeometryDataView*>(a_geometry.rendererData);
				HashValue(hash, geometryData);
				if (geometryData) {
					HashValue(hash, geometryData->vertexData);
					HashValue(hash, geometryData->vertexData ? geometryData->vertexData->vertexBlock : nullptr);
				}
			}
		});
		HashInteger(hash, geometryCount);
		return hash;
	}

	void Controller::Request(const DirtyFlag a_flag)
	{
		requestedDirty_.fetch_or(ToMask(a_flag), std::memory_order_acq_rel);
	}

	void Controller::ObserveEquipment()
	{
		Request(DirtyFlag::kEquipment);
		Request(DirtyFlag::kSkeletonAdjust);
	}

	void Controller::ObserveUpdate3DModel(const std::uint16_t a_updateFlags, const bool a_updateEditorDeadModel)
	{
		if (a_updateFlags == 0 && !a_updateEditorDeadModel) {
			return;
		}

		Request(DirtyFlag::kSkeletonAdjust);

		if ((a_updateFlags & kUpdateFlagScaleSkinBones) != 0) {
			Request(DirtyFlag::kSkeletonAdjust);
		}
		if ((a_updateFlags & kUpdateFlagHeadModel) != 0 || a_updateEditorDeadModel) {
			Request(DirtyFlag::kFaceStructure);
			Request(DirtyFlag::kFaceCustomization);
			Request(DirtyFlag::kMorphGeometry);
		}
		if ((a_updateFlags & kUpdateFlagFull3D) != 0) {
			Request(DirtyFlag::kEquipment);
			Request(DirtyFlag::kMorphGeometry);
		}

		Request(DirtyFlag::kMorphGeometry);
	}

	void Controller::BeginRequestedAudits()
	{
		const auto requested = requestedDirty_.exchange(0, std::memory_order_acq_rel);
		if ((requested & ToMask(DirtyFlag::kEquipment)) != 0) {
			equipmentAuditFrames_ = std::max(equipmentAuditFrames_, kEquipmentAuditFrames);
			equipmentQuietFrames_ = kEquipmentQuietFrames;
			equipmentAuditActive_.store(true, std::memory_order_release);
			pendingEquipmentSignature_ = 0;
			pendingEquipmentSignatureFrames_ = 0;
			morphQuietFrames_ = kMorphGeometryQuietFrames;
			pendingMorphSignature_ = 0;
			pendingMorphSignatureFrames_ = 0;
		}
		if ((requested & ToMask(DirtyFlag::kFaceStructure)) != 0) {
			committedVisualSignature_ = 0;
			faceCustomizationQuietFrames_ = kFaceCustomizationQuietFrames;
			pendingFaceCustomizationSignature_ = 0;
			pendingFaceCustomizationSignatureFrames_ = 0;
		}
		if ((requested & ToMask(DirtyFlag::kMorphGeometry)) != 0) {
			morphAuditFrames_ = std::max(morphAuditFrames_, kMorphGeometryAuditFrames);
			morphQuietFrames_ = kMorphGeometryQuietFrames;
			pendingMorphSignature_ = 0;
			pendingMorphSignatureFrames_ = 0;
		}
		if ((requested & ToMask(DirtyFlag::kSkeletonAdjust)) != 0) {
			skeletonAdjustmentRequested_ = true;
		}
		if ((requested & ToMask(DirtyFlag::kFaceCustomization)) != 0) {
			faceCustomizationQuietFrames_ = kFaceCustomizationQuietFrames;
			pendingFaceCustomizationSignature_ = 0;
			pendingFaceCustomizationSignatureFrames_ = 0;
		}
		if ((requested & ToMask(DirtyFlag::kBehaviorGraph)) != 0) {
			behaviorGraphRefreshRequested_ = true;
		}
	}

	void Controller::Reset()
	{
		requestedDirty_.store(0, std::memory_order_release);
		equipmentAuditActive_.store(false, std::memory_order_release);
		equipmentAuditFrames_ = 0;
		equipmentQuietFrames_ = 0;
		pendingEquipmentSignature_ = 0;
		pendingEquipmentSignatureFrames_ = 0;
		morphAuditFrames_ = 0;
		morphQuietFrames_ = 0;
		pendingMorphSignature_ = 0;
		pendingMorphSignatureFrames_ = 0;
		faceCustomizationQuietFrames_ = 0;
		pendingFaceCustomizationSignature_ = 0;
		pendingFaceCustomizationSignatureFrames_ = 0;
		ClearCommittedSignatures();
		skeletonAdjustmentRequested_ = false;
		behaviorGraphRefreshRequested_ = false;
	}

	void Controller::ClearCommittedSignatures()
	{
		committedEquipmentSignature_ = 0;
		committedVisualSignature_ = 0;
		committedMorphGeometrySignature_ = 0;
		committedFaceCustomizationSignature_ = 0;
	}

	bool Controller::NeedsSkeletonBuild(const std::uint64_t a_currentVisualSignature, const bool a_hasPreviewRoot) const
	{
		return !a_hasPreviewRoot || a_currentVisualSignature != committedVisualSignature_;
	}

	std::optional<std::uint64_t> Controller::TryResolveEquipmentSignature(
		const std::uint64_t a_currentSignature,
		const bool a_hasPendingModels)
	{
		if (equipmentAuditFrames_ == 0) {
			equipmentAuditActive_.store(false, std::memory_order_release);
			return std::nullopt;
		}
		if (HasRequested(DirtyFlag::kEquipment)) {
			equipmentQuietFrames_ = kEquipmentQuietFrames;
			pendingEquipmentSignature_ = 0;
			pendingEquipmentSignatureFrames_ = 0;
			return std::nullopt;
		}
		if (equipmentQuietFrames_ != 0) {
			--equipmentQuietFrames_;
			pendingEquipmentSignature_ = 0;
			pendingEquipmentSignatureFrames_ = 0;
			return std::nullopt;
		}
		if (a_hasPendingModels) {
			pendingEquipmentSignature_ = 0;
			pendingEquipmentSignatureFrames_ = 0;
			return std::nullopt;
		}

		--equipmentAuditFrames_;
		if (equipmentAuditFrames_ == 0) {
			equipmentAuditActive_.store(false, std::memory_order_release);
		}

		if (!TryAcceptStableChangedSignature(
				a_currentSignature,
				committedEquipmentSignature_,
				pendingEquipmentSignature_,
				pendingEquipmentSignatureFrames_,
				kEquipmentSignatureStableFrames)) {
			return std::nullopt;
		}

		pendingEquipmentSignature_ = 0;
		pendingEquipmentSignatureFrames_ = 0;
		equipmentAuditActive_.store(false, std::memory_order_release);
		return a_currentSignature;
	}

	std::optional<std::uint64_t> Controller::TryResolveMorphGeometrySignature(
		const std::uint64_t a_currentSignature)
	{
		if (morphAuditFrames_ == 0 || equipmentAuditFrames_ != 0) {
			return std::nullopt;
		}
		if (HasRequested(DirtyFlag::kEquipment) || HasRequested(DirtyFlag::kMorphGeometry)) {
			morphQuietFrames_ = kMorphGeometryQuietFrames;
			pendingMorphSignature_ = 0;
			pendingMorphSignatureFrames_ = 0;
			return std::nullopt;
		}
		if (morphQuietFrames_ != 0) {
			--morphQuietFrames_;
			pendingMorphSignature_ = 0;
			pendingMorphSignatureFrames_ = 0;
			return std::nullopt;
		}

		--morphAuditFrames_;
		if (!TryAcceptStableChangedSignature(
				a_currentSignature,
				committedMorphGeometrySignature_,
				pendingMorphSignature_,
				pendingMorphSignatureFrames_,
				kMorphSignatureStableFrames)) {
			return std::nullopt;
		}

		pendingMorphSignature_ = 0;
		pendingMorphSignatureFrames_ = 0;
		return a_currentSignature;
	}

	std::optional<std::uint64_t> Controller::TryResolveFaceCustomizationSignature(
		const std::uint64_t a_currentSignature)
	{
		if (HasRequested(DirtyFlag::kEquipment) ||
			HasRequested(DirtyFlag::kFaceStructure) ||
			HasRequested(DirtyFlag::kFaceCustomization) ||
			equipmentAuditFrames_ != 0) {
			faceCustomizationQuietFrames_ = kFaceCustomizationQuietFrames;
			pendingFaceCustomizationSignature_ = 0;
			pendingFaceCustomizationSignatureFrames_ = 0;
			return std::nullopt;
		}
		if (faceCustomizationQuietFrames_ != 0) {
			--faceCustomizationQuietFrames_;
			pendingFaceCustomizationSignature_ = 0;
			pendingFaceCustomizationSignatureFrames_ = 0;
			return std::nullopt;
		}

		if (!TryAcceptStableChangedSignature(
				a_currentSignature,
				committedFaceCustomizationSignature_,
				pendingFaceCustomizationSignature_,
				pendingFaceCustomizationSignatureFrames_,
				kFaceCustomizationSignatureStableFrames)) {
			return std::nullopt;
		}

		pendingFaceCustomizationSignature_ = 0;
		pendingFaceCustomizationSignatureFrames_ = 0;
		return a_currentSignature;
	}

	void Controller::CommitSkeletonBuild(
		const std::uint64_t a_equipmentSignature,
		const std::uint64_t a_visualSignature,
		const std::uint64_t a_morphGeometrySignature,
		const std::uint64_t a_faceCustomizationSignature)
	{
		committedEquipmentSignature_ = a_equipmentSignature;
		committedVisualSignature_ = a_visualSignature;
		committedMorphGeometrySignature_ = a_morphGeometrySignature;
		committedFaceCustomizationSignature_ = a_faceCustomizationSignature;
	}

	void Controller::CommitEquipmentLayer(
		const std::uint64_t a_equipmentSignature,
		const std::uint64_t a_morphGeometrySignature)
	{
		committedEquipmentSignature_ = a_equipmentSignature;
		committedMorphGeometrySignature_ = a_morphGeometrySignature;
		RequestBehaviorGraphRefresh();
	}

	void Controller::CommitFaceCustomization(const std::uint64_t a_faceCustomizationSignature)
	{
		committedFaceCustomizationSignature_ = a_faceCustomizationSignature;
	}

	void Controller::RequestSkeletonAdjustment()
	{
		skeletonAdjustmentRequested_ = true;
	}

	bool Controller::ConsumeSkeletonAdjustmentRequest()
	{
		const bool requested = skeletonAdjustmentRequested_;
		skeletonAdjustmentRequested_ = false;
		return requested;
	}

	void Controller::RequestBehaviorGraphRefresh()
	{
		behaviorGraphRefreshRequested_ = true;
	}

	bool Controller::ConsumeBehaviorGraphRefreshRequest()
	{
		const bool requested = behaviorGraphRefreshRequested_;
		behaviorGraphRefreshRequested_ = false;
		return requested;
	}

	bool Controller::HasRequested(const DirtyFlag a_flag) const
	{
		return (requestedDirty_.load(std::memory_order_acquire) & ToMask(a_flag)) != 0;
	}

	bool Controller::TryAcceptStableChangedSignature(
		const std::uint64_t a_currentSignature,
		const std::uint64_t a_committedSignature,
		std::uint64_t& a_pendingSignature,
		std::uint32_t& a_pendingFrames,
		const std::uint32_t a_requiredStableFrames)
	{
		if (a_currentSignature == 0 || a_currentSignature == a_committedSignature) {
			a_pendingSignature = 0;
			a_pendingFrames = 0;
			return false;
		}
		if (a_currentSignature != a_pendingSignature) {
			a_pendingSignature = a_currentSignature;
			a_pendingFrames = 1;
			return false;
		}

		++a_pendingFrames;
		return a_pendingFrames >= a_requiredStableFrames;
	}
}
