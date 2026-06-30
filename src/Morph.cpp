#include "Morph.h"

#include "Utils.h"

#include "RE/B/BipedAnim.h"
#include "RE/B/BSGeometry.h"
#include "RE/N/NiNode.h"
#include "RE/N/NiUpdateData.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace TF3DHud::Morph
{
	namespace
	{
		constexpr std::uint32_t kAuditFrames = 300;
		constexpr std::uint32_t kAdjustmentDelayFrames = 5;
		constexpr std::uint32_t kStableFrames = 3;

		std::atomic_uint32_t g_requestedMorphAuditFrames{ 0 };
		std::atomic_uint32_t g_requestedAdjustmentDelayFrames{ 0 };
		std::uint32_t g_morphAuditFrames{ 0 };
		std::uint32_t g_adjustmentDelayFrames{ 0 };
		std::uint64_t g_lastMorphSignature{ 0 };
		std::uint64_t g_pendingMorphSignature{ 0 };
		std::uint32_t g_pendingMorphSignatureFrames{ 0 };
		std::unordered_set<std::string> g_graphWrittenBoneNames;
		bool g_graphWrittenBoneNamesValid{ false };
		std::uint32_t g_diagnosticLogCount{ 0 };

		void LogDiagnostic(const std::string_view a_message)
		{
			if (g_diagnosticLogCount >= 80) {
				return;
			}

			REX::INFO("Morph: {}", a_message);
			++g_diagnosticLogCount;
		}

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

		void HashTransform(std::uint64_t& a_hash, const RE::NiTransform& a_transform)
		{
			HashFloat(a_hash, a_transform.translate.x);
			HashFloat(a_hash, a_transform.translate.y);
			HashFloat(a_hash, a_transform.translate.z);
			for (const auto& row : a_transform.rotate.entry) {
				HashFloat(a_hash, row.x);
				HashFloat(a_hash, row.y);
				HashFloat(a_hash, row.z);
			}
			HashFloat(a_hash, a_transform.scale);
		}

		[[nodiscard]] std::uint64_t BuildMorphSignature(RE::PlayerCharacter& a_player)
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
				HashValue(hash, a_geometry.rendererData);
				HashValue(hash, a_geometry.properties[0].get());
				HashValue(hash, a_geometry.properties[1].get());
				HashInteger(hash, a_geometry.vertexDesc.desc);
				HashInteger(hash, a_geometry.GetFlags());
				HashInteger(hash, a_geometry.GetAppCulled() ? 1 : 0);
				HashInteger(hash, a_geometry.type);
				HashInteger(hash, a_geometry.registered ? 1 : 0);
				HashFloat(hash, a_geometry.fadeAmount);
			});
			HashInteger(hash, geometryCount);
			return hash;
		}

		void RefreshGraphWrittenBoneNames(RE::PlayerCharacter& a_player)
		{
			g_graphWrittenBoneNames.clear();
			CollectGraphWrittenBoneNames(a_player, g_graphWrittenBoneNames);
			g_graphWrittenBoneNamesValid = !g_graphWrittenBoneNames.empty();
			REX::INFO("Morph: collected {} graph-written bone names", g_graphWrittenBoneNames.size());
		}

		[[nodiscard]] bool IsAdjustmentBone(const RE::NiAVObject& a_object)
		{
			if (!a_object.IsNode()) {
				return false;
			}

			const auto name = a_object.GetName();
			return !name.empty() && !g_graphWrittenBoneNames.contains(name.c_str());
		}

		void BuildPreviewTargetMap(
			[[maybe_unused]] RE::NiAVObject& a_previewRoot,
			RE::BSFlattenedBoneTree* a_previewFlattenedBoneTree,
			std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			a_previewNodes.clear();
			CollectFlattenedBoneNodes(a_previewFlattenedBoneTree, a_previewNodes);
		}

		[[nodiscard]] bool BuildSourceAdjustmentMap(
			RE::PlayerCharacter& a_player,
			std::unordered_map<std::string, RE::NiAVObject*>& a_sourceNodes)
		{
			a_sourceNodes.clear();

			auto* sourceRoot = a_player.Get3D(false);
			if (!sourceRoot) {
				LogDiagnostic("adjustment apply skipped: player third-person root is null");
				return false;
			}

			auto* sourceFlattenedTree = FindFlattenedBoneTree(sourceRoot);
			if (!sourceFlattenedTree) {
				LogDiagnostic("adjustment apply skipped: player flattened bone tree is null");
				return false;
			}

			CollectFlattenedBoneNodes(sourceFlattenedTree, a_sourceNodes);
			return !a_sourceNodes.empty();
		}

		[[nodiscard]] bool IsStableChangedSignature(
			const std::uint64_t a_currentSignature,
			std::uint64_t& a_lastSignature,
			std::uint64_t& a_pendingSignature,
			std::uint32_t& a_pendingFrames)
		{
			if (a_currentSignature == 0 || a_currentSignature == a_lastSignature) {
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
			if (a_pendingFrames < kStableFrames) {
				return false;
			}

			a_lastSignature = a_currentSignature;
			a_pendingSignature = 0;
			a_pendingFrames = 0;
			return true;
		}

		void LogAppliedAdjustmentTransform(
			const std::string_view a_name,
			const RE::NiTransform& a_previous,
			const RE::NiTransform& a_applied)
		{
			const auto& previousRot = a_previous.rotate.entry;
			const auto& appliedRot = a_applied.rotate.entry;
			REX::INFO(
				"Morph adjustment: bone='{}' previous pos=({:.6f}, {:.6f}, {:.6f}) "
				"rot=(({:.6f}, {:.6f}, {:.6f}), ({:.6f}, {:.6f}, {:.6f}), ({:.6f}, {:.6f}, {:.6f})) "
				"scale={:.6f}; applied pos=({:.6f}, {:.6f}, {:.6f}) "
				"rot=(({:.6f}, {:.6f}, {:.6f}), ({:.6f}, {:.6f}, {:.6f}), ({:.6f}, {:.6f}, {:.6f})) "
				"scale={:.6f}",
				a_name,
				a_previous.translate.x,
				a_previous.translate.y,
				a_previous.translate.z,
				previousRot[0].x,
				previousRot[0].y,
				previousRot[0].z,
				previousRot[1].x,
				previousRot[1].y,
				previousRot[1].z,
				previousRot[2].x,
				previousRot[2].y,
				previousRot[2].z,
				a_previous.scale,
				a_applied.translate.x,
				a_applied.translate.y,
				a_applied.translate.z,
				appliedRot[0].x,
				appliedRot[0].y,
				appliedRot[0].z,
				appliedRot[1].x,
				appliedRot[1].y,
				appliedRot[1].z,
				appliedRot[2].x,
				appliedRot[2].y,
				appliedRot[2].z,
				a_applied.scale);
		}

		[[nodiscard]] bool ApplySkeletalAdjustments(
			RE::NiAVObject& a_previewRoot,
			const std::unordered_map<std::string, RE::NiAVObject*>& a_sourceNodes,
			const std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			if (a_sourceNodes.empty()) {
				LogDiagnostic("adjustment apply skipped: source has no flattened bones");
				return false;
			}

			if (a_previewNodes.empty()) {
				LogDiagnostic("adjustment apply skipped: preview has no flattened bones");
				return false;
			}

			std::uint32_t copied = 0;
			std::uint32_t candidates = 0;
			std::uint32_t missingTargets = 0;
			for (const auto& [name, source] : a_sourceNodes) {
				if (!source || !IsAdjustmentBone(*source)) {
					continue;
				}

				++candidates;
				auto* target = FindNodeByName(a_previewNodes, name);
				if (!target || !target->IsNode()) {
					++missingTargets;
					continue;
				}

				const auto previousTransform = target->GetLocalTransform();
				const auto appliedTransform = source->GetLocalTransform();
				target->SetLocalTransform(appliedTransform);
				LogAppliedAdjustmentTransform(name, previousTransform, appliedTransform);
				++copied;
			}

			if (copied == 0) {
				REX::INFO(
					"Morph: adjustment apply copied 0 transforms; candidates={}, missingTargets={}, sourceNodes={}, previewNodes={}",
					candidates,
					missingTargets,
					a_sourceNodes.size(),
					a_previewNodes.size());
				return false;
			}

			RE::NiUpdateData updateData;
			a_previewRoot.Update(updateData);
			REX::INFO("Applied {} skeleton adjustment transforms to preview", copied);
			return true;
		}

		void BeginAuditsIfRequested()
		{
			if (const auto requestedFrames = g_requestedMorphAuditFrames.exchange(0, std::memory_order_acq_rel);
				requestedFrames != 0) {
				g_morphAuditFrames = std::max(g_morphAuditFrames, requestedFrames);
				g_pendingMorphSignature = 0;
				g_pendingMorphSignatureFrames = 0;
				REX::INFO("Morph: primary audit requested for {} frames", requestedFrames);
			}

			if (const auto requestedFrames = g_requestedAdjustmentDelayFrames.exchange(0, std::memory_order_acq_rel);
				requestedFrames != 0) {
				const bool wasIdle = g_adjustmentDelayFrames == 0;
				g_adjustmentDelayFrames = requestedFrames;
				if (wasIdle) {
					REX::INFO("Morph: skeletal adjustment debounce started for {} frames", requestedFrames);
				}
			}
		}
	}

	void MarkPrimaryDirty()
	{
		g_requestedMorphAuditFrames.store(kAuditFrames, std::memory_order_release);
		g_requestedAdjustmentDelayFrames.store(kAdjustmentDelayFrames, std::memory_order_release);
	}

	void MarkSecondaryDirty()
	{
		g_requestedAdjustmentDelayFrames.store(kAdjustmentDelayFrames, std::memory_order_release);
	}

	void Reset()
	{
		g_requestedMorphAuditFrames.store(0, std::memory_order_release);
		g_requestedAdjustmentDelayFrames.store(0, std::memory_order_release);
		g_morphAuditFrames = 0;
		g_adjustmentDelayFrames = 0;
		g_lastMorphSignature = 0;
		g_pendingMorphSignature = 0;
		g_pendingMorphSignatureFrames = 0;
		g_graphWrittenBoneNames.clear();
		g_graphWrittenBoneNamesValid = false;
	}

	UpdateResult Update(
		RE::PlayerCharacter& a_player,
		RE::NiAVObject& a_previewRoot,
		RE::BSFlattenedBoneTree* a_previewFlattenedBoneTree)
	{
		BeginAuditsIfRequested();

		UpdateResult result = UpdateResult::kNone;
		if (g_morphAuditFrames != 0) {
			--g_morphAuditFrames;
			if (IsStableChangedSignature(
					BuildMorphSignature(a_player),
					g_lastMorphSignature,
					g_pendingMorphSignature,
					g_pendingMorphSignatureFrames)) {
				result = result | UpdateResult::kGeometryRebuild;
			}
		}

		if (g_adjustmentDelayFrames != 0) {
			--g_adjustmentDelayFrames;
			if (g_adjustmentDelayFrames != 0) {
				return result;
			}

			RefreshGraphWrittenBoneNames(a_player);
			if (!g_graphWrittenBoneNamesValid) {
				LogDiagnostic("skeletal adjustment rebuild skipped: graph-written bone names unavailable");
				return result;
			}

			std::unordered_map<std::string, RE::NiAVObject*> sourceNodes;
			if (!BuildSourceAdjustmentMap(a_player, sourceNodes)) {
				return result;
			}

			std::unordered_map<std::string, RE::NiAVObject*> previewNodes;
			BuildPreviewTargetMap(a_previewRoot, a_previewFlattenedBoneTree, previewNodes);
			if (ApplySkeletalAdjustments(a_previewRoot, sourceNodes, previewNodes)) {
				result = result | UpdateResult::kAdjustmentsApplied;
			}
		}

		return result;
	}
}
