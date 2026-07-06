#include "ImguiMenu.h"

#include "Address.h"
#include "Animations.h"
#include "Config.h"
#include "Equipment.h"
#include "Localization.h"
#include "Previewer.h"
#include "Renderer.h"

#include "RE/B/BSGraphics.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESFile.h"
#include "RE/T/TESIdleForm.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace TF3DHud::Imgui
{
	namespace
	{
		using D3D11CreateDeviceAndSwapChain_t = HRESULT(WINAPI*)(
			IDXGIAdapter*,
			D3D_DRIVER_TYPE,
			HMODULE,
			UINT,
			const D3D_FEATURE_LEVEL*,
			UINT,
			UINT,
			const DXGI_SWAP_CHAIN_DESC*,
			IDXGISwapChain**,
			ID3D11Device**,
			D3D_FEATURE_LEVEL*,
			ID3D11DeviceContext**);
		using Present_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
		using ClipCursor_t = BOOL(WINAPI*)(const RECT*);

		auto& g_clipCursor = Address::ClipCursor;

		D3D11CreateDeviceAndSwapChain_t g_originalD3D11CreateDeviceAndSwapChain{ nullptr };
		Present_t g_originalPresent{ nullptr };
		ClipCursor_t g_originalClipCursor{ nullptr };
		WNDPROC g_originalWndProc{ nullptr };
		RECT g_windowRect{};
		HWND g_outputWindow{ nullptr };
		bool g_d3dHookInstalled{ false };
		bool g_presentHookInstalled{ false };
		bool g_clipCursorHookInstalled{ false };
		bool g_imguiInitialized{ false };
		bool g_menuOpen{ false };
		bool g_isDirty{ false };
		bool g_savePromptOpen{ false };
		bool g_windowActive{ true };
		std::string g_editingValueId;
		bool g_focusEditInput{ false };
		std::uint32_t g_clipRectEditFrame{ 0 };
		bool g_clipRectDirty{ false };
		constexpr float kPi = 3.14159265358979323846F;
		constexpr float kPreviewCameraAspect = 16.0F / 9.0F;
		constexpr float kDisplayRootY = 375.0F;
		constexpr float kVanillaDisplayLeft = -148.125F;
		constexpr float kVanillaDisplayTop = 79.875F;
		constexpr float kVanillaDisplayRight = 148.125F;
		constexpr float kVanillaDisplayBottom = -79.875F;
		constexpr const char* kSavePromptPopup = "Save changes?##SaveChanges";

		struct DisplayBounds
		{
			float left;
			float top;
			float right;
			float bottom;
		};

		[[nodiscard]] DisplayBounds GetOverlayScreenPlaneBounds(const Config& a_config);

		[[nodiscard]] const char* L(const char* a_key)
		{
			return Localization::GetText(a_key);
		}

		[[nodiscard]] std::string Label(const char* a_key, const char* a_id)
		{
			return std::string{ L(a_key) } + "##" + a_id;
		}

		void DrawHint(const char* a_key)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::BeginItemTooltip()) {
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0F);
				ImGui::TextUnformatted(L(a_key));
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
		}

		void DrawSectionHeader(const char* a_labelKey, const char* a_hintKey)
		{
			ImGui::TextUnformatted(L(a_labelKey));
			DrawHint(a_hintKey);
		}

		[[nodiscard]] const char* BoolText(const bool a_value)
		{
			return a_value ? L("state.true") : L("state.false");
		}

		void MarkDirty()
		{
			g_isDirty = true;
		}

		[[nodiscard]] bool SaveConfigFromMenu()
		{
			if (!SaveConfig()) {
				return false;
			}

			g_isDirty = false;
			return true;
		}

		void CloseMenu()
		{
			g_menuOpen = false;
			g_savePromptOpen = false;
			g_editingValueId.clear();
			::ShowCursor(FALSE);
		}

		void RequestCloseMenu()
		{
			if (g_isDirty) {
				g_savePromptOpen = true;
				g_menuOpen = true;
				return;
			}

			CloseMenu();
		}

		[[nodiscard]] DisplayBounds GetOverlayDisplayBounds(const Config& a_config)
		{
			const auto screenBounds = GetOverlayScreenPlaneBounds(a_config);
			const auto& clipRect = a_config.clipRect;
			const bool hasCustomHorizontal = clipRect.left != 0.0F || clipRect.right != 0.0F;
			const bool hasCustomVertical = clipRect.top != 0.0F || clipRect.bottom != 0.0F;
			const DisplayBounds bounds{
				.left = hasCustomHorizontal ? -(std::max)(clipRect.left, 0.0F) : screenBounds.left,
				.top = hasCustomVertical ? (std::max)(clipRect.top, 0.0F) : screenBounds.top,
				.right = hasCustomHorizontal ? (std::max)(clipRect.right, 0.0F) : screenBounds.right,
				.bottom = hasCustomVertical ? -(std::max)(clipRect.bottom, 0.0F) : screenBounds.bottom
			};
			if (bounds.right > bounds.left && bounds.top > bounds.bottom) {
				return bounds;
			}

			return {
				.left = screenBounds.left,
				.top = screenBounds.top,
				.right = screenBounds.right,
				.bottom = screenBounds.bottom
			};
		}

		[[nodiscard]] DisplayBounds GetOverlayScreenPlaneBounds(const Config& a_config)
		{
			const auto displaySize = ImGui::GetIO().DisplaySize;
			const auto aspect = displaySize.y > 0.0F ? displaySize.x / displaySize.y : kPreviewCameraAspect;
			const auto top = std::tan((a_config.fov * kPi / 180.0F) * 0.15F) * kDisplayRootY;
			const auto right = top * aspect;
			return {
				.left = -right,
				.top = top,
				.right = right,
				.bottom = -top
			};
		}

		[[nodiscard]] ImVec2 ProjectScreenPlanePoint(
			const DisplayBounds& a_screenBounds,
			const float a_x,
			const float a_z,
			const ImVec2& a_displaySize)
		{
			const auto normalizedX = (a_x - a_screenBounds.left) / (a_screenBounds.right - a_screenBounds.left);
			const auto normalizedY = (a_screenBounds.top - a_z) / (a_screenBounds.top - a_screenBounds.bottom);
			return {
				std::clamp(normalizedX, 0.0F, 1.0F) * a_displaySize.x,
				std::clamp(normalizedY, 0.0F, 1.0F) * a_displaySize.y
			};
		}

		void DrawClipRectOverlay()
		{
			const auto& config = GetConfig();
			const auto screenBounds = GetOverlayScreenPlaneBounds(config);
			const auto displayBounds = GetOverlayDisplayBounds(config);
			const auto centerX = (screenBounds.left + screenBounds.right) * 0.5F;
			const auto centerZ = (screenBounds.top + screenBounds.bottom) * 0.5F;

			float anchorX = centerX;
			float anchorZ = centerZ;
			switch (config.anchor) {
			case 1:
				anchorX = screenBounds.left;
				anchorZ = screenBounds.bottom;
				break;
			case 2:
				anchorX = centerX;
				anchorZ = screenBounds.bottom;
				break;
			case 3:
				anchorX = screenBounds.right;
				anchorZ = screenBounds.bottom;
				break;
			case 4:
				anchorX = screenBounds.left;
				anchorZ = centerZ;
				break;
			case 6:
				anchorX = screenBounds.right;
				anchorZ = centerZ;
				break;
			case 7:
				anchorX = screenBounds.left;
				anchorZ = screenBounds.top;
				break;
			case 8:
				anchorX = centerX;
				anchorZ = screenBounds.top;
				break;
			case 9:
				anchorX = screenBounds.right;
				anchorZ = screenBounds.top;
				break;
			case 5:
			default:
				break;
			}

			const auto translateX = anchorX + config.placementX;
			const auto translateZ = anchorZ + config.placementY;
			const auto displaySize = ImGui::GetIO().DisplaySize;
			const auto min = ProjectScreenPlanePoint(
				screenBounds,
				translateX + displayBounds.left,
				translateZ + displayBounds.top,
				displaySize);
			const auto max = ProjectScreenPlanePoint(
				screenBounds,
				translateX + displayBounds.right,
				translateZ + displayBounds.bottom,
				displaySize);

			ImGui::GetForegroundDrawList()->AddRect(
				min,
				max,
				IM_COL32(255, 0, 0, 255),
				0.0F,
				0,
				2.0F);
		}

		void ApplyLayoutEdit(const bool a_clipRectChanged = false)
		{
			MarkDirty();
			if (a_clipRectChanged) {
				g_clipRectDirty = true;
			}
			Previewer::ApplyConfigChanges();
		}

		void ApplyConfigEdit()
		{
			MarkDirty();
			Previewer::ApplyConfigChanges();
		}

		void DrawSavePromptModal()
		{
			if (g_savePromptOpen) {
				ImGui::OpenPopup(kSavePromptPopup);
			}

			if (!ImGui::BeginPopupModal(kSavePromptPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
				return;
			}

			ImGui::TextUnformatted(L("modal.save_changes.body"));
			ImGui::Separator();

			if (ImGui::Button(L("button.save"), ImVec2(96.0F, 0.0F))) {
				if (SaveConfigFromMenu()) {
					ImGui::CloseCurrentPopup();
					CloseMenu();
				}
			}
			ImGui::SameLine();
			if (ImGui::Button(L("button.close"), ImVec2(96.0F, 0.0F))) {
				ImGui::CloseCurrentPopup();
				CloseMenu();
			}
			ImGui::SameLine();
			if (ImGui::Button(L("button.cancel"), ImVec2(96.0F, 0.0F))) {
				g_savePromptOpen = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		void FlushClipRectEdit()
		{
			if (!g_clipRectDirty) {
				return;
			}

			++g_clipRectEditFrame;
			if (g_clipRectEditFrame < 10 || !Renderer::CanApplyDisplayClipRect()) {
				return;
			}

			Renderer::ApplyDisplayClipRect();
			g_clipRectDirty = false;
			g_clipRectEditFrame = 0;
		}

		bool DrawEditButton(const char* a_id)
		{
			ImGui::SameLine();
			if (ImGui::Button(L("button.edit"))) {
				g_editingValueId = a_id;
				g_focusEditInput = true;
				return true;
			}
			return false;
		}

		[[nodiscard]] bool DrawIntSliderEdit(
			const char* a_labelKey,
			const char* a_id,
			float& a_value,
			const int a_min,
			const int a_max,
			const int a_step)
		{
			bool changed = false;
			ImGui::PushID(a_id);
			const bool editing = g_editingValueId == a_id;
			int value = static_cast<int>(std::lround(a_value));
			const auto label = Label(a_labelKey, a_id);
			if (editing) {
				if (g_focusEditInput) {
					ImGui::SetKeyboardFocusHere();
					g_focusEditInput = false;
				}
				if (ImGui::InputInt(label.c_str(), &value, a_step, a_step, ImGuiInputTextFlags_EnterReturnsTrue) ||
				    ImGui::IsItemDeactivatedAfterEdit()) {
					value = std::clamp(value, a_min, a_max);
					a_value = static_cast<float>(value);
					g_editingValueId.clear();
					changed = true;
				}
			} else {
				if (ImGui::SliderInt(label.c_str(), &value, a_min, a_max, "%d", ImGuiSliderFlags_AlwaysClamp)) {
					if (a_step > 1) {
						value = std::clamp(((value + (a_step / 2)) / a_step) * a_step, a_min, a_max);
					}
					a_value = static_cast<float>(value);
					changed = true;
				}
				DrawEditButton(a_id);
			}
			ImGui::PopID();
			return changed;
		}

		[[nodiscard]] bool DrawFloatSliderEdit(
			const char* a_labelKey,
			const char* a_id,
			float& a_value,
			const float a_min,
			const float a_max,
			const float a_step)
		{
			bool changed = false;
			ImGui::PushID(a_id);
			const bool editing = g_editingValueId == a_id;
			const auto label = Label(a_labelKey, a_id);
			if (editing) {
				if (g_focusEditInput) {
					ImGui::SetKeyboardFocusHere();
					g_focusEditInput = false;
				}
				if (ImGui::InputFloat(label.c_str(), &a_value, a_step, a_step, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue) ||
				    ImGui::IsItemDeactivatedAfterEdit()) {
					a_value = std::clamp(a_value, a_min, a_max);
					g_editingValueId.clear();
					changed = true;
				}
			} else {
				if (ImGui::SliderFloat(label.c_str(), &a_value, a_min, a_max, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
					a_value = std::clamp(std::round(a_value / a_step) * a_step, a_min, a_max);
					changed = true;
				}
				DrawEditButton(a_id);
			}
			ImGui::PopID();
			return changed;
		}

		[[nodiscard]] bool IsAltTabSystemKey(const UINT a_msg, const WPARAM a_wparam)
		{
			return (a_msg == WM_SYSKEYDOWN || a_msg == WM_SYSKEYUP) &&
			       (a_wparam == VK_TAB || a_wparam == VK_MENU || a_wparam == VK_LMENU || a_wparam == VK_RMENU);
		}

		LRESULT CALLBACK ImGuiWndProc(HWND a_hwnd, UINT a_msg, WPARAM a_wparam, LPARAM a_lparam)
		{
			const auto callOriginal = [&]() -> LRESULT {
				return ::CallWindowProc(g_originalWndProc, a_hwnd, a_msg, a_wparam, a_lparam);
			};

			switch (a_msg) {
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
				if ((a_lparam & 0x40000000) == 0 && static_cast<std::uint32_t>(a_wparam) == GetConfig().uiMenuKey) {
					if (g_menuOpen) {
						RequestCloseMenu();
					} else {
						g_menuOpen = true;
						::ShowCursor(TRUE);
					}
					return 0;
				}
				break;
			case WM_ACTIVATEAPP:
				g_windowActive = a_wparam != FALSE;
				if (!g_windowActive) {
					::ClipCursor(nullptr);
				}
				break;
			case WM_ACTIVATE:
				g_windowActive = LOWORD(a_wparam) != WA_INACTIVE;
				if (!g_windowActive) {
					::ClipCursor(nullptr);
				}
				break;
			case WM_KILLFOCUS:
				g_windowActive = false;
				::ClipCursor(nullptr);
				break;
			case WM_SETFOCUS:
				g_windowActive = true;
				break;
			default:
				break;
			}

			if (g_imguiInitialized && g_menuOpen) {
				ImGui_ImplWin32_WndProcHandler(a_hwnd, a_msg, a_wparam, a_lparam);

				switch (a_msg) {
				case WM_ACTIVATE:
				case WM_ACTIVATEAPP:
				case WM_KILLFOCUS:
				case WM_SETFOCUS:
				case WM_SIZE:
				case WM_SYSCOMMAND:
					return callOriginal();
				case WM_SYSKEYDOWN:
				case WM_SYSKEYUP:
					return IsAltTabSystemKey(a_msg, a_wparam) ? callOriginal() : 0;
				case WM_KEYDOWN:
				case WM_KEYUP:
				case WM_CHAR:
				case WM_DEADCHAR:
				case WM_SYSCHAR:
				case WM_SYSDEADCHAR:
				case WM_INPUT:
				case WM_MOUSEMOVE:
				case WM_LBUTTONDOWN:
				case WM_LBUTTONUP:
				case WM_LBUTTONDBLCLK:
				case WM_RBUTTONDOWN:
				case WM_RBUTTONUP:
				case WM_RBUTTONDBLCLK:
				case WM_MBUTTONDOWN:
				case WM_MBUTTONUP:
				case WM_MBUTTONDBLCLK:
				case WM_XBUTTONDOWN:
				case WM_XBUTTONUP:
				case WM_XBUTTONDBLCLK:
				case WM_MOUSEWHEEL:
				case WM_MOUSEHWHEEL:
					return 0;
				default:
					break;
				}
			}

			return callOriginal();
		}

		void DrawActiveClip(const Animations::DebugSnapshot& a_snapshot)
		{
			const auto clipIter = std::ranges::find_if(a_snapshot.activeNodes, [](const auto& a_node) {
				return a_node.isClip;
			});

			if (clipIter == a_snapshot.activeNodes.end()) {
				ImGui::TextUnformatted(L("debug.active_clip.none"));
				return;
			}

			ImGui::Text(L("debug.active_clip"), clipIter->clipName.empty() ? L("state.unnamed") : clipIter->clipName.c_str());
			if (clipIter->inSubgraph) {
				ImGui::Text(L("debug.subgraph_slot"), clipIter->subgraphSlot, reinterpret_cast<void*>(clipIter->behaviorRootId));
			} else {
				ImGui::Text(L("debug.subgraph_root"), reinterpret_cast<void*>(clipIter->behaviorRootId));
			}
			ImGui::Text(L("debug.path"), clipIter->resolvedClipPath.empty() ? clipIter->authoredClipPath.c_str() : clipIter->resolvedClipPath.c_str());
			if (clipIter->hasTiming) {
				ImGui::Text(L("debug.time_local"), clipIter->currentTime, clipIter->duration);
				ImGui::Text(L("debug.control_time"), clipIter->controlLocalTime);
				ImGui::Text(
					L("debug.mode_fraction"),
					clipIter->playbackMode,
					clipIter->userControlledTimeFraction);
			} else {
				ImGui::TextUnformatted(L("debug.time_unavailable"));
			}
		}

		void DrawIdList(
			const char* a_label,
			const std::uint32_t a_count,
			const std::uint32_t a_shown,
			const std::array<std::uint64_t, Animations::kMaxSubgraphDebugRequestEntries>& a_values)
		{
			ImGui::Text(L("debug.list_count"), a_label, a_count, a_shown);
			if (a_count != 0 && a_shown == 0) {
				ImGui::SameLine();
				ImGui::TextDisabled(L("state.data_unavailable"));
				return;
			}
			if (a_count == 0) {
				ImGui::SameLine();
				ImGui::TextDisabled(L("state.none"));
				return;
			}

			ImGui::Indent();
			for (std::uint32_t index = 0; index < a_shown; ++index) {
				const auto value = a_values[index];
				ImGui::Text("0x%llX", static_cast<unsigned long long>(value));
			}
			ImGui::Unindent();
		}

		void DrawFileArray(
			const char* a_label,
			const std::uint32_t a_count,
			const std::uint32_t a_shown,
			const std::array<Animations::SubgraphFileDebugInfo, Animations::kMaxSubgraphDebugFiles>& a_files)
		{
			ImGui::Text(L("debug.list_count_plain"), a_label, a_count, a_shown);
			if (a_count != 0 && a_shown == 0) {
				ImGui::TextDisabled(L("state.data_unavailable"));
				return;
			}
			if (a_shown == 0) {
				return;
			}

			ImGui::Indent();
			for (std::uint32_t index = 0; index < a_shown; ++index) {
				const auto* file = a_files[index].path.data();
				ImGui::BulletText("%s", file[0] == '\0' ? L("state.empty") : file);
			}
			ImGui::Unindent();
		}

		void DrawSubgraphState(const Animations::DebugSnapshot& a_snapshot)
		{
			ImGui::Text(
				"swapData=%p slots=%u/%u requested=%u linked=%u pendingRemove=%u useCountTotal=%u stateMachine=%p behavior=%p",
				reinterpret_cast<void*>(a_snapshot.subgraphSwapData),
				a_snapshot.subgraphSwapSlots,
				a_snapshot.subgraphSwapCapacity,
				a_snapshot.subgraphSwapRequestedSlots,
				a_snapshot.subgraphSwapLinkedSlots,
				a_snapshot.subgraphSwapPendingRemoveSlots,
				a_snapshot.subgraphSwapUseCountTotal,
				reinterpret_cast<void*>(a_snapshot.subgraphSwapStateMachine),
				reinterpret_cast<void*>(a_snapshot.subgraphSwapBehavior));
			if (!a_snapshot.hasSubgraphSwapData) {
				ImGui::TextDisabled(L("debug.no_graph_swap_data"));
			}

			DrawIdList(
				L("debug.default_handles"),
				a_snapshot.defaultSubgraphHandleCount,
				a_snapshot.defaultSubgraphHandleShown,
				a_snapshot.defaultSubgraphHandles);
			DrawIdList(
				L("debug.default_ids"),
				a_snapshot.defaultSubgraphIdCount,
				a_snapshot.defaultSubgraphIdShown,
				a_snapshot.defaultSubgraphIds);
			DrawIdList(
				L("debug.weapon_handles"),
				a_snapshot.weaponSubgraphHandleCount,
				a_snapshot.weaponSubgraphHandleShown,
				a_snapshot.weaponSubgraphHandles);
			DrawIdList(
				L("debug.weapon_ids"),
				a_snapshot.weaponSubgraphIdCount,
				a_snapshot.weaponSubgraphIdShown,
				a_snapshot.weaponSubgraphIds);

			constexpr ImGuiTableFlags tableFlags =
				ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
				ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_SizingStretchProp;

			if (ImGui::BeginTable("subgraph_slots", 10, tableFlags)) {
				ImGui::TableSetupColumn(L("table.slot"), ImGuiTableColumnFlags_WidthFixed, 34.0F);
				ImGui::TableSetupColumn(L("table.state"), ImGuiTableColumnFlags_WidthFixed, 42.0F);
				ImGui::TableSetupColumn(L("table.handle"), ImGuiTableColumnFlags_WidthFixed, 92.0F);
				ImGui::TableSetupColumn(L("table.shared"), ImGuiTableColumnFlags_WidthFixed, 92.0F);
				ImGui::TableSetupColumn(L("table.root"), ImGuiTableColumnFlags_WidthFixed, 92.0F);
				ImGui::TableSetupColumn(L("table.root_name"), ImGuiTableColumnFlags_WidthFixed, 140.0F);
				ImGui::TableSetupColumn(L("table.use"), ImGuiTableColumnFlags_WidthFixed, 42.0F);
				ImGui::TableSetupColumn(L("table.remove"), ImGuiTableColumnFlags_WidthFixed, 58.0F);
				ImGui::TableSetupColumn(L("table.data_160"), ImGuiTableColumnFlags_WidthFixed, 68.0F);
				ImGui::TableSetupColumn(L("table.data_178"), ImGuiTableColumnFlags_WidthFixed, 68.0F);
				ImGui::TableHeadersRow();

				for (std::uint32_t index = 0; index < a_snapshot.subgraphSlotShown; ++index) {
					const auto& slot = a_snapshot.subgraphSlots[index];
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%u", slot.index);
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%u", slot.stateId);
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("0x%llX", static_cast<unsigned long long>(slot.handle));
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%p", reinterpret_cast<void*>(slot.sharedData));
					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%p", reinterpret_cast<void*>(slot.rootId));
					ImGui::TableSetColumnIndex(5);
					ImGui::TextUnformatted(slot.rootName.data());
					ImGui::TableSetColumnIndex(6);
					ImGui::Text("%u", slot.useCount);
					ImGui::TableSetColumnIndex(7);
					ImGui::Text("%u", slot.pendingRemove);
					ImGui::TableSetColumnIndex(8);
					ImGui::Text("%u", slot.files160Count);
					ImGui::TableSetColumnIndex(9);
					ImGui::Text("%u", slot.files178Count);
				}

				ImGui::EndTable();
			}

			for (std::uint32_t index = 0; index < a_snapshot.subgraphSlotShown; ++index) {
				const auto& slot = a_snapshot.subgraphSlots[index];
				if (slot.files160Shown == 0 && slot.files178Shown == 0) {
					continue;
				}

				ImGui::PushID(static_cast<int>(slot.index));
				if (ImGui::TreeNode(L("debug.file_arrays"))) {
					ImGui::SameLine();
					ImGui::TextDisabled(L("debug.slot"), slot.index);
					DrawFileArray(L("debug.data_160"), slot.files160Count, slot.files160Shown, slot.files160);
					DrawFileArray(L("debug.data_178"), slot.files178Count, slot.files178Shown, slot.files178);
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
		}

		void DrawSpeedChannelState(const Animations::DebugSnapshot& a_snapshot)
		{
			const auto& speed = a_snapshot.speedChannel;
			ImGui::Text(
				"Speed channel constructed=%s reset=%s polled=%s polls=%u applyAdjustments=%s",
				BoolText(speed.constructed),
				BoolText(speed.reset),
				BoolText(speed.polled),
				speed.pollCount,
				BoolText(speed.applyAdjustments));
			ImGui::Text(
				"Speed desired=%.3f scale=%.3f raw=%.3f contour=%.3f last=%.3f graph=%s %.3f",
				speed.desiredSpeed,
				speed.scale,
				speed.rawSpeed,
				speed.graphSpeed,
				speed.lastSpeed,
				speed.previewGraphSpeedHas ? L("state.yes") : L("state.no"),
				speed.previewGraphSpeed);
			ImGui::Text(
				"Freeze preview=%s actor=%s contours=%s actorGate=%s resolved=%s state=%s response=%u adjustments=%u applied=%s",
				BoolText(speed.previewFreeze),
				BoolText(speed.actorFreeze),
				BoolText(speed.useContours),
				BoolText(speed.actorAllowsContours),
				BoolText(speed.contourResolved),
				BoolText(speed.contourState),
				speed.contourResponse,
				speed.adjustmentCount,
				BoolText(speed.contourApplied));
		}

		void DrawActiveNodesTable(const Animations::DebugSnapshot& a_snapshot);

		void DrawGraphInfoTab(const Animations::DebugSnapshot& a_snapshot)
		{
			if (!a_snapshot.lastDiagnostic.empty()) {
				ImGui::Text(L("debug.last_diagnostic"), a_snapshot.lastDiagnostic.c_str());
			}

			ImGui::Text(L("debug.project"), a_snapshot.project.empty() ? L("state.none") : a_snapshot.project.c_str());
			ImGui::Text(
				"Manager=%p Graph=%p Behavior=%p ActiveGraph=%u/%u",
				reinterpret_cast<void*>(a_snapshot.manager),
				reinterpret_cast<void*>(a_snapshot.graph),
				reinterpret_cast<void*>(a_snapshot.behaviorGraph),
				a_snapshot.activeGraphIndex,
				a_snapshot.graphCount);
			ImGui::Text(
				"Behavior active=%s linked=%s updateActiveNodes=%s stateOrTransitionChanged=%s activeNodes=%u shown=%zu",
				BoolText(a_snapshot.behaviorActive),
				BoolText(a_snapshot.behaviorLinked),
				BoolText(a_snapshot.updateActiveNodes),
				BoolText(a_snapshot.stateOrTransitionChanged),
				a_snapshot.activeNodeCount,
				a_snapshot.activeNodes.size());
			ImGui::Text(
				"generateHavokBones=%s ragdollInterface=%s physicsWorld=%s windowActive=%s",
				BoolText(a_snapshot.generateHavokBones),
				BoolText(a_snapshot.hasRagdollInterface),
				BoolText(a_snapshot.hasPhysicsWorld),
				BoolText(g_windowActive));
			ImGui::Text(
				"Live sync active=%s time=%.4f/%.4f speed=%.4f points=%u",
				BoolText(a_snapshot.liveSync.active),
				a_snapshot.liveSync.currentTime,
				a_snapshot.liveSync.totalTime,
				a_snapshot.liveSync.speed,
				a_snapshot.liveSync.syncPointCount);
			ImGui::Text(
				"Preview sync active=%s time=%.4f/%.4f speed=%.4f points=%u",
				BoolText(a_snapshot.previewSync.active),
				a_snapshot.previewSync.currentTime,
				a_snapshot.previewSync.totalTime,
				a_snapshot.previewSync.speed,
				a_snapshot.previewSync.syncPointCount);
			DrawSpeedChannelState(a_snapshot);
		}

		void DrawActiveNodesTab(const Animations::DebugSnapshot& a_snapshot)
		{
			DrawActiveClip(a_snapshot);
			ImGui::Separator();
			DrawActiveNodesTable(a_snapshot);
		}

		void DrawFaceGenTab()
		{
			const auto snapshot = Previewer::GetFaceGenDebugSnapshot();
			if (snapshot.headParts.empty() && snapshot.geometries.empty() && snapshot.hairSkinBones.empty() && snapshot.sliders.empty()) {
				ImGui::TextDisabled(L("debug.no_facegen"));
				return;
			}

			constexpr ImGuiTableFlags tableFlags =
				ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
				ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_SizingStretchProp;

			DrawSectionHeader("debug.used_headparts", "section.debug_facegen.hint");
			if (snapshot.headParts.empty()) {
				ImGui::TextDisabled(L("debug.no_hdpt"));
			} else if (ImGui::BeginTable("facegen_headparts", 6, tableFlags)) {
				ImGui::TableSetupColumn(L("table.hdpt"), ImGuiTableColumnFlags_WidthStretch, 1.1F);
				ImGui::TableSetupColumn(L("table.form"), ImGuiTableColumnFlags_WidthFixed, 86.0F);
				ImGui::TableSetupColumn(L("table.ptr"), ImGuiTableColumnFlags_WidthFixed, 118.0F);
				ImGui::TableSetupColumn(L("table.type"), ImGuiTableColumnFlags_WidthFixed, 82.0F);
				ImGui::TableSetupColumn(L("table.name"), ImGuiTableColumnFlags_WidthStretch, 1.0F);
				ImGui::TableSetupColumn(L("table.model"), ImGuiTableColumnFlags_WidthStretch, 1.8F);
				ImGui::TableHeadersRow();

				for (const auto& headPart : snapshot.headParts) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(headPart.editorID.empty() ? L("state.no_editor_id") : headPart.editorID.c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%08X", headPart.formID);
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%llX", static_cast<unsigned long long>(headPart.ptr));
					ImGui::TableSetColumnIndex(3);
					ImGui::TextUnformatted(headPart.type.c_str());
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(headPart.fullName.empty() ? L("state.no_data") : headPart.fullName.c_str());
					ImGui::TableSetColumnIndex(5);
					ImGui::TextUnformatted(headPart.model.empty() ? L("state.no_data") : headPart.model.c_str());
				}

				ImGui::EndTable();
			}

			ImGui::Spacing();
			DrawSectionHeader("debug.preview_face_geometries", "section.debug_facegen.hint");
			if (snapshot.geometries.empty()) {
				ImGui::TextDisabled(L("debug.no_face_geometry"));
			} else if (ImGui::BeginTable("facegen_geometries", 4, tableFlags)) {
				ImGui::TableSetupColumn(L("table.geometry"), ImGuiTableColumnFlags_WidthStretch, 1.5F);
				ImGui::TableSetupColumn(L("table.geometry_ptr"), ImGuiTableColumnFlags_WidthFixed, 118.0F);
				ImGui::TableSetupColumn(L("table.parent"), ImGuiTableColumnFlags_WidthStretch, 1.0F);
				ImGui::TableSetupColumn(L("table.parent_ptr"), ImGuiTableColumnFlags_WidthFixed, 118.0F);
				ImGui::TableHeadersRow();

				for (const auto& geometry : snapshot.geometries) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(geometry.name.empty() ? L("state.unnamed") : geometry.name.c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%llX", static_cast<unsigned long long>(geometry.ptr));
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(geometry.parentName.empty() ? L("state.no_data") : geometry.parentName.c_str());
					ImGui::TableSetColumnIndex(3);
					if (geometry.parentPtr != 0) {
						ImGui::Text("%llX", static_cast<unsigned long long>(geometry.parentPtr));
					} else {
						ImGui::TextDisabled(L("state.no_data"));
					}
				}

				ImGui::EndTable();
			}

			ImGui::Spacing();
			if (ImGui::Button(L("button.dump_hair_skin_bones"))) {
				Previewer::LogHairSkinBoneDiagnostics();
			}
			DrawSectionHeader("debug.hair_skin_bones", "section.debug_facegen.hint");
			if (snapshot.hairSkinBones.empty()) {
				ImGui::TextDisabled(L("debug.no_hair_skin_bones"));
			} else if (ImGui::BeginTable("facegen_hair_skin_bones", 10, tableFlags)) {
				ImGui::TableSetupColumn(L("table.src"), ImGuiTableColumnFlags_WidthFixed, 58.0F);
				ImGui::TableSetupColumn(L("table.hdpt"), ImGuiTableColumnFlags_WidthStretch, 1.0F);
				ImGui::TableSetupColumn(L("table.geometry"), ImGuiTableColumnFlags_WidthStretch, 1.1F);
				ImGui::TableSetupColumn(L("table.index"), ImGuiTableColumnFlags_WidthFixed, 36.0F);
				ImGui::TableSetupColumn(L("table.bone"), ImGuiTableColumnFlags_WidthStretch, 1.0F);
				ImGui::TableSetupColumn(L("table.bone_ptr"), ImGuiTableColumnFlags_WidthFixed, 118.0F);
				ImGui::TableSetupColumn(L("table.parent"), ImGuiTableColumnFlags_WidthStretch, 1.0F);
				ImGui::TableSetupColumn(L("table.parent_ptr"), ImGuiTableColumnFlags_WidthFixed, 118.0F);
				ImGui::TableSetupColumn(L("table.local"), ImGuiTableColumnFlags_WidthStretch, 1.8F);
				ImGui::TableSetupColumn(L("table.world"), ImGuiTableColumnFlags_WidthStretch, 1.8F);
				ImGui::TableHeadersRow();

				for (const auto& bone : snapshot.hairSkinBones) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(bone.source.c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(bone.headPart.empty() ? L("state.no_data") : bone.headPart.c_str());
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(bone.geometry.empty() ? L("state.no_data") : bone.geometry.c_str());
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%u", bone.index);
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(bone.boneName.empty() ? L("state.no_data") : bone.boneName.c_str());
					ImGui::TableSetColumnIndex(5);
					if (bone.bonePtr != 0) {
						ImGui::Text("%llX", static_cast<unsigned long long>(bone.bonePtr));
					} else {
						ImGui::TextDisabled(L("state.no_data"));
					}
					ImGui::TableSetColumnIndex(6);
					ImGui::TextUnformatted(bone.parentName.empty() ? L("state.no_data") : bone.parentName.c_str());
					ImGui::TableSetColumnIndex(7);
					if (bone.parentPtr != 0) {
						ImGui::Text("%llX", static_cast<unsigned long long>(bone.parentPtr));
					} else {
						ImGui::TextDisabled(L("state.no_data"));
					}
					ImGui::TableSetColumnIndex(8);
					ImGui::TextUnformatted(bone.local.empty() ? L("state.no_data") : bone.local.c_str());
					ImGui::TableSetColumnIndex(9);
					ImGui::TextUnformatted(bone.world.empty() ? L("state.no_data") : bone.world.c_str());
				}

				ImGui::EndTable();
			}

			ImGui::Separator();
			DrawSectionHeader("debug.sliders_tints", "section.debug_facegen.hint");
			if (snapshot.sliders.empty()) {
				ImGui::TextDisabled(L("debug.no_facegen_slider"));
				return;
			}

			if (!ImGui::BeginTable("facegen_sliders", 4, tableFlags)) {
				return;
			}

			ImGui::TableSetupColumn(L("table.category"), ImGuiTableColumnFlags_WidthStretch, 1.3F);
			ImGui::TableSetupColumn(L("table.index"), ImGuiTableColumnFlags_WidthFixed, 54.0F);
			ImGui::TableSetupColumn(L("table.live"), ImGuiTableColumnFlags_WidthStretch, 1.5F);
			ImGui::TableSetupColumn(L("table.preview"), ImGuiTableColumnFlags_WidthStretch, 1.5F);
			ImGui::TableHeadersRow();

			for (const auto& slider : snapshot.sliders) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(slider.category.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", slider.id);
				ImGui::TableSetColumnIndex(2);
				if (slider.hasLive) {
					ImGui::TextUnformatted(slider.liveValue.c_str());
				} else {
					ImGui::TextDisabled(L("state.no_data"));
				}
				ImGui::TableSetColumnIndex(3);
				if (slider.hasPreview) {
					ImGui::TextUnformatted(slider.previewValue.c_str());
				} else {
					ImGui::TextDisabled(L("state.no_data"));
				}
			}

			ImGui::EndTable();
		}

		void DrawActiveNodesTable(const Animations::DebugSnapshot& a_snapshot)
		{
			constexpr ImGuiTableFlags tableFlags =
				ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
				ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_SizingStretchProp;

			if (!ImGui::BeginTable("active_nodes", 13, tableFlags)) {
				return;
			}

			ImGui::TableSetupColumn(L("table.index"), ImGuiTableColumnFlags_WidthFixed, 34.0F);
			ImGui::TableSetupColumn(L("table.entry"), ImGuiTableColumnFlags_WidthFixed, 92.0F);
			ImGui::TableSetupColumn(L("table.node"), ImGuiTableColumnFlags_WidthFixed, 92.0F);
			ImGui::TableSetupColumn(L("table.type"), ImGuiTableColumnFlags_WidthFixed, 54.0F);
			ImGui::TableSetupColumn(L("table.sg"), ImGuiTableColumnFlags_WidthFixed, 38.0F);
			ImGui::TableSetupColumn(L("table.root"), ImGuiTableColumnFlags_WidthFixed, 92.0F);
			ImGui::TableSetupColumn(L("table.name"), ImGuiTableColumnFlags_WidthStretch, 1.4F);
			ImGui::TableSetupColumn(L("table.clip_path"), ImGuiTableColumnFlags_WidthStretch, 2.2F);
			ImGui::TableSetupColumn(L("table.time"), ImGuiTableColumnFlags_WidthFixed, 92.0F);
			ImGui::TableSetupColumn(L("table.ctrl"), ImGuiTableColumnFlags_WidthFixed, 72.0F);
			ImGui::TableSetupColumn(L("table.mode"), ImGuiTableColumnFlags_WidthFixed, 48.0F);
			ImGui::TableSetupColumn(L("table.frac"), ImGuiTableColumnFlags_WidthFixed, 60.0F);
			ImGui::TableSetupColumn(L("table.behavior"), ImGuiTableColumnFlags_WidthFixed, 92.0F);
			ImGui::TableHeadersRow();

			for (std::size_t index = 0; index < a_snapshot.activeNodes.size(); ++index) {
				const auto& node = a_snapshot.activeNodes[index];
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%zu", index);
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%p", reinterpret_cast<void*>(node.entry));
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%p", reinterpret_cast<void*>(node.node));
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(node.isClip ? L("debug.clip") : L("debug.node"));
				ImGui::TableSetColumnIndex(4);
				if (node.inSubgraph) {
					ImGui::Text("%u", node.subgraphSlot);
				} else {
					ImGui::TextDisabled(L("state.no_data"));
				}
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%p", reinterpret_cast<void*>(node.behaviorRootId));
				ImGui::TableSetColumnIndex(6);
				if (!node.nodeName.empty()) {
					ImGui::TextUnformatted(node.nodeName.c_str());
				} else if (!node.clipName.empty()) {
					ImGui::TextUnformatted(node.clipName.c_str());
				} else {
					ImGui::TextDisabled(L("state.unnamed"));
				}
				ImGui::TableSetColumnIndex(7);
				if (node.isClip) {
					const auto& path = node.resolvedClipPath.empty() ? node.authoredClipPath : node.resolvedClipPath;
					ImGui::TextUnformatted(path.empty() ? L("state.empty") : path.c_str());
				} else {
					ImGui::TextDisabled(L("state.no_data"));
				}
				ImGui::TableSetColumnIndex(8);
				if (node.hasTiming) {
					ImGui::Text("%.3f/%.3f", node.currentTime, node.duration);
				} else {
					ImGui::TextDisabled(L("state.no_data"));
				}
				ImGui::TableSetColumnIndex(9);
				if (node.hasControlLocalTime) {
					ImGui::Text("%.3f", node.controlLocalTime);
				} else {
					ImGui::TextDisabled(L("state.no_data"));
				}
				ImGui::TableSetColumnIndex(10);
				if (node.isClip) {
					ImGui::Text("%u", node.playbackMode);
				} else {
					ImGui::TextDisabled(L("state.no_data"));
				}
				ImGui::TableSetColumnIndex(11);
				if (node.isClip) {
					ImGui::Text("%.3f", node.userControlledTimeFraction);
				} else {
					ImGui::TextDisabled(L("state.no_data"));
				}
				ImGui::TableSetColumnIndex(12);
				ImGui::Text("%p", reinterpret_cast<void*>(node.behaviorGraph));
			}

			ImGui::EndTable();
		}

		[[nodiscard]] const char* AnchorLabel(const std::int32_t a_anchor)
		{
			switch (a_anchor) {
			case 1:
				return L("anchor.bottom_left");
			case 2:
				return L("anchor.bottom_center");
			case 3:
				return L("anchor.bottom_right");
			case 4:
				return L("anchor.middle_left");
			case 5:
				return L("anchor.middle_center");
			case 6:
				return L("anchor.middle_right");
			case 7:
				return L("anchor.top_left");
			case 8:
				return L("anchor.top_center");
			case 9:
				return L("anchor.top_right");
			default:
				return L("anchor.unknown");
			}
		}

		void DrawAnchorButton(Config& a_config, const std::int32_t a_anchor)
		{
			const bool selected = a_config.anchor == a_anchor;
			ImGui::PushID(a_anchor);
			ImGui::PushStyleColor(ImGuiCol_Button, selected ? ImVec4(0.25F, 0.65F, 0.95F, 1.0F) : ImVec4(0.0F, 0.0F, 0.0F, 1.0F));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? ImVec4(0.35F, 0.75F, 1.0F, 1.0F) : ImVec4(0.35F, 0.35F, 0.35F, 1.0F));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25F, 0.65F, 0.95F, 1.0F));
			if (ImGui::Button(AnchorLabel(a_anchor), ImVec2(118.0F, 28.0F))) {
				a_config.anchor = a_anchor;
				ApplyLayoutEdit();
			}
			ImGui::PopStyleColor(3);
			ImGui::PopID();
		}

		void DrawLayoutTab()
		{
			auto& config = GetMutableConfig();

			DrawSectionHeader("section.anchor", "section.anchor.hint");
			for (int row = 2; row >= 0; --row) {
				for (int col = 0; col < 3; ++col) {
					const auto anchor = (row * 3) + col + 1;
					DrawAnchorButton(config, anchor);
					if (col < 2) {
						ImGui::SameLine();
					}
				}
			}

			ImGui::Separator();
			DrawSectionHeader("section.view", "section.view.hint");
			if (DrawIntSliderEdit("field.fov", "FOV", config.fov, 10, 120, 1)) {
				ApplyLayoutEdit();
			}
			if (DrawIntSliderEdit("field.placement_x", "PlacementX", config.placementX, -200, 200, 1)) {
				ApplyLayoutEdit();
			}
			if (DrawIntSliderEdit("field.placement_y", "PlacementY", config.placementY, -200, 200, 1)) {
				ApplyLayoutEdit();
			}
			if (DrawIntSliderEdit("field.camera_distance", "CameraDistance", config.cameraDistance, 100, 2000, 5)) {
				ApplyLayoutEdit();
			}
			if (DrawFloatSliderEdit("field.model_scale", "ModelScale", config.modelScale, 0.01F, 2.0F, 0.01F)) {
				ApplyLayoutEdit();
			}
			if (DrawIntSliderEdit("field.yaw_degrees", "YawDegrees", config.yawDegrees, 0, 360, 1)) {
				ApplyLayoutEdit();
			}

			ImGui::Separator();
			DrawSectionHeader("section.camera", "section.camera.hint");
			const char* targets[] = { L("camera.head"), L("camera.chest"), L("camera.pelvis"), L("camera.root") };
			int target = static_cast<int>(config.camera.target);
			const auto targetLabel = Label("field.target", "CameraTarget");
			if (ImGui::Combo(targetLabel.c_str(), &target, targets, static_cast<int>(std::size(targets)))) {
				config.camera.target = static_cast<CameraFramingTarget>(target);
				ApplyLayoutEdit();
			}
			ImGui::SameLine();
			if (ImGui::Checkbox(Label("field.follow", "CameraFollow").c_str(), &config.camera.follow)) {
				ApplyLayoutEdit();
			}
			DrawSectionHeader("section.follow_axis", "section.follow_axis.hint");
			if (ImGui::Checkbox(Label("field.x", "CameraFollowX").c_str(), &config.camera.followX)) {
				ApplyLayoutEdit();
			}
			ImGui::SameLine();
			if (ImGui::Checkbox(Label("field.y", "CameraFollowY").c_str(), &config.camera.followY)) {
				ApplyLayoutEdit();
			}
			ImGui::SameLine();
			if (ImGui::Checkbox(Label("field.z", "CameraFollowZ").c_str(), &config.camera.followZ)) {
				ApplyLayoutEdit();
			}

			ImGui::Separator();
			DrawSectionHeader("section.clip_rect", "section.clip_rect.hint");
			if (DrawIntSliderEdit("field.left", "ClipRectLeft", config.clipRect.left, -100, 200, 1)) {
				ApplyLayoutEdit(true);
			}
			if (DrawIntSliderEdit("field.right", "ClipRectRight", config.clipRect.right, -100, 200, 1)) {
				ApplyLayoutEdit(true);
			}
			if (DrawIntSliderEdit("field.top", "ClipRectTop", config.clipRect.top, -100, 200, 1)) {
				ApplyLayoutEdit(true);
			}
			if (DrawIntSliderEdit("field.bottom", "ClipRectBottom", config.clipRect.bottom, -100, 200, 1)) {
				ApplyLayoutEdit(true);
			}
			FlushClipRectEdit();

			ImGui::Separator();
			DrawSectionHeader("section.general", "section.general.hint");
			if (ImGui::Checkbox(Label("field.enabled", "Enabled").c_str(), &config.enabled)) {
				ApplyLayoutEdit();
			}
			if (ImGui::Checkbox(Label("field.hide_in_power_armor", "HideInPowerArmor").c_str(), &config.hideInPowerArmor)) {
				ApplyLayoutEdit();
			}
		}

		[[nodiscard]] std::string DynamicActivationIdleLabel(const RE::TESIdleForm& a_idle)
		{
			if (!a_idle.formEditorID.empty()) {
				return a_idle.formEditorID.c_str();
			}
			if (!a_idle.animFileName.empty()) {
				return a_idle.animFileName.c_str();
			}

			std::array<char, 32> buffer{};
			std::snprintf(buffer.data(), buffer.size(), "%08X", a_idle.GetFormID());
			return buffer.data();
		}

		[[nodiscard]] std::string DynamicActivationIdleKey(RE::TESIdleForm& a_idle)
		{
			auto* file = a_idle.GetFile(0);
			if (!file || file->filename[0] == '\0') {
				return {};
			}

			std::array<char, 64> localID{};
			std::snprintf(localID.data(), localID.size(), "0x%X", a_idle.GetLocalFormID());
			return std::string{ file->filename } + "|" + localID.data();
		}

		[[nodiscard]] bool HasDynamicActivationIdles(const Animations::DynamicActivationIdleMap& a_idles)
		{
			return std::any_of(a_idles.begin(), a_idles.end(), [](const auto& entry) {
				return !entry.second.empty();
			});
		}

		[[nodiscard]] std::uint32_t ResolveDynamicActivationIdleIndexInFile(
			const std::vector<RE::TESIdleForm*>& a_fileIdles,
			const std::string& a_key)
		{
			for (std::size_t index = 0; index < a_fileIdles.size(); ++index) {
				auto* idle = a_fileIdles[index];
				if (idle && DynamicActivationIdleKey(*idle) == a_key) {
					return static_cast<std::uint32_t>(index);
				}
			}
			return 0;
		}

		void SelectDynamicActivationIdle(AnimationSettings& a_animation, RE::TESIdleForm& a_idle)
		{
			const auto key = DynamicActivationIdleKey(a_idle);
			if (a_animation.dynamicActivationIdle == key) {
				return;
			}

			a_animation.dynamicActivationIdle = key;
			MarkDirty();
			Animations::StopIdleAnimation();
			Previewer::ClearAnimationObjects();
		}

		[[nodiscard]] std::size_t ResolveDynamicActivationIdleFileIndex(
			const std::vector<const std::pair<const std::string, std::vector<RE::TESIdleForm*>>*>& a_files,
			const std::string& a_key)
		{
			if (!a_key.empty()) {
				for (std::size_t fileIndex = 0; fileIndex < a_files.size(); ++fileIndex) {
					for (auto* idle : a_files[fileIndex]->second) {
						if (idle && DynamicActivationIdleKey(*idle) == a_key) {
							return fileIndex;
						}
					}
				}
			}
			return 0;
		}

		void DrawAnimationTab()
		{
			auto& animation = GetMutableConfig().animation;
			auto& mirrorEvents = animation.mirrorEvents;

			DrawSectionHeader("section.live_animation", "section.live_animation.hint");
			const bool wasUsingLiveAnimation = animation.useLiveAnimation;
			if (ImGui::Checkbox(Label("field.use_live_animation", "UseLiveAnimation").c_str(), &animation.useLiveAnimation) &&
				wasUsingLiveAnimation != animation.useLiveAnimation) {
				MarkDirty();
				if (animation.useLiveAnimation) {
					Animations::StopIdleAnimation();
					Previewer::ClearAnimationObjects();
				} else {
					Animations::ResetGraphPreservingIdlePlayback();
				}
			}
			ImGui::Separator();
			if (animation.useLiveAnimation) {
				DrawSectionHeader("section.mirror_events", "section.mirror_events.hint");
				bool changed = false;
				changed |= ImGui::Checkbox(Label("field.locomotion", "MirrorLocomotion").c_str(), &mirrorEvents.locomotion);
				changed |= ImGui::Checkbox(Label("field.sneak", "MirrorSneak").c_str(), &mirrorEvents.sneak);
				changed |= ImGui::Checkbox(Label("field.jump", "MirrorJump").c_str(), &mirrorEvents.jump);
				changed |= ImGui::Checkbox(Label("field.weapon_fire", "MirrorWeaponFire").c_str(), &mirrorEvents.weaponFire);
				changed |= ImGui::Checkbox(Label("field.weapon_reload", "MirrorWeaponReload").c_str(), &mirrorEvents.weaponReload);
				changed |= ImGui::Checkbox(Label("field.melee", "MirrorMelee").c_str(), &mirrorEvents.melee);
				changed |= ImGui::Checkbox(Label("field.throw", "MirrorThrow").c_str(), &mirrorEvents.throwable);
				if (changed) {
					MarkDirty();
				}
				return;
			}

			DrawSectionHeader("section.idle_animation", "section.idle_animation.hint");
			const auto& idles = Animations::GetDynamicActivationIdles();
			if (!HasDynamicActivationIdles(idles)) {
				const char* none = L("state.none");
				int selected = 0;
				ImGui::BeginDisabled();
				ImGui::PushItemWidth(320.0F);
				ImGui::Combo(Label("field.file", "IdleFile").c_str(), &selected, &none, 1);
				ImGui::Combo(Label("field.idle_animation", "IdleAnimation").c_str(), &selected, &none, 1);
				ImGui::PopItemWidth();
				ImGui::Button(Label("button.previous", "IdleAnimationPrev").c_str());
				ImGui::SameLine();
				ImGui::Button(Label("button.next", "IdleAnimationNext").c_str());
				ImGui::EndDisabled();
				ImGui::SameLine();
				if (ImGui::Checkbox(Label("field.hide_weapon", "HideWeaponDuringIdleAnimation").c_str(), &animation.hideWeaponDuringIdleAnimation)) {
					MarkDirty();
				}
				ImGui::SameLine();
				const bool wasSheathingWeapon = animation.sheatheWeaponDuringIdleAnimation;
				if (ImGui::Checkbox(Label("field.sheathe_weapon", "SheatheWeaponDuringIdleAnimation").c_str(), &animation.sheatheWeaponDuringIdleAnimation) &&
					wasSheathingWeapon != animation.sheatheWeaponDuringIdleAnimation) {
					MarkDirty();
					Animations::ResetGraphPreservingIdlePlayback();
				}
				return;
			}

			std::vector<const std::pair<const std::string, std::vector<RE::TESIdleForm*>>*> files;
			files.reserve(idles.size());
			std::vector<std::string> fileLabels;
			fileLabels.reserve(idles.size());
			std::vector<const char*> fileItems;
			fileItems.reserve(idles.size());
			for (const auto& entry : idles) {
				if (entry.second.empty()) {
					continue;
				}
				files.push_back(std::addressof(entry));
				fileLabels.push_back(entry.first + " (" + std::to_string(entry.second.size()) + ")");
				fileItems.push_back(fileLabels.back().c_str());
			}

			if (files.empty()) {
				return;
			}

			auto selectedFileIndex = ResolveDynamicActivationIdleFileIndex(files, animation.dynamicActivationIdle);
			auto* selectedFile = files[selectedFileIndex];
			const auto& fileIdles = selectedFile->second;
			auto selectedIndex =
				ResolveDynamicActivationIdleIndexInFile(fileIdles, animation.dynamicActivationIdle);
			if (animation.dynamicActivationIdle.empty()) {
				if (auto* idle = fileIdles[selectedIndex]) {
					animation.dynamicActivationIdle = DynamicActivationIdleKey(*idle);
					MarkDirty();
				}
			}

			ImGui::PushItemWidth(320.0F);
			int selectedFileComboIndex = static_cast<int>(selectedFileIndex);
			if (ImGui::Combo(Label("field.file", "IdleFile").c_str(), &selectedFileComboIndex, fileItems.data(), static_cast<int>(fileItems.size()))) {
				selectedFileIndex = static_cast<std::size_t>(selectedFileComboIndex);
				selectedFile = files[selectedFileIndex];
				if (auto* idle = selectedFile->second.front()) {
					SelectDynamicActivationIdle(animation, *idle);
				}
			}

			selectedFileIndex = ResolveDynamicActivationIdleFileIndex(files, animation.dynamicActivationIdle);
			selectedFile = files[selectedFileIndex];
			const auto& selectedFileIdles = selectedFile->second;

			std::vector<std::string> idleLabels;
			idleLabels.reserve(selectedFileIdles.size());
			std::vector<const char*> idleItems;
			idleItems.reserve(selectedFileIdles.size());
			for (auto* idle : selectedFileIdles) {
				idleLabels.push_back(idle ? DynamicActivationIdleLabel(*idle) : Localization::GetString("state.null"));
				idleItems.push_back(idleLabels.back().c_str());
			}

			selectedIndex =
				ResolveDynamicActivationIdleIndexInFile(selectedFileIdles, animation.dynamicActivationIdle);
			int selected = static_cast<int>(selectedIndex);
			if (ImGui::Combo(Label("field.idle_animation", "IdleAnimation").c_str(), &selected, idleItems.data(), static_cast<int>(idleItems.size()))) {
				if (auto* idle = selectedFileIdles[static_cast<std::size_t>(selected)]) {
					SelectDynamicActivationIdle(animation, *idle);
				}
			}
			ImGui::PopItemWidth();
			if (ImGui::Button(Label("button.previous", "IdleAnimationPrev").c_str())) {
				selectedIndex = selectedIndex == 0 ?
					static_cast<std::uint32_t>(selectedFileIdles.size() - 1) :
					selectedIndex - 1;
				if (auto* idle = selectedFileIdles[selectedIndex]) {
					SelectDynamicActivationIdle(animation, *idle);
				}
			}
			ImGui::SameLine();
			if (ImGui::Button(Label("button.next", "IdleAnimationNext").c_str())) {
				selectedIndex = static_cast<std::uint32_t>((selectedIndex + 1) % selectedFileIdles.size());
				if (auto* idle = selectedFileIdles[selectedIndex]) {
					SelectDynamicActivationIdle(animation, *idle);
				}
			}
			ImGui::SameLine();
			if (ImGui::Checkbox(Label("field.hide_weapon", "HideWeaponDuringIdleAnimation").c_str(), &animation.hideWeaponDuringIdleAnimation)) {
				MarkDirty();
			}
			ImGui::SameLine();
			const bool wasSheathingWeapon = animation.sheatheWeaponDuringIdleAnimation;
			if (ImGui::Checkbox(Label("field.sheathe_weapon", "SheatheWeaponDuringIdleAnimation").c_str(), &animation.sheatheWeaponDuringIdleAnimation) &&
				wasSheathingWeapon != animation.sheatheWeaponDuringIdleAnimation) {
				MarkDirty();
				Animations::ResetGraphPreservingIdlePlayback();
			}
		}

		void ApplyEquipmentEdit()
		{
			MarkDirty();
			Previewer::MarkEquipmentDirty();
		}

		void DrawEquipmentSlotTable(EquipmentSettings& a_equipment)
		{
			constexpr ImGuiTableFlags tableFlags =
				ImGuiTableFlags_BordersInnerH |
				ImGuiTableFlags_BordersInnerV |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_SizingStretchSame;

			if (!ImGui::BeginTable("equipment_slots", 8, tableFlags)) {
				return;
			}

			for (std::uint32_t column = 0; column < 8; ++column) {
				ImGui::TableSetupColumn("");
			}

			for (std::uint32_t row = 0; row < 4; ++row) {
				ImGui::TableNextRow();
				for (std::uint32_t column = 0; column < 8; ++column) {
					const auto slotIndex = row * 8 + column;
					const auto bit = 1u << slotIndex;
					bool enabled = (a_equipment.syncSlotMask & bit) != 0;

					char label[64]{};
					std::snprintf(
						label,
						sizeof(label),
						"%u %s",
						Equipment::kEditorSlotBase + slotIndex,
						Equipment::PrettySlotName(slotIndex));

					ImGui::TableSetColumnIndex(static_cast<int>(column));
					ImGui::PushID(static_cast<int>(slotIndex));
					if (ImGui::Checkbox(label, &enabled)) {
						if (enabled) {
							a_equipment.syncSlotMask |= bit;
						} else {
							a_equipment.syncSlotMask &= ~bit;
						}
						ApplyEquipmentEdit();
					}
					ImGui::PopID();
				}
			}

			ImGui::EndTable();
		}

		void DrawEquippedArmorList()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			std::vector<Equipment::EquippedArmorInfo> armors;
			if (player) {
				armors = Equipment::CollectEquippedArmors(*player, Equipment::EffectiveEditorSlotMask(*player));
			}

			if (ImGui::BeginChild("equipped_armors", ImVec2(0.0F, 220.0F), true)) {
				if (!player) {
					ImGui::TextDisabled(L("equipment.player_unavailable"));
				} else if (armors.empty()) {
					ImGui::TextDisabled(L("equipment.no_equipped_armor"));
				} else {
					for (const auto& armor : armors) {
						const auto slots = Equipment::FormatSlotList(armor.slotMask);
						if (armor.excluded) {
							ImGui::TextDisabled("%s (%s)", armor.name.c_str(), slots.c_str());
						} else {
							ImGui::Text("%s (%s)", armor.name.c_str(), slots.c_str());
						}
					}
				}
			}
			ImGui::EndChild();
		}

		void DrawEquipmentTab()
		{
			auto& equipment = GetMutableConfig().equipment;
			DrawSectionHeader("section.equipment_slots", "section.equipment_slots.hint");
			DrawEquipmentSlotTable(equipment);
			ImGui::Separator();
			DrawSectionHeader("section.equipped_armor", "section.equipped_armor.hint");
			DrawEquippedArmorList();
		}

		[[nodiscard]] bool LightNameExists(const Config& a_config, const std::string& a_name, const std::size_t a_ignoreIndex)
		{
			for (std::size_t index = 0; index < a_config.lights.size(); ++index) {
				if (index != a_ignoreIndex && a_config.lights[index].name == a_name) {
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] std::string MakeUniqueLightName(
			const Config& a_config,
			const std::string& a_baseName,
			const std::size_t a_ignoreIndex = static_cast<std::size_t>(-1))
		{
			const auto baseName = a_baseName.empty() ? std::string{ "Light" } : a_baseName;
			if (!LightNameExists(a_config, baseName, a_ignoreIndex)) {
				return baseName;
			}

			for (std::uint32_t suffix = 2; suffix < 10000; ++suffix) {
				auto candidate = baseName + "_" + std::to_string(suffix);
				if (!LightNameExists(a_config, candidate, a_ignoreIndex)) {
					return candidate;
				}
			}

			return baseName;
		}

		void DrawLightTab()
		{
			auto& config = GetMutableConfig();
			DrawSectionHeader("section.lights", "section.lights.hint");
			if (ImGui::Button(L("button.add"))) {
				LightSettings light;
				light.name = MakeUniqueLightName(config, "Directional");
				light.type = LightType::kDirectional;
				light.fixed.position = {};
				light.fixed.intensity = 1.0F;
				config.lights.push_back(light);
				ApplyConfigEdit();
			}

			std::optional<std::size_t> deleteIndex;
			for (std::size_t index = 0; index < config.lights.size(); ++index) {
				auto& light = config.lights[index];
				ImGui::PushID(static_cast<int>(index));
				const auto treeLabel = (light.name.empty() ? Localization::GetString("state.unnamed") : light.name) + "###Light";
				const bool expanded = ImGui::TreeNode(treeLabel.c_str());
				if (expanded) {
					if (ImGui::Button(L("button.delete"))) {
						deleteIndex = index;
					}

					std::array<char, 128> nameBuffer{};
					const auto copySize = (std::min)(light.name.size(), nameBuffer.size() - 1);
					std::memcpy(nameBuffer.data(), light.name.data(), copySize);
					if (ImGui::InputText(Label("field.name", "LightName").c_str(), nameBuffer.data(), nameBuffer.size())) {
						light.name = nameBuffer.data();
						MarkDirty();
					}
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						light.name = MakeUniqueLightName(config, light.name, index);
						MarkDirty();
					}

					int type = static_cast<int>(light.type);
					const char* types[] = { L("light.directional"), L("light.fixed"), L("light.tod") };
					if (ImGui::Combo(Label("field.type", "LightType").c_str(), &type, types, static_cast<int>(std::size(types)))) {
						light.type = static_cast<LightType>(type);
						ApplyConfigEdit();
					}
					if (light.type != LightType::kDirectional && ImGui::Checkbox(Label("field.use_in_interior", "UseInInterior").c_str(), &light.useInInterior)) {
						ApplyConfigEdit();
					}

					const auto drawFixedControls = [&](FixedLightSettings& a_settings) {
						bool changed = false;
						changed |= DrawIntSliderEdit("field.position_x", "PositionX", a_settings.position.x, -300, 300, 1);
						changed |= DrawIntSliderEdit("field.position_y", "PositionY", a_settings.position.y, -300, 300, 1);
						changed |= DrawIntSliderEdit("field.position_z", "PositionZ", a_settings.position.z, -300, 300, 1);

						float diffuse[3] = { a_settings.diffuse.r, a_settings.diffuse.g, a_settings.diffuse.b };
						if (ImGui::ColorEdit3(Label("field.diffuse", "Diffuse").c_str(), diffuse, ImGuiColorEditFlags_Float)) {
							a_settings.diffuse.r = diffuse[0];
							a_settings.diffuse.g = diffuse[1];
							a_settings.diffuse.b = diffuse[2];
							changed = true;
						}

						changed |= DrawIntSliderEdit("field.distance", "Distance", a_settings.distance, 50, 2000, 5);
						changed |= DrawFloatSliderEdit("field.intensity", "Intensity", a_settings.intensity, 0.01F, 10.0F, 0.01F);
						return changed;
					};

					switch (light.type) {
					case LightType::kDirectional:
						if (DrawIntSliderEdit("field.position_x", "PositionX", light.fixed.position.x, -300, 300, 1) ||
						    DrawIntSliderEdit("field.position_y", "PositionY", light.fixed.position.y, -300, 300, 1) ||
						    DrawIntSliderEdit("field.position_z", "PositionZ", light.fixed.position.z, -300, 300, 1) ||
						    DrawFloatSliderEdit("field.intensity", "Intensity", light.fixed.intensity, 0.01F, 10.0F, 0.01F)) {
							ApplyConfigEdit();
						}
						break;
					case LightType::kFixed:
						if (drawFixedControls(light.fixed)) {
							ApplyConfigEdit();
						}
						break;
					case LightType::kTimeOfDay:
						if (DrawFloatSliderEdit("field.start_tod", "StartToD", light.timeOfDay.startTimeOfDay, 0.0F, 24.0F, 0.01F) ||
						    DrawFloatSliderEdit("field.end_tod", "EndToD", light.timeOfDay.endTimeOfDay, 0.0F, 24.0F, 0.01F)) {
							ApplyConfigEdit();
						}
						if (ImGui::TreeNode(L("field.start"))) {
							if (drawFixedControls(light.timeOfDay.start)) {
								ApplyConfigEdit();
							}
							ImGui::TreePop();
						}
						if (ImGui::TreeNode(L("field.end"))) {
							if (drawFixedControls(light.timeOfDay.end)) {
								ApplyConfigEdit();
							}
							ImGui::TreePop();
						}
						break;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			if (deleteIndex && *deleteIndex < config.lights.size()) {
				config.lights.erase(config.lights.begin() + static_cast<std::ptrdiff_t>(*deleteIndex));
				ApplyConfigEdit();
			}
		}

		void DrawDebugTab()
		{
			if (ImGui::BeginTabBar("debug_tabs")) {
				if (ImGui::BeginTabItem(L("debug.tab.graph_info"))) {
					const auto snapshot = Animations::GetDebugSnapshot();
					DrawGraphInfoTab(snapshot);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem(L("debug.tab.subgraphs"))) {
					const auto snapshot = Animations::GetDebugSnapshot();
					DrawSubgraphState(snapshot);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem(L("debug.tab.active_nodes"))) {
					const auto snapshot = Animations::GetDebugSnapshot();
					DrawActiveNodesTab(snapshot);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem(L("debug.tab.facegen"))) {
					DrawFaceGenTab();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}

		void DrawMainWindow()
		{
			ImGuiIO& io = ImGui::GetIO();
			ImGui::SetNextWindowPos(ImVec2(20.0F, 20.0F), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowSize(ImVec2((std::min)(920.0F, io.DisplaySize.x - 40.0F), 620.0F), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowBgAlpha(0.92F);

			bool windowOpen = true;
			const char* windowTitle = g_isDirty ? "TF3DHud*###TF3DHud" : "TF3DHud###TF3DHud";
			if (!ImGui::Begin(windowTitle, &windowOpen, ImGuiWindowFlags_MenuBar)) {
				ImGui::End();
				if (!windowOpen) {
					RequestCloseMenu();
				}
				DrawSavePromptModal();
				return;
			}

			if (ImGui::BeginMenuBar()) {
				if (ImGui::BeginMenu(L("menu.file"))) {
					if (ImGui::MenuItem(L("menu.save"))) {
						(void)SaveConfigFromMenu();
					}
					if (ImGui::MenuItem(L("menu.reload"))) {
						Previewer::ReloadConfig();
						g_isDirty = false;
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu(L("menu.language"))) {
					const auto currentLanguage = Localization::GetCurrentLanguage();
					for (const auto& language : Localization::GetSupportedLanguages()) {
						if (ImGui::MenuItem(language.c_str(), nullptr, language == currentLanguage)) {
							(void)Localization::SetCurrentLanguage(language);
						}
					}
					ImGui::EndMenu();
				}
				ImGui::EndMenuBar();
			}

			if (ImGui::BeginTabBar("tf3dhud_tabs")) {
				if (ImGui::BeginTabItem(L("tab.layout"))) {
					DrawLayoutTab();
					DrawClipRectOverlay();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem(L("tab.animation"))) {
					DrawAnimationTab();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem(L("tab.equipment"))) {
					DrawEquipmentTab();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem(L("tab.light"))) {
					DrawLightTab();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem(L("tab.debug"))) {
					DrawDebugTab();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}

			ImGui::End();
			if (!windowOpen) {
				RequestCloseMenu();
			}
			DrawSavePromptModal();
		}

		[[nodiscard]] bool Initialize()
		{
			if (g_imguiInitialized) {
				return true;
			}

			auto* rendererData = RE::BSGraphics::GetRendererData();
			if (!rendererData || !rendererData->device || !rendererData->context) {
				return false;
			}

			auto hwnd = reinterpret_cast<HWND>(rendererData->renderWindow[0].hwnd);
			if (!hwnd) {
				hwnd = g_outputWindow ? g_outputWindow : ::FindWindowA("Fallout4", nullptr);
			}
			if (!hwnd) {
				return false;
			}
			::GetWindowRect(hwnd, std::addressof(g_windowRect));

			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			Localization::Initialize();
			Localization::LoadFonts(io);

			g_originalWndProc = reinterpret_cast<WNDPROC>(
				::SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ImGuiWndProc)));
			if (!g_originalWndProc) {
				REX::WARN("Animations: ImGui WndProc hook failed");
				return false;
			}

			ImGui_ImplWin32_Init(hwnd);
			ImGui_ImplDX11_Init(
				reinterpret_cast<ID3D11Device*>(rendererData->device),
				reinterpret_cast<ID3D11DeviceContext*>(rendererData->context));

			g_imguiInitialized = true;
			return true;
		}

		void RenderFrame()
		{
			if (!Initialize()) {
				return;
			}

			auto* rendererData = RE::BSGraphics::GetRendererData();
			if (!rendererData || !rendererData->context) {
				return;
			}

			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			if (g_menuOpen) {
				DrawMainWindow();
			}
			ImGui::Render();

			auto* context = rendererData->context;
			REX::W32::ID3D11RenderTargetView* oldRTV = nullptr;
			REX::W32::ID3D11DepthStencilView* oldDSV = nullptr;
			context->OMGetRenderTargets(1, &oldRTV, &oldDSV);
			if (auto* backBufferRTV = rendererData->renderWindow[0].swapChainRenderTarget.rtView) {
				context->OMSetRenderTargets(1, &backBufferRTV, nullptr);
			}
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			context->OMSetRenderTargets(1, &oldRTV, oldDSV);
			if (oldRTV) {
				oldRTV->Release();
			}
			if (oldDSV) {
				oldDSV->Release();
			}
		}

		HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags)
		{
			RenderFrame();
			return g_originalPresent(a_swapChain, a_syncInterval, a_flags);
		}

		void InstallPresentHook(IDXGISwapChain* a_swapChain)
		{
			if (g_presentHookInstalled || !a_swapChain) {
				return;
			}

			auto** swapChainVTable = *reinterpret_cast<void***>(a_swapChain);
			if (!swapChainVTable) {
				return;
			}

			REL::Relocation<std::uintptr_t> swapChainVTableRel{ reinterpret_cast<std::uintptr_t>(swapChainVTable) };
			g_originalPresent = reinterpret_cast<Present_t>(swapChainVTableRel.write_vfunc(8, &HookedPresent));
			g_presentHookInstalled = true;
		}

		void InstallPresentHookFromRenderer()
		{
			if (g_presentHookInstalled) {
				return;
			}

			auto* rendererData = RE::BSGraphics::GetRendererData();
			if (!rendererData || !rendererData->renderWindow[0].swapChain) {
				return;
			}

			InstallPresentHook(reinterpret_cast<IDXGISwapChain*>(rendererData->renderWindow[0].swapChain));
		}

		BOOL WINAPI HookedClipCursor(const RECT* a_rect)
		{
			if (g_imguiInitialized && g_menuOpen) {
				a_rect = g_windowActive ? std::addressof(g_windowRect) : nullptr;
			}

			return g_originalClipCursor ? g_originalClipCursor(a_rect) : TRUE;
		}

		HRESULT WINAPI HookedD3D11CreateDeviceAndSwapChain(
			IDXGIAdapter* a_adapter,
			D3D_DRIVER_TYPE a_driverType,
			HMODULE a_software,
			UINT a_flags,
			const D3D_FEATURE_LEVEL* a_featureLevels,
			UINT a_featureLevelCount,
			UINT a_sdkVersion,
			const DXGI_SWAP_CHAIN_DESC* a_swapChainDesc,
			IDXGISwapChain** a_swapChain,
			ID3D11Device** a_device,
			D3D_FEATURE_LEVEL* a_featureLevel,
			ID3D11DeviceContext** a_immediateContext)
		{
			const auto result = g_originalD3D11CreateDeviceAndSwapChain(
				a_adapter,
				a_driverType,
				a_software,
				a_flags,
				a_featureLevels,
				a_featureLevelCount,
				a_sdkVersion,
				a_swapChainDesc,
				a_swapChain,
				a_device,
				a_featureLevel,
				a_immediateContext);

			if (a_swapChainDesc) {
				g_outputWindow = a_swapChainDesc->OutputWindow;
			}

			if (a_swapChain && *a_swapChain) {
				InstallPresentHook(*a_swapChain);
			} else {
				InstallPresentHookFromRenderer();
			}

			return result;
		}
	}

	void InstallHooks()
	{
		if (!g_d3dHookInstalled) {
			REL::Relocation<std::uintptr_t> callSite{ Address::D3D11CreateDeviceAndSwapChainCall.address() };
			g_originalD3D11CreateDeviceAndSwapChain =
				reinterpret_cast<D3D11CreateDeviceAndSwapChain_t>(
					callSite.write_call<5>(reinterpret_cast<std::uintptr_t>(&HookedD3D11CreateDeviceAndSwapChain)));
			g_d3dHookInstalled = true;
		}

		if (!g_clipCursorHookInstalled) {
			g_originalClipCursor = *reinterpret_cast<ClipCursor_t*>(g_clipCursor.address());
			g_clipCursor.write_vfunc(0, &HookedClipCursor);
			g_clipCursorHookInstalled = true;
		}
	}
}
