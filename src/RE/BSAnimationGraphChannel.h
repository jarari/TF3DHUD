#pragma once

#include "RE/B/BSFixedString.h"
#include "RE/B/BSIntrusiveRefCounted.h"

#include <cstddef>
#include <cstdint>

namespace RE
{
	class BSAnimationGraphChannel :
		public BSIntrusiveRefCounted
	{
	public:
		virtual ~BSAnimationGraphChannel() = default;
		virtual void PollChannelUpdate(bool a_shouldApplyAdjustments) = 0;
		virtual void Reset() = 0;

		BSFixedString variableName;  // 10
		std::uint32_t unk18{ 0 };    // 18
	};
	static_assert(offsetof(BSAnimationGraphChannel, variableName) == 0x10);
}
