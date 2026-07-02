#include "Previewer.h"
#include "Animations.h"
#include "Morph.h"
#include "Renderer.h"
#include "Utils.h"

#include "BSSkin.h"
#include "Config.h"

#include "RE/B/BSFadeNode.h"
#include "RE/B/BSFlattenedBoneTree.h"
#include "RE/B/BSGeometry.h"
#include "RE/B/BGSObjectInstance.h"
#include "RE/B/BGSHeadPart.h"
#include "RE/B/BSLightingShaderProperty.h"
#include "RE/B/BSModelDB.h"
#include "RE/B/BSShaderMaterial.h"
#include "RE/B/BSShaderProperty.h"
#include "RE/B/BipedAnim.h"
#include "RE/M/MemoryManager.h"
#include "RE/N/NiCloningProcess.h"
#include "RE/N/NiExtraData.h"
#include "RE/N/NiNode.h"
#include "RE/N/NiUpdateData.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESNPC.h"
#include "RE/T/TESModel.h"
#include "RE/T/TESObjectARMA.h"
#include "RE/T/TESObjectARMO.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESRace.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace RE
{
	struct BSFaceGenExpression
	{
		float expression[54];
	};
	static_assert(sizeof(BSFaceGenExpression) == 0xD8);

	namespace FaceEmotionalIdles
	{
		struct InstanceData
		{
			std::uint32_t pad00;
			std::uint32_t handle;
			std::uint32_t pad08;
			float blinkTimer;
			float lidFollowEyes;
			std::uint64_t pad18;
			std::uint64_t pad20;
			std::uint32_t unk28;
			std::uint32_t pad2C;
			BSFixedString archeType;
		};
		static_assert(sizeof(InstanceData) == 0x38);
	}

	class BSFaceGenAnimationData :
		public NiExtraData
	{
	public:
		BSFaceGenExpression currentExpression;
		BSFaceGenExpression modifierExpression;
		BSFaceGenExpression baseExpression;
		std::array<std::byte, 0x38> emotionalIdleData;
		bool morphsDirty;
		bool forceMorphUpdate;
		std::uint8_t pad2DA;
		bool disableMorphUpdate;
		std::uint32_t morphUpdateState;
		std::uint32_t unk2E0;
		std::uint32_t pad2E4;
	};
	static_assert(sizeof(BSFaceGenAnimationData) == 0x2E8);
	static_assert(offsetof(BSFaceGenAnimationData, currentExpression) == 0x18);
	static_assert(offsetof(BSFaceGenAnimationData, modifierExpression) == 0xF0);
	static_assert(offsetof(BSFaceGenAnimationData, baseExpression) == 0x1C8);
	static_assert(offsetof(BSFaceGenAnimationData, emotionalIdleData) == 0x2A0);
	static_assert(offsetof(BSFaceGenAnimationData, morphsDirty) == 0x2D8);
	static_assert(offsetof(BSFaceGenAnimationData, forceMorphUpdate) == 0x2D9);
	static_assert(offsetof(BSFaceGenAnimationData, disableMorphUpdate) == 0x2DB);
	static_assert(offsetof(BSFaceGenAnimationData, morphUpdateState) == 0x2DC);
	static_assert(offsetof(BSFaceGenAnimationData, unk2E0) == 0x2E0);

	class BSFaceGenNiNode :
		public NiNode
	{
	public:
		std::array<std::byte, 0x30> faceGenData;
		BSFaceGenAnimationData* animationData;
		float updateTime;
		std::uint16_t faceGenFlags;
	};
	static_assert(offsetof(BSFaceGenNiNode, animationData) == 0x170);
	static_assert(offsetof(BSFaceGenNiNode, faceGenFlags) == 0x17C);
}

namespace TF3DHud
{
	namespace
	{
		constexpr std::uint64_t kNiAVObjectTopFadeNode = 0x4000;
		constexpr std::uint64_t kNiAVObjectFadeDone = 0x8000;

		using CreateHeadForNPC_t = bool(RE::TESNPC*, RE::NiPointer<RE::BSFaceGenNiNode>&, bool, bool, void*);
		using CalculateBodyTintColor_t =
			void(RE::TESNPC*, RE::NiColorA&, RE::BGSCharacterTint::Entry*, bool, bool);
		using UpdateBodyTintColorsOnScene_t = void(RE::NiAVObject*, const RE::NiColorA&);
		using ConvertNodeTree_t = RE::NiAVObject*(RE::NiAVObject*);
		using GetActorBodyPart3D_t =
			RE::NiAVObject*(RE::Actor*, RE::NiAVObject*, const std::uint32_t*, bool, bool);
		using GetNPCHeadPart_t = RE::BGSHeadPart*(RE::TESNPC*, RE::BGSHeadPart::HeadPartType);
		using GetDefaultRaceHeadPart_t =
			RE::BGSHeadPart*(const RE::TESRace*, RE::SEX, RE::BGSHeadPart::HeadPartType);
		using GetNumSegments_t = std::uint32_t(const RE::BSGeometrySegmentData*);
		using GetSubSegmentIndex_t =
			std::uint32_t(const RE::BSGeometrySegmentData*, std::uint32_t, std::uint32_t);
		using SetSegmentEnabled_t = void(RE::BSGeometrySegmentData*, std::uint32_t, std::uint32_t, bool);
		using CreateClothFor3D_t =
			RE::NiExtraData*(RE::NiAVObject&, const char*, const RE::NiTransform&, RE::NiAVObject*);
		using SetClothWorld_t = void(RE::NiExtraData*, void*);
		using SetClothSettleOnTransitionToSim_t = void(RE::NiExtraData*, bool);
		using FixFaceGenHeadSkinInstances_t = void(RE::BSFaceGenNiNode*, RE::NiAVObject*, bool);
		using ResetFaceGenCurrentMorphs_t = int(RE::BSFaceGenAnimationData*, float);

		struct BorrowedBipedPointer
		{
			RE::BipedAnim* biped{ nullptr };
		};
		static_assert(sizeof(BorrowedBipedPointer) == sizeof(void*));

		using BipedAnimCtor_t = RE::BipedAnim*(RE::BipedAnim*, RE::TESObjectREFR*, bool);
		using BipedAnimDtor_t = void(RE::BipedAnim*);
		using GetSkin_t = RE::TESObjectARMO*(RE::TESNPC*);
		using AddArmorToBiped_t =
			void(const RE::BGSObjectInstance*, RE::TESRace*, const BorrowedBipedPointer*, RE::SEX, std::uint16_t);
		using InitWornObject_t = bool(RE::TESNPC*, const BorrowedBipedPointer*, const RE::BGSObjectInstance*);
		using GetChargenModelName_t = void(char*, std::uint32_t, const char*, bool);

		struct SkinComplexionContext
		{
			RE::BIPED_OBJECT slot{ RE::BIPED_OBJECT::kNone };
			std::uint32_t pad{ 0 };
			RE::BIPOBJECT* objects{ nullptr };
			RE::ObjectRefHandle actorRef{};
			std::uint32_t pad2{ 0 };
		};
		static_assert(offsetof(SkinComplexionContext, objects) == 0x8);
		static_assert(offsetof(SkinComplexionContext, actorRef) == 0x10);
		using DoAdjustSkinComplexion_t = std::uint64_t(SkinComplexionContext*, RE::NiAVObject*);

		REL::Relocation<void (*)(RE::NiAVObject*)> g_createBoneMap{ REL::ID(1131947) };
		REL::Relocation<CreateHeadForNPC_t*> g_createHeadForNPC{ REL::ID(1455012) };
		REL::Relocation<CalculateBodyTintColor_t*> g_calculateBodyTintColor{ REL::ID(134537) };
		REL::Relocation<UpdateBodyTintColorsOnScene_t*> g_updateBodyTintColorsOnScene{ REL::ID(49935) };
		REL::Relocation<DoAdjustSkinComplexion_t*> g_doAdjustSkinComplexion{ REL::ID(1295935) };
		REL::Relocation<ConvertNodeTree_t*> g_convertNodeTree{ REL::ID(633230) };
		REL::Relocation<GetActorBodyPart3D_t*> g_getActorBodyPart3D{ REL::ID(157573) };
		REL::Relocation<GetNPCHeadPart_t*> g_getNPCHeadPart{ REL::ID(946253) };
		REL::Relocation<GetDefaultRaceHeadPart_t*> g_getDefaultRaceHeadPart{ REL::ID(1120148) };
		REL::Relocation<GetNumSegments_t*> g_getNumSegments{ REL::ID(331465) };
		REL::Relocation<GetSubSegmentIndex_t*> g_getSubSegmentIndex{ REL::ID(453731) };
		REL::Relocation<SetSegmentEnabled_t*> g_enableSegment{ REL::ID(1134184) };
		REL::Relocation<SetSegmentEnabled_t*> g_disableSegment{ REL::ID(1466142) };
		REL::Relocation<CreateClothFor3D_t*> g_createClothFor3D{ REL::ID(1322043) };
		REL::Relocation<SetClothWorld_t*> g_setClothWorld{ REL::ID(19064) };
		REL::Relocation<SetClothSettleOnTransitionToSim_t*> g_setClothSettleOnTransitionToSim{ REL::ID(638869) };
		REL::Relocation<FixFaceGenHeadSkinInstances_t*> g_fixFaceGenHeadSkinInstances{ REL::ID(1131949) };
		REL::Relocation<ResetFaceGenCurrentMorphs_t*> g_resetFaceGenCurrentMorphs{ REL::ID(1174798) };
		REL::Relocation<BipedAnimCtor_t*> g_bipedAnimCtor{ REL::ID(724121) };
		REL::Relocation<BipedAnimDtor_t*> g_bipedAnimDtor{ REL::ID(1494601) };
		REL::Relocation<GetSkin_t*> g_getSkin{ REL::ID(1042540) };
		REL::Relocation<AddArmorToBiped_t*> g_addArmorToBiped{ REL::ID(724793) };
		REL::Relocation<InitWornObject_t*> g_initWornObject{ REL::ID(1374346) };
		REL::Relocation<GetChargenModelName_t*> g_getChargenModelName{ REL::ID(1493791) };

		struct PreviewAttachment
		{
			RE::BIPED_OBJECT slot{ RE::BIPED_OBJECT::kNone };
			RE::NiPointer<RE::NiAVObject> object;
			RE::NiNode* parent{ nullptr };
		};

		RE::NiPointer<RE::NiAVObject> g_previewRoot;
		RE::BSFlattenedBoneTree* g_previewFlattenedBoneTree{ nullptr };
		RE::NiPointer<RE::BSFaceGenNiNode> g_previewFaceNode;
		std::vector<PreviewAttachment> g_previewAttachments;
		std::vector<RE::NiPointer<RE::NiAVObject>> g_retiredPreviewObjects;
		constexpr std::uint32_t kEquipmentAuditFrames = 300;
		constexpr std::uint32_t kBipedSignatureStableFrames = 3;
		std::atomic_uint32_t g_requestedEquipmentAuditFrames{ 0 };
		std::atomic_bool g_equipmentAuditActive{ false };
		std::uint32_t g_equipmentAuditFrames{ 0 };
		std::uint64_t g_pendingBipedSignature{ 0 };
		std::uint32_t g_pendingBipedSignatureFrames{ 0 };
		std::uint64_t g_deferredBipedRebuildSignature{ 0 };
		std::uint64_t g_bipedSignature{ 0 };
		std::uint64_t g_visualSignature{ 0 };
		bool g_pendingMorphGeometryRebuild{ false };
		bool g_pendingRendererAttach{ false };
		bool g_pendingFramingUpdate{ false };
		bool g_renderStateDirty{ false };
		bool g_committingRenderState{ false };
		float g_pendingRenderDelta{ 0.0F };
		bool g_looksMenuSuspended{ false };
		std::string g_lastDiagnostic;

		void HashInteger(std::uint64_t& a_hash, const std::uintptr_t a_value)
		{
			HashValue(a_hash, reinterpret_cast<void*>(a_value));
		}

		void HashFloat(std::uint64_t& a_hash, const float a_value)
		{
			std::uint32_t bits = 0;
			std::memcpy(std::addressof(bits), std::addressof(a_value), sizeof(bits));
			HashInteger(a_hash, bits);
		}

		[[nodiscard]] RE::NiAVObject* GetSourceFaceNode(RE::Actor& a_sourceActor)
		{
			if (auto* faceNode = a_sourceActor.GetFaceNodeSkinned()) {
				return reinterpret_cast<RE::NiAVObject*>(faceNode);
			}

			auto* sourceRoot = a_sourceActor.Get3D(false);
			if (!sourceRoot) {
				sourceRoot = a_sourceActor.Get3D();
			}
			return sourceRoot ? sourceRoot->GetObjectByName(RE::BSFixedString("BSFaceGenNiNodeSkinned")) : nullptr;
		}

		[[nodiscard]] RE::BSFaceGenAnimationData* GetLiveFaceAnimationData(const RE::PlayerCharacter& a_player)
		{
			auto* process = a_player.currentProcess;
			auto* middleHigh = process ? process->middleHigh : nullptr;
			return middleHigh ? middleHigh->faceAnimationData : nullptr;
		}

