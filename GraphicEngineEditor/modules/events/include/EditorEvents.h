#pragma once

// ================================================================================
// EditorEvents.h
// ================================================================================
// Convenience umbrella header that includes all editor-specific event types.
// ================================================================================

#include "events/include/AddModelRequestedEvent.h"
#include "events/include/AssetImportRequestedEvent.h"
#include "events/include/AssetRegistryChangedEvent.h"
#include "events/include/AssetReimportRequestedEvent.h"
#include "events/include/AssetRenameRequestedEvent.h"
#include "events/include/AssetWorkflowFeedbackEvent.h"
#include "events/include/CameraPreviewChangedEvent.h"
#include "events/include/CameraPreviewOpenRequestedEvent.h"
#include "events/include/CameraSettingsChangedEvent.h"
#include "events/include/CommandExecuteRequestedEvent.h"
#include "events/include/DebugRenderToggledEvent.h"
#include "events/include/EcsComponentMutationRequestedEvent.h"
#include "events/include/EcsEntityCreateRequestedEvent.h"
#include "events/include/EcsEntityDeleteRequestedEvent.h"
#include "events/include/EcsEntityReparentRequestedEvent.h"
#include "events/include/EcsEntitySelectedEvent.h"
#include "events/include/EntityDeselectedEvent.h"
#include "events/include/MaterialAssetEditRequestedEvent.h"
#include "events/include/MaterialAssetSelectionRequestedEvent.h"
#include "events/include/MaterialChangedEvent.h"
#include "events/include/MaterialTextureAssignmentRequestedEvent.h"
#include "events/include/ModelReassignmentRequestedEvent.h"
#include "events/include/SceneAmbientLightChangedEvent.h"
#include "events/include/SceneChangedEvent.h"
#include "events/include/SceneFileActionRequestedEvent.h"
#include "events/include/SceneModifiedEvent.h"
#include "events/include/ViewportChangedEvent.h"
#include "events/include/ViewportClickedEvent.h"
#include "events/include/ViewportHoveredEvent.h"
#include "events/include/ViewportResizeRequestedEvent.h"
