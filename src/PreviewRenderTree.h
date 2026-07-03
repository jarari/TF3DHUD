#pragma once

#include "RE/N/NiAVObject.h"

#include <cstdint>
#include <vector>

namespace TF3DHud::PreviewRenderTree
{
	void PrepareForInterface3DOffscreen(RE::NiAVObject& a_root);
	void PreparePreviewTree(RE::NiAVObject& a_previewRoot);
	std::uint32_t StripControllerChains(RE::NiAVObject& a_root);
	void SanitizePreviewRenderTree(RE::NiAVObject& a_previewRoot);
	void RestorePreviewShaderAlpha(RE::NiAVObject& a_previewRoot);
	[[nodiscard]] std::vector<RE::NiPointer<RE::NiAVObject>> StripClonedGeometry(RE::NiAVObject& a_previewRoot);
}
