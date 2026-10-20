# Materials

The renderer supports the GGX and GTR microfacet distributions. New materials use GGX. The Inspector exposes base color, HDR emissive radiance, roughness, model, workflow, dielectric reflectance, alpha mode, double-sided rendering, texture UV selectors, normal scale, occlusion strength, and per-slot UV transforms. Metallic appears only in the metallic-roughness workflow because specular-glossiness shading does not consume it. The GTR gamma control appears only for GTR materials. Dielectric reflectance controls appear only in the metallic-roughness workflow, with either index of refraction or explicit F0 shown according to the selected mode.

Primitive materials are stored on their mesh renderer. Imported models can store separate overrides by material slot. Scene saves preserve all material values, including boundary values and each texture transform. Applying an override creates an entity-owned renderer material instance so objects sharing an imported model retain independent settings.

Scene files that contain Legacy lighting data, or material records that omit the historic model field, are rejected with an error naming the material path and asking the author to reauthor it with GGX or GTR. Loading does not rewrite or reinterpret those materials.
