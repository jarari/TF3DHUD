#pragma once

#include "RE/B/BSFlattenedBoneTree.h"
#include "RE/N/NiAVObject.h"

namespace TF3DHud::PreviewFraming
{
	bool ApplyHeadCentered(RE::NiAVObject& a_previewRoot, RE::BSFlattenedBoneTree*& a_flattenedCache);
	void ApplyHeadFollowTranslation(RE::NiAVObject& a_previewRoot, RE::BSFlattenedBoneTree*& a_flattenedCache);
}
