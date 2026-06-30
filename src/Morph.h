#pragma once

#include "RE/N/NiAVObject.h"
#include "RE/P/PlayerCharacter.h"

#include <cstdint>

namespace RE
{
	class BSFlattenedBoneTree;
}

namespace TF3DHud::Morph
{
	enum class UpdateResult : std::uint8_t
	{
		kNone = 0,
		kGeometryRebuild = 1 << 0,
		kAdjustmentsApplied = 1 << 1
	};

	[[nodiscard]] constexpr bool HasResult(UpdateResult a_result, UpdateResult a_flag)
	{
		return (std::to_underlying(a_result) & std::to_underlying(a_flag)) != 0;
	}

	[[nodiscard]] constexpr UpdateResult operator|(UpdateResult a_lhs, UpdateResult a_rhs)
	{
		return static_cast<UpdateResult>(std::to_underlying(a_lhs) | std::to_underlying(a_rhs));
	}

	void MarkPrimaryDirty();
	void MarkSecondaryDirty();
	void Reset();
	UpdateResult Update(
		RE::PlayerCharacter& a_player,
		RE::NiAVObject& a_previewRoot,
		RE::BSFlattenedBoneTree* a_previewFlattenedBoneTree);
}
