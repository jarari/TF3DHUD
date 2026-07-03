#include "Morph.h"

#include "Utils.h"

#include "RE/B/BipedAnim.h"
#include "RE/B/BSGeometry.h"
#include "RE/N/NiNode.h"
#include "RE/N/NiUpdateData.h"

#include <atomic>
#include <cstdint>
#include <mutex>
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
		std::mutex g_stateLock;

		void HashInteger(std::uint64_t& a_hash, const std::uintptr_t a_value)
		{
			HashValue(a_hash, reinterpret_cast<void*>(a_value));
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
				HashInteger(hash, a_geometry.vertexDesc.desc);
				HashInteger(hash, a_geometry.type);

				if (a_geometry.IsTriShape()) {
					// F4EE body morphs fork BSGeometryData and swap this pointer on BSTriShape.
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

		void RefreshGraphWrittenBoneNames(RE::PlayerCharacter& a_player)
		{
			g_graphWrittenBoneNames.clear();
			CollectGraphWrittenBoneNames(a_player, g_graphWrittenBoneNames);
			g_graphWrittenBoneNamesValid = !g_graphWrittenBoneNames.empty();
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
				return false;
			}

			auto* sourceFlattenedTree = FindFlattenedBoneTree(sourceRoot);
			if (!sourceFlattenedTree) {
				return false;
			}

			CollectFlattenedBoneNodes(sourceFlattenedTree, a_sourceNodes);
			return !a_sourceNodes.empty();
		}

		[[nodiscard]] bool CheckStableChangedSignature(
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

		[[nodiscard]] std::uint32_t ApplySkeletalAdjustments(
			RE::NiAVObject& a_previewRoot,
			const std::unordered_map<std::string, RE::NiAVObject*>& a_sourceNodes,
			const std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			if (a_sourceNodes.empty()) {
				return 0;
			}

			if (a_previewNodes.empty()) {
				return 0;
			}

			std::uint32_t copied = 0;
			for (const auto& [name, source] : a_sourceNodes) {
				if (!source || !IsAdjustmentBone(*source)) {
					continue;
				}

				auto* target = FindNodeByName(a_previewNodes, name);
				if (!target || !target->IsNode()) {
					continue;
				}

				const auto appliedTransform = source->GetLocalTransform();
				target->SetLocalTransform(appliedTransform);
				++copied;
			}

			if (copied == 0) {
				return 0;
			}

			RE::NiUpdateData updateData;
			a_previewRoot.Update(updateData);
			return copied;
		}

		void BeginAuditsIfRequested()
		{
			if (const auto requestedFrames = g_requestedMorphAuditFrames.exchange(0, std::memory_order_acq_rel);
				requestedFrames != 0) {
				g_morphAuditFrames = std::max(g_morphAuditFrames, requestedFrames);
				g_pendingMorphSignature = 0;
				g_pendingMorphSignatureFrames = 0;
			}

			if (const auto requestedFrames = g_requestedAdjustmentDelayFrames.exchange(0, std::memory_order_acq_rel);
				requestedFrames != 0) {
				g_adjustmentDelayFrames = requestedFrames;
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

	void ClearGeometryAudit()
	{
		std::scoped_lock lock(g_stateLock);
		g_requestedMorphAuditFrames.store(0, std::memory_order_release);
		g_morphAuditFrames = 0;
		g_lastMorphSignature = 0;
		g_pendingMorphSignature = 0;
		g_pendingMorphSignatureFrames = 0;
	}

	void Reset()
	{
		std::scoped_lock lock(g_stateLock);
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
		std::scoped_lock lock(g_stateLock);
		BeginAuditsIfRequested();

		UpdateResult result = UpdateResult::kNone;
		if (g_morphAuditFrames != 0) {
			--g_morphAuditFrames;
			const auto morphSignature = BuildMorphSignature(a_player);
			if (CheckStableChangedSignature(
					morphSignature,
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
				return result;
			}

			std::unordered_map<std::string, RE::NiAVObject*> sourceNodes;
			if (!BuildSourceAdjustmentMap(a_player, sourceNodes)) {
				return result;
			}

			std::unordered_map<std::string, RE::NiAVObject*> previewNodes;
			BuildPreviewTargetMap(a_previewRoot, a_previewFlattenedBoneTree, previewNodes);
			const auto adjustedNodes = ApplySkeletalAdjustments(a_previewRoot, sourceNodes, previewNodes);
			if (adjustedNodes != 0) {
				result = result | UpdateResult::kAdjustmentsApplied;
			}
		}

		return result;
	}
}
