#include "PreviewHeadParts.h"

#include "Address.h"
#include "Equipment.h"
#include "Utils.h"

#include "RE/B/BGSHeadPart.h"
#include "RE/B/BSGeometry.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESModel.h"

#include <cstdint>
#include <cstring>
#include <utility>

namespace TF3DHud::PreviewHeadParts
{
	namespace
	{
		auto& g_getNPCHeadPart = Address::GetNPCHeadPart;
		auto& g_getDefaultRaceHeadPart = Address::GetDefaultRaceHeadPart;
		auto& g_getNumSegments = Address::GetNumSegments;
		auto& g_getSubSegmentCount = Address::GetSubSegmentCount;
		auto& g_getSubSegmentIndex = Address::GetSubSegmentIndex;
		auto& g_getUserIndex = Address::GetUserIndex;
		auto& g_enableAllSegments = Address::EnableAllSegments;
		auto& g_setSegmentDisableCount = Address::SetSegmentDisableCount;
		auto& g_enableSegment = Address::EnableSegment;
		auto& g_disableSegment = Address::DisableSegment;

		struct HeadPartVisibilityStats
		{
			std::uint32_t segmentEdits{ 0 };
			std::uint32_t objectCulls{ 0 };
		};

		[[nodiscard]] bool IsRaceHeadPartSlot(const RE::TESRace& a_race, const std::int32_t a_slot)
		{
			const auto headSlot = std::to_underlying(a_race.data.headObject);
			const auto hairSlot = std::to_underlying(a_race.data.hairObject);
			const auto beardSlot = std::to_underlying(a_race.data.beardObject);
			return a_slot == headSlot ||
			       a_slot == hairSlot ||
			       a_slot == hairSlot + 1 ||
			       a_slot == beardSlot;
		}

		[[nodiscard]] bool IsIgnoredHeadPartMaskForm(const RE::TESForm& a_form)
		{
			// IDA OG 1.10.163: FixDisplayedHeadParts skips light, weapon, and
			// ammo forms while building the equipped biped-slot mask.
			return a_form.Is(RE::ENUM_FORM_ID::kLIGH) ||
			       a_form.Is(RE::ENUM_FORM_ID::kWEAP) ||
			       a_form.Is(RE::ENUM_FORM_ID::kAMMO);
		}

		[[nodiscard]] std::uint32_t BuildEquippedInventoryBipedMask(
			RE::TESObjectREFR& a_reference,
			const std::uint32_t a_editorSlotMask,
			bool& a_usedInventory)
		{
			auto* inventory = a_reference.inventoryList;
			if (!inventory) {
				a_usedInventory = false;
				return 0;
			}

			a_usedInventory = true;
			std::uint32_t mask = 0;
			for (auto& item : inventory->data) {
				auto* form = item.object;
				if (!form || !item.IsEquipped(0) || IsIgnoredHeadPartMaskForm(*form)) {
					continue;
				}
				if (Equipment::IsArmorExcludedBySlotMask(*form, a_editorSlotMask)) {
					continue;
				}

				const auto filledSlots = form->GetFilledSlots();
				if (filledSlots != static_cast<std::uint32_t>(-1)) {
					mask |= filledSlots & a_editorSlotMask;
				}
			}
			return mask;
		}

		[[nodiscard]] std::uint32_t BuildEquippedBipedMaskFromBiped(
			const RE::BipedAnim& a_biped,
			const RE::TESRace& a_race,
			const std::uint32_t a_editorSlotMask)
		{
			std::uint32_t mask = 0;
			for (std::int32_t i = 0; i < 32; ++i) {
				const auto& object = a_biped.object[i];
				if (Equipment::IsBipedObjectExcludedBySlotMask(
						static_cast<RE::BIPED_OBJECT>(i),
						object,
						a_editorSlotMask)) {
					continue;
				}

				if (auto* form = object.parent.object) {
					if (IsIgnoredHeadPartMaskForm(*form)) {
						continue;
					}

					const auto filledSlots = form->GetFilledSlots();
					if (filledSlots != static_cast<std::uint32_t>(-1)) {
						mask |= filledSlots & a_editorSlotMask;
						continue;
					}
				}

				if (object.partClone &&
					Equipment::IsSlotEnabled(a_editorSlotMask, static_cast<std::uint32_t>(i)) &&
					!IsRaceHeadPartSlot(a_race, i)) {
					const auto fallbackSlot = 1u << static_cast<std::uint32_t>(i);
					mask |= fallbackSlot;
				}
			}
			return mask;
		}

