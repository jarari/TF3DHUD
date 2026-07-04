#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace TF3DHud::Previewer
{
	struct FaceGenSliderDebugInfo
	{
		std::string category;
		std::uint32_t id{ 0 };
		std::string liveValue;
		std::string previewValue;
		bool hasLive{ false };
		bool hasPreview{ false };
	};

	struct FaceGenDebugSnapshot
	{
		std::vector<FaceGenSliderDebugInfo> sliders;
	};

	void Update(float a_deltaTime);
	void CommitRenderState();
	void Reset();
	void MarkEquipmentDirty();
	void ObserveUpdate3DModel(std::uint16_t a_updateFlags, bool a_updateEditorDeadModel);
	void ApplyConfigChanges();
	void ReloadConfig();
	void SuspendForLooksMenu();
	void ResumeAfterLooksMenu();
	void LogRightHandBoneHierarchy();
	FaceGenDebugSnapshot GetFaceGenDebugSnapshot();
}
