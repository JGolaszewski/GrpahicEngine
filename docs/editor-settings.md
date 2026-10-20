# Editor settings

The editor stores user-wide settings in `%LOCALAPPDATA%/GraphicEngine/editor-settings.json`.
The file is created when a setting is changed. A missing file uses the built-in defaults.

Settings use versioned JSON categories:

- `runtime`: frame limiter frequency in hertz (`0` disables the limiter). Simulation pause is session state and resets on launch.
- `rendering`: editor gizmos, wireframe plane, shadow map resolution, shadow caster limit, and VSync.
- `camera_preview`: display scale and render quality, stored as fractions from `0.0` to `1.0`.

Invalid or unreadable settings are reported in the editor log and defaults are used so the editor can still launch.
Out-of-range numeric values are clamped to the supported editor ranges. Existing `editor-settings.txt` preferences
are read once and migrated to JSON when possible; the old file is left untouched.

## Project and scene selection

Use **File > Open Project Folder...** to select a project folder. The editor
refreshes its asset registry before remembering the new project. Cancelling
leaves the current project active; an invalid folder or registry error is
reported in the editor log and leaves the previous project active.

Use **File > Open...** to load a `.scene.json` file from the active project.
After a successful load, the scene hierarchy and Inspector refresh to show the
new scene. Cancelling or selecting an unreadable scene keeps the current scene
active. The scene document path is updated only after activation succeeds.
**File > New** creates an empty scene; **Save** writes to the active scene path
or opens the save dialog when the scene has no path, and **Save As...** always
opens the dialog. Scene files must remain inside the active project. **Reset
Sandbox** loads `Sandbox.template.scene.json` and sets `Sandbox.scene.json` as
the active save path.

## Asset rename and assignment

Use **Create material** in the Assets panel to create a reusable default
material. Materials appear alongside models and textures and can be renamed
with **Rename selected**; the editor moves the material document and metadata
sidecar together while preserving its GUID. Material properties and shared
material GUID references are stored in project assets and scene documents,
respectively. The Inspector can assign an asset material to a mesh or edit
material properties. General Material Properties include the base-color factor,
color and normal texture slots, their UV set, offset, scale, and rotation, plus
normal-map strength, metallic, roughness, emissive color, workflow, and
dielectric specular controls.
MDF Parameters list every supported distribution and expose its specific
parameters. Alpha mode, mask cutoff, and double-sided rendering are also editable.

Select a model or texture in the Assets panel and use **Rename selected** to change its filename. The editor moves the
asset and its adjacent metadata sidecar together, preserving the asset GUID used by saved scene references.

Drag a model from the Assets panel onto **Drop a model asset here** in a mesh component to assign it. Drag a texture
onto a material's **Base color** or **Normal** slot to assign that texture. Incompatible or stale drops are rejected,
leave the component unchanged, and display a message in the Inspector.
