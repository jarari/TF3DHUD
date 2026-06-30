#pragma once

namespace TF3DHud::Previewer
{
	void Update(float a_deltaTime);
	void Reset();
	void MarkEquipmentDirty();
	void SuspendForLooksMenu();
	void ResumeAfterLooksMenu();
}
