#pragma once

#include "RE/N/NiAVObject.h"

#include <string>
#include <unordered_map>

namespace TF3DHud::PreviewClone
{
	RE::NiPointer<RE::NiAVObject> CloneObject(
		RE::NiAVObject& a_source,
		RE::NiAVObject* a_previewRoot = nullptr,
		const std::unordered_map<std::string, RE::NiAVObject*>* a_previewNodes = nullptr);
}
