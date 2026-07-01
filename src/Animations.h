#pragma once

namespace RE
{
	class NiAVObject;
	class PlayerCharacter;
}

namespace TF3DHud::Animations
{
	void Reset();
	void Update(RE::PlayerCharacter& a_player, RE::NiAVObject& a_previewRoot, float a_deltaTime);
}
