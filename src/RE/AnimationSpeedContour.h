#pragma once

#include "RE/B/BSIntrusiveRefCounted.h"

namespace RE
{
	class AnimationSpeedContour :
		public BSIntrusiveRefCounted
	{
	public:
		virtual ~AnimationSpeedContour() = default;
	};

	namespace AnimationSpeedInformationTypes
	{
		struct RequestedSpeed
		{
			float value;
		};
		static_assert(sizeof(RequestedSpeed) == 0x4);

		struct GraphSpeedInput
		{
			float speed;
		};
		static_assert(sizeof(GraphSpeedInput) == 0x4);
	}
}
