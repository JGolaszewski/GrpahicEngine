# Asset and Material Workflow

## Projects and scenes

The editor remembers the selected project folder under the current user's local application data. Imported assets and scene documents belong to that project; compiled shaders remain under the engine build output. Scene documents store project-relative paths for the scene file and stable GUIDs for model and texture references.

Changing projects goes through the unsaved-scene prompt. Scene loading stages the new scene before replacing the current one, and a failed load leaves the current scene active.

## Import and identity

The Assets panel imports glTF/GLB models and PNG/JPEG images. Each source file has an adjacent `.meta` file containing its stable GUID. The editor scans those sidecars when opening a project, so assets remain listed after restarting. Reimport keeps the GUID and advances the imported generation; live scene instances are refreshed after successful resource creation.

Use the Assets panel's reimport action after changing a source file. Do not copy a `.meta` sidecar to give another asset an identity: duplicate GUIDs are rejected and reported.

## Models and materials

Models can be added from the Hierarchy panel. To replace the model on an existing object, select a model in Assets and use **Replace selected object model**. Replacement retains the object ID, transform, render flags, compatible material properties, and texture assignments; undo and redo retain the old and new entities.

The Inspector selects a model material slot before editing it. Base-color and normal maps can be selected from imported textures or imported as part of assignment. Clearing a manual assignment restores the imported material map when one exists. Scene files store texture GUIDs per material slot, not source paths or GPU handles. Missing optional texture references use renderer fallback maps and are shown in the Inspector.

## Undo and saved state

Entity membership, transforms, material properties, texture references, and model replacement use command history. Commands keep imported assets intact and retain the entity/material references needed for undo. Scene dirty state is tracked separately from history, and opening another scene or project clears history so commands cannot target a different scene.
