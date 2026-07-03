#include "PreviewCloth.h"

#include "Address.h"
#include "Utils.h"

#include "RE/B/BGSHeadPart.h"
#include "RE/T/TESObjectCELL.h"

#include <cstddef>

namespace TF3DHud::PreviewCloth
{
	namespace
	{
		auto& g_createClothFor3D = Address::CreateClothFor3D;
		auto& g_setClothWorld = Address::SetClothWorld;
		auto& g_setClothSettleOnTransitionToSim = Address::SetClothSettleOnTransitionToSim;

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

		bool InitializeHeadPart(
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
	}

	std::uint32_t Initialize(
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

	void InitializeHeadParts(
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
				InitializeHeadPart(a_reference, a_faceNode, a_previewRoot, a_headPart);
			});
		}
	}
}
