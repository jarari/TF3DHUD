#include "ImguiMenu.h"

#include "Address.h"
#include "Animations.h"
#include "Config.h"
#include "Previewer.h"
#include "Renderer.h"

#include "RE/B/BSGraphics.h"

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

		struct DisplayBounds
		{
			float left;
			float top;
			float right;
			float bottom;
		};

		[[nodiscard]] DisplayBounds GetOverlayScreenPlaneBounds(const Config& a_config);

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
			if (a_clipRectChanged) {
				g_clipRectDirty = true;
			}
			Previewer::ApplyConfigChanges();
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
			if (ImGui::Button("E")) {
				g_editingValueId = a_id;
				g_focusEditInput = true;
				return true;
			}
			return false;
		}

		[[nodiscard]] bool DrawIntSliderEdit(
			const char* a_label,
			float& a_value,
			const int a_min,
			const int a_max,
			const int a_step)
		{
			bool changed = false;
			ImGui::PushID(a_label);
			const bool editing = g_editingValueId == a_label;
			int value = static_cast<int>(std::lround(a_value));
			if (editing) {
				if (g_focusEditInput) {
					ImGui::SetKeyboardFocusHere();
					g_focusEditInput = false;
				}
				if (ImGui::InputInt(a_label, &value, a_step, a_step, ImGuiInputTextFlags_EnterReturnsTrue) ||
				    ImGui::IsItemDeactivatedAfterEdit()) {
					value = std::clamp(value, a_min, a_max);
					a_value = static_cast<float>(value);
					g_editingValueId.clear();
					changed = true;
				}
			} else {
				if (ImGui::SliderInt(a_label, &value, a_min, a_max, "%d", ImGuiSliderFlags_AlwaysClamp)) {
					if (a_step > 1) {
						value = std::clamp(((value + (a_step / 2)) / a_step) * a_step, a_min, a_max);
					}
					a_value = static_cast<float>(value);
					changed = true;
				}
				DrawEditButton(a_label);
			}
			ImGui::PopID();
			return changed;
		}

		[[nodiscard]] bool DrawFloatSliderEdit(
			const char* a_label,
			float& a_value,
			const float a_min,
			const float a_max,
			const float a_step)
		{
			bool changed = false;
			ImGui::PushID(a_label);
			const bool editing = g_editingValueId == a_label;
			if (editing) {
				if (g_focusEditInput) {
					ImGui::SetKeyboardFocusHere();
					g_focusEditInput = false;
				}
				if (ImGui::InputFloat(a_label, &a_value, a_step, a_step, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue) ||
				    ImGui::IsItemDeactivatedAfterEdit()) {
					a_value = std::clamp(a_value, a_min, a_max);
					g_editingValueId.clear();
					changed = true;
				}
			} else {
				if (ImGui::SliderFloat(a_label, &a_value, a_min, a_max, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
					a_value = std::clamp(std::round(a_value / a_step) * a_step, a_min, a_max);
					changed = true;
				}
				DrawEditButton(a_label);
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
					g_menuOpen = !g_menuOpen;
					::ShowCursor(g_menuOpen);
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
				ImGui::TextUnformatted("Active clip: none");
				return;
			}

			ImGui::Text("Active clip: %s", clipIter->clipName.empty() ? "(unnamed)" : clipIter->clipName.c_str());
			if (clipIter->inSubgraph) {
				ImGui::Text("Subgraph: slot %u root=%p", clipIter->subgraphSlot, reinterpret_cast<void*>(clipIter->behaviorRootId));
			} else {
				ImGui::Text("Subgraph: root graph root=%p", reinterpret_cast<void*>(clipIter->behaviorRootId));
			}
			ImGui::Text("Path: %s", clipIter->resolvedClipPath.empty() ? clipIter->authoredClipPath.c_str() : clipIter->resolvedClipPath.c_str());
			if (clipIter->hasTiming) {
				ImGui::Text("Time (+0x140 localTime): %.4f / %.4f", clipIter->currentTime, clipIter->duration);
				ImGui::Text("Control +0x10: %.4f", clipIter->controlLocalTime);
				ImGui::Text(
					"Mode (+0xBE): %u  User fraction (+0xB8): %.4f",
					clipIter->playbackMode,
					clipIter->userControlledTimeFraction);
			} else {
				ImGui::TextUnformatted("Time: unavailable");
			}
		}

		void DrawIdList(
			const char* a_label,
			const std::uint32_t a_count,
			const std::uint32_t a_shown,
			const std::array<std::uint64_t, Animations::kMaxSubgraphDebugRequestEntries>& a_values)
		{
			ImGui::Text("%s count=%u shown=%u:", a_label, a_count, a_shown);
			if (a_count != 0 && a_shown == 0) {
				ImGui::SameLine();
				ImGui::TextDisabled("data unavailable");
				return;
			}
			if (a_count == 0) {
				ImGui::SameLine();
				ImGui::TextDisabled("none");
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
			ImGui::Text("%s count=%u shown=%u", a_label, a_count, a_shown);
			if (a_count != 0 && a_shown == 0) {
				ImGui::TextDisabled("data unavailable");
				return;
			}
			if (a_shown == 0) {
				return;
			}

			ImGui::Indent();
			for (std::uint32_t index = 0; index < a_shown; ++index) {
				const auto* file = a_files[index].path.data();
				ImGui::BulletText("%s", file[0] == '\0' ? "(empty)" : file);
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
				ImGui::TextDisabled("No graph swap data attached.");
			}

			DrawIdList(
				"default handles",
				a_snapshot.defaultSubgraphHandleCount,
				a_snapshot.defaultSubgraphHandleShown,
				a_snapshot.defaultSubgraphHandles);
			DrawIdList(
				"default ids",
				a_snapshot.defaultSubgraphIdCount,
				a_snapshot.defaultSubgraphIdShown,
				a_snapshot.defaultSubgraphIds);
			DrawIdList(
				"weapon handles",
				a_snapshot.weaponSubgraphHandleCount,
				a_snapshot.weaponSubgraphHandleShown,
				a_snapshot.weaponSubgraphHandles);
			DrawIdList(
				"weapon ids",
				a_snapshot.weaponSubgraphIdCount,
				a_snapshot.weaponSubgraphIdShown,
				a_snapshot.weaponSubgraphIds);

			constexpr ImGuiTableFlags tableFlags =
				ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
				ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_SizingStretchProp;

			if (ImGui::BeginTable("subgraph_slots", 10, tableFlags)) {
				ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 34.0F);
				ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 42.0F);
				ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthFixed, 92.0F);
				ImGui::TableSetupColumn("Shared", ImGuiTableColumnFlags_WidthFixed, 92.0F);
				ImGui::TableSetupColumn("Root", ImGuiTableColumnFlags_WidthFixed, 92.0F);
				ImGui::TableSetupColumn("Root Name", ImGuiTableColumnFlags_WidthFixed, 140.0F);
				ImGui::TableSetupColumn("Use", ImGuiTableColumnFlags_WidthFixed, 42.0F);
				ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_WidthFixed, 58.0F);
				ImGui::TableSetupColumn("data+160", ImGuiTableColumnFlags_WidthFixed, 68.0F);
				ImGui::TableSetupColumn("data+178", ImGuiTableColumnFlags_WidthFixed, 68.0F);
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
				if (ImGui::TreeNode("file arrays")) {
					ImGui::SameLine();
					ImGui::TextDisabled("slot %u", slot.index);
					DrawFileArray("data+0x160", slot.files160Count, slot.files160Shown, slot.files160);
					DrawFileArray("data+0x178", slot.files178Count, slot.files178Shown, slot.files178);
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
				speed.constructed ? "true" : "false",
				speed.reset ? "true" : "false",
				speed.polled ? "true" : "false",
				speed.pollCount,
				speed.applyAdjustments ? "true" : "false");
			ImGui::Text(
				"Speed desired=%.3f scale=%.3f raw=%.3f contour=%.3f last=%.3f graph=%s %.3f",
				speed.desiredSpeed,
				speed.scale,
				speed.rawSpeed,
				speed.graphSpeed,
				speed.lastSpeed,
				speed.previewGraphSpeedHas ? "yes" : "no",
				speed.previewGraphSpeed);
			ImGui::Text(
				"Freeze preview=%s actor=%s contours=%s actorGate=%s resolved=%s state=%s response=%u adjustments=%u applied=%s",
				speed.previewFreeze ? "true" : "false",
				speed.actorFreeze ? "true" : "false",
				speed.useContours ? "true" : "false",
				speed.actorAllowsContours ? "true" : "false",
				speed.contourResolved ? "true" : "false",
				speed.contourState ? "true" : "false",
				speed.contourResponse,
				speed.adjustmentCount,
				speed.contourApplied ? "true" : "false");
		}

		void DrawActiveNodesTable(const Animations::DebugSnapshot& a_snapshot);

		void DrawGraphInfoTab(const Animations::DebugSnapshot& a_snapshot)
		{
			if (!a_snapshot.lastDiagnostic.empty()) {
				ImGui::Text("Last diagnostic: %s", a_snapshot.lastDiagnostic.c_str());
			}
			if (ImGui::Button("Log Right-Hand Bone Hierarchy")) {
				Previewer::LogRightHandBoneHierarchy();
			}
			ImGui::Separator();

			ImGui::Text("Project: %s", a_snapshot.project.empty() ? "(none)" : a_snapshot.project.c_str());
			ImGui::Text(
				"Manager=%p Graph=%p Behavior=%p ActiveGraph=%u/%u",
				reinterpret_cast<void*>(a_snapshot.manager),
				reinterpret_cast<void*>(a_snapshot.graph),
				reinterpret_cast<void*>(a_snapshot.behaviorGraph),
				a_snapshot.activeGraphIndex,
				a_snapshot.graphCount);
			ImGui::Text(
				"Behavior active=%s linked=%s updateActiveNodes=%s stateOrTransitionChanged=%s activeNodes=%u shown=%zu",
				a_snapshot.behaviorActive ? "true" : "false",
				a_snapshot.behaviorLinked ? "true" : "false",
				a_snapshot.updateActiveNodes ? "true" : "false",
				a_snapshot.stateOrTransitionChanged ? "true" : "false",
				a_snapshot.activeNodeCount,
				a_snapshot.activeNodes.size());
			ImGui::Text(
				"generateHavokBones=%s ragdollInterface=%s physicsWorld=%s windowActive=%s",
				a_snapshot.generateHavokBones ? "true" : "false",
				a_snapshot.hasRagdollInterface ? "true" : "false",
				a_snapshot.hasPhysicsWorld ? "true" : "false",
				g_windowActive ? "true" : "false");
			ImGui::Text(
				"Live sync active=%s time=%.4f/%.4f speed=%.4f points=%u",
				a_snapshot.liveSync.active ? "true" : "false",
				a_snapshot.liveSync.currentTime,
				a_snapshot.liveSync.totalTime,
				a_snapshot.liveSync.speed,
				a_snapshot.liveSync.syncPointCount);
			ImGui::Text(
				"Preview sync active=%s time=%.4f/%.4f speed=%.4f points=%u",
				a_snapshot.previewSync.active ? "true" : "false",
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
			if (snapshot.sliders.empty()) {
				ImGui::TextDisabled("No FaceGen slider data.");
				return;
			}

			constexpr ImGuiTableFlags tableFlags =
				ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
				ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_SizingStretchProp;

			if (!ImGui::BeginTable("facegen_sliders", 4, tableFlags)) {
				return;
			}

			ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthStretch, 1.3F);
			ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 54.0F);
			ImGui::TableSetupColumn("Live", ImGuiTableColumnFlags_WidthStretch, 1.5F);
			ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, 1.5F);
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
					ImGui::TextDisabled("-");
				}
				ImGui::TableSetColumnIndex(3);
				if (slider.hasPreview) {
					ImGui::TextUnformatted(slider.previewValue.c_str());
				} else {
					ImGui::TextDisabled("-");
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

			ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 34.0F);
			ImGui::TableSetupColumn("Entry", ImGuiTableColumnFlags_WidthFixed, 92.0F);
			ImGui::TableSetupColumn("Node", ImGuiTableColumnFlags_WidthFixed, 92.0F);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 54.0F);
			ImGui::TableSetupColumn("SG", ImGuiTableColumnFlags_WidthFixed, 38.0F);
			ImGui::TableSetupColumn("Root", ImGuiTableColumnFlags_WidthFixed, 92.0F);
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.4F);
			ImGui::TableSetupColumn("Clip Path", ImGuiTableColumnFlags_WidthStretch, 2.2F);
			ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 92.0F);
			ImGui::TableSetupColumn("Ctrl", ImGuiTableColumnFlags_WidthFixed, 72.0F);
			ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 48.0F);
			ImGui::TableSetupColumn("Frac", ImGuiTableColumnFlags_WidthFixed, 60.0F);
			ImGui::TableSetupColumn("Behavior", ImGuiTableColumnFlags_WidthFixed, 92.0F);
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
				ImGui::TextUnformatted(node.isClip ? "clip" : "node");
				ImGui::TableSetColumnIndex(4);
				if (node.inSubgraph) {
					ImGui::Text("%u", node.subgraphSlot);
				} else {
					ImGui::TextDisabled("-");
				}
				ImGui::TableSetColumnIndex(5);
				ImGui::Text("%p", reinterpret_cast<void*>(node.behaviorRootId));
				ImGui::TableSetColumnIndex(6);
				if (!node.nodeName.empty()) {
					ImGui::TextUnformatted(node.nodeName.c_str());
				} else if (!node.clipName.empty()) {
					ImGui::TextUnformatted(node.clipName.c_str());
				} else {
					ImGui::TextDisabled("(unnamed)");
				}
				ImGui::TableSetColumnIndex(7);
				if (node.isClip) {
					const auto& path = node.resolvedClipPath.empty() ? node.authoredClipPath : node.resolvedClipPath;
					ImGui::TextUnformatted(path.empty() ? "(empty)" : path.c_str());
				} else {
					ImGui::TextDisabled("-");
				}
				ImGui::TableSetColumnIndex(8);
				if (node.hasTiming) {
					ImGui::Text("%.3f/%.3f", node.currentTime, node.duration);
				} else {
					ImGui::TextDisabled("-");
				}
				ImGui::TableSetColumnIndex(9);
				if (node.hasControlLocalTime) {
					ImGui::Text("%.3f", node.controlLocalTime);
				} else {
					ImGui::TextDisabled("-");
				}
				ImGui::TableSetColumnIndex(10);
				if (node.isClip) {
					ImGui::Text("%u", node.playbackMode);
				} else {
					ImGui::TextDisabled("-");
				}
				ImGui::TableSetColumnIndex(11);
				if (node.isClip) {
					ImGui::Text("%.3f", node.userControlledTimeFraction);
				} else {
					ImGui::TextDisabled("-");
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
				return "bottom left";
			case 2:
				return "bottom center";
			case 3:
				return "bottom right";
			case 4:
				return "middle left";
			case 5:
				return "middle center";
			case 6:
				return "middle right";
			case 7:
				return "top left";
			case 8:
				return "top center";
			case 9:
				return "top right";
			default:
				return "unknown";
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

			ImGui::TextUnformatted("Anchor");
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
			ImGui::TextUnformatted("View");
			if (DrawIntSliderEdit("FOV", config.fov, 10, 120, 1)) {
				ApplyLayoutEdit();
			}
			if (DrawIntSliderEdit("PlacementX", config.placementX, -200, 200, 1)) {
				ApplyLayoutEdit();
			}
			if (DrawIntSliderEdit("PlacementY", config.placementY, -200, 200, 1)) {
				ApplyLayoutEdit();
			}
			if (DrawIntSliderEdit("CameraDistance", config.cameraDistance, 100, 2000, 5)) {
				ApplyLayoutEdit();
			}
			if (DrawFloatSliderEdit("ModelScale", config.modelScale, 0.01F, 2.0F, 0.01F)) {
				ApplyLayoutEdit();
			}
			if (DrawIntSliderEdit("YawDegrees", config.yawDegrees, 0, 360, 1)) {
				ApplyLayoutEdit();
			}

			ImGui::Separator();
			ImGui::TextUnformatted("ClipRect");
			if (DrawIntSliderEdit("Left", config.clipRect.left, -100, 200, 1)) {
				ApplyLayoutEdit(true);
			}
			if (DrawIntSliderEdit("Right", config.clipRect.right, -100, 200, 1)) {
				ApplyLayoutEdit(true);
			}
			if (DrawIntSliderEdit("Top", config.clipRect.top, -100, 200, 1)) {
				ApplyLayoutEdit(true);
			}
			if (DrawIntSliderEdit("Bottom", config.clipRect.bottom, -100, 200, 1)) {
				ApplyLayoutEdit(true);
			}
			FlushClipRectEdit();

			ImGui::Separator();
			ImGui::TextUnformatted("General");
			if (ImGui::Checkbox("Enabled", &config.enabled)) {
				ApplyLayoutEdit();
			}
			if (ImGui::Checkbox("HideInPowerArmor", &config.hideInPowerArmor)) {
				ApplyLayoutEdit();
			}
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
			if (ImGui::Button("Add")) {
				LightSettings light;
				light.name = MakeUniqueLightName(config, "Directional");
				light.type = LightType::kDirectional;
				light.fixed.position = {};
				light.fixed.intensity = 1.0F;
				config.lights.push_back(light);
				Previewer::ApplyConfigChanges();
			}

			std::optional<std::size_t> deleteIndex;
			for (std::size_t index = 0; index < config.lights.size(); ++index) {
				auto& light = config.lights[index];
				ImGui::PushID(static_cast<int>(index));
				const auto treeLabel = (light.name.empty() ? std::string{ "(unnamed)" } : light.name) + "###Light";
				const bool expanded = ImGui::TreeNode(treeLabel.c_str());
				if (expanded) {
					if (ImGui::Button("Delete")) {
						deleteIndex = index;
					}

					std::array<char, 128> nameBuffer{};
					const auto copySize = (std::min)(light.name.size(), nameBuffer.size() - 1);
					std::memcpy(nameBuffer.data(), light.name.data(), copySize);
					if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size())) {
						light.name = nameBuffer.data();
					}
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						light.name = MakeUniqueLightName(config, light.name, index);
					}

					int type = static_cast<int>(light.type);
					const char* types[] = { "Directional", "Fixed", "ToD" };
					if (ImGui::Combo("Type", &type, types, static_cast<int>(std::size(types)))) {
						light.type = static_cast<LightType>(type);
						Previewer::ApplyConfigChanges();
					}
					if (light.type != LightType::kDirectional && ImGui::Checkbox("UseInInterior", &light.useInInterior)) {
						Previewer::ApplyConfigChanges();
					}

					const auto drawFixedControls = [&](FixedLightSettings& a_settings) {
						bool changed = false;
						changed |= DrawIntSliderEdit("PositionX", a_settings.position.x, -300, 300, 1);
						changed |= DrawIntSliderEdit("PositionY", a_settings.position.y, -300, 300, 1);
						changed |= DrawIntSliderEdit("PositionZ", a_settings.position.z, -300, 300, 1);

						float diffuse[3] = { a_settings.diffuse.r, a_settings.diffuse.g, a_settings.diffuse.b };
						if (ImGui::ColorEdit3("Diffuse", diffuse, ImGuiColorEditFlags_Float)) {
							a_settings.diffuse.r = diffuse[0];
							a_settings.diffuse.g = diffuse[1];
							a_settings.diffuse.b = diffuse[2];
							changed = true;
						}

						changed |= DrawIntSliderEdit("Distance", a_settings.distance, 50, 2000, 5);
						changed |= DrawFloatSliderEdit("Intensity", a_settings.intensity, 0.01F, 10.0F, 0.01F);
						return changed;
					};

					switch (light.type) {
					case LightType::kDirectional:
						if (DrawIntSliderEdit("PositionX", light.fixed.position.x, -300, 300, 1) ||
						    DrawIntSliderEdit("PositionY", light.fixed.position.y, -300, 300, 1) ||
						    DrawIntSliderEdit("PositionZ", light.fixed.position.z, -300, 300, 1) ||
						    DrawFloatSliderEdit("Intensity", light.fixed.intensity, 0.01F, 10.0F, 0.01F)) {
							Previewer::ApplyConfigChanges();
						}
						break;
					case LightType::kFixed:
						if (drawFixedControls(light.fixed)) {
							Previewer::ApplyConfigChanges();
						}
						break;
					case LightType::kTimeOfDay:
						if (DrawFloatSliderEdit("StartToD", light.timeOfDay.startTimeOfDay, 0.0F, 24.0F, 0.01F) ||
						    DrawFloatSliderEdit("EndToD", light.timeOfDay.endTimeOfDay, 0.0F, 24.0F, 0.01F)) {
							Previewer::ApplyConfigChanges();
						}
						if (ImGui::TreeNode("Start")) {
							if (drawFixedControls(light.timeOfDay.start)) {
								Previewer::ApplyConfigChanges();
							}
							ImGui::TreePop();
						}
						if (ImGui::TreeNode("End")) {
							if (drawFixedControls(light.timeOfDay.end)) {
								Previewer::ApplyConfigChanges();
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
				Previewer::ApplyConfigChanges();
			}
		}

		void DrawDebugTab()
		{
			if (ImGui::BeginTabBar("debug_tabs")) {
				if (ImGui::BeginTabItem("Graph Info")) {
					const auto snapshot = Animations::GetDebugSnapshot();
					DrawGraphInfoTab(snapshot);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Subgraphs")) {
					const auto snapshot = Animations::GetDebugSnapshot();
					DrawSubgraphState(snapshot);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Active Nodes")) {
					const auto snapshot = Animations::GetDebugSnapshot();
					DrawActiveNodesTab(snapshot);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("FaceGen")) {
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

			if (!ImGui::Begin("TF3DHud", nullptr, ImGuiWindowFlags_MenuBar)) {
				ImGui::End();
				return;
			}

			if (ImGui::BeginMenuBar()) {
				if (ImGui::BeginMenu("File")) {
					if (ImGui::MenuItem("Save")) {
						(void)SaveConfig();
					}
					if (ImGui::MenuItem("Reload")) {
						Previewer::ReloadConfig();
					}
					ImGui::EndMenu();
				}
				ImGui::EndMenuBar();
			}

			if (ImGui::BeginTabBar("tf3dhud_tabs")) {
				if (ImGui::BeginTabItem("Layout")) {
					DrawLayoutTab();
					DrawClipRectOverlay();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Light")) {
					DrawLightTab();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Debug")) {
					DrawDebugTab();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}

			ImGui::End();
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
