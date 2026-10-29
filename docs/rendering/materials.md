# Materials

The renderer supports GGX, GTR, and PRISM-generated NDFs. New materials use GGX. The Inspector exposes base color, HDR emissive radiance, roughness, model, workflow, dielectric reflectance, alpha mode, double-sided rendering, texture UV selectors, normal scale, occlusion strength, and per-slot UV transforms. Metallic appears only in the metallic-roughness workflow because specular-glossiness shading does not consume it. The GTR gamma control appears only for GTR materials. Dielectric reflectance controls appear only in the metallic-roughness workflow, with either index of refraction or explicit F0 shown according to the selected mode. PRISM payloads and provenance are serialized in material assets and scenes; see [Generated PRISM NDFs](generated-ndf.md).

Primitive materials are stored on their mesh renderer. Imported models can store separate overrides by material slot. Scene saves preserve all material values, including boundary values and each texture transform. Applying an override creates an entity-owned renderer material instance so objects sharing an imported model retain independent settings.

Scene files that contain Legacy lighting data, or material records that omit the historic model field, are rejected with an error naming the material path and asking the author to reauthor it with GGX or GTR. Loading does not rewrite or reinterpret those materials.

## Texture anisotropic filtering

Engine Settings sets the maximum anisotropy to 1x, 2x, 4x, 8x, or 16x (8x by default). Each material texture slot can follow the global cap or request a fixed supported level. The renderer clamps a slot request to the engine maximum. A resulting 1x level keeps the imported minification, magnification, and mip filters; higher levels use anisotropic sampling. Imported texture address modes and generated mip levels are preserved, and changes apply to loaded materials at the next frame boundary.
