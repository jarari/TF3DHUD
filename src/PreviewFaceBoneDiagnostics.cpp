#include "PreviewFaceBoneDiagnostics.h"

#include "RE/T/TESForm.h"
#include "RE/T/TESModel.h"
#include "RE/T/TESObjectARMA.h"

#include <utility>

namespace TF3DHud::PreviewFaceBoneDiagnostics
{
	namespace
	{
		[[nodiscard]] const char* SafeCString(const char* a_value)
		{
			return a_value ? a_value : "";
		}

		[[nodiscard]] std::uint32_t FilledSlotsForLog(const RE::TESForm* a_form)
		{
			if (!a_form) {
				return 0;
			}

			const auto slots = a_form->GetFilledSlots();
			return slots == static_cast<std::uint32_t>(-1) ? 0 : slots;
		}
	}

	FaceBoneCandidateDetail MakeCandidateDetail(
		const char* a_source,
		const std::int32_t a_slot,
		const RE::BIPOBJECT& a_object,
		const char* a_selectedFacebones,
		std::string a_result,
		const std::uint32_t a_pruned)
	{
		auto* parent = a_object.parent.object;
		auto* addon = a_object.armorAddon;

		FaceBoneCandidateDetail detail;
		detail.source = SafeCString(a_source);
		detail.slot = a_slot;
		detail.parentPtr = reinterpret_cast<std::uintptr_t>(parent);
		detail.parentFormID = parent ? parent->GetFormID() : 0;
		detail.parentType = parent ? SafeCString(parent->GetFormTypeString()) : "";
		detail.parentEditorID = parent ? SafeCString(parent->GetFormEditorID()) : "";
		detail.parentSlots = FilledSlotsForLog(parent);
		detail.addonPtr = reinterpret_cast<std::uintptr_t>(addon);
		detail.addonFormID = addon ? addon->GetFormID() : 0;
		detail.addonEditorID = addon ? SafeCString(addon->GetFormEditorID()) : "";
		detail.addonSlots = addon ? addon->bipedModelData.bipedObjectSlots : 0;
		detail.partModel = a_object.part ? SafeCString(a_object.part->GetModel()) : "";
		detail.maleFacebones = addon ? SafeCString(addon->bipedModelFacebones[0].GetModel()) : "";
		detail.femaleFacebones = addon ? SafeCString(addon->bipedModelFacebones[1].GetModel()) : "";
		detail.selectedFacebones = SafeCString(a_selectedFacebones);
		detail.result = std::move(a_result);
		detail.pruned = a_pruned;
		return detail;
	}

	void LogAttachFailure(
		const RE::BipedAnim& a_biped,
		const RE::NiAVObject& a_actorRoot,
		const RE::SEX a_sex,
		const std::size_t a_previewNodeCount,
		const FaceBoneAttachStats& a_stats,
		const std::vector<FaceBoneCandidateDetail>& a_details)
	{
		REX::WARN(
			"preview facebone scan failed: biped={:X}, actorRoot={:X}, sex={}, previewNodes={}, candidates={}, noFacebonesModel={}, duplicatePath={}, loadFailed={}, prunedEmpty={}, prunedDuplicates={}, movedUnknownNodes={}, converted={}, reattached={}, skeletonCandidates={}, skeletonAttached={}, skeletonLoadFailed={}, skeletonPrunedEmpty={}",
			reinterpret_cast<std::uintptr_t>(std::addressof(a_biped)),
			reinterpret_cast<std::uintptr_t>(std::addressof(a_actorRoot)),
			std::to_underlying(a_sex),
			a_previewNodeCount,
			a_stats.candidates,
			a_stats.noFacebonesModel,
			a_stats.duplicatePath,
			a_stats.loadFailed,
			a_stats.prunedEmpty,
			a_stats.prunedDuplicates,
			a_stats.movedUnknownNodes,
			a_stats.converted,
			a_stats.reattached,
			a_stats.skeletonCandidates,
			a_stats.skeletonAttached,
			a_stats.skeletonLoadFailed,
			a_stats.skeletonPrunedEmpty);

		for (const auto& detail : a_details) {
			REX::WARN(
				"preview facebone candidate: source={}, slot={}, parent={:X}/{:08X}/{} '{}', parentSlots={:08X}, addon={:X}/{:08X} '{}', addonSlots={:08X}, part='{}', facebonesMale='{}', facebonesFemale='{}', selected='{}', result={}, pruned={}",
				detail.source,
				detail.slot,
				detail.parentPtr,
				detail.parentFormID,
				detail.parentType,
				detail.parentEditorID,
				detail.parentSlots,
				detail.addonPtr,
				detail.addonFormID,
				detail.addonEditorID,
				detail.addonSlots,
				detail.partModel,
				detail.maleFacebones,
				detail.femaleFacebones,
				detail.selectedFacebones,
				detail.result,
				detail.pruned);
		}
	}
}
