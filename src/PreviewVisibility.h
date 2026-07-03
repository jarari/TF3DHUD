#pragma once

#include "RE/B/BipedAnim.h"
#include "RE/P/PlayerCharacter.h"

#include <cstdint>
#include <string>

namespace TF3DHud::PreviewVisibility
{
	[[nodiscard]] bool ShouldShow(const RE::PlayerCharacter* a_player, std::string& a_reason);
	[[nodiscard]] bool IsPreviewSourceReady(
		RE::PlayerCharacter& a_player,
		const RE::BipedAnim& a_biped,
		std::string& a_reason);
	[[nodiscard]] bool HasPendingBipedModelHandles(const RE::BipedAnim& a_biped, std::int32_t& a_pendingSlot);
}