		void SyncPreviewFacialExpression(const RE::PlayerCharacter& a_player)
		{
			if (!g_previewFaceNode || !g_previewFaceNode->animationData) {
				return;
			}

			auto* liveData = GetLiveFaceAnimationData(a_player);
			auto* previewData = g_previewFaceNode->animationData;
			if (!liveData || liveData == previewData) {
				return;
			}

			// MiddleHigh owns emotional-idle timers and handles. Mirror only the
			// expression payload consumed by the preview head.
			previewData->currentExpression = liveData->currentExpression;
			previewData->modifierExpression = liveData->modifierExpression;
			previewData->baseExpression = liveData->baseExpression;
			previewData->morphsDirty = true;
			previewData->forceMorphUpdate = true;
			g_previewFaceNode->faceGenFlags &= static_cast<std::uint16_t>(~0x4u);
		}

		[[nodiscard]] std::uint64_t BuildVisualSignature(RE::PlayerCharacter& a_player)
		{
			std::uint64_t hash = 1469598103934665603ull;

			auto* playerBase = a_player.GetObjectReference();
			auto* npc = playerBase ? playerBase->As<RE::TESNPC>() : nullptr;
			HashValue(hash, npc);
			HashValue(hash, npc ? npc->GetRootFaceNPC() : nullptr);
			HashValue(hash, a_player.GetVisualsRace());
			HashValue(hash, a_player.charGenRace);
			HashValue(hash, a_player.complexion);
			HashValue(hash, a_player.tintingData);
			HashValue(hash, GetSourceFaceNode(a_player));
			HashInteger(hash, static_cast<std::uintptr_t>(a_player.GetSex()));

			if (npc) {
				HashValue(hash, npc->headRelatedData);
				HashValue(hash, npc->originalRace);
				HashValue(hash, npc->faceNPC);
				HashValue(hash, g_getSkin(npc));
				HashValue(hash, npc->headParts);
				HashValue(hash, npc->morphRegionSliderValues);
				HashValue(hash, npc->facialBoneRegionSliderValues);
				HashValue(hash, npc->morphSliderValues);
				HashValue(hash, npc->tintingData);
				HashInteger(hash, static_cast<std::uintptr_t>(npc->GetSex()));
				HashInteger(hash, static_cast<std::uintptr_t>(npc->numHeadParts));
				HashInteger(hash, static_cast<std::uint8_t>(npc->bodyTintColorR));
				HashInteger(hash, static_cast<std::uint8_t>(npc->bodyTintColorG));
				HashInteger(hash, static_cast<std::uint8_t>(npc->bodyTintColorB));
				HashInteger(hash, static_cast<std::uint8_t>(npc->bodyTintColorA));
				for (std::int32_t i = 0; i < npc->numHeadParts && npc->headParts; ++i) {
					HashValue(hash, npc->headParts[i]);
				}
			}

			return hash;
		}

		[[nodiscard]] std::uint64_t BuildBipedSignature(const RE::BipedAnim* a_biped)
		{
			if (!a_biped) {
				return 0;
			}

			std::uint64_t hash = 1469598103934665603ull;
			for (std::int32_t i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
				const auto& object = a_biped->object[i];
				HashValue(hash, object.parent.object);
				HashValue(hash, object.parent.instanceData.get());
				HashValue(hash, object.modExtra);
				HashValue(hash, object.armorAddon);
				HashValue(hash, object.part);
				HashValue(hash, object.skinTexture);
				HashValue(hash, object.partClone.get());
				HashValue(hash, object.objectGraphManager.get());
				HashValue(hash, object.hitEffect.get());
				HashValue(hash, reinterpret_cast<void*>(static_cast<std::uintptr_t>(object.skinned)));
			}

			auto* root = a_biped->GetRoot();
			HashValue(hash, root);
			if (root) {
				std::unordered_set<RE::NiAVObject*> resolvedSlotRoots;
				for (std::int32_t i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
					if (auto* partClone = a_biped->object[i].partClone.get()) {
						resolvedSlotRoots.insert(partClone);
					}
				}

				std::uint32_t unresolvedSkinnedGeometry = 0;
				std::uint32_t unresolvedVisibleGeometry = 0;
				ForEachGeometry(root, [&](RE::BSGeometry& a_geometry) {
					if (!a_geometry.skinInstance) {
						return;
					}

					auto* geometryObject = static_cast<RE::NiAVObject*>(std::addressof(a_geometry));
					for (auto* parent = geometryObject; parent; parent = parent->parent) {
						if (resolvedSlotRoots.contains(parent)) {
							return;
						}
					}

					++unresolvedSkinnedGeometry;
					if (!a_geometry.GetAppCulled()) {
						++unresolvedVisibleGeometry;
					}
					HashValue(hash, geometryObject);
					HashValue(hash, geometryObject->parent);
					HashValue(hash, a_geometry.skinInstance.get());
					HashInteger(hash, a_geometry.GetFlags());
					HashInteger(hash, a_geometry.GetAppCulled() ? 1 : 0);
					HashFloat(hash, a_geometry.fadeAmount);
					HashValue(hash, a_geometry.properties[0].get());
					HashValue(hash, a_geometry.properties[1].get());
				});
				HashInteger(hash, unresolvedSkinnedGeometry);
				HashInteger(hash, unresolvedVisibleGeometry);
			}
			return hash;
		}

		[[nodiscard]] bool IsHudAvailable(std::string& a_reason)
		{
			const auto ui = RE::UI::GetSingleton();
			if (!ui) {
				a_reason = "UI singleton is null";
				return false;
			}

			const auto hudMenu = ui->GetMenu<RE::HUDMenu>();
			if (!hudMenu) {
				a_reason = "HUDMenu is null";
				return false;
			}

			if (hudMenu->hudShowMenuState != RE::HUDMenu::ShowMenuState::kShown) {
				a_reason = "HUDMenu is hidden by show state " + std::to_string(std::to_underlying(hudMenu->hudShowMenuState.get()));
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

		void LogDiagnostic(std::string a_message)
		{
			if (a_message == g_lastDiagnostic) {
				return;
			}

			g_lastDiagnostic = std::move(a_message);
			REX::INFO("state: {}", g_lastDiagnostic);
		}

		void HideRendererAndResetAnimation()
		{
			Renderer::Hide();
			Animations::Reset();
		}

		void MarkRenderStateDirty(const float a_deltaTime = 0.0F)
		{
			g_renderStateDirty = true;
			g_pendingRenderDelta += a_deltaTime;
		}

		[[nodiscard]] bool ShouldShow(const RE::PlayerCharacter* a_player, std::string& a_reason)
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
			if (ui->menuMode != 0 || itemMenuMode != 0 || ui->freezeFrameMenuBG != 0 || ui->freezeFramePause != 0) {
				a_reason =
					"blocked by menu/freeze-frame state menuMode=" + std::to_string(ui->menuMode) +
					", itemMenuMode=" + std::to_string(itemMenuMode) +
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

		[[nodiscard]] bool IsPreviewSourceReady(RE::PlayerCharacter& a_player, const RE::BipedAnim& a_biped, std::string& a_reason)
		{
			if (!a_biped.GetRoot()) {
				a_reason = "third-person biped root is null";
				return false;
			}

			if (!GetSourceFaceNode(a_player)) {
				a_reason = "player face node is not initialized";
				return false;
			}

			return true;
		}

		[[nodiscard]] bool HasPendingBipedModelHandles(const RE::BipedAnim& a_biped, std::int32_t& a_pendingSlot)
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
					buffered.handleList.head ||
					buffered.objectGraphManager ||
					buffered.hitEffect) {
					a_pendingSlot = i;
					return true;
				}
			}

			a_pendingSlot = -1;
			return false;
		}

		RE::NiNode* FindPreviewAttachParent(
			RE::NiAVObject& a_sourceObject,
			RE::NiAVObject& a_previewRoot,
			const std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			for (auto* sourceParent = a_sourceObject.parent; sourceParent; sourceParent = sourceParent->parent) {
				auto* previewObject = FindNodeByName(a_previewNodes, sourceParent->GetName());
				auto* previewNode = previewObject ? previewObject->IsNode() : nullptr;
				if (previewNode) {
					return previewNode;
				}
			}

			return a_previewRoot.IsNode();
		}

		void CollectSkinInstances(RE::NiAVObject* a_root, std::unordered_set<RE::BSSkin::Instance*>& a_skins)
		{
			ForEachGeometry(a_root, [&](RE::BSGeometry& a_geometry) {
				if (a_geometry.skinInstance) {
					a_skins.insert(a_geometry.skinInstance.get());
				}
			});
		}

		[[nodiscard]] bool IsHairBipedSlot(const RE::BIPED_OBJECT a_slot)
		{
			return a_slot == RE::BIPED_OBJECT::kHairTop ||
			       a_slot == RE::BIPED_OBJECT::kHairLong ||
			       a_slot == RE::BIPED_OBJECT::kScalp;
		}

		[[nodiscard]] void* GetHclClothWorld(RE::TESObjectREFR& a_reference)
		{
			auto* cell = a_reference.GetParentCell();
			auto* havokWorld = cell ? cell->GetbhkWorld() : nullptr;
			if (!havokWorld) {
				return nullptr;
			}

			// IDA: BipedAnim::AttachSkinnedObject and BSFaceGenUtils::AttachHeadHelper
			// pass TESObjectCELL::GetbhkWorld()+0x148 to BSClothExtraData::SetWorld.
			return *reinterpret_cast<void**>(reinterpret_cast<std::byte*>(havokWorld) + 0x148);
		}

		template <class Visitor>
		void ForEachHairHeadPart(RE::BGSHeadPart* a_headPart, Visitor&& a_visitor)
		{
			if (!a_headPart) {
				return;
			}

			if (*a_headPart->type == RE::BGSHeadPart::HeadPartType::kHair) {
				a_visitor(*a_headPart);
			}

			for (auto* extraPart : a_headPart->extraParts) {
				ForEachHairHeadPart(extraPart, a_visitor);
			}
		}

		bool InitializePreviewHeadPartCloth(
			RE::TESObjectREFR& a_reference,
			RE::NiAVObject& a_faceNode,
			RE::NiAVObject& a_previewRoot,
			RE::BGSHeadPart& a_headPart)
		{
			if (*a_headPart.type != RE::BGSHeadPart::HeadPartType::kHair || a_headPart.formEditorID.empty()) {
				return false;
			}

			auto* object = a_faceNode.GetObjectByName(a_headPart.formEditorID);
			if (!object) {
				return false;
			}

			const RE::BSFixedString clothExtraName{ "CED" };
			auto* assetClothExtra = object->GetExtraData(clothExtraName);
			if (!assetClothExtra) {
				return false;
			}

			auto* runtimeClothExtra = g_createClothFor3D(
				*object,
				a_headPart.GetModel(),
				a_previewRoot.GetWorldTransform(),
				std::addressof(a_previewRoot));
			if (!runtimeClothExtra) {
				REX::WARN(
					"headpart cloth init failed: headPart={:08X}, object={:X}, name='{}', assetClothExtra={:X}",
					a_headPart.GetFormID(),
					reinterpret_cast<std::uintptr_t>(object),
					object->GetName(),
					reinterpret_cast<std::uintptr_t>(assetClothExtra));
				return false;
			}

			g_setClothSettleOnTransitionToSim(runtimeClothExtra, true);
			auto* clothWorld = GetHclClothWorld(a_reference);
			if (clothWorld) {
				g_setClothWorld(runtimeClothExtra, clothWorld);
			}

			return true;
		}

		std::uint32_t InitializePreviewCloth(
			RE::TESObjectREFR& a_reference,
			RE::NiAVObject& a_object,
			RE::NiAVObject& a_previewRoot,
			const char* a_modelPath)
		{
			if (!a_modelPath || a_modelPath[0] == '\0') {
				return 0;
			}

			const RE::BSFixedString clothExtraName{ "CED" };
			const auto previewRootTransform = a_previewRoot.GetWorldTransform();
			auto* clothWorld = GetHclClothWorld(a_reference);
			std::uint32_t initialized = 0;

			ForEachAVObject(std::addressof(a_object), [&](RE::NiAVObject& a_candidate) {
				if (!a_candidate.GetExtraData(clothExtraName)) {
					return;
				}

				auto* runtimeClothExtra = g_createClothFor3D(
					a_candidate,
					a_modelPath,
					previewRootTransform,
					std::addressof(a_previewRoot));
				if (!runtimeClothExtra) {
					REX::WARN(
						"preview cloth init failed: object={:X}, name='{}', model='{}'",
						reinterpret_cast<std::uintptr_t>(std::addressof(a_candidate)),
						a_candidate.GetName(),
						a_modelPath);
					return;
				}

				g_setClothSettleOnTransitionToSim(runtimeClothExtra, true);
				if (clothWorld) {
					g_setClothWorld(runtimeClothExtra, clothWorld);
				}
				++initialized;
			});

			return initialized;
		}

