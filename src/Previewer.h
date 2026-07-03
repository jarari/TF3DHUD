#pragma once

#include <cstdint>

namespace TF3DHud::Previewer
{
	void Update(float a_deltaTime);
	void CommitRenderState();
	void Reset();
	void MarkEquipmentDirty();
	void ObserveUpdate3DModel(std::uint16_t a_updateFlags, bool a_updateEditorDeadModel);
	void ApplyConfigChanges();
	void ReloadConfig();
	void SuspendForLooksMenu();
	void ResumeAfterLooksMenu();
}
