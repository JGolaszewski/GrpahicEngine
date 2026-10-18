# Editor guide

This guide describes the editor controls that are implemented in the current
checkout. The editor opens with a default scene containing a cube and a spot
light. Its dock layout includes the **Viewport**, **Scene Hierarchy**,
**Inspector**, **Assets**, **Statistics**, and **Engine Console** panels.

## Viewport navigation

The **Viewport** shows the active scene through an editor-owned navigation
camera. To look around, move the pointer over the viewport, hold the right mouse
button, and move the mouse. While the viewport is hovered, use these keys to
move:

| Input | Movement |
| --- | --- |
| `W` / `S` | Forward / backward, following the camera direction |
| `A` / `D` | Left / right, following the camera direction |
| `E` / `Q` | Up / down in world space |
| `Left Shift` | Move faster while held |

Normal movement speed is 5 world units per second; holding `Left Shift` raises
it to 15. Mouse look uses relative mouse motion while the right button is held.
When the pointer leaves the viewport, held movement keys and mouse look are
cleared. The viewport target resizes with its panel.

The viewport camera is editor state: it is separate from scene camera entities
and is not shown in the hierarchy. The current UI does not expose controls for
changing its field of view or clipping planes. Viewport clicks are forwarded as
viewport events, but clicking a mesh does not currently select that entity;
select entities in **Scene Hierarchy** instead.

## Managing the scene

Use **Scene Hierarchy** to inspect and edit the active scene:

- Select an entity by clicking its row. The **Inspector** then shows its name,
  transform, visibility, and any mesh renderer, spot light, or camera
  components.
- Use the search field to filter by entity name or component type. Matching
  descendants keep their parent rows visible.
- Use **+ Create Entity** or the hierarchy background context menu to create an
  empty entity, mesh entity, light, or camera under the scene root.
- Right-click an entity to create a child entity or one of the child mesh,
  light, and camera presets, or to delete a non-root entity.
- Drag a non-root entity onto another hierarchy row to reparent it. The scene
  root cannot be deleted.
- In the Inspector, edit local position, pitch/yaw/roll, and scale under
  **Transform**. Scale can be locked to preserve proportions; horizontal and
  vertical flips are also available.
- A **Mesh** component can use an imported model or a cube, sphere, plane, or
  cone primitive. Its controls include visibility, cast/receive shadows,
  wireframe, depth testing, and material assignment. Use **Add Component** to
  add Mesh, Light, or Camera components; component headers remove them.
- A **Light** component supports directional, point, and spot types. The
  Inspector shows direction, range, and cone controls only when they apply,
  along with color, intensity, shadow bias, and cast-shadows controls.

Component and hierarchy edits update the active scene immediately. Entity
selection is separate from the viewport navigation camera, and selecting a
camera entity also opens its viewport picture-in-picture preview.

Use **File > Open Project Folder...** to select a project and refresh its asset
registry. Use **File > Open...** to load a `.scene.json` file from the active
project, **New** to start an empty scene, and **Save** or **Save As...** to
write the active scene. `Ctrl+N`, `Ctrl+O`, `Ctrl+S`, and `Ctrl+Shift+S` are
the corresponding shortcuts. Scene files must be saved inside the active
project. **File > Reset Sandbox** loads the active project's
`Sandbox.template.scene.json` and sets `Sandbox.scene.json` as the active save
path. See [Editor workflows](editor-workflows.md) for the repeatable Sandbox
setup.

## Scene cameras and previews

Scene cameras are authored entities with a transform and projection settings.
Select a camera in the hierarchy to see its live, aspect-preserving picture in
picture (PiP) in the viewport. The preview follows scene edits while the
editor-owned navigation view stays visible. Select another camera to switch the
PiP; select a non-camera entity or delete the selected camera to hide it.

The camera controls are in the Inspector's **Camera** section:

- **Field of view**, **Near plane**, and **Far plane** set perspective
  projection and clipping.
- **Output resolution** offers 720p, 1080p, 1440p, 4K UHD, and custom positive
  width and height values.
- **Priority** and **Active camera** are scene camera properties used by
  rendering systems.

Click the PiP image to open **Camera Preview**. Use **Refresh** to render it
again, **Fit** to fit it to the viewer, and **1:1** to display output pixels at
native scale. Drag the image to pan and hover over it while scrolling to zoom.
The viewer can save a PNG or an EXR; EXR output lets you choose from the
available render AOVs. See [Scene cameras](rendering/cameras.md) for preview
resolution and rendering details.

## Editor settings

Open **Window > Settings** for runtime, rendering, and backend controls.

| Section | Setting | Effect |
| --- | --- | --- |
| Runtime | Pause simulation | Pauses or resumes application simulation. |
| Runtime | Frame limit (Hz) | Sets the engine frame limiter from 0 to 240 Hz; 0 disables it. VSync may still cap presentation. |
| Rendering | Show light gizmos | Toggles light debug geometry in the viewport. |
| Rendering | Show camera frustums | Toggles visible scene camera frustums. |
| Rendering | Show wireframe plane | Toggles the debug wireframe plane. |
| Rendering | Camera PiP display scale | Sets the on-screen preview size from 10% to 80% of the viewport, subject to the preview's 480-pixel maximum edge. |
| Rendering | Camera PiP render quality | Sets preview render resolution from 5% to 100% of the camera output dimensions. |
| Rendering | Shadow map resolution | Sets shadow map resolution between 128 and 8192 pixels. Higher values use more GPU memory and bandwidth. |
| Rendering | Shadow-casting lights | Sets how many shadow maps the renderer produces per scene, up to its supported maximum. |
| Backend | VSync | Enables or disables vertical sync when the active backend supports control; changes apply on the next present. |

The **Graphics Backend** section also reports the active backend, device readiness, and
adapter vendor/device IDs. Backend and adapter selection require restarting the
editor; selection controls are not provided in this panel. Settings marked as
editor preferences persist locally in
`%LOCALAPPDATA%/GraphicEngine/editor-settings.txt`. Runtime and renderer values
are applied to the current application session.

## Current editor limits

- The viewport supports camera navigation, not entity picking or transform
  gizmos. Use the hierarchy and Inspector for selection and edits.
- The navigation camera has no visible settings panel. Authored scene cameras
  have their own Inspector controls and can be previewed or exported.
- Panel arrangement is dockable, but the editor does not expose a layout reset
  command.
