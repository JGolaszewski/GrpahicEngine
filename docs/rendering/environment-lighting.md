# Environment lighting

Each `ViewportBinding` can select an environment source for that view through its `Environment` member. The default is
`EnvironmentPreset::Disabled`; assigning `EnvironmentPreset::Studio` enables the renderer's built-in studio source.
`Environment.Intensity` scales the indirect diffuse and specular terms. The renderer clamps it to a finite,
non-negative value.

```cpp
Engine::ViewportBinding view;
view.Environment.Preset = Engine::EnvironmentPreset::Studio;
view.Environment.Intensity = 1.0F;
```

The same setting can be passed to `RenderSystem::RenderFinal` or `BeginFinalRender` when producing a final output.
Environment selection is per view, so two views of the same scene can enable or disable indirect lighting independently.

The renderer generates its built-in Studio environment resources once when the scene renderer initializes. It creates a
lat-long irradiance texture, a roughness-prefiltered lat-long radiance mip chain, and a split-sum BRDF integration LUT.
They are bound with the scene resources at shader registers `t7/s7`, `t8/s8`, and `t9/s9`; the shadow map remains at
`t6/s6`. The shader reads an explicit `EnvironmentEnabled` field from the lights constant buffer rather than padding.
The CPU and shader both pack the light count, enabled flag, intensity, and final padding into the trailing 16 bytes of
that buffer.

Disabled views do not sample environment lighting and do not need an environment asset assigned. The engine-owned
Studio resources remain bound because both shader variants share the same scene binding layout. If GPU texture,
sampler, upload-command-list, or binding creation fails, the renderer logs a setup diagnostic; scene binding creation
then fails with a render-pass failure instead of silently treating the environment as available.

The current source is a deterministic built-in Studio preset. Loading authored HDR environment files and selecting
them from the editor are not implemented yet.
