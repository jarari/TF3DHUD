#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

namespace RE
{
	class Actor;
	class BipedAnim;
	class NiAVObject;
	class PlayerCharacter;
}

namespace TF3DHud::PreviewRebuilder
{
	[[nodiscard]] RE::NiAVObject* GetSourceFaceNode(RE::Actor& a_sourceActor);
	[[nodiscard]] std::uint64_t BuildVisualSignature(RE::PlayerCharacter& a_player);
	[[nodiscard]] std::uint64_t BuildFaceCustomizationSignature(RE::PlayerCharacter& a_player);
	[[nodiscard]] std::uint64_t BuildEquipmentSignature(const RE::BipedAnim* a_biped);
	[[nodiscard]] std::uint64_t BuildMorphGeometrySignature(RE::PlayerCharacter& a_player);

	enum class DirtyFlag : std::uint32_t
	{
		kNone = 0,
		kEquipment = 1u << 0,
		kMorphGeometry = 1u << 1,
		kSkeletonAdjust = 1u << 2,
		kFaceStructure = 1u << 3,
		kBehaviorGraph = 1u << 4,
		kFaceCustomization = 1u << 5,
	};

	class Controller
	{
	public:
		void Request(DirtyFlag a_flag);
		void ObserveEquipment();
		void ObserveUpdate3DModel(std::uint16_t a_updateFlags, bool a_updateEditorDeadModel);
		void BeginRequestedAudits();
		void Reset();
		void ClearCommittedSignatures();

		[[nodiscard]] bool NeedsSkeletonBuild(std::uint64_t a_currentVisualSignature, bool a_hasPreviewRoot) const;
		[[nodiscard]] std::optional<std::uint64_t> TryResolveEquipmentSignature(
			std::uint64_t a_currentSignature,
			bool a_hasPendingModels);
		[[nodiscard]] std::optional<std::uint64_t> TryResolveMorphGeometrySignature(
			std::uint64_t a_currentSignature);
		[[nodiscard]] std::optional<std::uint64_t> TryResolveFaceCustomizationSignature(
			std::uint64_t a_currentSignature);
		void CommitSkeletonBuild(
			std::uint64_t a_equipmentSignature,
			std::uint64_t a_visualSignature,
			std::uint64_t a_morphGeometrySignature,
			std::uint64_t a_faceCustomizationSignature);
		void CommitEquipmentLayer(
			std::uint64_t a_equipmentSignature,
			std::uint64_t a_morphGeometrySignature);
		void CommitFaceCustomization(std::uint64_t a_faceCustomizationSignature);

		void RequestSkeletonAdjustment();
		[[nodiscard]] bool ConsumeSkeletonAdjustmentRequest();
		void RequestBehaviorGraphRefresh();
		[[nodiscard]] bool ConsumeBehaviorGraphRefreshRequest();

	private:
		[[nodiscard]] bool HasRequested(DirtyFlag a_flag) const;
		[[nodiscard]] bool TryAcceptStableChangedSignature(
			std::uint64_t a_currentSignature,
			std::uint64_t a_committedSignature,
			std::uint64_t& a_pendingSignature,
			std::uint32_t& a_pendingFrames,
			std::uint32_t a_requiredStableFrames);

		std::atomic_uint32_t requestedDirty_{ 0 };
		std::atomic_bool equipmentAuditActive_{ false };
		std::uint32_t equipmentAuditFrames_{ 0 };
		std::uint32_t equipmentQuietFrames_{ 0 };
		std::uint64_t pendingEquipmentSignature_{ 0 };
		std::uint32_t pendingEquipmentSignatureFrames_{ 0 };
		std::uint32_t morphAuditFrames_{ 0 };
		std::uint32_t morphQuietFrames_{ 0 };
		std::uint64_t pendingMorphSignature_{ 0 };
		std::uint32_t pendingMorphSignatureFrames_{ 0 };
		std::uint32_t faceCustomizationQuietFrames_{ 0 };
		std::uint64_t pendingFaceCustomizationSignature_{ 0 };
		std::uint32_t pendingFaceCustomizationSignatureFrames_{ 0 };
		std::uint64_t committedEquipmentSignature_{ 0 };
		std::uint64_t committedVisualSignature_{ 0 };
		std::uint64_t committedMorphGeometrySignature_{ 0 };
		std::uint64_t committedFaceCustomizationSignature_{ 0 };
		bool skeletonAdjustmentRequested_{ false };
		bool behaviorGraphRefreshRequested_{ false };
	};
}
