#pragma once

namespace TF3DHud::Previewer
{
	void Update(float a_deltaTime);
	void CommitRenderState();
	void Reset();
	void MarkEquipmentDirty();
	void ApplyConfigChanges();
	void ReloadConfig();
	void SuspendForLooksMenu();
	void ResumeAfterLooksMenu();
}
