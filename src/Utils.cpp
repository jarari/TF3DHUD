#include "Utils.h"

#include "RE/B/BSAnimationGraphManager.h"
#include "RE/B/BSFlattenedBoneTree.h"

namespace TF3DHud
{
	namespace
	{
		struct GraphBoneRef
		{
			void* target;
			std::int32_t boneIndex;
			std::int32_t pad;
		};
		static_assert(sizeof(GraphBoneRef) == 0x10);

		void AddGraphBoneName(const RE::BSFixedString& a_name, std::unordered_set<std::string>& a_names)
		{
			if (!a_name.empty()) {
				a_names.emplace(a_name.c_str());
			}
		}

		template <std::size_t N>
		void CopyBoundedText(std::array<char, N>& a_out, const char* a_text)
		{
			a_out[0] = '\0';
			if (!a_text || a_text[0] == '\0') {
				return;
			}

			const auto length = (std::min)(std::strlen(a_text), N - 1);
			std::memcpy(a_out.data(), a_text, length);
			a_out[length] = '\0';
		}

		void RecordGraphTargetIssue(
			GraphTargetStats& a_stats,
			const void* a_target,
			const std::int32_t a_boneIndex,
			const char* a_name,
			const char* a_reason)
		{
			++a_stats.outsideExpectedRoot;
			if (a_stats.issueCount >= a_stats.issues.size()) {
				return;
			}

			auto& issue = a_stats.issues[a_stats.issueCount++];
			issue.target = reinterpret_cast<std::uintptr_t>(a_target);
			issue.boneIndex = a_boneIndex;
			CopyBoundedText(issue.name, a_name);
			CopyBoundedText(issue.reason, a_reason);
		}
	}

	std::int32_t MakeRel32Displacement(const std::uintptr_t a_sourceNext, const std::uintptr_t a_destination)
	{
		const auto displacement =
			static_cast<std::int64_t>(a_destination) - static_cast<std::int64_t>(a_sourceNext);
		if (displacement < (std::numeric_limits<std::int32_t>::min)() ||
			displacement > (std::numeric_limits<std::int32_t>::max)()) {
			REX::FAIL(
				"rel32 displacement out of range sourceNext={:X} destination={:X}",
				a_sourceNext,
				a_destination);
		}

		return static_cast<std::int32_t>(displacement);
	}

	void WriteBranch5(const std::uintptr_t a_source, const std::uintptr_t a_destination)
	{
		auto& trampoline = REL::GetTrampoline();
		const auto branch = trampoline.allocate_branch5(a_destination);
		const REL::ASM::JMP5 assembly{
			MakeRel32Displacement(a_source + sizeof(REL::ASM::JMP5), branch)
		};
		REL::WriteSafeData(a_source, assembly);
	}

	bool NamesEqual(const std::string_view a_lhs, const std::string_view a_rhs)
	{
		return !a_lhs.empty() && a_lhs == a_rhs;
	}

	void CollectNamedNodes(RE::NiAVObject* a_object, std::unordered_map<std::string, RE::NiAVObject*>& a_nodes)
	{
		if (!a_object) {
			return;
		}

		const auto name = a_object->GetName();
		if (!name.empty()) {
			const auto key = std::string(name);
			if (!a_nodes.contains(key)) {
				a_nodes.emplace(key, a_object);
			}
		}

		auto* node = a_object->IsNode();
		if (!node) {
			return;
		}

		for (auto& child : node->children) {
			CollectNamedNodes(child.get(), a_nodes);
		}
	}

	RE::NiAVObject* FindNodeByName(
		const std::unordered_map<std::string, RE::NiAVObject*>& a_nodes,
		const std::string_view a_name)
	{
		if (a_name.empty()) {
			return nullptr;
		}

		for (const auto& [name, node] : a_nodes) {
			if (NamesEqual(name, a_name)) {
				return node;
			}
		}
		return nullptr;
	}

	void CollectFlattenedBoneNodes(
		RE::BSFlattenedBoneTree* a_tree,
		std::unordered_map<std::string, RE::NiAVObject*>& a_nodes)
	{
		if (!a_tree || !a_tree->bone || a_tree->boneCount <= 0 || a_tree->boneCount > 1024) {
			return;
		}

		for (std::int32_t i = 0; i < a_tree->boneCount; ++i) {
			const auto& bone = a_tree->bone[i];
			if (bone.name.empty() || !bone.node) {
				continue;
			}

			const auto key = std::string(bone.name);
			a_nodes.insert_or_assign(key, bone.node.get());
		}
	}

	RE::BSFlattenedBoneTree* FindFlattenedBoneTree(RE::NiAVObject* a_root)
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

	RE::IAnimationGraphManagerHolder& GetAnimationGraphHolder(RE::TESObjectREFR& a_reference)
	{
		return static_cast<RE::IAnimationGraphManagerHolder&>(a_reference);
	}

