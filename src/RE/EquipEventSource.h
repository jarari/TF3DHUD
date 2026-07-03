#pragma once

#include "RE/B/BSTEvent.h"
#include "RE/T/TESEquipEvent.h"

namespace RE
{
	class EquipEventSource :
		public BSTEventSource<TESEquipEvent>
	{};
}
