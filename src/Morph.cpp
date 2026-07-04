#include "Morph.h"

#include "Address.h"
#include "Utils.h"

#include "RE/B/BSGeometry.h"
#include "RE/N/NiNode.h"
#include "RE/N/NiUpdateData.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
		std::vector<std::vector<std::string>> g_trustedSkeletonPaths;
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
			return !name.empty() && !NamesEqual(name.c_str(), "Root") && !g_graphWrittenBoneNames.contains(name.c_str());
		}

		void BuildPreviewTargetMap(
			RE::NiAVObject& a_previewRoot,
			RE::BSFlattenedBoneTree* a_previewFlattenedBoneTree,
			std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			a_previewNodes.clear();
			CollectFlattenedBoneNodes(a_previewFlattenedBoneTree, a_previewNodes);
			CollectNamedNodes(std::addressof(a_previewRoot), a_previewNodes);
		}

		[[nodiscard]] RE::NiAVObject* FindNamedNodeRecursive(RE::NiAVObject& a_object, const std::string_view a_name)
		{
			if (a_object.IsNode() && NamesEqual(a_object.GetName(), a_name)) {
				return std::addressof(a_object);
			}

			auto* node = a_object.IsNode();
			if (!node) {
				return nullptr;
			}

			for (auto& child : node->children) {
				if (!child) {
					continue;
				}
				if (auto* found = FindNamedNodeRecursive(*child, a_name)) {
					return found;
				}
			}
			return nullptr;
		}

		void CollectTrustedSkeletonLeafPaths(
			RE::NiAVObject& a_object,
			std::vector<std::string>& a_currentPath,
			std::vector<std::vector<std::string>>& a_paths)
		{
			auto* node = a_object.IsNode();
			if (!node) {
				return;
			}

			const auto name = a_object.GetName();
			if (name.empty()) {
				return;
			}

			a_currentPath.emplace_back(name.c_str());
			std::vector<RE::NiAVObject*> nodeChildren;
			nodeChildren.reserve(node->children.size());
			for (auto& child : node->children) {
				if (child && child->IsNode()) {
					nodeChildren.push_back(child.get());
				}
			}

			if (nodeChildren.empty()) {
				if (a_currentPath.size() >= 2) {
					a_paths.push_back(a_currentPath);
				}
				a_currentPath.pop_back();
				return;
			}

			for (auto* child : nodeChildren) {
				CollectTrustedSkeletonLeafPaths(*child, a_currentPath, a_paths);
			}

			a_currentPath.pop_back();
		}

		void CaptureTrustedSkeletonPaths(
			RE::NiAVObject& a_previewRoot,
			[[maybe_unused]] RE::BSFlattenedBoneTree* a_previewFlattenedBoneTree,
			std::vector<std::vector<std::string>>& a_paths)
		{
			a_paths.clear();
			auto* root = FindNamedNodeRecursive(a_previewRoot, "Root");
			if (!root) {
				root = std::addressof(a_previewRoot);
			}
			if (!root->IsNode()) {
				return;
			}

			std::vector<std::string> currentPath;
			CollectTrustedSkeletonLeafPaths(*root, currentPath, a_paths);

			std::ranges::sort(a_paths, [](const auto& a_lhs, const auto& a_rhs) {
				return a_lhs < a_rhs;
			});
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

			if (auto* sourceFlattenedTree = FindFlattenedBoneTree(sourceRoot)) {
				CollectFlattenedBoneNodes(sourceFlattenedTree, a_sourceNodes);
			}
			CollectNamedNodes(sourceRoot, a_sourceNodes);
			return !a_sourceNodes.empty();
		}

		[[nodiscard]] bool EnsurePreviewParent(RE::NiAVObject& a_child, RE::NiNode& a_parent)
		{
			if (a_child.parent == std::addressof(a_parent)) {
				return false;
			}
			if (std::addressof(a_child) == std::addressof(a_parent) || IsDescendantOf(a_parent, a_child)) {
				return false;
			}

			RE::NiPointer<RE::NiAVObject> keepAlive(std::addressof(a_child));
			if (auto* oldParent = a_child.parent) {
				oldParent->DetachChild(keepAlive.get());
			}
			a_parent.AttachChild(keepAlive.get(), false);
			return true;
		}

		[[nodiscard]] RE::NiPointer<RE::NiNode> CreatePreviewAdjustmentNode(RE::NiAVObject& a_source)
		{
			const auto name = a_source.GetName();
			if (name.empty()) {
				return nullptr;
			}

			auto node = RE::make_nismart<RE::NiNode>(0);
			node->name = name;
			node->SetLocalTransform(a_source.GetLocalTransform());
			node->SetWorldTransform(a_source.GetWorldTransform());
			node->fadeAmount = 1.0F;
			return node;
		}

		[[nodiscard]] RE::NiNode* GetNamedNode(RE::NiAVObject& a_root, const std::string_view a_name)
		{
			if (a_name.empty()) {
				return nullptr;
			}
			auto* object = a_root.GetObjectByName(RE::BSFixedString(a_name.data()));
			return object ? object->IsNode() : nullptr;
		}

		void RefreshPreviewAdjustmentLookup(
			RE::NiAVObject& a_previewRoot,
			RE::BSFlattenedBoneTree*& a_previewFlattenedBoneTree,
			std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			Address::CreateBoneMap(std::addressof(a_previewRoot));
			a_previewFlattenedBoneTree = FindFlattenedBoneTree(std::addressof(a_previewRoot));
			if (a_previewFlattenedBoneTree) {
				Address::CreateBoneMap(a_previewFlattenedBoneTree);
			}
			BuildPreviewTargetMap(a_previewRoot, a_previewFlattenedBoneTree, a_previewNodes);
		}

		struct ReconstructionStats
		{
			std::uint32_t created{ 0 };
			std::uint32_t reparented{ 0 };
			std::uint32_t cachedStops{ 0 };
			std::uint32_t transformed{ 0 };
		};

		[[nodiscard]] ReconstructionStats ReconstructTrustedLeafPathFromLive(
			RE::NiAVObject& a_liveRoot,
			RE::NiAVObject& a_previewRoot,
			const std::string_view a_leafName,
			std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes,
			std::unordered_set<RE::NiAVObject*>& a_visitedLiveNodes)
		{
			ReconstructionStats stats;
			auto* liveLeaf = GetNamedNode(a_liveRoot, a_leafName);
			if (!liveLeaf) {
				return stats;
			}

			std::vector<RE::NiAVObject*> livePathLeafToRoot;
			livePathLeafToRoot.reserve(16);
			std::unordered_set<RE::NiAVObject*> seen;
			for (auto* current = static_cast<RE::NiAVObject*>(liveLeaf); current; current = current->parent) {
				if (!current->IsNode() || !seen.insert(current).second) {
					return stats;
				}

				livePathLeafToRoot.push_back(current);
				if (NamesEqual(current->GetName().c_str(), "Root")) {
					break;
				}
				if (a_visitedLiveNodes.contains(current)) {
					++stats.cachedStops;
					break;
				}
				if (current == std::addressof(a_liveRoot)) {
					break;
				}
				if (livePathLeafToRoot.size() > 256) {
					return stats;
				}
			}

			RE::NiNode* previewParent = nullptr;
			std::vector<RE::NiAVObject*> reconstructed;
			reconstructed.reserve(livePathLeafToRoot.size());

			for (auto it = livePathLeafToRoot.rbegin(); it != livePathLeafToRoot.rend(); ++it) {
				auto* liveNode = *it;
				const auto name = liveNode->GetName();
				if (name.empty()) {
					return stats;
				}
				const bool isRootNode = NamesEqual(name.c_str(), "Root");

				auto* previewNode = GetNamedNode(a_previewRoot, name.c_str());
				RE::NiPointer<RE::NiNode> created;
				if (!previewNode) {
					if (isRootNode) {
						return stats;
					}
					if (!previewParent) {
						return stats;
					}

					created = CreatePreviewAdjustmentNode(*liveNode);
					previewNode = created.get();
					if (!previewNode) {
						return stats;
					}

					previewParent->AttachChild(previewNode, false);
					a_previewNodes.insert_or_assign(std::string(name), previewNode);
					++stats.created;
				} else if (!isRootNode && previewParent && previewNode != previewParent &&
						   EnsurePreviewParent(*previewNode, *previewParent)) {
					++stats.reparented;
				}

				if (IsAdjustmentBone(*liveNode)) {
					previewNode->SetLocalTransform(liveNode->GetLocalTransform());
					++stats.transformed;
				}

				reconstructed.push_back(liveNode);
				previewParent = previewNode;
			}

			for (auto* liveNode : reconstructed) {
				a_visitedLiveNodes.insert(liveNode);
			}

			return stats;
		}

		[[nodiscard]] ReconstructionStats ReconstructTrustedSkeletonAdjustmentHierarchy(
			RE::NiAVObject& a_liveRoot,
			RE::NiAVObject& a_previewRoot,
			std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			ReconstructionStats total;
			if (g_trustedSkeletonPaths.empty()) {
				return total;
			}

			std::unordered_set<RE::NiAVObject*> visitedLiveNodes;
			visitedLiveNodes.reserve(g_trustedSkeletonPaths.size() * 4);

			for (const auto& path : g_trustedSkeletonPaths) {
				if (path.empty()) {
					continue;
				}

				const auto stats = ReconstructTrustedLeafPathFromLive(
					a_liveRoot,
					a_previewRoot,
					path.back(),
					a_previewNodes,
					visitedLiveNodes);
				total.created += stats.created;
				total.reparented += stats.reparented;
				total.cachedStops += stats.cachedStops;
				total.transformed += stats.transformed;
			}
			return total;
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
			RE::NiAVObject& a_liveRoot,
			RE::NiAVObject& a_previewRoot,
			RE::BSFlattenedBoneTree*& a_previewFlattenedBoneTree,
			const std::unordered_map<std::string, RE::NiAVObject*>& a_sourceNodes,
			std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			if (a_sourceNodes.empty() || a_previewNodes.empty()) {
				return 0;
			}

			const auto reconstruction = ReconstructTrustedSkeletonAdjustmentHierarchy(
				a_liveRoot,
				a_previewRoot,
				a_previewNodes);
			const auto topologyChanges = reconstruction.created + reconstruction.reparented;
			if (topologyChanges != 0) {
				RefreshPreviewAdjustmentLookup(a_previewRoot, a_previewFlattenedBoneTree, a_previewNodes);
			}

			std::uint32_t copied = 0;
			for (const auto& [name, source] : a_sourceNodes) {
				if (!source || !IsAdjustmentBone(*source)) {
					continue;
				}

				auto* target = FindNodeByName(a_previewNodes, name);
				if (!target || target == std::addressof(a_previewRoot) || !target->IsNode()) {
					continue;
				}

				target->SetLocalTransform(source->GetLocalTransform());
				++copied;
			}

			if (copied != 0 || topologyChanges != 0) {
				RE::NiUpdateData updateData;
				a_previewRoot.Update(updateData);
			}

			return copied + topologyChanges;
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

	void CaptureTrustedSkeleton(
		RE::NiAVObject& a_previewRoot,
		RE::BSFlattenedBoneTree* a_previewFlattenedBoneTree)
	{
		std::scoped_lock lock(g_stateLock);
		CaptureTrustedSkeletonPaths(a_previewRoot, a_previewFlattenedBoneTree, g_trustedSkeletonPaths);
	}

	std::vector<std::string> GetTrustedSkeletonLeafNames()
	{
		std::scoped_lock lock(g_stateLock);

		std::vector<std::string> leaves;
		leaves.reserve(g_trustedSkeletonPaths.size());
		std::unordered_set<std::string> seen;
		for (const auto& path : g_trustedSkeletonPaths) {
			if (path.empty()) {
				continue;
			}
			if (seen.emplace(path.back()).second) {
				leaves.push_back(path.back());
			}
		}

		std::ranges::sort(leaves);
		return leaves;
	}

	std::vector<std::vector<std::string>> GetTrustedSkeletonLeafPaths()
	{
		std::scoped_lock lock(g_stateLock);
		return g_trustedSkeletonPaths;
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
		g_trustedSkeletonPaths.clear();
		g_graphWrittenBoneNamesValid = false;
	}

	UpdateResult Update(
		RE::PlayerCharacter& a_player,
		[[maybe_unused]] RE::NiAVObject& a_previewRoot,
		[[maybe_unused]] RE::BSFlattenedBoneTree*& a_previewFlattenedBoneTree)
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

			auto* liveRoot = a_player.Get3D(false);
			if (!liveRoot) {
				return result;
			}

			std::unordered_map<std::string, RE::NiAVObject*> sourceNodes;
			if (!BuildSourceAdjustmentMap(a_player, sourceNodes)) {
				return result;
			}

			std::unordered_map<std::string, RE::NiAVObject*> previewNodes;
			BuildPreviewTargetMap(a_previewRoot, a_previewFlattenedBoneTree, previewNodes);
			const auto adjustedNodes =
				ApplySkeletalAdjustments(*liveRoot, a_previewRoot, a_previewFlattenedBoneTree, sourceNodes, previewNodes);
			if (adjustedNodes != 0) {
				result = result | UpdateResult::kAdjustmentsApplied;
			}
		}

		return result;
	}
}
