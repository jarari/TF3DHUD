#pragma once

#include <cstdint>

namespace RE
{
	class Actor;
}

namespace TF3DHud::Previewer
{
	void Update(float a_deltaTime);
	void Reset();
	bool IsPreviewActor(const RE::Actor* a_actor);
	bool IsBuildActive();
}
