#pragma once

namespace TF3DHud::Previewer
{
	void Update(float a_deltaTime);
	void Reset();
	void SuspendForLooksMenu();
	void ResumeAfterLooksMenu();
}
