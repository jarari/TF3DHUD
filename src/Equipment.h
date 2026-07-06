#pragma once

#include "RE/B/BIPED_OBJECT.h"

#include <cstdint>
#include <string>
#include <vector>

namespace RE
{
	class BIPOBJECT;
	class TESForm;
	class TESObjectREFR;
}

namespace TF3DHud::Equipment
{
	inline constexpr std::uint32_t kEditorSlotBase = 30;
	inline constexpr std::uint32_t kEditorSlotCount = 32;
	inline constexpr std::uint32_t kAllEditorSlotsMask = 0xFFFF'FFFFu;

	struct EquippedArmorInfo
	{
		std::string name;
		std::uint32_t slotMask{ 0 };
		bool excluded{ false };
	};

	[[nodiscard]] const char* PrettySlotName(std::uint32_t a_slotIndex);
	[[nodiscard]] std::string FormatSlotList(std::uint32_t a_slotMask);
	[[nodiscard]] bool IsEditorSlotIndex(std::int32_t a_slotIndex);
	[[nodiscard]] bool IsSlotEnabled(std::uint32_t a_editorSlotMask, std::uint32_t a_slotIndex);
	[[nodiscard]] bool IsArmorExcludedBySlotMask(const RE::TESForm& a_form, std::uint32_t a_editorSlotMask);
	[[nodiscard]] bool IsBipedObjectExcludedBySlotMask(
		RE::BIPED_OBJECT a_slot,
		const RE::BIPOBJECT& a_object,
		std::uint32_t a_editorSlotMask);
	[[nodiscard]] std::uint32_t EffectiveEditorSlotMask(const RE::TESObjectREFR& a_reference);
	[[nodiscard]] std::vector<EquippedArmorInfo> CollectEquippedArmors(
		RE::TESObjectREFR& a_reference,
		std::uint32_t a_effectiveEditorSlotMask);
}
