#include "PreviewVisibility.h"

#include "Config.h"
#include "Events.h"
#include "PreviewRebuilder.h"

#include "RE/P/PlayerCamera.h"
#include "RE/P/PlayerControls.h"
#include "RE/P/PowerArmor.h"
#include "RE/U/UI.h"

#include <string>

namespace TF3DHud::PreviewVisibility
{
	namespace
	{
		[[nodiscard]] bool IsHudAvailable(std::string& a_reason)
		{
			if (!Events::IsHUDMenuOpen()) {
				a_reason = "HUDMenu is closed";
				return false;
			}

			return true;
		}

		[[nodiscard]] bool IsFirstPerson(std::string& a_reason)
		{
			const auto camera = RE::PlayerCamera::GetSingleton();
			if (!camera) {
				a_reason = "PlayerCamera singleton is null";
				return false;
			}

			if (!camera->QCameraEquals(RE::CameraState::kFirstPerson)) {
				a_reason = "camera is not first person";
				return false;
			}

			return true;
		}

		[[nodiscard]] bool IsQuickContainerActive()
		{
			const auto controls = RE::PlayerControls::GetSingleton();
			if (!controls) {
				return false;
			}

			for (const auto handler : controls->handlers) {
				if (handler && handler->inQuickContainer) {
					return true;
				}
			}

			return false;
		}
	}

	bool ShouldShow(const RE::PlayerCharacter* a_player, std::string& a_reason)
	{
		const auto& config = GetConfig();
		if (!config.enabled) {
			a_reason = "disabled by INI";
			return false;
		}

		if (!a_player) {
			a_reason = "PlayerCharacter singleton is null";
			return false;
		}

		const auto ui = RE::UI::GetSingleton();
		if (!ui) {
			a_reason = "UI singleton is null";
			return false;
		}

		const auto itemMenuMode = ui->itemMenuMode.load_unchecked();
		const auto quickContainerActive = IsQuickContainerActive();
		if (ui->menuMode != 0 || (itemMenuMode != 0 && !quickContainerActive) || ui->freezeFrameMenuBG != 0 ||
			ui->freezeFramePause != 0) {
			a_reason =
				"blocked by menu/freeze-frame state menuMode=" + std::to_string(ui->menuMode) +
				", itemMenuMode=" + std::to_string(itemMenuMode) +
				", quickContainerActive=" + std::to_string(quickContainerActive) +
				", freezeFrameMenuBG=" + std::to_string(ui->freezeFrameMenuBG) +
				", freezeFramePause=" + std::to_string(ui->freezeFramePause);
			return false;
		}

		if (!IsHudAvailable(a_reason) || !IsFirstPerson(a_reason)) {
			return false;
		}

		if (config.hideInPowerArmor && RE::PowerArmor::ActorInPowerArmor(*a_player)) {
			a_reason = "player is in power armor";
			return false;
		}

		return true;
	}

	bool IsPreviewSourceReady(RE::PlayerCharacter& a_player, const RE::BipedAnim& a_biped, std::string& a_reason)
	{
		if (!a_biped.GetRoot()) {
			a_reason = "third-person biped root is null";
			return false;
		}

		if (!PreviewRebuilder::GetSourceFaceNode(a_player)) {
			a_reason = "player face node is not initialized";
			return false;
		}

		return true;
	}

	bool HasPendingBipedModelHandles(const RE::BipedAnim& a_biped, std::int32_t& a_pendingSlot)
	{
		for (std::int32_t i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
			const auto& object = a_biped.object[i];
			// IDA: object[].handleList is retained resource ownership for a
			// loaded slot, so it can stay non-null forever. bufferedObjects[]
			// is the transition area LoadBipedParts consumes/clears while a
			// slot is being replaced.
			if (object.handleList.head && !object.partClone) {
				a_pendingSlot = i;
				return true;
			}

			const auto& buffered = a_biped.bufferedObjects[i];
			if (buffered.parent.object ||
				buffered.parent.instanceData ||
				buffered.modExtra ||
				buffered.armorAddon ||
				buffered.part ||
				buffered.partClone ||
				buffered.objectGraphManager ||
				buffered.hitEffect) {
				a_pendingSlot = i;
				return true;
			}
		}

		a_pendingSlot = -1;
		return false;
	}
}
