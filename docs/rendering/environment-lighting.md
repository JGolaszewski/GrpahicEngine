# Environment lighting

`ViewportBinding::Environment` selects the environment source only when a scene has no `Ecs::Skybox` component. The
default is `EnvironmentPreset::Disabled`; assigning `EnvironmentPreset::Studio` explicitly enables the renderer's
built-in studio source. `Environment.Intensity` scales indirect diffuse and specular terms. If a scene-root Skybox is
present, the renderer uses its selected background and enables environment lighting only when HDR mode has a valid
assigned panorama and `EnableEnvironmentLighting` is on. Its `Intensity` controls that HDR-derived lighting. The
component supports an HDR equirectangular panorama, a 2:1 PNG equirectangular panorama, or a solid linear RGB color. PNG
and solid-color modes affect only the background. Missing or disabled HDRI does not enable Studio lighting. Background
visibility is independent of the lighting toggle.

```cpp
Engine::ViewportBinding view;
view.Environment.Preset = Engine::EnvironmentPreset::Studio;
view.Environment.Intensity = 1.0F;
```

The same per-view setting can be passed to `RenderSystem::RenderFinal` or `BeginFinalRender` for final output when the
scene has no Skybox component. Scene
skyboxes are resolved independently for each editor, preview, or final view. `ViewportBinding::ShowEnvironmentBackground`
controls background visibility; the editor uses its **Show skybox in editor viewport** preference, while preview and
final views default to visible.

The renderer generates its built-in Studio resources once when the scene renderer initializes. Each prepared HDR asset
provides a linear panorama texture plus a cosine-weighted irradiance map and roughness-prefiltered radiance mip chain.
The selected maps bind at `t7/s7` and `t8/s8`; the shared split-sum BRDF LUT binds at `t9/s9`, and the shadow map stays
at `t6/s6`. The background shader samples the panorama at `t0/s0` and reconstructs a world ray from the per-view camera
basis and field of view. PNG panorama textures use sRGB sampling; solid-color mode supplies linear RGB directly. Its
fullscreen draw writes only color, leaves depth and auxiliary outputs at their clear values, and runs before scene
geometry so opaque geometry covers it.

The lights constant buffer carries an explicit `EnvironmentEnabled` flag and intensity. The CPU and shader pack the
light count, enabled flag, intensity, and ambient-light intensity into the same 16-byte block. Without a Skybox
component, the per-view `EnvironmentPreset` controls Studio environment lighting. With a Skybox component, only a valid,
enabled HDR panorama contributes environment lighting; the Studio environment is not used as an implicit fallback.

Environment GPU resources are prepared outside frame recording and cached by asset identity and generation. If panorama
loading or GPU setup fails, the background remains at the viewport clear color and a diagnostic identifies the asset;
environment lighting stays disabled for that Skybox. Scene bindings fail the forward pass if explicitly requested Studio
resources cannot be created.
