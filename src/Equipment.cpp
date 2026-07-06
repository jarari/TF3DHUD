#include "Equipment.h"

#include "Config.h"

#include "RE/A/Actor.h"
#include "RE/B/BGSInventoryList.h"
#include "RE/B/BIPOBJECT.h"
#include "RE/P/PowerArmor.h"
#include "RE/T/TESBoundObject.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESFullName.h"
#include "RE/T/TESObjectARMO.h"
#include "RE/T/TESObjectREFR.h"

#include <array>
#include <cstdio>
#include <utility>

namespace TF3DHud::Equipment
{
	namespace
	{
		constexpr std::array<const char*, kEditorSlotCount> kPrettySlotNames{
			"Hair Top",
			"Hair Long",
			"Head",
			"Body",
			"L Hand",
			"R Hand",
			"Under Torso",
			"Under L Arm",
			"Under R Arm",
			"Under L Leg",
			"Under R Leg",
			"Above Torso",
			"Above L Arm",
			"Above R Arm",
			"Above L Leg",
			"Above R Leg",
			"Headband",
			"Eyes",
			"Beard",
			"Mouth",
			"Neck",
			"Ring",
			"Scalp",
			"Decapitation",
			"Unnamed 1",
			"Unnamed 2",
			"Unnamed 3",
			"Unnamed 4",
			"Unnamed 5",
			"Shield",
			"Pip-Boy",
			"FX",
		};

		[[nodiscard]] std::string ResolveArmorName(const RE::BGSInventoryItem& a_item, const RE::TESObjectARMO& a_armor, const std::uint32_t a_stackID)
		{
			if (const auto* displayName = a_item.GetDisplayFullName(a_stackID); displayName && displayName[0] != '\0') {
				return displayName;
			}
			if (const auto* fullName = a_armor.GetFullName(); fullName && fullName[0] != '\0') {
				return fullName;
			}
			if (const auto* editorID = a_armor.GetFormEditorID(); editorID && editorID[0] != '\0') {
				return editorID;
			}

			char buffer[32]{};
			std::snprintf(buffer, sizeof(buffer), "0x%08X", a_armor.GetFormID());
			return buffer;
		}
	}

	const char* PrettySlotName(const std::uint32_t a_slotIndex)
	{
		return a_slotIndex < kPrettySlotNames.size() ? kPrettySlotNames[a_slotIndex] : "Unknown";
	}

	std::string FormatSlotList(const std::uint32_t a_slotMask)
	{
		std::string result;
		for (std::uint32_t index = 0; index < kEditorSlotCount; ++index) {
			if ((a_slotMask & (1u << index)) == 0) {
				continue;
			}

			if (!result.empty()) {
				result += ", ";
			}
			result += std::to_string(kEditorSlotBase + index);
		}

		return result.empty() ? "-" : result;
	}

	bool IsEditorSlotIndex(const std::int32_t a_slotIndex)
	{
		return a_slotIndex >= 0 && a_slotIndex < static_cast<std::int32_t>(kEditorSlotCount);
	}

	bool IsSlotEnabled(const std::uint32_t a_editorSlotMask, const std::uint32_t a_slotIndex)
	{
		return a_slotIndex < kEditorSlotCount && (a_editorSlotMask & (1u << a_slotIndex)) != 0;
	}

	bool IsArmorExcludedBySlotMask(const RE::TESForm& a_form, const std::uint32_t a_editorSlotMask)
	{
		if (!a_form.Is(RE::ENUM_FORM_ID::kARMO)) {
			return false;
		}

		const auto filledSlots = a_form.GetFilledSlots();
		if (filledSlots == static_cast<std::uint32_t>(-1)) {
			return false;
		}

		return (filledSlots & ~a_editorSlotMask) != 0;
	}

	bool IsBipedObjectExcludedBySlotMask(
		const RE::BIPED_OBJECT a_slot,
		const RE::BIPOBJECT& a_object,
		const std::uint32_t a_editorSlotMask)
	{
		const auto slotIndex = std::to_underlying(a_slot);
		if (!IsEditorSlotIndex(slotIndex)) {
			return false;
		}

		if (!IsSlotEnabled(a_editorSlotMask, static_cast<std::uint32_t>(slotIndex))) {
			return true;
		}

		auto* form = a_object.parent.object;
		return form && IsArmorExcludedBySlotMask(*form, a_editorSlotMask);
	}

	std::uint32_t EffectiveEditorSlotMask(const RE::TESObjectREFR& a_reference)
	{
		if (const auto* actor = a_reference.As<RE::Actor>(); actor && RE::PowerArmor::ActorInPowerArmor(*actor)) {
			return kAllEditorSlotsMask;
		}

		return GetConfig().equipment.syncSlotMask & kAllEditorSlotsMask;
	}

	std::vector<EquippedArmorInfo> CollectEquippedArmors(
		RE::TESObjectREFR& a_reference,
		const std::uint32_t a_effectiveEditorSlotMask)
	{
		std::vector<EquippedArmorInfo> armors;

		auto* inventory = a_reference.inventoryList;
		if (!inventory) {
			return armors;
		}

		for (auto& item : inventory->data) {
			auto* armor = item.object ? item.object->As<RE::TESObjectARMO>() : nullptr;
			if (!armor) {
				continue;
			}

			const auto filledSlots = armor->GetFilledSlots();
			if (filledSlots == static_cast<std::uint32_t>(-1) || filledSlots == 0) {
				continue;
			}

			std::uint32_t stackID = 0;
			for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++stackID) {
				if (!stack->IsEquipped()) {
					continue;
				}

				armors.push_back({
					.name = ResolveArmorName(item, *armor, stackID),
					.slotMask = filledSlots,
					.excluded = (filledSlots & ~a_effectiveEditorSlotMask) != 0,
				});
			}
		}

		return armors;
	}
}