		[[nodiscard]] std::uint32_t BuildEquippedBipedMask(
			RE::TESObjectREFR& a_reference,
			const RE::BipedAnim& a_biped,
			const RE::TESRace& a_race)
		{
			const auto editorSlotMask = Equipment::EffectiveEditorSlotMask(a_reference);
			bool usedInventory = false;
			const auto inventoryMask = BuildEquippedInventoryBipedMask(a_reference, editorSlotMask, usedInventory);
			const auto bipedMask = BuildEquippedBipedMaskFromBiped(a_biped, a_race, editorSlotMask);
			// FixDisplayedHeadParts uses inventory; biped attach visibility uses
			// the live BipedAnim object array. The preview applies both outcomes.
			return (usedInventory ? inventoryMask : 0) | bipedMask;
		}

		[[nodiscard]] bool IsBipedSlotMasked(const std::uint32_t a_mask, const RE::BIPED_OBJECT a_slot)
		{
			const auto slot = std::to_underlying(a_slot);
			if (slot < 0 || slot >= 32) {
				return false;
			}
			return (a_mask & (1u << static_cast<std::uint32_t>(slot))) != 0;
		}

		[[nodiscard]] RE::BIPED_OBJECT AddBipedSlot(const RE::BIPED_OBJECT a_slot, const std::int32_t a_delta)
		{
			return static_cast<RE::BIPED_OBJECT>(std::to_underlying(a_slot) + a_delta);
		}

		[[nodiscard]] bool ApplyBipedSlotSegmentVisibility(
			RE::NiAVObject& a_object,
			const RE::BIPED_OBJECT a_slot,
			const bool a_hide,
			HeadPartVisibilityStats& a_stats)
		{
			auto* geometry = a_object.IsGeometry();
			if (!geometry) {
				return true;
			}

			bool editedSegment = false;
			const auto subsegmentID = static_cast<std::uint32_t>(std::to_underlying(a_slot) + 30);
			auto* segmentData = geometry->GetSegmentData();
			if (segmentData) {
				const auto segmentCount = g_getNumSegments(segmentData);
				for (std::uint32_t segment = 0; segment < segmentCount; ++segment) {
					const auto subsegment = g_getSubSegmentIndex(segmentData, segment, subsegmentID);
					if (subsegment == 0xFF) {
						continue;
					}

					editedSegment = true;
					++a_stats.segmentEdits;
					if (a_hide) {
						g_disableSegment(segmentData, segment, subsegment, false);
					} else {
						g_enableSegment(segmentData, segment, subsegment, false);
					}
				}
			}

			return editedSegment;
		}

		[[nodiscard]] const char* GetBipedPartModelName(const RE::BIPOBJECT& a_object)
		{
			auto* model = a_object.part;
			const auto* path = model ? model->GetModel() : nullptr;
			return path && path[0] != '\0' ? path : nullptr;
		}

		[[nodiscard]] bool BipedPartModelMatches(
			const RE::BipedAnim& a_biped,
			const std::int32_t a_lhsSlot,
			const std::int32_t a_rhsSlot)
		{
			if (a_lhsSlot < 0 || a_lhsSlot >= 32 || a_rhsSlot < 0 || a_rhsSlot >= 32) {
				return false;
			}

			const auto* lhs = GetBipedPartModelName(a_biped.object[a_lhsSlot]);
			const auto* rhs = GetBipedPartModelName(a_biped.object[a_rhsSlot]);
			return lhs && rhs && _stricmp(lhs, rhs) == 0;
		}

		[[nodiscard]] bool ShouldEnableBipedUserIndex(
			const RE::BipedAnim& a_biped,
			const RE::BIPED_OBJECT a_activeSlot,
			const std::uint32_t a_editorSlotMask,
			const std::uint32_t a_userIndex)
		{
			if (a_userIndex < 30 || a_userIndex > 61) {
				return false;
			}

			const auto indexedSlot = static_cast<std::int32_t>(a_userIndex - 30);
			if (!Equipment::IsSlotEnabled(a_editorSlotMask, static_cast<std::uint32_t>(indexedSlot))) {
				return true;
			}

			const auto activeSlot = std::to_underlying(a_activeSlot);
			return indexedSlot == activeSlot || BipedPartModelMatches(a_biped, indexedSlot, activeSlot);
		}

