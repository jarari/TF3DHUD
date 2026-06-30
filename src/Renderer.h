#pragma once

namespace RE
{
	class NiAVObject;
	namespace Interface3D
	{
		class Renderer;
	}
}

namespace TF3DHud::Renderer
{
	RE::Interface3D::Renderer* Get();
	bool IsConfigured();
	void Configure();
	void ConfigureLighting();
	void Hide();
	bool Enable();
	void Reset();
	void ClearPreviewRoot(bool a_disableRenderer = true);
	void AttachPreviewRoot(RE::NiAVObject& a_previewRoot);
	void ApplyOffscreenFraming(RE::NiAVObject& a_object);
}
