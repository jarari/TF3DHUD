#pragma once

#include "RE/B/BipedAnim.h"
#include "RE/N/NiAVObject.h"
#include "RE/T/TESNPC.h"
#include "RE/T/TESObjectREFR.h"
#include "RE/T/TESRace.h"

namespace TF3DHud::PreviewHeadParts
{
	void ApplyBipedVisibility(
		RE::TESNPC& a_npc,
		RE::TESRace& a_race,
		RE::NiAVObject& a_faceNode,
		RE::TESObjectREFR& a_reference,
		const RE::BipedAnim& a_sourceBiped);
}
