#include "Utils.h"

namespace TF3DHud
{
	std::int32_t MakeRel32Displacement(const std::uintptr_t a_sourceNext, const std::uintptr_t a_destination)
	{
		const auto displacement =
			static_cast<std::int64_t>(a_destination) - static_cast<std::int64_t>(a_sourceNext);
		if (displacement < (std::numeric_limits<std::int32_t>::min)() ||
			displacement > (std::numeric_limits<std::int32_t>::max)()) {
			REX::FAIL(
				"TF3DHud V1 rel32 displacement out of range sourceNext={:X} destination={:X}",
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
