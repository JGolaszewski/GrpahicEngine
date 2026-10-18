# Editor workflows

## Sandbox project

The Sandbox project provides a repeatable editor scene for trying the common
content workflows. It includes an imported sample model with a texture, all
built-in primitives, authored materials, directional/point/spot lights, and a
camera.

1. Run the editor and choose **File > Open Project Folder...**.
2. Select the repository's `SandboxApp/Project` folder.
3. Choose **File > Reset Sandbox** to load the bundled known-good scene.
4. Inspect and edit the scene to exercise the editor workflows.
5. Choose **File > Reset Sandbox** again to restore the bundled scene.

The reset command reads `Sandbox.template.scene.json`. The template is kept
separate so experiments can be discarded without reconstructing the test scene.
The project assets and scene template are included in source control. The unit
test also saves the instantiated scene to a temporary project and reloads it to
check that the content remains restorable.

Use **File > New** to start an empty scene. **File > Open...** loads a scene from
the current project. **File > Save** writes the active scene to its current
path; for a new scene, it opens the save dialog. **File > Save As...** always
opens that dialog. Scene files must be saved inside the active project.
Scene serialization also preserves an empty Mesh component before a model or
primitive is assigned.

The scene contains one imported model with a checker texture, cube/sphere/plane/
cone primitives with distinct material settings, three light types, and a
camera. It is intended for manual checks of selection, transform editing,
material changes, model/texture assignment, primitive creation, and scene save
behavior. Automated coverage for loading, saving, and restoring this content
runs in `SandboxSceneTests`.
