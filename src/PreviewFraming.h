#pragma once

#include "RE/B/BSFlattenedBoneTree.h"
#include "RE/N/NiAVObject.h"

namespace TF3DHud::PreviewFraming
{
	bool ApplyTargetCentered(RE::NiAVObject& a_previewRoot, RE::BSFlattenedBoneTree*& a_flattenedCache);
	void ApplyTargetFollowTranslation(RE::NiAVObject& a_previewRoot, RE::BSFlattenedBoneTree*& a_flattenedCache);
}