	[[nodiscard]] const GraphBoneRef* GetActiveGraphBoneRefs(
		const RE::IAnimationGraphManagerHolder& a_holder,
		std::uint32_t& a_count)
	{
		a_count = 0;
		RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
		if (!a_holder.GetAnimationGraphManagerImpl(manager) || !manager || manager->graph.empty()) {
			return nullptr;
		}

		const auto activeGraph = manager->activeGraph;
		if (activeGraph >= manager->graph.size()) {
			return nullptr;
		}

		auto* graph = manager->graph[activeGraph].get();
		if (!graph) {
			return nullptr;
		}

		// IDA: BShkbAnimationGraph::GenerateImpl passes graph+0x2A0 and graph+0x2B0
		// to BShkbUtils::SyncSceneGraphs. SyncFromTargetImpl shows each entry as
		// { target, boneIndex }; boneIndex < 0 means target is a direct NiAVObject,
		// otherwise target is a BSFlattenedBoneTree and boneIndex selects a flattened bone.
		const auto graphBase = reinterpret_cast<const std::byte*>(graph);
		const auto* refs = *reinterpret_cast<GraphBoneRef* const*>(graphBase + 0x2A0);
		a_count = *reinterpret_cast<const std::uint32_t*>(graphBase + 0x2B0);
		constexpr std::uint32_t kMaxExpectedGraphBones = 1024;
		if (!refs || a_count > kMaxExpectedGraphBones) {
			a_count = 0;
			return nullptr;
		}

		return refs;
	}

	GraphTargetStats InspectGraphTargets(
		const RE::IAnimationGraphManagerHolder& a_holder,
		RE::NiAVObject& a_expectedRoot)
	{
		GraphTargetStats stats;

		std::uint32_t count = 0;
		const auto* refs = GetActiveGraphBoneRefs(a_holder, count);
		if (!refs || count == 0) {
			return stats;
		}

		std::unordered_set<const RE::NiAVObject*> expectedObjects;
		ForEachAVObject(std::addressof(a_expectedRoot), [&](RE::NiAVObject& a_object) {
			expectedObjects.insert(std::addressof(a_object));
		});

		stats.refs = count;
		for (std::uint32_t i = 0; i < count; ++i) {
			const auto& ref = refs[i];
			if (!ref.target) {
				RecordGraphTargetIssue(stats, nullptr, ref.boneIndex, "", "null target");
				continue;
			}

			if (ref.boneIndex < 0) {
				++stats.directRefs;
				const auto* object = reinterpret_cast<const RE::NiAVObject*>(ref.target);
				if (!expectedObjects.contains(object)) {
					RecordGraphTargetIssue(stats, object, ref.boneIndex, object->GetName().c_str(), "outside root");
				}
				continue;
			}

			++stats.flattenedRefs;
			const auto* tree = reinterpret_cast<const RE::BSFlattenedBoneTree*>(ref.target);
			if (expectedObjects.contains(tree)) {
				continue;
			}

			const auto boneIndex = static_cast<std::int32_t>(ref.boneIndex);
			if (!tree->bone || boneIndex >= tree->boneCount || !tree->bone[boneIndex].node) {
				const auto* boneName =
					tree->bone && boneIndex < tree->boneCount ? tree->bone[boneIndex].name.c_str() : "";
				RecordGraphTargetIssue(stats, tree, ref.boneIndex, boneName, "null flattened bone");
				continue;
			}

			if (!expectedObjects.contains(tree->bone[boneIndex].node.get())) {
				RecordGraphTargetIssue(
					stats,
					tree->bone[boneIndex].node.get(),
					ref.boneIndex,
					tree->bone[boneIndex].name.c_str(),
					"outside root");
			}
		}

		return stats;
	}

	void CollectGraphWrittenBoneNames(
		const RE::IAnimationGraphManagerHolder& a_holder,
		std::unordered_set<std::string>& a_names)
	{
		std::uint32_t count = 0;
		const auto* refs = GetActiveGraphBoneRefs(a_holder, count);
		if (!refs || count == 0) {
			return;
		}

		for (std::uint32_t i = 0; i < count; ++i) {
			const auto& ref = refs[i];
			if (!ref.target) {
				continue;
			}

			if (ref.boneIndex < 0) {
				const auto* object = reinterpret_cast<const RE::NiAVObject*>(ref.target);
				AddGraphBoneName(object->GetName(), a_names);
				continue;
			}

			const auto* tree = reinterpret_cast<const RE::BSFlattenedBoneTree*>(ref.target);
			const auto boneIndex = static_cast<std::int32_t>(ref.boneIndex);
			if (!tree->bone || boneIndex >= tree->boneCount) {
				continue;
			}

			const auto& bone = tree->bone[boneIndex];
			AddGraphBoneName(bone.name, a_names);
			if (bone.name.empty() && bone.node) {
				AddGraphBoneName(bone.node->GetName(), a_names);
			}
		}
	}

	void ForEachGeometry(RE::NiAVObject* a_object, const std::function<void(RE::BSGeometry&)>& a_visitor)
	{
		if (!a_object) {
			return;
		}

		if (auto* geometry = a_object->IsGeometry()) {
			a_visitor(*geometry);
		}

		auto* node = a_object->IsNode();
		if (!node) {
			return;
		}

		for (auto& child : node->children) {
			ForEachGeometry(child.get(), a_visitor);
		}
	}

	void ForEachAVObject(RE::NiAVObject* a_object, const std::function<void(RE::NiAVObject&)>& a_visitor)
	{
		if (!a_object) {
			return;
		}

		a_visitor(*a_object);

		auto* node = a_object->IsNode();
		if (!node) {
			return;
		}

		for (auto& child : node->children) {
			ForEachAVObject(child.get(), a_visitor);
		}
	}
}