		void ApplyBipedSkinPartsVisibility(
			RE::BSGeometry& a_geometry,
			const RE::BipedAnim& a_biped,
			const RE::BIPED_OBJECT a_activeSlot,
			const std::uint32_t a_editorSlotMask,
			HeadPartVisibilityStats& a_stats)
		{
			auto* segmentData = a_geometry.GetSegmentData();
			if (!segmentData) {
				return;
			}

			const auto segmentCount = g_getNumSegments(segmentData);
			for (std::uint32_t segment = 0; segment < segmentCount; ++segment) {
				const auto subSegmentCount = g_getSubSegmentCount(segmentData, segment);
				for (std::uint32_t subSegment = 0; subSegment < subSegmentCount; ++subSegment) {
					const auto userIndex = g_getUserIndex(segmentData, segment, subSegment);
					if (userIndex >= 30 && userIndex <= 61) {
						g_setSegmentDisableCount(segmentData, segment, subSegment, 0);
						if (ShouldEnableBipedUserIndex(a_biped, a_activeSlot, a_editorSlotMask, userIndex)) {
							g_enableSegment(segmentData, segment, subSegment, false);
						} else {
							g_disableSegment(segmentData, segment, subSegment, false);
						}
						++a_stats.segmentEdits;
					} else if (userIndex >= 100 && userIndex <= 102) {
						g_setSegmentDisableCount(segmentData, segment, subSegment, 0);
						g_disableSegment(segmentData, segment, subSegment, false);
						++a_stats.segmentEdits;
					}
				}
			}
		}

		void ApplyBipedSkinPartsVisibilityRecursive(
			RE::NiAVObject& a_root,
			const RE::BipedAnim& a_biped,
			const RE::BIPED_OBJECT a_activeSlot,
			const std::uint32_t a_editorSlotMask,
			HeadPartVisibilityStats& a_stats)
		{
			// IDA OG 1.10.163: BipedAnim::HideShowBufferedSkin recurses to
			// geometries, then HideShowSkinParts processes user indices 30..61.
			ForEachGeometry(std::addressof(a_root), [&](RE::BSGeometry& a_geometry) {
				ApplyBipedSkinPartsVisibility(a_geometry, a_biped, a_activeSlot, a_editorSlotMask, a_stats);
			});
		}

		void ApplyHeadPartObjectVisibility(
			RE::NiAVObject& a_object,
			const RE::BIPED_OBJECT a_slot,
			const bool a_hide,
			HeadPartVisibilityStats& a_stats)
		{
			const bool editedSegment = ApplyBipedSlotSegmentVisibility(a_object, a_slot, a_hide, a_stats);
			if (!editedSegment) {
				a_object.SetAppCulled(a_hide);
				++a_stats.objectCulls;
			} else if (!a_hide) {
				a_object.SetAppCulled(false);
				a_object.fadeAmount = 1.0F;
				++a_stats.objectCulls;
			}
		}

		void ApplyHeadPartVisibilityRecursive(
			RE::NiAVObject& a_faceNode,
			RE::BGSHeadPart* a_headPart,
			const RE::BIPED_OBJECT a_slot,
			const bool a_hide,
			HeadPartVisibilityStats& a_stats)
		{
			if (!a_headPart || a_headPart->formEditorID.empty()) {
				return;
			}

			if (auto* object = a_faceNode.GetObjectByName(a_headPart->formEditorID)) {
				ApplyHeadPartObjectVisibility(*object, a_slot, a_hide, a_stats);
			}

			for (auto* extraPart : a_headPart->extraParts) {
				ApplyHeadPartVisibilityRecursive(a_faceNode, extraPart, a_slot, a_hide, a_stats);
			}
		}

		[[nodiscard]] RE::BGSHeadPart* GetDisplayedHeadPart(
			RE::TESNPC& a_npc,
			const RE::TESRace& a_race,
			const RE::BGSHeadPart::HeadPartType a_type)
		{
			if (auto* headPart = g_getNPCHeadPart(std::addressof(a_npc), a_type)) {
				return headPart;
			}
			return g_getDefaultRaceHeadPart(std::addressof(a_race), a_npc.GetSex(), a_type);
		}

		void EnableAllSegments(RE::NiAVObject& a_object)
		{
			ForEachGeometry(std::addressof(a_object), [&](RE::BSGeometry& a_geometry) {
				if (auto* segmentData = a_geometry.GetSegmentData()) {
					g_enableAllSegments(segmentData);
				}
			});
		}

