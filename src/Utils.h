#pragma once

#include "RE/B/BSGeometry.h"
#include "RE/N/NiNode.h"
#include "REL/ASM.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>

namespace TF3DHud
{
	std::int32_t MakeRel32Displacement(std::uintptr_t a_sourceNext, std::uintptr_t a_destination);
	void WriteBranch5(std::uintptr_t a_source, std::uintptr_t a_destination);

	template <class T>
	T CreateBranchGateway5(
		const char* a_name,
		REL::Relocation<std::uintptr_t>& a_target,
		const std::size_t a_prologueSize,
		void* a_hook)
	{
		const auto targetAddress = a_target.address();
		auto& trampoline = REL::GetTrampoline();
		auto* gateway = static_cast<std::byte*>(trampoline.allocate(a_prologueSize + sizeof(REL::ASM::JMP14)));

		std::memcpy(gateway, reinterpret_cast<const void*>(targetAddress), a_prologueSize);

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
	void CollectNamedNodes(RE::NiAVObject* a_object, std::unordered_map<std::string, RE::NiAVObject*>& a_nodes);
	RE::NiAVObject* FindNodeByName(
		const std::unordered_map<std::string, RE::NiAVObject*>& a_nodes,
		std::string_view a_name);
	void ForEachGeometry(RE::NiAVObject* a_object, const std::function<void(RE::BSGeometry&)>& a_visitor);
	void ForEachAVObject(RE::NiAVObject* a_object, const std::function<void(RE::NiAVObject&)>& a_visitor);
}