		void InitializePreviewHeadPartCloth(
			RE::TESNPC& a_npc,
			RE::TESObjectREFR& a_reference,
			RE::NiAVObject& a_faceNode,
			RE::NiAVObject& a_previewRoot)
		{
			if (!a_npc.headParts || a_npc.numHeadParts <= 0) {
				return;
			}

			for (std::int32_t index = 0; index < a_npc.numHeadParts; ++index) {
				ForEachHairHeadPart(a_npc.headParts[index], [&](RE::BGSHeadPart& a_headPart) {
					InitializePreviewHeadPartCloth(a_reference, a_faceNode, a_previewRoot, a_headPart);
				});
			}
		}

		[[nodiscard]] std::uint32_t BuildEquippedBipedMask(const RE::BipedAnim& a_biped)
		{
			std::uint32_t mask = 0;
			for (std::int32_t i = 0; i < 32; ++i) {
				const auto& object = a_biped.object[i];
				if (auto* form = object.parent.object) {
					const auto filledSlots = form->GetFilledSlots();
					if (filledSlots != static_cast<std::uint32_t>(-1)) {
						mask |= filledSlots;
						continue;
					}
				}

				if (object.partClone) {
					const auto fallbackSlot = 1u << static_cast<std::uint32_t>(i);
					mask |= fallbackSlot;
				}
			}
			return mask;
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

		struct HeadPartVisibilityStats
		{
			std::uint32_t segmentEdits{ 0 };
			std::uint32_t objectCulls{ 0 };
		};

		[[nodiscard]] bool ApplyBipedSlotSegmentVisibility(
			RE::NiAVObject& a_object,
			const RE::BIPED_OBJECT a_slot,
			const bool a_hide,
			HeadPartVisibilityStats& a_stats)
		{
			auto* geometry = a_object.IsGeometry();
			bool editedSegment = false;
			RE::BSGeometrySegmentData* segmentData = nullptr;
			std::uint32_t segmentCount = 0;
			const auto subsegmentID = static_cast<std::uint32_t>(std::to_underlying(a_slot) + 30);
			if (geometry) {
				segmentData = geometry->GetSegmentData();
				if (segmentData) {
					segmentCount = g_getNumSegments(segmentData);
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
			}

			return editedSegment;
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

		void ApplyFaceGenBipedSegmentVisibility(
			RE::NiAVObject& a_faceNode,
			const std::uint32_t a_equippedMask,
			HeadPartVisibilityStats& a_stats)
		{
			ForEachAVObject(std::addressof(a_faceNode), [&](RE::NiAVObject& a_object) {
				for (std::int32_t slot = 0; slot < std::to_underlying(RE::BIPED_OBJECT::kEditorTotal); ++slot) {
					const auto bipedSlot = static_cast<RE::BIPED_OBJECT>(slot);
					const bool hide = IsBipedSlotMasked(a_equippedMask, bipedSlot);
					(void)ApplyBipedSlotSegmentVisibility(a_object, bipedSlot, hide, a_stats);
				}
			});
		}

		void ApplyFaceGenHeadObjectVisibility(
			RE::NiAVObject& a_faceNode,
			const RE::TESRace& a_race,
			const std::uint32_t a_equippedMask,
			HeadPartVisibilityStats& a_stats)
		{
			auto* headObject = a_faceNode.GetObjectByName(RE::BSFixedString("RaceHeadSkinned"));
			if (!headObject) {
				return;
			}

			headObject->SetAppCulled(IsBipedSlotMasked(a_equippedMask, a_race.data.headObject));
			++a_stats.objectCulls;
		}

		void ApplyHeadPartBipedVisibility(
			RE::TESNPC& a_npc,
			RE::NiAVObject& a_faceNode,
			const RE::BipedAnim& a_sourceBiped)
		{
			auto* race = a_npc.originalRace;
			if (!race) {
				return;
			}

			const auto equippedMask = BuildEquippedBipedMask(a_sourceBiped);
			auto* hairPart = GetDisplayedHeadPart(a_npc, *race, RE::BGSHeadPart::HeadPartType::kHair);
			auto* facialHairPart = GetDisplayedHeadPart(a_npc, *race, RE::BGSHeadPart::HeadPartType::kFacialHair);

			const auto hairSlot = race->data.hairObject;
			const auto hairLongSlot = AddBipedSlot(hairSlot, 1);
			const auto beardSlot = race->data.beardObject;
			const bool hideHairTop = IsBipedSlotMasked(equippedMask, hairSlot);
			const bool hideHairLong = IsBipedSlotMasked(equippedMask, hairLongSlot);
			const bool hideFacialHair = IsBipedSlotMasked(equippedMask, beardSlot);

			HeadPartVisibilityStats stats;
			ApplyFaceGenBipedSegmentVisibility(a_faceNode, equippedMask, stats);
			ApplyFaceGenHeadObjectVisibility(a_faceNode, *race, equippedMask, stats);
			ApplyHeadPartVisibilityRecursive(a_faceNode, hairPart, hairSlot, hideHairTop, stats);
			ApplyHeadPartVisibilityRecursive(a_faceNode, hairPart, hairLongSlot, hideHairLong, stats);
			ApplyHeadPartVisibilityRecursive(a_faceNode, facialHairPart, beardSlot, hideFacialHair, stats);
		}

		void CollectSourceBipedSkinInstances(
			const RE::BipedAnim& a_biped,
			std::unordered_set<RE::BSSkin::Instance*>& a_skins)
		{
			for (std::int32_t i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
				CollectSkinInstances(a_biped.object[i].partClone.get(), a_skins);
			}
		}

		class ScopedSourceControllerDetach
		{
		public:
			explicit ScopedSourceControllerDetach(RE::NiAVObject& a_root)
			{
				ForEachAVObject(std::addressof(a_root), [&](RE::NiAVObject& a_object) {
					if (!a_object.controllers) {
						return;
					}

					detached_.push_back({
						.object = std::addressof(a_object),
						.controllers = a_object.controllers,
					});
					a_object.controllers.reset();
				});
			}

			~ScopedSourceControllerDetach()
			{
				for (auto& detached : detached_) {
					if (detached.object) {
						detached.object->controllers = detached.controllers;
					}
				}
			}

			ScopedSourceControllerDetach(const ScopedSourceControllerDetach&) = delete;
			ScopedSourceControllerDetach(ScopedSourceControllerDetach&&) = delete;
			ScopedSourceControllerDetach& operator=(const ScopedSourceControllerDetach&) = delete;
			ScopedSourceControllerDetach& operator=(ScopedSourceControllerDetach&&) = delete;

		private:
			struct DetachedController
			{
				RE::NiAVObject* object{ nullptr };
				RE::NiPointer<RE::NiTimeController> controllers;
			};

			std::vector<DetachedController> detached_;
		};

		void SeedSkinCloneMappings(
			RE::NiCloningProcess& a_cloneProcess,
			RE::NiAVObject& a_source,
			RE::NiAVObject* a_previewRoot,
			const std::unordered_map<std::string, RE::NiAVObject*>* a_previewNodes)
		{
			if (!a_previewRoot || !a_previewNodes || a_previewNodes->empty()) {
				return;
			}

			ForEachGeometry(std::addressof(a_source), [&](RE::BSGeometry& a_geometry) {
				auto* skin = a_geometry.skinInstance.get();
				if (!skin || skin->bones.size() > RE::BSSkin::kMaxExpectedBones) {
					return;
				}

				if (skin->rootNode) {
					a_cloneProcess.cloneMap.emplace(skin->rootNode, a_previewRoot);
				}

				for (std::uint32_t index = 0; index < skin->bones.size(); ++index) {
					auto* sourceBone = skin->bones[index];
					if (!sourceBone) {
						continue;
					}

					auto* previewBone = FindNodeByName(*a_previewNodes, sourceBone->GetName());
					if (!previewBone) {
						continue;
					}

					a_cloneProcess.cloneMap.emplace(sourceBone, previewBone);
				}
			});
		}

		RE::NiPointer<RE::NiAVObject> ClonePreviewObject(
			RE::NiAVObject& a_source,
			RE::NiAVObject* a_previewRoot = nullptr,
			const std::unordered_map<std::string, RE::NiAVObject*>* a_previewNodes = nullptr)
		{
			RE::NiCloningProcess cloneProcess;
			cloneProcess.appendChar = '$';
			cloneProcess.copyType = RE::NiCloningProcess::CopyType::kCopyExact;
			cloneProcess.scale = { 1.0F, 1.0F, 1.0F };

			SeedSkinCloneMappings(cloneProcess, a_source, a_previewRoot, a_previewNodes);

			ScopedSourceControllerDetach detachControllers(a_source);
			auto* clone = a_source.CreateClone(cloneProcess);
			a_source.ProcessClone(cloneProcess);
			auto* clonedObject = clone ? static_cast<RE::NiAVObject*>(clone) : nullptr;
			return clonedObject;
		}

		[[nodiscard]] const char* GetRaceSkeletonModelPath(RE::TESRace& a_race, const std::uint32_t a_sex)
		{
			const auto sex = std::min<std::uint32_t>(a_sex, 1);
			if (const auto* model = a_race.skeletonModel[sex].GetModel(); model && model[0] != '\0') {
				return model;
			}
			if (const auto* model = a_race.skeletonModel[0].GetModel(); model && model[0] != '\0') {
				return model;
			}
			if (const auto* model = a_race.skeletonModel[1].GetModel(); model && model[0] != '\0') {
				return model;
			}
			return nullptr;
		}

		struct FaceBoneModelPath
		{
			std::string source;
			std::string path;
		};

		void AddFaceBoneModelPath(std::vector<FaceBoneModelPath>& a_paths, std::string a_source, const char* a_path)
		{
			if (!a_path || a_path[0] == '\0') {
				return;
			}

			for (const auto& path : a_paths) {
				if (path.path == a_path) {
					return;
				}
			}

			a_paths.push_back({
				.source = std::move(a_source),
				.path = a_path,
			});
		}

		[[nodiscard]] std::vector<FaceBoneModelPath> GetRaceSkeletonFaceBoneModelPaths(
			RE::TESRace& a_race,
			const std::uint32_t a_sex)
		{
			std::vector<FaceBoneModelPath> paths;
			const auto sex = std::min<std::uint32_t>(a_sex, 1);
			AddFaceBoneModelPath(paths, "race skeletonChargenModel[sex]", a_race.skeletonChargenModel[sex].GetModel());
			AddFaceBoneModelPath(paths, "race skeletonChargenModel[0]", a_race.skeletonChargenModel[0].GetModel());
			AddFaceBoneModelPath(paths, "race skeletonChargenModel[1]", a_race.skeletonChargenModel[1].GetModel());

			if (const auto* skeletonPath = GetRaceSkeletonModelPath(a_race, sex)) {
				std::array<char, 260> chargenPath{};
				g_getChargenModelName(chargenPath.data(), static_cast<std::uint32_t>(chargenPath.size()), skeletonPath, false);
				AddFaceBoneModelPath(paths, "race skeleton derived facebones", chargenPath.data());

				if (sex == std::to_underlying(RE::SEX::kFemale)) {
					chargenPath.fill('\0');
					g_getChargenModelName(chargenPath.data(), static_cast<std::uint32_t>(chargenPath.size()), skeletonPath, true);
					AddFaceBoneModelPath(paths, "race skeleton derived female facebones", chargenPath.data());
				}
			}

			return paths;
		}

		[[nodiscard]] bool ConvertPreviewSkeletonToFlattenedTree(RE::PlayerCharacter& a_player, RE::NiAVObject& a_skeletonRoot)
		{
			constexpr std::uint32_t kFlattenedSkeletonBodyPart = 0x12;
			auto* convertTarget = g_getActorBodyPart3D(
				std::addressof(a_player),
				std::addressof(a_skeletonRoot),
				std::addressof(kFlattenedSkeletonBodyPart),
				false,
				false);
			if (!convertTarget) {
				REX::WARN(
					"preview race skeleton flatten failed: body-part lookup returned null; root={:X}, rootName='{}', bodyPart={}",
					reinterpret_cast<std::uintptr_t>(std::addressof(a_skeletonRoot)),
					a_skeletonRoot.GetName(),
					kFlattenedSkeletonBodyPart);
				return false;
			}

			if (convertTarget != std::addressof(a_skeletonRoot)) {
				g_convertNodeTree(convertTarget);
			}

			g_createBoneMap(std::addressof(a_skeletonRoot));

			auto* afterFlattened = FindFlattenedBoneTree(std::addressof(a_skeletonRoot));
			if (!afterFlattened) {
				REX::WARN(
					"preview race skeleton flatten did not produce BSFlattenedBoneTree: target={:X}, root={:X}, targetName='{}', rootName='{}'",
					reinterpret_cast<std::uintptr_t>(convertTarget),
					reinterpret_cast<std::uintptr_t>(std::addressof(a_skeletonRoot)),
					convertTarget->GetName(),
					a_skeletonRoot.GetName());
				return false;
			}

			g_createBoneMap(afterFlattened);
			g_previewFlattenedBoneTree = afterFlattened;

			return true;
		}

		[[nodiscard]] RE::BSFlattenedBoneTree::FlattenedBone* FindFlattenedBoneByName(
			RE::BSFlattenedBoneTree& a_tree,
			const std::string_view a_name)
		{
			if (!a_tree.bone || a_tree.boneCount <= 0) {
				return nullptr;
			}

			for (std::int32_t index = 0; index < a_tree.boneCount; ++index) {
				auto& bone = a_tree.bone[index];
				const auto* name = bone.name.c_str();
				if (name && a_name == name) {
					return std::addressof(bone);
				}
			}
			return nullptr;
		}

		[[nodiscard]] bool TryGetPreviewHeadWorld(RE::NiAVObject& a_previewRoot, RE::NiPoint3& a_out)
		{
			auto* flattened = g_previewFlattenedBoneTree;
			if (!flattened) {
				flattened = FindFlattenedBoneTree(std::addressof(a_previewRoot));
				g_previewFlattenedBoneTree = flattened;
			}
			if (!flattened) {
				return false;
			}

			if (auto* headObject = flattened->GetObjectByName(RE::BSFixedString("HEAD"))) {
				a_out = headObject->world.translate;
				return true;
			}

			auto* head = FindFlattenedBoneByName(*flattened, "HEAD");
			if (!head) {
				return false;
			}

			a_out = head->node ? head->node->world.translate : head->world.translate;
			return true;
		}

		bool ApplyHeadCenteredFraming(RE::NiAVObject& a_previewRoot)
		{
			auto* flattened = g_previewFlattenedBoneTree;
			if (!flattened) {
				flattened = FindFlattenedBoneTree(std::addressof(a_previewRoot));
				g_previewFlattenedBoneTree = flattened;
			}

			if (!flattened) {
				Renderer::ApplyOffscreenFraming(a_previewRoot);
				REX::WARN("head-centered framing fell back to bounds: no cached BSFlattenedBoneTree");
				return false;
			}

			auto* headObject = flattened->GetObjectByName(RE::BSFixedString("HEAD"));
			auto* head = headObject ? nullptr : FindFlattenedBoneByName(*flattened, "HEAD");
			if (!headObject && !head) {
				Renderer::ApplyOffscreenFraming(a_previewRoot);
				REX::WARN(
					"head-centered framing fell back to bounds: HEAD bone not found in flattenedTree='{}', bones={}",
					flattened->GetName(),
					flattened->boneCount);
				return false;
			}

			const auto& config = GetConfig();
			RE::NiTransform transform = RE::NiTransform::IDENTITY;
			transform.scale = config.modelScale;
			transform.rotate.FromEulerAnglesXYZ(0.0F, 0.0F, config.yawDegrees * 3.14159265358979323846F / 180.0F);
			a_previewRoot.SetLocalTransform(transform);

			RE::NiUpdateData updateData;
			a_previewRoot.Update(updateData);

			const auto headWorld = headObject ? headObject->world.translate : (head->node ? head->node->world.translate : head->world.translate);
			auto centeredTransform = a_previewRoot.GetLocalTransform();
			centeredTransform.translate = {
				-headWorld.x,
				-headWorld.y + config.cameraDistance,
				-headWorld.z
			};
			a_previewRoot.SetLocalTransform(centeredTransform);
			a_previewRoot.Update(updateData);

			return true;
		}

		void ApplyHeadFollowTranslation(RE::NiAVObject& a_previewRoot)
		{
			RE::NiPoint3 headWorld;
			if (!TryGetPreviewHeadWorld(a_previewRoot, headWorld)) {
				return;
			}

			const auto& config = GetConfig();
			auto transform = a_previewRoot.GetLocalTransform();
			transform.translate.x += -headWorld.x;
			transform.translate.y += config.cameraDistance - headWorld.y;
			transform.translate.z += -headWorld.z;
			a_previewRoot.SetLocalTransform(transform);

			RE::NiUpdateData updateData;
			a_previewRoot.Update(updateData);
		}

		RE::NiPointer<RE::NiAVObject> LoadPreviewRaceSkeleton(RE::PlayerCharacter& a_player)
		{
			auto* race = a_player.GetVisualsRace();
			if (!race) {
				if (auto* playerBase = a_player.GetObjectReference()) {
					if (auto* npc = playerBase->As<RE::TESNPC>()) {
						race = npc->GetFormRace();
					}
				}
			}
			if (!race) {
				LogDiagnostic("preview race skeleton load failed: player race is null");
				return nullptr;
			}

			const auto sex = static_cast<std::uint32_t>(a_player.GetSex());
			const auto* skeletonPath = GetRaceSkeletonModelPath(*race, sex);
			if (!skeletonPath) {
				LogDiagnostic("preview race skeleton load failed: race skeleton path is empty");
				return nullptr;
			}

			RE::NiPointer<RE::NiNode> loadedRoot;
			RE::BSModelDB::DBTraits::ArgsType args{};
			args.loadLevel = 3;
			args.prepareAfterLoad = true;
			args.performProcess = true;
			args.createFadeNode = true;
			args.loadTextures = true;
			const auto result = RE::BSModelDB::Demand(skeletonPath, std::addressof(loadedRoot), args);
			if (result != RE::BSResource::ErrorCode::kNone || !loadedRoot) {
				REX::WARN(
					"preview race skeleton load failed: path='{}', result={}",
					skeletonPath,
					std::to_underlying(result));
				LogDiagnostic("preview race skeleton load failed: BSModelDB demand failed");
				return nullptr;
			}

			auto previewRoot = ClonePreviewObject(*loadedRoot);
			if (!previewRoot) {
				REX::WARN("preview race skeleton clone failed: path='{}'", skeletonPath);
				LogDiagnostic("preview race skeleton clone failed");
				return nullptr;
			}

			if (!ConvertPreviewSkeletonToFlattenedTree(a_player, *previewRoot)) {
				LogDiagnostic("preview race skeleton flatten failed");
				return nullptr;
			}
			return previewRoot;
		}

		[[nodiscard]] RE::NiPointer<RE::NiAVObject> LoadPreviewModel(const char* a_path)
		{
			if (!a_path || a_path[0] == '\0') {
				return nullptr;
			}

			RE::NiPointer<RE::NiNode> loadedRoot;
			RE::BSModelDB::DBTraits::ArgsType args{};
			args.loadLevel = 3;
			args.prepareAfterLoad = true;
			args.performProcess = true;
			args.createFadeNode = true;
			args.loadTextures = true;
			const auto result = RE::BSModelDB::Demand(a_path, std::addressof(loadedRoot), args);
			if (result != RE::BSResource::ErrorCode::kNone || !loadedRoot) {
				REX::WARN(
					"preview model load failed: path='{}', result={}",
					a_path,
					std::to_underlying(result));
				return nullptr;
			}

			auto previewRoot = ClonePreviewObject(*loadedRoot);
			if (!previewRoot) {
				REX::WARN("preview model clone failed: path='{}'", a_path);
				return nullptr;
			}

			return previewRoot;
		}


		void RepairShaderFadeNodes(RE::NiAVObject& a_object, RE::BSFadeNode* a_currentFadeNode);

		void MakePreviewFadeNodeVisible(RE::BSFadeNode& a_fadeNode)
		{
			// IDA: BSLightingShaderProperty::GetRenderPasses clears deferred passes
			// when fadeNode lacks 0x8000 and currentFade <= 0.0. BSFadeNode::OnVisible
			// also fast-paths visible nodes when 0x8000, NiAVObject fade, and currentFade
			// are all set.
			a_fadeNode.flags.flags |= kNiAVObjectFadeDone;
			a_fadeNode.fadeAmount = 1.0F;
			a_fadeNode.currentFade = 1.0F;
			a_fadeNode.currentDecalFade = 1.0F;
			a_fadeNode.previousMaxA = 1.0F;
		}

		void PrepareForInterface3DOffscreen(RE::NiAVObject& a_root)
		{
			// The preview tree is assembled from separately loaded/cloned subtrees.
			// Repair shader fade-node ownership once after final attachment so no
			// geometry keeps a stale fade node from the source or attachment root.
			RepairShaderFadeNodes(a_root, nullptr);

			bool updated = false;
			ForEachAVObject(std::addressof(a_root), [&](RE::NiAVObject& a_object) {
				auto* fadeNode = a_object.IsFadeNode();
				if (!fadeNode) {
					return;
				}

				if ((fadeNode->flags.flags & kNiAVObjectFadeDone) == 0 ||
					fadeNode->fadeAmount != 1.0F ||
					fadeNode->currentFade != 1.0F ||
					fadeNode->currentDecalFade != 1.0F) {
					updated = true;
				}
				MakePreviewFadeNodeVisible(*fadeNode);

				if ((a_object.flags.flags & kNiAVObjectTopFadeNode) != 0) {
					a_object.flags.flags &= ~kNiAVObjectTopFadeNode;
					updated = true;
				}
			});

			if (updated) {
				RE::NiUpdateData updateData;
				a_root.Update(updateData);
			}
		}

		void RepairShaderFadeNodes(RE::NiAVObject& a_object, RE::BSFadeNode* a_currentFadeNode)
		{
			if (auto* fadeNode = a_object.IsFadeNode()) {
				a_currentFadeNode = fadeNode;
			}

			if (a_currentFadeNode) {
				if (auto* geometry = a_object.IsGeometry()) {
					for (auto& property : geometry->properties) {
						auto* shaderProperty = netimmerse_cast<RE::BSShaderProperty*>(property.get());
						if (shaderProperty) {
							shaderProperty->fadeNode = a_currentFadeNode;
						}
					}
				}
			}

			auto* node = a_object.IsNode();
			if (!node) {
				return;
			}

			for (auto& child : node->children) {
				if (child) {
					RepairShaderFadeNodes(*child, a_currentFadeNode);
				}
			}
		}

		void PreparePreviewTree(RE::NiAVObject& a_previewRoot)
		{
			ForEachAVObject(std::addressof(a_previewRoot), [&](RE::NiAVObject& a_object) {
				if (a_object.GetAppCulled()) {
					a_object.SetAppCulled(false);
				}
				a_object.fadeAmount = 1.0F;
				if (auto* fadeNode = a_object.IsFadeNode()) {
					MakePreviewFadeNodeVisible(*fadeNode);
				}
			});

			RepairShaderFadeNodes(a_previewRoot, nullptr);
		}

		std::uint32_t StripControllerChains(RE::NiAVObject& a_root)
		{
			std::uint32_t strippedControllers = 0;
			ForEachAVObject(std::addressof(a_root), [&](RE::NiAVObject& a_object) {
				if (a_object.controllers) {
					a_object.controllers.reset();
					++strippedControllers;
				}
			});

			return strippedControllers;
		}

		[[nodiscard]] RE::BSShaderProperty* GetGeometryShaderProperty(RE::BSGeometry& a_geometry)
		{
			auto* shaderProperty0 = netimmerse_cast<RE::BSShaderProperty*>(a_geometry.properties[0].get());
			auto* shaderProperty1 = netimmerse_cast<RE::BSShaderProperty*>(a_geometry.properties[1].get());
			return shaderProperty1 ? shaderProperty1 : shaderProperty0;
		}

		[[nodiscard]] bool IsPreviewNeckGoreGeometry(const std::string_view a_name)
		{
			return a_name == "FemaleNeckGore" || a_name == "MaleNeckGore" || a_name == "NeckGore";
		}

		void SanitizePreviewRenderTree(RE::NiAVObject& a_previewRoot)
		{
			bool updated = false;
			ForEachGeometry(std::addressof(a_previewRoot), [&](RE::BSGeometry& a_geometry) {
				const std::string_view geometryName(a_geometry.GetName());
				const auto* shaderProperty = GetGeometryShaderProperty(a_geometry);
				const bool missingShaderPath = !shaderProperty || !shaderProperty->material;
				const bool neckGore = IsPreviewNeckGoreGeometry(geometryName);

				if (missingShaderPath) {
					a_geometry.SetAppCulled(true);
					a_geometry.fadeAmount = 0.0F;
					updated = true;
				}
				if (neckGore) {
					a_geometry.SetAppCulled(true);
					a_geometry.fadeAmount = 0.0F;
					updated = true;
				}
			});

			if (!updated) {
				return;
			}

			RE::NiUpdateData updateData;
			a_previewRoot.Update(updateData);
		}

		void RestorePreviewShaderAlpha(RE::NiAVObject& a_previewRoot)
		{
			ForEachGeometry(std::addressof(a_previewRoot), [&](RE::BSGeometry& a_geometry) {
				for (auto& property : a_geometry.properties) {
					auto* lightingProperty = netimmerse_cast<RE::BSLightingShaderProperty*>(property.get());
					if (!lightingProperty) {
						continue;
					}

					if (lightingProperty->alpha <= 0.0F) {
						lightingProperty->alpha = 1.0F;
					}
				}
			});
		}

		void StripClonedGeometry(RE::NiAVObject& a_previewRoot)
		{
			std::vector<std::pair<RE::NiNode*, RE::NiPointer<RE::NiAVObject>>> detachedObjects;

			ForEachAVObject(std::addressof(a_previewRoot), [&](RE::NiAVObject& a_object) {
				if (!a_object.IsGeometry()) {
					return;
				}

				auto* parent = a_object.parent;
				if (!parent) {
					return;
				}

				detachedObjects.emplace_back(parent, RE::NiPointer<RE::NiAVObject>(std::addressof(a_object)));
			});

			for (auto& [parent, object] : detachedObjects) {
				if (parent && object) {
					parent->DetachChild(object.get());
					g_retiredPreviewObjects.emplace_back(std::move(object));
				}
			}

		}

		void PrepareAttachmentSkinComplexion(
			RE::NiAVObject& a_attachmentRoot,
			const RE::BIPED_OBJECT a_slot,
			const RE::BipedAnim& a_sourceBiped)
		{
			if (a_slot == RE::BIPED_OBJECT::kNone) {
				return;
			}

			SkinComplexionContext context{
				.slot = a_slot,
				.objects = const_cast<RE::BIPOBJECT*>(a_sourceBiped.object),
				.actorRef = a_sourceBiped.GetRequester(),
			};

			ForEachGeometry(std::addressof(a_attachmentRoot), [&](RE::BSGeometry& a_geometry) {
				g_doAdjustSkinComplexion(std::addressof(context), std::addressof(a_geometry));
			});
		}

		[[nodiscard]] RE::BGSModelMaterialSwap* SelectArmorAddonFacebonesModel(RE::TESObjectARMA& a_addon, const RE::SEX a_sex)
		{
			const auto sex = a_sex == RE::SEX::kFemale ? 1u : 0u;
			auto* model = std::addressof(a_addon.bipedModelFacebones[sex]);
			if (model->GetModel() && model->GetModel()[0] != '\0') {
				return model;
			}

			model = std::addressof(a_addon.bipedModelFacebones[0]);
			return model->GetModel() && model->GetModel()[0] != '\0' ? model : nullptr;
		}

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

		[[nodiscard]] FaceBoneCandidateDetail MakeFaceBoneCandidateDetail(
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

		void LogFaceBoneAttachFailure(
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

		[[nodiscard]] RE::TESRace* ResolvePreviewRace(RE::PlayerCharacter& a_player, RE::TESNPC& a_npc)
		{
			if (auto* race = a_player.GetVisualsRace()) {
				return race;
			}
			if (auto* race = a_npc.GetFormRace()) {
				return race;
			}
			return a_npc.originalRace;
		}

		class PreviewBipedData
		{
		public:
			PreviewBipedData(RE::TESObjectREFR& a_reference, RE::NiNode& a_actorRoot)
			{
				biped_ = RE::malloc<RE::BipedAnim>();
				if (!biped_) {
					return;
				}

				g_bipedAnimCtor(biped_, std::addressof(a_reference), false);
				// This is a borrowed native smart-pointer payload for population
				// helpers only. Keep a non-zero refcount so any transient native
				// smart-pointer copies cannot delete this preview-owned object.
				biped_->refCount = 1;
				biped_->root = std::addressof(a_actorRoot);
				borrowed_.biped = biped_;
			}

			~PreviewBipedData()
			{
				if (!biped_) {
					return;
				}

				g_bipedAnimDtor(biped_);
				RE::free(biped_);
			}

			PreviewBipedData(const PreviewBipedData&) = delete;
			PreviewBipedData(PreviewBipedData&&) = delete;
			PreviewBipedData& operator=(const PreviewBipedData&) = delete;
			PreviewBipedData& operator=(PreviewBipedData&&) = delete;

			[[nodiscard]] explicit operator bool() const { return biped_ != nullptr; }
			[[nodiscard]] RE::BipedAnim& Get() const { return *biped_; }
			[[nodiscard]] const BorrowedBipedPointer* Borrowed() const { return std::addressof(borrowed_); }

		private:
			RE::BipedAnim* biped_{ nullptr };
			BorrowedBipedPointer borrowed_;
		};

		struct PreviewBipedPopulationStats
		{
			bool skinAdded{ false };
			std::uint32_t wornObjects{ 0 };
			std::uint32_t objectAddons{ 0 };
			std::uint32_t bufferedAddons{ 0 };
		};

		[[nodiscard]] std::uint32_t CountBipedArmorAddons(const RE::BIPOBJECT* a_objects)
		{
			std::uint32_t count = 0;
			for (std::int32_t i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
				if (a_objects[i].armorAddon) {
					++count;
				}
			}
			return count;
		}

		PreviewBipedPopulationStats PopulatePreviewBipedFromLiveNPC(
			RE::PlayerCharacter& a_player,
			RE::TESNPC& a_npc,
			const RE::BipedAnim& a_sourceBiped,
			PreviewBipedData& a_previewBiped)
		{
			PreviewBipedPopulationStats stats;
			auto* race = ResolvePreviewRace(a_player, a_npc);
			if (!race) {
				return stats;
			}

			auto* skin = g_getSkin(std::addressof(a_npc));
			if (skin) {
				RE::BGSObjectInstance skinInstance(skin, nullptr);
				g_addArmorToBiped(
					std::addressof(skinInstance),
					race,
					a_previewBiped.Borrowed(),
					a_npc.GetSex(),
					skin->armorData.index);
				stats.skinAdded = true;
			}

			for (std::int32_t i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
				const auto& sourceObject = a_sourceBiped.object[i];
				if (!sourceObject.parent.object || sourceObject.parent.object == skin) {
					continue;
				}

				if (g_initWornObject(
						std::addressof(a_npc),
						a_previewBiped.Borrowed(),
						std::addressof(sourceObject.parent))) {
					++stats.wornObjects;
				}
			}

			stats.objectAddons = CountBipedArmorAddons(a_previewBiped.Get().object);
			stats.bufferedAddons = CountBipedArmorAddons(a_previewBiped.Get().bufferedObjects);
			return stats;
		}

		bool IsDescendantOf(RE::NiAVObject& a_object, RE::NiAVObject& a_potentialAncestor)
		{
			for (auto* current = std::addressof(a_object); current; current = current->parent) {
				if (current == std::addressof(a_potentialAncestor)) {
					return true;
				}
			}
			return false;
		}

		bool ContainsObject(RE::NiAVObject& a_root, RE::NiAVObject& a_target)
		{
			if (std::addressof(a_root) == std::addressof(a_target)) {
				return true;
			}

			auto* node = a_root.IsNode();
			if (!node) {
				return false;
			}

			for (auto& child : node->children) {
				if (child && ContainsObject(*child, a_target)) {
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] RE::NiAVObject* GetTopRoot(RE::NiAVObject* a_object)
		{
			auto* current = a_object;
			while (current && current->parent) {
				current = current->parent;
			}
			return current;
		}

		[[nodiscard]] const RE::BSFlattenedBoneTree::FlattenedBone* FindFlattenedBoneByWorldTransform(
			RE::BSFlattenedBoneTree& a_tree,
			const RE::NiTransform* a_worldTransform)
		{
			if (!a_tree.bone || !a_worldTransform || a_tree.boneCount <= 0 ||
				a_tree.boneCount > static_cast<std::int32_t>(RE::BSSkin::kMaxExpectedBones)) {
				return nullptr;
			}

			for (std::int32_t index = 0; index < a_tree.boneCount; ++index) {
				const auto& entry = a_tree.bone[index];
				if (std::addressof(entry.world) == a_worldTransform) {
					return std::addressof(entry);
				}
			}
			return nullptr;
		}

		[[nodiscard]] RE::NiNode* FindExistingNodeForFlattenedBone(
			const RE::BSFlattenedBoneTree::FlattenedBone& a_entry,
			const std::array<RE::NiAVObject*, 4>& a_searchRoots)
		{
			if (auto* existing = a_entry.node.get()) {
				return existing;
			}

			const std::string_view name(a_entry.name);
			if (name.empty()) {
				return nullptr;
			}

			std::unordered_set<RE::NiAVObject*> visitedRoots;
			for (auto* root : a_searchRoots) {
				if (!root || !visitedRoots.insert(root).second) {
					continue;
				}
				if (auto* found = root->GetObjectByName(a_entry.name)) {
					return found->IsNode();
				}
			}
			return nullptr;
		}

		void RefreshPreviewBoneLookup(RE::NiAVObject& a_previewRoot)
		{
			g_createBoneMap(std::addressof(a_previewRoot));

			auto* flattened = FindFlattenedBoneTree(std::addressof(a_previewRoot));
			if (!flattened) {
				return;
			}

			g_createBoneMap(flattened);
			g_previewFlattenedBoneTree = flattened;
			g_createBoneMap(std::addressof(a_previewRoot));
		}

		[[nodiscard]] RE::NiNode* GetPreviewActor3DRootNode(RE::NiAVObject& a_previewRoot)
		{
			auto* flattened = g_previewFlattenedBoneTree;
			if (!flattened || !IsDescendantOf(*flattened, a_previewRoot)) {
				flattened = FindFlattenedBoneTree(std::addressof(a_previewRoot));
				g_previewFlattenedBoneTree = flattened;
			}

			if (flattened) {
				return flattened->IsNode();
			}

			return a_previewRoot.IsNode();
		}

		void CollectPreviewSkinTargetNodes(
			RE::NiAVObject& a_previewRoot,
			std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			a_previewNodes.clear();

			if (auto* flattened = FindFlattenedBoneTree(std::addressof(a_previewRoot))) {
				CollectFlattenedBoneNodes(flattened, a_previewNodes);
			}

			// Keep flattened skeleton bones authoritative for duplicate names such
			// as Head_skin; add non-flattened attachment/helper nodes only when a
			// skeleton target with the same name does not already exist.
			CollectNamedNodes(std::addressof(a_previewRoot), a_previewNodes);
		}

		void ResolveNullSkinBonesFromFlattenedTree(
			RE::BSSkin::Instance& a_skin,
			RE::NiAVObject& a_attachmentRoot)
		{
			if (a_skin.bones.empty() || a_skin.bones.size() > RE::BSSkin::kMaxExpectedBones) {
				return;
			}
			if (a_skin.worldTransforms.empty() || a_skin.worldTransforms.size() != a_skin.bones.size()) {
				return;
			}

			bool hasNullBones = false;
			for (auto* bone : a_skin.bones) {
				if (!bone) {
					hasNullBones = true;
					break;
				}
			}
			if (!hasNullBones) {
				return;
			}

			auto* flattened = FindFlattenedBoneTree(a_skin.rootNode);
			if (!flattened) {
				flattened = FindFlattenedBoneTree(GetTopRoot(std::addressof(a_attachmentRoot)));
			}
			if (!flattened) {
				return;
			}

			const std::array<RE::NiAVObject*, 4> searchRoots{
				a_skin.rootNode,
				std::addressof(a_attachmentRoot),
				GetTopRoot(a_skin.rootNode),
				GetTopRoot(std::addressof(a_attachmentRoot)),
			};

			for (std::uint32_t index = 0; index < a_skin.bones.size(); ++index) {
				if (a_skin.bones[index]) {
					continue;
				}

				const auto* entry = FindFlattenedBoneByWorldTransform(*flattened, a_skin.worldTransforms[index]);
				auto* node = entry ? FindExistingNodeForFlattenedBone(*entry, searchRoots) : nullptr;
				if (!node) {
					continue;
				}

				a_skin.bones[index] = node;
				a_skin.worldTransforms[index] = std::addressof(node->world);
			}
		}

		void MergeAttachmentSkinBones(
			RE::NiAVObject& a_attachmentRoot,
			RE::NiNode& a_previewRootNode,
			RE::NiAVObject& a_previewRoot,
			std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			std::vector<RE::NiPointer<RE::NiAVObject>> candidateBones;
			struct ExternalBoneClone
			{
				RE::NiAVObject* sourceBone{ nullptr };
				RE::NiNode* previewParent{ nullptr };
			};
			std::vector<ExternalBoneClone> externalBones;
			std::unordered_set<RE::NiAVObject*> seenBones;
			std::unordered_set<RE::NiAVObject*> seenExternalRoots;
			std::unordered_set<std::string> queuedMissingBoneNames;
			ForEachGeometry(std::addressof(a_attachmentRoot), [&](RE::BSGeometry& a_geometry) {
				auto* skin = a_geometry.skinInstance.get();
				if (!skin || skin->bones.size() > RE::BSSkin::kMaxExpectedBones) {
					return;
				}

				for (auto* bone : skin->bones) {
					if (!bone || !seenBones.insert(bone).second) {
						continue;
					}
					const auto* boneName = bone->GetName().c_str();
					if (!boneName || boneName[0] == '\0') {
						continue;
					}
					if (FindNodeByName(a_previewNodes, boneName)) {
						continue;
					}
					if (!queuedMissingBoneNames.emplace(boneName).second) {
						continue;
					}

					if (IsDescendantOf(*bone, a_attachmentRoot)) {
						candidateBones.emplace_back(bone);
						continue;
					}

					auto* cloneRoot = bone;
					auto* previewParent = std::addressof(a_previewRootNode);
					bool matchedPreviewParent = false;
					for (auto* parent = bone->parent; parent; parent = parent->parent) {
						if (auto* matchedParent = FindNodeByName(a_previewNodes, parent->GetName())) {
							if (auto* matchedParentNode = matchedParent->IsNode()) {
								previewParent = matchedParentNode;
								matchedPreviewParent = true;
								break;
							}
						}
						cloneRoot = parent;
					}
					if (!matchedPreviewParent) {
						cloneRoot = bone;
					}

					if (cloneRoot && seenExternalRoots.insert(cloneRoot).second) {
						externalBones.push_back({
							.sourceBone = cloneRoot,
							.previewParent = previewParent,
						});
					}
				}
			});

			std::uint32_t mergedBones = 0;
			std::uint32_t externalClonedBones = 0;
			for (auto& candidate : candidateBones) {
				if (!candidate || !candidate->parent || FindNodeByName(a_previewNodes, candidate->GetName())) {
					continue;
				}

				bool hasMissingAncestor = false;
				for (auto& other : candidateBones) {
					if (other && other.get() != candidate.get() && IsDescendantOf(*candidate, *other)) {
						hasMissingAncestor = true;
						break;
					}
				}
				if (hasMissingAncestor) {
					continue;
				}

				auto previewClone = ClonePreviewObject(*candidate, std::addressof(a_previewRoot), std::addressof(a_previewNodes));
				if (!previewClone) {
					continue;
				}

				StripControllerChains(*previewClone);
				StripClonedGeometry(*previewClone);
				RE::bhkWorld::RemoveObjects(previewClone.get(), true, true);
				PreparePreviewTree(*previewClone);

				a_previewRootNode.AttachChild(previewClone.get(), false);
				g_previewAttachments.push_back({
					.slot = RE::BIPED_OBJECT::kNone,
					.object = previewClone,
					.parent = std::addressof(a_previewRootNode),
				});
				CollectNamedNodes(previewClone.get(), a_previewNodes);
				++mergedBones;
			}

			for (const auto& external : externalBones) {
				if (!external.sourceBone || FindNodeByName(a_previewNodes, external.sourceBone->GetName())) {
					continue;
				}

				auto previewClone = ClonePreviewObject(*external.sourceBone, std::addressof(a_previewRoot), std::addressof(a_previewNodes));
				if (!previewClone) {
					continue;
				}

				StripControllerChains(*previewClone);
				StripClonedGeometry(*previewClone);
				RE::bhkWorld::RemoveObjects(previewClone.get(), true, true);
				PreparePreviewTree(*previewClone);

				auto* previewParent = external.previewParent ? external.previewParent : std::addressof(a_previewRootNode);
				previewParent->AttachChild(previewClone.get(), false);
				g_previewAttachments.push_back({
					.slot = RE::BIPED_OBJECT::kNone,
					.object = previewClone,
					.parent = previewParent,
				});
				CollectNamedNodes(previewClone.get(), a_previewNodes);
				++externalClonedBones;
			}

			if (mergedBones > 0 || externalClonedBones > 0) {
				RefreshPreviewBoneLookup(a_previewRoot);
				CollectPreviewSkinTargetNodes(a_previewRoot, a_previewNodes);
			}
		}

		std::uint32_t MergeDuplicateNodeChildrenIntoKnownNode(
			RE::NiNode& a_duplicateNode,
			RE::NiNode& a_knownNode,
			std::unordered_map<std::string, RE::NiAVObject*>& a_knownNodes,
			std::uint32_t& a_movedUnknownNodes)
		{
			std::vector<RE::NiPointer<RE::NiAVObject>> children;
			children.reserve(a_duplicateNode.children.size());
			for (auto& child : a_duplicateNode.children) {
				if (child) {
					children.push_back(child);
				}
			}

			std::uint32_t pruned = 0;
			for (auto& child : children) {
				const auto* childName = child->GetName().c_str();
				auto* knownObject = childName && childName[0] != '\0' ? FindNodeByName(a_knownNodes, childName) : nullptr;
				if (knownObject) {
					if (auto* duplicateChildNode = child->IsNode()) {
						if (auto* knownChildNode = knownObject->IsNode()) {
							pruned += MergeDuplicateNodeChildrenIntoKnownNode(
								*duplicateChildNode,
								*knownChildNode,
								a_knownNodes,
								a_movedUnknownNodes);
						}
					}

					a_duplicateNode.DetachChild(child.get());
					g_retiredPreviewObjects.emplace_back(std::move(child));
					++pruned;
					continue;
				}

				a_duplicateNode.DetachChild(child.get());
				a_knownNode.AttachChild(child.get(), false);
				CollectNamedNodes(child.get(), a_knownNodes);
				++a_movedUnknownNodes;
			}

			return pruned;
		}

		std::uint32_t PruneKnownNamedChildSubtrees(
			RE::NiAVObject& a_root,
			std::unordered_map<std::string, RE::NiAVObject*>& a_knownNodes,
			std::uint32_t& a_movedUnknownNodes)
		{
			auto* node = a_root.IsNode();
			if (!node) {
				return 0;
			}

			std::vector<RE::NiPointer<RE::NiAVObject>> children;
			children.reserve(node->children.size());
			for (auto& child : node->children) {
				if (child) {
					children.push_back(child);
				}
			}

			std::uint32_t pruned = 0;
			for (auto& child : children) {
				const auto* childName = child->GetName().c_str();
				auto* knownObject = childName && childName[0] != '\0' ? FindNodeByName(a_knownNodes, childName) : nullptr;
				if (knownObject) {
					if (auto* duplicateChildNode = child->IsNode()) {
						if (auto* knownChildNode = knownObject->IsNode()) {
							pruned += MergeDuplicateNodeChildrenIntoKnownNode(
								*duplicateChildNode,
								*knownChildNode,
								a_knownNodes,
								a_movedUnknownNodes);
						}
					}

					node->DetachChild(child.get());
					g_retiredPreviewObjects.emplace_back(std::move(child));
					++pruned;
					continue;
				}

				pruned += PruneKnownNamedChildSubtrees(*child, a_knownNodes, a_movedUnknownNodes);
			}

			return pruned;
		}

		bool ContainsUnknownNamedNode(
			RE::NiAVObject& a_root,
			const std::unordered_map<std::string, RE::NiAVObject*>& a_knownNodes)
		{
			const auto* rootName = a_root.GetName().c_str();
			if (rootName && rootName[0] != '\0' && !FindNodeByName(a_knownNodes, rootName)) {
				return true;
			}

			auto* node = a_root.IsNode();
			if (!node) {
				return false;
			}

			for (auto& child : node->children) {
				if (child && ContainsUnknownNamedNode(*child, a_knownNodes)) {
					return true;
				}
			}
			return false;
		}

		bool ContainsUnknownNamedDescendant(
			RE::NiAVObject& a_root,
			const std::unordered_map<std::string, RE::NiAVObject*>& a_knownNodes)
		{
			auto* node = a_root.IsNode();
			if (!node) {
				return false;
			}

			for (auto& child : node->children) {
				if (child && ContainsUnknownNamedNode(*child, a_knownNodes)) {
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] bool SyncPreviewAttachmentParentsFromBiped(
			RE::NiAVObject& a_previewRoot,
			const RE::BipedAnim& a_sourceBiped)
		{
			if (g_previewAttachments.empty()) {
				return false;
			}

			std::unordered_map<std::string, RE::NiAVObject*> previewNodes;
			CollectPreviewSkinTargetNodes(a_previewRoot, previewNodes);
			if (previewNodes.empty()) {
				return false;
			}

			bool changed = false;
			for (auto& attachment : g_previewAttachments) {
				if (!attachment.object) {
					continue;
				}

				const auto slotIndex = std::to_underlying(attachment.slot);
				if (slotIndex < 0 || slotIndex >= std::to_underlying(RE::BIPED_OBJECT::kTotal)) {
					continue;
				}

				auto* sourceClone = a_sourceBiped.object[slotIndex].partClone.get();
				if (!sourceClone) {
					continue;
				}

				auto* parent = FindPreviewAttachParent(*sourceClone, a_previewRoot, previewNodes);
				if (!parent || parent == attachment.parent) {
					continue;
				}

				if (attachment.parent) {
					attachment.parent->DetachChild(attachment.object.get());
				}
				parent->AttachChild(attachment.object.get(), false);
				attachment.parent = parent;
				changed = true;

				REX::INFO(
					"Preview attachment reparented: slot={}, object='{}', parent='{}'",
					slotIndex,
					attachment.object->GetName().c_str(),
					parent->GetName().c_str());
			}

			if (changed) {
				RE::NiUpdateData updateData;
				a_previewRoot.Update(updateData);
			}
			return changed;
		}

		void RetirePreviewAttachments(const bool a_detach)
		{
			for (auto& attachment : g_previewAttachments) {
				if (a_detach && attachment.parent && attachment.object) {
					attachment.parent->DetachChild(attachment.object.get());
				}
				if (attachment.object) {
					g_retiredPreviewObjects.emplace_back(std::move(attachment.object));
				}
			}
			g_previewAttachments.clear();
		}

		void ResetPreview3DState(const bool a_disableRenderer)
		{
			Renderer::ClearPreviewRoot(a_disableRenderer);
			RetirePreviewAttachments(true);
			g_previewRoot.reset();
			g_previewFlattenedBoneTree = nullptr;
			g_previewFaceNode.reset();
			g_pendingRendererAttach = false;
			g_pendingFramingUpdate = false;
			g_renderStateDirty = false;
			g_pendingRenderDelta = 0.0F;
		}

		void StagePreview3DReplacement()
		{
			RetirePreviewAttachments(true);
			if (g_previewRoot) {
				g_retiredPreviewObjects.emplace_back(std::move(g_previewRoot));
			}
			g_previewFlattenedBoneTree = nullptr;
			g_previewFaceNode.reset();
		}

		void ReleasePreview3DState()
		{
			ResetPreview3DState(true);
			g_retiredPreviewObjects.clear();
		}

		void ClearPreviewRebuildState()
		{
			g_requestedEquipmentAuditFrames.store(0, std::memory_order_release);
			g_equipmentAuditActive.store(false, std::memory_order_release);
			g_equipmentAuditFrames = 0;
			g_pendingBipedSignature = 0;
			g_pendingBipedSignatureFrames = 0;
			g_deferredBipedRebuildSignature = 0;
			g_bipedSignature = 0;
			g_visualSignature = 0;
			g_pendingMorphGeometryRebuild = false;
			g_pendingRendererAttach = false;
			g_pendingFramingUpdate = false;
			g_renderStateDirty = false;
			g_pendingRenderDelta = 0.0F;
			g_lastDiagnostic.clear();
		}

		void BeginEquipmentAuditIfRequested()
		{
			const auto requestedFrames = g_requestedEquipmentAuditFrames.exchange(0, std::memory_order_acq_rel);
			if (requestedFrames == 0) {
				return;
			}

			g_equipmentAuditFrames = std::max(g_equipmentAuditFrames, requestedFrames);
			g_equipmentAuditActive.store(true, std::memory_order_release);
			g_pendingBipedSignature = 0;
			g_pendingBipedSignatureFrames = 0;
		}

		[[nodiscard]] bool TryBuildBipedSignature(const RE::BipedAnim& a_biped, std::uint64_t& a_signature)
		{
			a_signature = BuildBipedSignature(std::addressof(a_biped));
			if (a_signature == 0) {
				LogDiagnostic("third-person biped has empty signature");
				return false;
			}
			return true;
		}

		[[nodiscard]] bool TryResolveAuditedBipedSignature(const RE::BipedAnim& a_biped, std::uint64_t& a_signature)
		{
			if (g_equipmentAuditFrames == 0) {
				g_equipmentAuditActive.store(false, std::memory_order_release);
				return false;
			}

			std::int32_t pendingSlot = -1;
			if (HasPendingBipedModelHandles(a_biped, pendingSlot)) {
				g_pendingBipedSignature = 0;
				g_pendingBipedSignatureFrames = 0;
				LogDiagnostic("equipment rebuild delayed: biped slot pending in slot " + std::to_string(pendingSlot));
				return false;
			}

			--g_equipmentAuditFrames;
			if (g_equipmentAuditFrames == 0) {
				g_equipmentAuditActive.store(false, std::memory_order_release);
			}

			std::uint64_t currentSignature = 0;
			if (!TryBuildBipedSignature(a_biped, currentSignature)) {
				g_pendingBipedSignature = 0;
				g_pendingBipedSignatureFrames = 0;
				return false;
			}

			if (currentSignature == g_bipedSignature) {
				g_pendingBipedSignature = 0;
				g_pendingBipedSignatureFrames = 0;
				return false;
			}

			if (currentSignature != g_pendingBipedSignature) {
				g_pendingBipedSignature = currentSignature;
				g_pendingBipedSignatureFrames = 1;
				return false;
			}

			++g_pendingBipedSignatureFrames;
			if (g_pendingBipedSignatureFrames < kBipedSignatureStableFrames) {
				return false;
			}

			a_signature = currentSignature;
			g_equipmentAuditActive.store(false, std::memory_order_release);
			g_pendingBipedSignature = 0;
			g_pendingBipedSignatureFrames = 0;
			return true;
		}

		void UpdatePostAnimationBipedSignature(const RE::BipedAnim& a_biped)
		{
			if (g_deferredBipedRebuildSignature != 0) {
				return;
			}

			std::uint64_t signature = 0;
			if (g_equipmentAuditFrames != 0) {
				if (TryResolveAuditedBipedSignature(a_biped, signature)) {
					g_deferredBipedRebuildSignature = signature;
					LogDiagnostic("equipment rebuild scheduled from post-animation biped hash");
				}
				return;
			}

			if (TryBuildBipedSignature(a_biped, signature)) {
				g_bipedSignature = signature;
			}
		}

		void RebindSkinInstance(
			RE::BSSkin::Instance& a_skin,
			RE::NiAVObject& a_previewRoot,
			const std::unordered_map<std::string, RE::NiAVObject*>& a_previewNodes)
		{
			if (a_skin.bones.empty()) {
				return;
			}

			if (a_skin.bones.size() > RE::BSSkin::kMaxExpectedBones) {
				return;
			}

			if (!a_skin.worldTransforms.empty() && a_skin.worldTransforms.size() != a_skin.bones.size()) {
				return;
			}

			for (std::uint32_t i = 0; i < a_skin.bones.size(); ++i) {
				auto* originalBone = a_skin.bones[i];
				if (!originalBone) {
					continue;
				}

				auto* previewBone = FindNodeByName(a_previewNodes, originalBone->GetName());
				if (!previewBone) {
					continue;
				}

				if (a_skin.bones[i] != previewBone) {
					a_skin.bones[i] = previewBone;
				}
				if (!a_skin.worldTransforms.empty()) {
					a_skin.worldTransforms[i] = std::addressof(previewBone->world);
				}
			}

			a_skin.rootNode = std::addressof(a_previewRoot);
			a_skin.paletteStamp = 0;
		}

		void FixPreviewFaceGenSkinInstances(RE::BSFaceGenNiNode& a_faceNode, RE::NiAVObject& a_attachRoot)
		{
			// IDA: BSFaceGenUtils::AttachHeadHelper attaches the face node to
			// actor3D->IsNode(), then calls the BSFaceGenNiNode vfunc at 0x218.
			// The vfunc iterates direct headpart geometries and runs the same
			// internal FixSkinInstances path used by AddHeadPartOnActor.
			// CreateHeadForNPC clears faceGenFlags 0x4/0x10; AttachHeadHelper
			// sets them again immediately before this call.
			a_faceNode.faceGenFlags |= 0x14;
			g_fixFaceGenHeadSkinInstances(std::addressof(a_faceNode), std::addressof(a_attachRoot), true);
			if (a_faceNode.animationData) {
				g_resetFaceGenCurrentMorphs(a_faceNode.animationData, 0.0F);
			}
		}

		void RebindPreviewSkinInstances(
			RE::NiAVObject& a_previewRoot,
			const RE::BipedAnim& a_sourceBiped)
		{
			RefreshPreviewBoneLookup(a_previewRoot);

			std::unordered_map<std::string, RE::NiAVObject*> previewNodes;
			CollectPreviewSkinTargetNodes(a_previewRoot, previewNodes);
			if (previewNodes.empty()) {
				return;
			}

			std::unordered_set<RE::BSSkin::Instance*> sourceSkins;
			CollectSourceBipedSkinInstances(a_sourceBiped, sourceSkins);

			std::uint32_t rebound = 0;
			ForEachGeometry(std::addressof(a_previewRoot), [&](RE::BSGeometry& a_geometry) {
				if (g_previewFaceNode && IsDescendantOf(a_geometry, *g_previewFaceNode)) {
					return;
				}

				auto* skin = a_geometry.skinInstance.get();
				if (!skin || sourceSkins.contains(skin)) {
					return;
				}

				RebindSkinInstance(*skin, a_previewRoot, previewNodes);
				++rebound;
			});

			if (rebound == 0) {
				return;
			}

			RE::NiUpdateData updateData;
			a_previewRoot.Update(updateData);
		}

		[[nodiscard]] bool SyncEquipmentsFromBiped(
			RE::PlayerCharacter& a_player,
			RE::NiAVObject& a_previewRoot,
			const RE::BipedAnim& a_sourceBiped)
		{
			auto* previewRootNode = a_previewRoot.IsNode();
			if (!previewRootNode) {
				LogDiagnostic("equipment sync skipped: preview root is not a NiNode");
				return false;
			}

			RetirePreviewAttachments(true);

			std::unordered_map<std::string, RE::NiAVObject*> previewNodes;
			CollectPreviewSkinTargetNodes(a_previewRoot, previewNodes);
			if (previewNodes.empty()) {
				LogDiagnostic("equipment sync skipped: preview root has no named nodes");
				return false;
			}

			std::unordered_set<RE::NiAVObject*> seenSourceObjects;
			std::uint32_t attachedObjects = 0;

			auto mirrorObject = [&](const RE::BIPED_OBJECT a_slot, const RE::BIPOBJECT& a_sourceObject) {
				auto* sourceClone = a_sourceObject.partClone.get();
				if (!sourceClone || !seenSourceObjects.insert(sourceClone).second) {
					return;
				}

				std::unordered_set<RE::BSSkin::Instance*> sourceSkins;
				CollectSkinInstances(sourceClone, sourceSkins);

				auto previewClone = ClonePreviewObject(*sourceClone, std::addressof(a_previewRoot), std::addressof(previewNodes));
				if (!previewClone) {
					return;
				}
				StripControllerChains(*previewClone);

				auto* parent = FindPreviewAttachParent(*sourceClone, a_previewRoot, previewNodes);
				if (!parent) {
					g_retiredPreviewObjects.emplace_back(std::move(previewClone));
					return;
				}

				RE::bhkWorld::RemoveObjects(previewClone.get(), true, true);
				PreparePreviewTree(*previewClone);
				(void)InitializePreviewCloth(
					a_player,
					*previewClone,
					a_previewRoot,
					a_sourceObject.part ? a_sourceObject.part->GetModel() : nullptr);
				ForEachGeometry(previewClone.get(), [&](RE::BSGeometry& a_geometry) {
					if (auto* skin = a_geometry.skinInstance.get()) {
						ResolveNullSkinBonesFromFlattenedTree(
							*skin,
							*previewClone);
					}
				});
				MergeAttachmentSkinBones(
					*previewClone,
					*previewRootNode,
					a_previewRoot,
					previewNodes);

				ForEachGeometry(previewClone.get(), [&](RE::BSGeometry& a_geometry) {
					auto* skin = a_geometry.skinInstance.get();
					if (!skin) {
						return;
					}
					if (sourceSkins.contains(skin)) {
						return;
					}
					RebindSkinInstance(*skin, a_previewRoot, previewNodes);
				});
				PrepareAttachmentSkinComplexion(
					*previewClone,
					a_slot,
					a_sourceBiped);

				parent->AttachChild(previewClone.get(), false);
				g_previewAttachments.push_back({
					.slot = a_slot,
					.object = previewClone,
					.parent = parent,
				});
				++attachedObjects;
			};

			for (std::int32_t i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
				const auto slot = static_cast<RE::BIPED_OBJECT>(i);
				switch (slot) {
				case RE::BIPED_OBJECT::kFaceGenHead:
					continue;
				default:
					break;
				}
				const auto& sourceObject = a_sourceBiped.object[i];
				mirrorObject(slot, sourceObject);
			}

			RefreshPreviewBoneLookup(a_previewRoot);

			RE::NiUpdateData updateData;
			a_previewRoot.Update(updateData);

			return attachedObjects > 0;
		}

		bool AttachPreviewFaceBonesFromBiped(
			const RE::BipedAnim& a_sourceBiped,
			RE::NiAVObject& a_previewRoot,
			RE::TESRace& a_race,
			const RE::SEX a_sex)
		{
			auto* actorRootNode = GetPreviewActor3DRootNode(a_previewRoot);
			if (!actorRootNode) {
				return false;
			}

			std::unordered_map<std::string, RE::NiAVObject*> previewNodes;
			CollectPreviewSkinTargetNodes(a_previewRoot, previewNodes);
			if (previewNodes.empty()) {
				return false;
			}

			FaceBoneAttachStats stats;
			std::vector<FaceBoneCandidateDetail> details;
			std::unordered_set<std::string> loadedPaths;

			enum class FaceBoneModelAttachResult
			{
				kAttached,
				kLoadFailed,
				kPrunedEmpty
			};

			auto attachFaceBoneModel = [&](const char* a_modelPath) {
				auto faceBones = LoadPreviewModel(a_modelPath);
				if (!faceBones) {
					return FaceBoneModelAttachResult::kLoadFailed;
				}

				StripControllerChains(*faceBones);
				RE::bhkWorld::RemoveObjects(faceBones.get(), true, true);
				PreparePreviewTree(*faceBones);
				StripClonedGeometry(*faceBones);
				std::uint32_t movedUnknownNodes = 0;
				const auto pruned = PruneKnownNamedChildSubtrees(*faceBones, previewNodes, movedUnknownNodes);
				stats.prunedDuplicates += pruned;
				stats.movedUnknownNodes += movedUnknownNodes;
				const bool hasRemainingUnknownNodes = ContainsUnknownNamedDescendant(*faceBones, previewNodes);
				if (!hasRemainingUnknownNodes && movedUnknownNodes == 0) {
					return FaceBoneModelAttachResult::kPrunedEmpty;
				}

				if (hasRemainingUnknownNodes) {
					actorRootNode->AttachChild(faceBones.get(), false);
					if (auto* convertedFaceBones = g_convertNodeTree(faceBones.get())) {
						if (convertedFaceBones != faceBones.get()) {
							faceBones.reset(convertedFaceBones);
						}
						++stats.converted;
					}
					if (!ContainsObject(*actorRootNode, *faceBones)) {
						actorRootNode->AttachChild(faceBones.get(), false);
						++stats.reattached;
					}
					g_previewAttachments.push_back({
						.slot = RE::BIPED_OBJECT::kFaceGenHead,
						.object = faceBones,
						.parent = actorRootNode,
					});
					CollectNamedNodes(faceBones.get(), previewNodes);
				} else {
					g_convertNodeTree(actorRootNode);
					++stats.converted;
				}

				++stats.attached;
				return FaceBoneModelAttachResult::kAttached;
			};

			for (const auto& candidate : GetRaceSkeletonFaceBoneModelPaths(a_race, std::to_underlying(a_sex))) {
				++stats.skeletonCandidates;
				if (!loadedPaths.insert(candidate.path).second) {
					continue;
				}

				const auto result = attachFaceBoneModel(candidate.path.c_str());
				if (result == FaceBoneModelAttachResult::kAttached) {
					++stats.skeletonAttached;
					REX::INFO(
						"Preview race skeleton facebones attached: source='{}', path='{}'",
						candidate.source,
						candidate.path);
					break;
				}

				if (result == FaceBoneModelAttachResult::kLoadFailed) {
					++stats.skeletonLoadFailed;
				} else {
					++stats.skeletonPrunedEmpty;
				}
			}

			auto attachFromObjects = [&](const RE::BIPOBJECT* a_objects, const char* a_sourceName) {
				for (std::int32_t i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
					const auto& sourceObject = a_objects[i];
					if (!sourceObject.armorAddon) {
						continue;
					}

					auto* model = SelectArmorAddonFacebonesModel(*sourceObject.armorAddon, a_sex);
					const auto* modelPath = model ? model->GetModel() : nullptr;
					++stats.candidates;
					if (!modelPath || modelPath[0] == '\0') {
						++stats.noFacebonesModel;
						details.push_back(MakeFaceBoneCandidateDetail(
							a_sourceName,
							i,
							sourceObject,
							modelPath,
							"no facebones model",
							0));
						continue;
					}
					if (!loadedPaths.insert(modelPath).second) {
						++stats.duplicatePath;
						details.push_back(MakeFaceBoneCandidateDetail(
							a_sourceName,
							i,
							sourceObject,
							modelPath,
							"duplicate facebones path",
							0));
						continue;
					}

					const auto result = attachFaceBoneModel(modelPath);
					if (result != FaceBoneModelAttachResult::kAttached) {
						if (result == FaceBoneModelAttachResult::kLoadFailed) {
							++stats.loadFailed;
						} else {
							++stats.prunedEmpty;
						}
						details.push_back(MakeFaceBoneCandidateDetail(
							a_sourceName,
							i,
							sourceObject,
							modelPath,
							result == FaceBoneModelAttachResult::kLoadFailed ? "model load failed" : "no unknown named facebone descendants after pruning",
							0));
						continue;
					}
				}
			};

			attachFromObjects(a_sourceBiped.object, "object");
			attachFromObjects(a_sourceBiped.bufferedObjects, "buffered");

			if (stats.attached > 0) {
				RefreshPreviewBoneLookup(a_previewRoot);
				REX::INFO(
					"Headpart facebone models attached: models={}, converted={}, prunedDuplicates={}",
					stats.attached,
					stats.converted,
					stats.prunedDuplicates);
			} else {
				LogFaceBoneAttachFailure(
					a_sourceBiped,
					*actorRootNode,
					a_sex,
					previewNodes.size(),
					stats,
					details);
			}

			return stats.attached > 0;
		}

		bool EnsurePreviewHead(RE::PlayerCharacter& a_player, RE::NiAVObject& a_previewRoot, const RE::BipedAnim& a_sourceBiped)
		{
			auto* playerBase = a_player.GetObjectReference();
			auto* npc = playerBase ? playerBase->As<RE::TESNPC>() : nullptr;
			if (!npc) {
				LogDiagnostic("preview head creation skipped: player base is not TESNPC");
				return false;
			}

			auto* rootFaceNPC = npc->GetRootFaceNPC();
			if (!rootFaceNPC) {
				LogDiagnostic("preview head creation skipped: root face NPC is null");
				return false;
			}

			RE::NiPointer<RE::BSFaceGenNiNode> faceNode;
			const bool createdHead = g_createHeadForNPC(npc, faceNode, true, true, nullptr);

			if (!createdHead || !faceNode) {
				LogDiagnostic("preview head creation failed: CreateHeadForNPC returned null");
				return false;
			}

			RE::bhkWorld::RemoveObjects(faceNode.get(), true, true);
			PreparePreviewTree(*faceNode);
			InitializePreviewHeadPartCloth(*npc, a_player, *faceNode, a_previewRoot);
			ApplyHeadPartBipedVisibility(*npc, *faceNode, a_sourceBiped);

			auto* actorRootNode = GetPreviewActor3DRootNode(a_previewRoot);
			if (!actorRootNode) {
				LogDiagnostic("preview head creation failed: preview actor root is not a NiNode");
				return false;
			}

			PreviewBipedData previewBiped(a_player, *actorRootNode);
			if (!previewBiped) {
				LogDiagnostic("preview head creation failed: preview biped allocation failed");
				return false;
			}

			auto* previewRace = ResolvePreviewRace(a_player, *npc);
			if (!previewRace) {
				LogDiagnostic("preview head creation failed: preview race is null");
				return false;
			}

			const auto previewBipedStats =
				PopulatePreviewBipedFromLiveNPC(a_player, *npc, a_sourceBiped, previewBiped);
			if (!AttachPreviewFaceBonesFromBiped(previewBiped.Get(), a_previewRoot, *previewRace, npc->GetSex())) {
				REX::WARN(
					"preview head creation: no preview facebone model was attached; skinAdded={}, wornObjects={}, objectAddons={}, bufferedAddons={}",
					previewBipedStats.skinAdded,
					previewBipedStats.wornObjects,
					previewBipedStats.objectAddons,
					previewBipedStats.bufferedAddons);
			}

			actorRootNode->AttachChild(faceNode.get(), false);
			g_previewFaceNode = faceNode;
			RefreshPreviewBoneLookup(a_previewRoot);
			FixPreviewFaceGenSkinInstances(*faceNode, *actorRootNode);
			RE::NiUpdateData updateData;
			actorRootNode->Update(updateData);
			RefreshPreviewBoneLookup(a_previewRoot);
			return true;
		}

		void ApplyPreviewBodyTint(RE::TESNPC& a_npc, RE::NiAVObject& a_previewRoot)
		{
			RE::NiColorA bodyTint{ 0.0F, 0.0F, 0.0F, 0.0F };
			g_calculateBodyTintColor(std::addressof(a_npc), bodyTint, nullptr, true, false);
			bodyTint.a = 1.0F;
			g_updateBodyTintColorsOnScene(std::addressof(a_previewRoot), bodyTint);
			RestorePreviewShaderAlpha(a_previewRoot);
		}

		bool RebuildPreview(
			RE::PlayerCharacter& a_player,
			const RE::BipedAnim& a_biped,
			std::uint64_t a_bipedSignature,
			std::uint64_t a_visualSignature)
		{
			// Keep the renderer-owned root stable until the Interface3D render boundary.
			// Native Interface3D consumes roots from DrawWorld; swapping here can race the
			// offscreen deferred passes when rebuilds happen mid-frame.
			StagePreview3DReplacement();

			RE::NiPointer<RE::NiAVObject> previewRoot = LoadPreviewRaceSkeleton(a_player);
			if (!previewRoot) {
				g_previewRoot.reset();
				g_previewFlattenedBoneTree = nullptr;
				g_bipedSignature = 0;
				g_visualSignature = 0;
				LogDiagnostic("preview race skeleton load returned null");
				return false;
			}

			RE::bhkWorld::RemoveObjects(previewRoot.get(), true, true);
			StripControllerChains(*previewRoot);
			PreparePreviewTree(*previewRoot);
			if (!SyncEquipmentsFromBiped(a_player, *previewRoot, a_biped)) {
				g_previewRoot.reset();
				g_previewFlattenedBoneTree = nullptr;
				g_bipedSignature = 0;
				g_visualSignature = 0;
				return false;
			}
			if (!EnsurePreviewHead(a_player, *previewRoot, a_biped)) {
				g_previewRoot.reset();
				g_previewFlattenedBoneTree = nullptr;
				g_bipedSignature = 0;
				g_visualSignature = 0;
				return false;
			}
			if (auto* playerBase = a_player.GetObjectReference(); playerBase) {
				if (auto* npc = playerBase->As<RE::TESNPC>(); npc) {
					ApplyPreviewBodyTint(*npc, *previewRoot);
				}
			}
			RebindPreviewSkinInstances(*previewRoot, a_biped);

			SanitizePreviewRenderTree(*previewRoot);
			ApplyHeadCenteredFraming(*previewRoot);
			PrepareForInterface3DOffscreen(*previewRoot);

			g_previewRoot = previewRoot;
			g_bipedSignature = a_bipedSignature;
			g_visualSignature = a_visualSignature;

			g_pendingRendererAttach = true;
			g_pendingFramingUpdate = false;
			MarkRenderStateDirty();
			Animations::ResetInitialState();
			Morph::MarkSecondaryDirty();

			return true;
		}

		void Tick(const float a_deltaTime)
		{
			if (g_looksMenuSuspended) {
				LogDiagnostic("halted: LooksMenu is open");
				HideRendererAndResetAnimation();
				return;
			}

			auto player = RE::PlayerCharacter::GetSingleton();
			std::string reason;
			if (!ShouldShow(player, reason)) {
				LogDiagnostic("hidden: " + reason);
				HideRendererAndResetAnimation();
				return;
			}

			if (!Renderer::IsConfigured() || !Renderer::Get()) {
				Renderer::Configure();
				Renderer::ConfigureLighting();
			}

			if (!Renderer::Get()) {
				LogDiagnostic("renderer unavailable after configuration");
				return;
			}

			const auto& biped = player->GetBiped(false);
			if (!biped) {
				LogDiagnostic("third-person biped is null");
				HideRendererAndResetAnimation();
				return;
			}

			std::string sourceReason;
			if (!IsPreviewSourceReady(*player, *biped, sourceReason)) {
				LogDiagnostic("preview render postponed: " + sourceReason);
				HideRendererAndResetAnimation();
				return;
			}

			BeginEquipmentAuditIfRequested();

			const auto visualSignature = BuildVisualSignature(*player);
			std::uint64_t bipedSignature = 0;
			const bool forceRebuild = !g_previewRoot || visualSignature != g_visualSignature;
			const bool auditRebuild = !forceRebuild && g_deferredBipedRebuildSignature != 0;
			if (auditRebuild) {
				bipedSignature = g_deferredBipedRebuildSignature;
				g_deferredBipedRebuildSignature = 0;
			}

			if (forceRebuild) {
				std::int32_t pendingSlot = -1;
				if (HasPendingBipedModelHandles(*biped, pendingSlot)) {
					LogDiagnostic("preview rebuild postponed: biped slot pending in slot " + std::to_string(pendingSlot));
					HideRendererAndResetAnimation();
					return;
				}

				if (!TryBuildBipedSignature(*biped, bipedSignature)) {
					HideRendererAndResetAnimation();
					return;
				}
			}

			if (forceRebuild || auditRebuild) {
				if (!RebuildPreview(*player, *biped, bipedSignature, visualSignature)) {
					HideRendererAndResetAnimation();
					return;
				}
			}

			if (g_previewRoot) {
				MarkRenderStateDirty(a_deltaTime);
			}

			if (Renderer::Enable()) {
				LogDiagnostic("renderer enabled");
			}
		}

		void CommitRenderStateImpl()
		{
			if (g_committingRenderState || (!g_renderStateDirty && !g_pendingRendererAttach)) {
				return;
			}
			g_committingRenderState = true;

			const REX::TScopeExit clearCommitGuard{ [&]() noexcept {
				g_committingRenderState = false;
			} };

			if (!g_previewRoot) {
				g_renderStateDirty = false;
				g_pendingRenderDelta = 0.0F;
				g_pendingRendererAttach = false;
				return;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}

			const auto& biped = player->GetBiped(false);
			if (!biped) {
				return;
			}

			const auto renderDelta = g_pendingRenderDelta;
			g_pendingRenderDelta = 0.0F;
			g_renderStateDirty = false;

			if (g_pendingFramingUpdate) {
				ApplyHeadCenteredFraming(*g_previewRoot);
				g_pendingFramingUpdate = false;
			}

			const auto morphResult = Morph::Update(*player, *g_previewRoot, g_previewFlattenedBoneTree);
			g_pendingMorphGeometryRebuild =
				g_pendingMorphGeometryRebuild ||
				Morph::HasResult(morphResult, Morph::UpdateResult::kGeometryRebuild);

			if (g_pendingMorphGeometryRebuild) {
				std::int32_t pendingSlot = -1;
				if (HasPendingBipedModelHandles(*biped, pendingSlot)) {
					LogDiagnostic("morph rebuild postponed: biped slot pending in slot " + std::to_string(pendingSlot));
					return;
				}

				std::uint64_t bipedSignature = 0;
				if (!TryBuildBipedSignature(*biped, bipedSignature)) {
					HideRendererAndResetAnimation();
					return;
				}

				if (!RebuildPreview(*player, *biped, bipedSignature, BuildVisualSignature(*player))) {
					HideRendererAndResetAnimation();
					return;
				}
				g_pendingMorphGeometryRebuild = false;
			}

			if (!g_previewRoot) {
				return;
			}

			(void)SyncPreviewAttachmentParentsFromBiped(*g_previewRoot, *biped);
			Animations::Update(*player, *g_previewRoot, renderDelta);
			SyncPreviewFacialExpression(*player);
			ApplyHeadFollowTranslation(*g_previewRoot);
			UpdatePostAnimationBipedSignature(*biped);

			if (g_pendingRendererAttach) {
				Renderer::AttachPreviewRoot(*g_previewRoot);
				g_pendingRendererAttach = false;
			}
		}
	}

	namespace Previewer
	{
		void Update(float a_deltaTime)
		{
			Tick(a_deltaTime);
		}

		void CommitRenderState()
		{
			CommitRenderStateImpl();
		}

		void Reset()
		{
			g_looksMenuSuspended = false;
			Renderer::Hide();
			Renderer::Reset();
			ReleasePreview3DState();
			g_previewRoot.reset();
			g_previewFaceNode.reset();
			ClearPreviewRebuildState();
			Morph::Reset();
			Animations::Reset();
		}

		void MarkEquipmentDirty()
		{
			if (g_equipmentAuditActive.load(std::memory_order_acquire)) {
				return;
			}

			std::uint32_t expected = 0;
			g_requestedEquipmentAuditFrames.compare_exchange_strong(
				expected,
				kEquipmentAuditFrames,
				std::memory_order_acq_rel,
				std::memory_order_acquire);
		}

		void ApplyConfigChanges()
		{
			if (!GetConfig().enabled) {
				HideRendererAndResetAnimation();
				return;
			}

			if (g_previewRoot) {
				g_pendingFramingUpdate = true;
				MarkRenderStateDirty();
			}

			if (!Renderer::Get()) {
				return;
			}

			Renderer::ApplyDisplayLayout();
			Renderer::ConfigureLighting();
		}

		void ReloadConfig()
		{
			LoadConfig();
			g_lastDiagnostic.clear();

			if (!GetConfig().enabled) {
				HideRendererAndResetAnimation();
				return;
			}

			if (g_previewRoot) {
				g_pendingFramingUpdate = true;
				g_pendingRendererAttach = true;
				MarkRenderStateDirty();
			}

			if (!Renderer::Get()) {
				return;
			}

			Renderer::Configure();
			Renderer::ConfigureLighting();
		}

		void SuspendForLooksMenu()
		{
			if (g_looksMenuSuspended) {
				return;
			}

			g_looksMenuSuspended = true;
			Renderer::Hide();
			ReleasePreview3DState();
			ClearPreviewRebuildState();
			Morph::Reset();
			Animations::Reset();
		}

		void ResumeAfterLooksMenu()
		{
			if (!g_looksMenuSuspended) {
				return;
			}

			g_looksMenuSuspended = false;
			ClearPreviewRebuildState();
		}
	}

}