		RE::NiAVObject* ApplyFaceGenHeadObjectVisibility(
			RE::NiAVObject& a_faceNode,
			const RE::TESRace& a_race,
			const std::uint32_t a_equippedMask,
			HeadPartVisibilityStats& a_stats)
		{
			auto* headObject = a_faceNode.GetObjectByName(RE::BSFixedString("RaceHeadSkinned"));
			if (!headObject) {
				return nullptr;
			}

			const bool hideHead = IsBipedSlotMasked(a_equippedMask, a_race.data.headObject);
			headObject->SetAppCulled(hideHead);
			++a_stats.objectCulls;
			if (!hideHead) {
				EnableAllSegments(*headObject);
			}
			return headObject;
		}
	}

	void RestoreDisabledSlotVisibility(RE::NiAVObject& a_root, const std::uint32_t a_editorSlotMask)
	{
		ForEachAVObject(std::addressof(a_root), [](RE::NiAVObject& a_object) {
			a_object.SetAppCulled(false);
			a_object.fadeAmount = 1.0F;
		});

		ForEachGeometry(std::addressof(a_root), [&](RE::BSGeometry& a_geometry) {
			auto* segmentData = a_geometry.GetSegmentData();
			if (!segmentData) {
				return;
			}

			const auto segmentCount = g_getNumSegments(segmentData);
			for (std::uint32_t segment = 0; segment < segmentCount; ++segment) {
				const auto subSegmentCount = g_getSubSegmentCount(segmentData, segment);
				for (std::uint32_t subSegment = 0; subSegment < subSegmentCount; ++subSegment) {
					const auto userIndex = g_getUserIndex(segmentData, segment, subSegment);
					if (userIndex < 30 || userIndex > 61) {
						continue;
					}

					const auto slotIndex = userIndex - 30;
					if (Equipment::IsSlotEnabled(a_editorSlotMask, slotIndex)) {
						continue;
					}

					g_setSegmentDisableCount(segmentData, segment, subSegment, 0);
					g_enableSegment(segmentData, segment, subSegment, false);
				}
			}
		});
	}

	void ApplyBipedVisibility(
		RE::TESNPC& a_npc,
		RE::TESRace& a_race,
		RE::NiAVObject& a_faceNode,
		RE::TESObjectREFR& a_reference,
		const RE::BipedAnim& a_sourceBiped)
	{
		const auto editorSlotMask = Equipment::EffectiveEditorSlotMask(a_reference);
		const auto equippedMask = BuildEquippedBipedMask(a_reference, a_sourceBiped, a_race);
		auto* hairPart = GetDisplayedHeadPart(a_npc, a_race, RE::BGSHeadPart::HeadPartType::kHair);
		auto* facialHairPart = GetDisplayedHeadPart(a_npc, a_race, RE::BGSHeadPart::HeadPartType::kFacialHair);

		const auto hairSlot = a_race.data.hairObject;
		const auto hairLongSlot = AddBipedSlot(hairSlot, 1);
		const auto beardSlot = a_race.data.beardObject;
		const bool hideHead = IsBipedSlotMasked(equippedMask, a_race.data.headObject);
		const bool hideHairTop = IsBipedSlotMasked(equippedMask, hairSlot);
		const bool hideHairLong = IsBipedSlotMasked(equippedMask, hairLongSlot);
		const bool hideFacialHair = IsBipedSlotMasked(equippedMask, beardSlot);

		HeadPartVisibilityStats stats;
		// Editor slot 32 is the race head object. In the preview the face
		// node owns hair/facial headparts, so cull it when that slot is filled.
		a_faceNode.SetAppCulled(hideHead);
		(void)ApplyFaceGenHeadObjectVisibility(a_faceNode, a_race, equippedMask, stats);
		if (hideHead) {
			ApplyBipedSkinPartsVisibilityRecursive(a_faceNode, a_sourceBiped, a_race.data.headObject, editorSlotMask, stats);
			return;
		}

		ApplyHeadPartVisibilityRecursive(a_faceNode, hairPart, hairSlot, hideHairTop, stats);
		ApplyHeadPartVisibilityRecursive(a_faceNode, hairPart, hairLongSlot, hideHairLong, stats);
		ApplyHeadPartVisibilityRecursive(a_faceNode, facialHairPart, beardSlot, hideFacialHair, stats);
	}
}
