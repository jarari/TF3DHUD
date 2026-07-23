#pragma once

#include "RE/B/BSFlattenedBoneTree.h"
#include "RE/B/BSGeometry.h"
#include "RE/I/IAnimationGraphManagerHolder.h"
#include "RE/N/NiNode.h"
#include "RE/T/TESObjectREFR.h"
#include "REL/ASM.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <array>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace TF3DHud
{
	struct GraphTargetStats
	{
		std::uint32_t refs{ 0 };
	};

	std::int32_t MakeRel32Displacement(std::uintptr_t a_sourceNext, std::uintptr_t a_destination);
	void WriteBranch5(std::uintptr_t a_source, std::uintptr_t a_destination);

	struct RipRel32Patch
	{
		std::size_t instructionOffset;
		std::size_t displacementOffset;
		std::size_t instructionSize;
	};

	bool ReadExistingBranchTarget(
		std::uintptr_t a_targetAddress,
		const std::byte* a_targetBytes,
		std::uintptr_t& a_branchTarget);

	template <class T>
	T CreateBranchGateway5(
		const char* a_name,
		REL::Relocation<std::uintptr_t>& a_target,
		const std::size_t a_prologueSize,
		void* a_hook,
		std::initializer_list<RipRel32Patch> a_ripPatches = {})
	{
		const auto targetAddress = a_target.address();
		const auto* targetBytes = reinterpret_cast<const std::byte*>(targetAddress);
		auto& trampoline = REL::GetTrampoline();

		std::uintptr_t existingBranchTarget = 0;
		if (ReadExistingBranchTarget(targetAddress, targetBytes, existingBranchTarget)) {
			auto* gateway = trampoline.allocate<REL::ASM::JMP14>(existingBranchTarget);
			WriteBranch5(targetAddress, reinterpret_cast<std::uintptr_t>(a_hook));
			REX::INFO(
				"{} found existing branch at {:X}; chaining through {:X}",
				a_name,
				targetAddress,
				existingBranchTarget);
			return reinterpret_cast<T>(gateway);
		}

		if (a_prologueSize >= sizeof(REL::ASM::CALL5) && targetBytes[0] == std::byte{ 0xE8 }) {
			REX::WARN("{} branch gateway not installed at {:X}; captured CALL rel32", a_name, targetAddress);
			return nullptr;
		}

		auto* gateway = static_cast<std::byte*>(trampoline.allocate(a_prologueSize + sizeof(REL::ASM::JMP14)));
		std::memcpy(gateway, targetBytes, a_prologueSize);

		for (const auto& patch : a_ripPatches) {
			std::int32_t oldDisplacement = 0;
			std::memcpy(
				std::addressof(oldDisplacement),
				targetBytes + patch.displacementOffset,
				sizeof(oldDisplacement));
			const auto originalTarget =
				targetAddress + patch.instructionOffset + patch.instructionSize + oldDisplacement;
			const auto gatewayNext =
				reinterpret_cast<std::uintptr_t>(gateway) + patch.instructionOffset + patch.instructionSize;
			const auto newDisplacement = MakeRel32Displacement(gatewayNext, originalTarget);
			std::memcpy(
				gateway + patch.displacementOffset,
				std::addressof(newDisplacement),
				sizeof(newDisplacement));
		}

		const REL::ASM::JMP14 jumpBack{ targetAddress + a_prologueSize };
		std::memcpy(gateway + a_prologueSize, std::addressof(jumpBack), sizeof(jumpBack));

		WriteBranch5(targetAddress, reinterpret_cast<std::uintptr_t>(a_hook));
		REX::INFO("{} branch gateway installed at {:X}; prologueSize={}", a_name, targetAddress, a_prologueSize);
		return reinterpret_cast<T>(gateway);
	}

	template <class T>
	void HashValue(std::uint64_t& a_hash, T a_value)
	{
		const auto value = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(a_value));
		a_hash ^= value + 0x9E3779B97F4A7C15ull + (a_hash << 6) + (a_hash >> 2);
	}

	bool NamesEqual(std::string_view a_lhs, std::string_view a_rhs);
	bool IsDescendantOf(RE::NiAVObject& a_object, RE::NiAVObject& a_potentialAncestor);
	bool ContainsObject(RE::NiAVObject& a_root, RE::NiAVObject& a_target);
	void CollectNamedNodes(RE::NiAVObject* a_object, std::unordered_map<std::string, RE::NiAVObject*>& a_nodes);
	RE::NiAVObject* FindNodeByName(
		const std::unordered_map<std::string, RE::NiAVObject*>& a_nodes,
		std::string_view a_name);
	RE::BSFlattenedBoneTree::FlattenedBone* FindFlattenedBoneByName(
		RE::BSFlattenedBoneTree& a_tree,
		std::string_view a_name);
	void CollectFlattenedBoneNodes(
		RE::BSFlattenedBoneTree* a_tree,
		std::unordered_map<std::string, RE::NiAVObject*>& a_nodes);
	RE::BSFlattenedBoneTree* FindFlattenedBoneTree(RE::NiAVObject* a_root);
	RE::IAnimationGraphManagerHolder& GetAnimationGraphHolder(RE::TESObjectREFR& a_reference);
	GraphTargetStats InspectGraphTargets(
		const RE::IAnimationGraphManagerHolder& a_holder,
		RE::NiAVObject& a_expectedRoot);
	void CollectGraphWrittenBoneNames(
		const RE::IAnimationGraphManagerHolder& a_holder,
		std::unordered_set<std::string>& a_names);
	void ForEachGeometry(RE::NiAVObject* a_object, const std::function<void(RE::BSGeometry&)>& a_visitor);
	void ForEachAVObject(RE::NiAVObject* a_object, const std::function<void(RE::NiAVObject&)>& a_visitor);
}
