#include "ImguiMenu.h"

#include "Animations.h"
#include "Config.h"

#include "RE/B/BSGraphics.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
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

		REL::Relocation<std::uintptr_t> g_d3d11CreateDeviceAndSwapChainCall{ REL::ID{ 224250, 4492363 } };
		REL::Relocation<std::uintptr_t> g_clipCursor{ REL::ID{ 641385, 4823626 } };
		constexpr std::uintptr_t kD3D11CreateDeviceAndSwapChainCallOffsetOG = 0x419;
		constexpr std::uintptr_t kD3D11CreateDeviceAndSwapChainCallOffsetAE = 0x410;

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
			if (!ImGui::CollapsingHeader("Subgraphs", ImGuiTreeNodeFlags_DefaultOpen)) {
				return;
			}

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

		void DrawAnimationDebugWindow()
		{
			const auto snapshot = Animations::GetDebugSnapshot();

			ImGuiIO& io = ImGui::GetIO();
			ImGui::SetNextWindowPos(ImVec2(20.0F, 20.0F), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowSize(ImVec2((std::min)(920.0F, io.DisplaySize.x - 40.0F), 620.0F), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowBgAlpha(0.92F);

			if (!ImGui::Begin("TF3DHud Animation Debug", std::addressof(g_menuOpen))) {
				ImGui::End();
				return;
			}

			ImGui::Text("Project: %s", snapshot.project.empty() ? "(none)" : snapshot.project.c_str());
			if (!snapshot.lastDiagnostic.empty()) {
				ImGui::Text("Last diagnostic: %s", snapshot.lastDiagnostic.c_str());
			}
			ImGui::Separator();

			ImGui::Text(
				"Manager=%p Graph=%p Behavior=%p ActiveGraph=%u/%u",
				reinterpret_cast<void*>(snapshot.manager),
				reinterpret_cast<void*>(snapshot.graph),
				reinterpret_cast<void*>(snapshot.behaviorGraph),
				snapshot.activeGraphIndex,
				snapshot.graphCount);
			ImGui::Text(
				"Behavior active=%s linked=%s updateActiveNodes=%s stateOrTransitionChanged=%s activeNodes=%u shown=%zu",
				snapshot.behaviorActive ? "true" : "false",
				snapshot.behaviorLinked ? "true" : "false",
				snapshot.updateActiveNodes ? "true" : "false",
				snapshot.stateOrTransitionChanged ? "true" : "false",
				snapshot.activeNodeCount,
				snapshot.activeNodes.size());
			ImGui::Text(
				"generateHavokBones=%s ragdollInterface=%s physicsWorld=%s windowActive=%s",
				snapshot.generateHavokBones ? "true" : "false",
				snapshot.hasRagdollInterface ? "true" : "false",
				snapshot.hasPhysicsWorld ? "true" : "false",
				g_windowActive ? "true" : "false");
			ImGui::Text(
				"Live sync active=%s time=%.4f/%.4f speed=%.4f points=%u",
				snapshot.liveSync.active ? "true" : "false",
				snapshot.liveSync.currentTime,
				snapshot.liveSync.totalTime,
				snapshot.liveSync.speed,
				snapshot.liveSync.syncPointCount);
			ImGui::Text(
				"Preview sync active=%s time=%.4f/%.4f speed=%.4f points=%u",
				snapshot.previewSync.active ? "true" : "false",
				snapshot.previewSync.currentTime,
				snapshot.previewSync.totalTime,
				snapshot.previewSync.speed,
				snapshot.previewSync.syncPointCount);
			ImGui::Separator();

			DrawSubgraphState(snapshot);
			ImGui::Separator();
			DrawActiveClip(snapshot);
			ImGui::Separator();
			DrawActiveNodesTable(snapshot);

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
			REX::INFO("Animations: ImGui debug menu initialized, key=0x{:02X}", GetConfig().uiMenuKey);
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
				DrawAnimationDebugWindow();
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
			REX::INFO("Animations: Present hook installed for ImGui debug menu");
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
			auto d3d11Call = g_d3d11CreateDeviceAndSwapChainCall.address();
			d3d11Call += REX::FModule::IsRuntimeOG() ?
				kD3D11CreateDeviceAndSwapChainCallOffsetOG :
				kD3D11CreateDeviceAndSwapChainCallOffsetAE;

			REL::Relocation<std::uintptr_t> callSite{ d3d11Call };
			g_originalD3D11CreateDeviceAndSwapChain =
				reinterpret_cast<D3D11CreateDeviceAndSwapChain_t>(
					callSite.write_call<5>(reinterpret_cast<std::uintptr_t>(&HookedD3D11CreateDeviceAndSwapChain)));
			g_d3dHookInstalled = true;
			REX::INFO("Animations: D3D11CreateDeviceAndSwapChain hook installed at {:X}", d3d11Call);
		}

		if (!g_clipCursorHookInstalled) {
			g_originalClipCursor = *reinterpret_cast<ClipCursor_t*>(g_clipCursor.address());
			g_clipCursor.write_vfunc(0, &HookedClipCursor);
			g_clipCursorHookInstalled = true;
			REX::INFO("Animations: ClipCursor hook installed for ImGui debug menu");
		}
	}
}
