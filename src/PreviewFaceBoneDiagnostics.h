#pragma once

#include "RE/B/BipedAnim.h"
#include "RE/N/NiAVObject.h"
#include "RE/S/SEX.h"

#include <cstdint>
#include <string>
#include <vector>

namespace TF3DHud::PreviewFaceBoneDiagnostics
{
	struct FaceBoneAttachStats
	{
		std::uint32_t candidates{ 0 };
		std::uint32_t noFacebonesModel{ 0 };
		std::uint32_t duplicatePath{ 0 };
		std::uint32_t loadFailed{ 0 };
		std::uint32_t prunedEmpty{ 0 };
		std::uint32_t attached{ 0 };
		std::uint32_t converted{ 0 };
		std::uint32_t reattached{ 0 };
		std::uint32_t prunedDuplicates{ 0 };
		std::uint32_t movedUnknownNodes{ 0 };
		std::uint32_t skeletonCandidates{ 0 };
		std::uint32_t skeletonAttached{ 0 };
		std::uint32_t skeletonLoadFailed{ 0 };
		std::uint32_t skeletonPrunedEmpty{ 0 };
	};

	struct FaceBoneCandidateDetail
	{
		std::string source;
		std::int32_t slot{ -1 };
		std::uintptr_t parentPtr{ 0 };
		std::uint32_t parentFormID{ 0 };
		std::string parentType;
		std::string parentEditorID;
		std::uint32_t parentSlots{ 0 };
		std::uintptr_t addonPtr{ 0 };
		std::uint32_t addonFormID{ 0 };
		std::string addonEditorID;
		std::uint32_t addonSlots{ 0 };
		std::string partModel;
		std::string maleFacebones;
		std::string femaleFacebones;
		std::string selectedFacebones;
		std::string result;
		std::uint32_t pruned{ 0 };
	};

	[[nodiscard]] FaceBoneCandidateDetail MakeCandidateDetail(
		const char* a_source,
		std::int32_t a_slot,
		const RE::BIPOBJECT& a_object,
		const char* a_selectedFacebones,
		std::string a_result,
		std::uint32_t a_pruned);
	void LogAttachFailure(
		const RE::BipedAnim& a_biped,
		const RE::NiAVObject& a_actorRoot,
		RE::SEX a_sex,
		std::size_t a_previewNodeCount,
		const FaceBoneAttachStats& a_stats,
		const std::vector<FaceBoneCandidateDetail>& a_details);
}
