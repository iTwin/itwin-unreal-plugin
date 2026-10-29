/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSelectionOutlineRenderer.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>

/// Screen-space outline of the selected iModel Elements.
///
/// The selected geometry is expected to be rendered in the CustomDepth pass with the custom stencil value
/// #StencilValue (see FITwinTileSelectionOutline in ITwinRuntime). A scene view extension then draws, just
/// before post-processing, an outline wherever the stencil mask has an edge.
///
/// Rendering is always enabled for registered selection sources, with a 2-pixel opaque teal outline
/// (display-space color 0.1, 0.8, 0.65, 1.0) that also shows occluded parts.
namespace ITwin::SelectionOutline
{
	/// Custom stencil value written by the geometry to outline.
	constexpr int32 StencilValue = 0xA5;

	/// Whether selection outlines are enabled. Always true. Game thread only.
	ITWINRENDERING_API bool IsEnabled();

	/// Notify that some geometry is (resp. no longer) feeding the custom stencil mask: the outline pass is
	/// only active when at least one source is registered. Game thread only.
	ITWINRENDERING_API void AddActiveSource();
	ITWINRENDERING_API void RemoveActiveSource();
	/// Number of currently registered sources (diagnostics and tests).
	ITWINRENDERING_API int32 GetActiveSourceCount();
}
