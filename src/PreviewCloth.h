#pragma once

#include "RE/N/NiAVObject.h"
#include "RE/T/TESNPC.h"
#include "RE/T/TESObjectREFR.h"

#include <cstdint>

namespace TF3DHud::PreviewCloth
{
	std::uint32_t Initialize(
		RE::TESObjectREFR& a_reference,
		RE::NiAVObject& a_object,
		RE::NiAVObject& a_previewRoot,
		const char* a_modelPath);
	void InitializeHeadParts(
		RE::TESNPC& a_npc,
		RE::TESObjectREFR& a_reference,
		RE::NiAVObject& a_faceNode,
		RE::NiAVObject& a_previewRoot);
}
