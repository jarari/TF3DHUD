#pragma once

#include "RE/B/BSIntrusiveRefCounted.h"

#include <cstddef>

namespace RE
{
	class BShkbAnimationGraph :
		public BSIntrusiveRefCounted
	{
	public:
		virtual ~BShkbAnimationGraph() = default;
	};
	static_assert(sizeof(BShkbAnimationGraph) == 0x10);
}
