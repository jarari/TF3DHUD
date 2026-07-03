#pragma once

#include <cstddef>
#include <cstdint>

namespace RE
{
	class BSAnimationUpdateData
	{
	public:
		float deltaTime{ 0.0F };
		std::uint32_t pad04{ 0 };
		void* unk08{ nullptr };
		void* postUpdateFunctor{ nullptr };
		std::uint32_t flags18{ 0x01000000 };
		std::uint16_t flags1C{ 0x0101 };
		std::uint16_t pad1E{ 0 };
	};
	static_assert(offsetof(BSAnimationUpdateData, flags18) == 0x18);
	static_assert(offsetof(BSAnimationUpdateData, flags1C) == 0x1C);
	static_assert(sizeof(BSAnimationUpdateData) == 0x20);
}
