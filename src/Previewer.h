#pragma once

#include "RE/B/BSFixedString.h"

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

	struct FaceGenHeadPartDebugInfo
	{
		std::uint32_t formID{ 0 };
		std::uintptr_t ptr{ 0 };
		std::string editorID;
		std::string fullName;
		std::string type;
		std::string model;
	};

	struct FaceGenGeometryDebugInfo
	{
		std::uintptr_t ptr{ 0 };
		std::string name;
		std::uintptr_t parentPtr{ 0 };
		std::string parentName;
	};

	struct HairSkinBoneDebugInfo
	{
		std::string source;
		std::string headPart;
		std::string geometry;
		std::uint32_t index{ 0 };
		std::string boneName;
		std::uintptr_t bonePtr{ 0 };
		std::string parentName;
		std::uintptr_t parentPtr{ 0 };
		std::string local;
		std::string world;
	};

	struct FaceGenDebugSnapshot
	{
		std::vector<FaceGenHeadPartDebugInfo> headParts;
		std::vector<FaceGenGeometryDebugInfo> geometries;
		std::vector<HairSkinBoneDebugInfo> hairSkinBones;
		std::vector<FaceGenSliderDebugInfo> sliders;
	};

	void Update(float a_deltaTime);
	void CommitRenderState();
	void Reset();
	void MarkEquipmentDirty();
	void ObserveUpdate3DModel(std::uint16_t a_updateFlags, bool a_updateEditorDeadModel);
	void HandleAnimationObjectEvent(const RE::BSFixedString& a_tag, const RE::BSFixedString& a_payload);
	void ClearAnimationObjects();
	void ApplyConfigChanges();
	void ReloadConfig();
	void SuspendForLooksMenu();
	void ResumeAfterLooksMenu();
	FaceGenDebugSnapshot GetFaceGenDebugSnapshot();
	void LogHairSkinBoneDiagnostics();
}
