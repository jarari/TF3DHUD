#pragma once

#include "RE/B/BSFixedString.h"
#include "RE/N/NiExtraData.h"
#include "RE/N/NiNode.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace RE
{
	struct BSFaceGenExpression
	{
		float expression[54];
	};
	static_assert(sizeof(BSFaceGenExpression) == 0xD8);

	namespace FaceEmotionalIdles
	{
		struct InstanceData
		{
			std::uint32_t pad00;
			std::uint32_t handle;
			std::uint32_t pad08;
			float blinkTimer;
			float lidFollowEyes;
			std::uint64_t pad18;
			std::uint64_t pad20;
			std::uint32_t unk28;
			std::uint32_t pad2C;
			BSFixedString archeType;
		};
		static_assert(sizeof(InstanceData) == 0x38);
	}

	class BSFaceGenAnimationData :
		public NiExtraData
	{
	public:
		BSFaceGenExpression currentExpression;
		BSFaceGenExpression modifierExpression;
		BSFaceGenExpression baseExpression;
		std::array<std::byte, 0x38> emotionalIdleData;
		bool morphsDirty;
		bool forceMorphUpdate;
		std::uint8_t pad2DA;
		bool disableMorphUpdate;
		std::uint32_t morphUpdateState;
		std::uint32_t unk2E0;
		std::uint32_t pad2E4;
	};
	static_assert(sizeof(BSFaceGenAnimationData) == 0x2E8);
	static_assert(offsetof(BSFaceGenAnimationData, currentExpression) == 0x18);
	static_assert(offsetof(BSFaceGenAnimationData, modifierExpression) == 0xF0);
	static_assert(offsetof(BSFaceGenAnimationData, baseExpression) == 0x1C8);
	static_assert(offsetof(BSFaceGenAnimationData, emotionalIdleData) == 0x2A0);
	static_assert(offsetof(BSFaceGenAnimationData, morphsDirty) == 0x2D8);
	static_assert(offsetof(BSFaceGenAnimationData, forceMorphUpdate) == 0x2D9);
	static_assert(offsetof(BSFaceGenAnimationData, disableMorphUpdate) == 0x2DB);
	static_assert(offsetof(BSFaceGenAnimationData, morphUpdateState) == 0x2DC);
	static_assert(offsetof(BSFaceGenAnimationData, unk2E0) == 0x2E0);

	class BSFaceGenNiNode :
		public NiNode
	{
	public:
		std::array<std::byte, 0x30> faceGenData;
		BSFaceGenAnimationData* animationData;
		float updateTime;
		std::uint16_t faceGenFlags;
	};
	static_assert(offsetof(BSFaceGenNiNode, animationData) == 0x170);
	static_assert(offsetof(BSFaceGenNiNode, faceGenFlags) == 0x17C);
}
