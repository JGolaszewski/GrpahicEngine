# Lighting and material math analysis

**Scope:** local source review of glTF mesh/material import, texture mip generation and color spaces, material packing, the D3D11 forward shaders, shared BRDF equations, scene light conversion, and PRISM NDF evaluation. No issue or project item was created. No product code was changed. This is a static review; no shader build, test, or rendered-scene reproduction was run.

## Conclusions

1. **Confirmed: double-sided materials shade their backfaces with the front-facing normal.** The renderer disables culling when `DoubleSided` is set, but neither forward pixel shader reads `SV_IsFrontFace` or flips the geometric/shading normal. On a back-facing fragment, `N·V` is negative and is saturated to zero in `BuildBrdfQuery`; all BRDF component functions then return zero. Direct lighting on the visible back side therefore disappears (apart from ambient/emissive). Normal maps are also transformed through the unflipped tangent frame. This affects every BRDF model and both textured and untextured materials.

2. **Confirmed: untextured specular-glossiness roughness is inverted.** Import stores `1-glossiness` as `MaterialProperties.Roughness`; `BuildMaterialGpu` packs that unchanged. The untextured shader then computes `1-SurfaceParams.x` for specular-glossiness, yielding glossiness as roughness. Example: glossiness `0.8` should produce roughness `0.2` and alpha `0.04`, but this path produces roughness `0.8` and alpha `0.64`. The textured shader instead computes `1-(1-roughnessFactor)*glossinessSample`, which yields the intended `0.2` with a white fallback texture. This makes otherwise equivalent textured and untextured materials render markedly different direct highlights and environment mip levels for every BRDF model.

3. **The shared direct-light vector convention is internally consistent for ordinary nondegenerate lights.** Surface-to-camera is `V = cameraPosition - worldPosition`; surface-to-light is `L = lightPosition - worldPosition` for positional lights and `L = -uploadedDirection` for directional lights; `H = normalize(V + L)`. The BRDF uses `N·L` as the cosine in the rendering integral. The CPU uploads the light's local emission direction transformed into world space, matching the shader negation for directional illumination and the spot-cone test.

4. **Confirmed: environment specular is not model-matched for most BRDFs, and even GGX direct/IBL use different visibility terms.** Direct lighting dispatches all eight classic distributions and PRISM, but the environment branch dispatches only PRISM specially. Other models use the GGX-prefiltered reflection and GGX integration LUT. This mismatches each non-GGX distribution (GTR gamma 2 matches only the NDF). In addition, direct lighting uses correlated Smith GGX visibility while the LUT is integrated with separable Smith `G1(V)*G1(L)`, so even GGX direct and split-sum environment lighting are not the same BRDF. Ashikhmin-Shirley also loses its authored anisotropic axis response in environment reflections.

5. **The standalone NDF equations and projected-disk PRISM estimator remain plausible and have useful reference coverage, but full GPU correctness is unproven.** Existing CPU tests integrate several NDFs, while the GPU render coverage is concentrated on PRISM and selected generic scenes. I did not execute tests in this review. The normal-incidence IOR/F0 and Fresnel equations themselves are coherent; that does not validate the workflow dispatch gaps above.

6. **Confirmed validation gap: the CPU BRDF reference is not numerically equivalent to the production shader's diffuse term.** `Brdf.h` uses the constant factor `(1-F0)` in its diffuse result, while HLSL uses angle-dependent `(1-F(V,H))`. Existing CPU white-furnace/reciprocity results therefore do not exactly validate the direct-light shader's diffuse response, especially at grazing angles. The NDF parity tests cover PRISM distribution values but do not close this general BRDF component gap.

7. **Additional concern: PRISM Inspector preview and GPU horizon fade use different inputs.** The shader fades the signed residual with `smoothstep(0, 0.1, saturate(N·V))`; `EvaluatePrismNdfPreview` fades it with `smoothstep(0, 0.1, sqrt(1-r²))`, where `r` is the displayed half-vector disk coordinate. That is a different quantity, so the preview cannot visually predict the GPU's horizon-fade option for a fixed view. This is a preview parity issue, not evidence that the direct-light shader's `N·V` convention is wrong.

8. **Confirmed: specular-glossiness diffuse applies a second Fresnel remainder after the workflow has already partitioned it.** Both shaders pre-scale authored diffuse by `1-max(F0)`, then the shared direct BRDF multiplies that color by `(1-F(V,H))` again. Split-sum and PRISM environment diffuse similarly multiply the already pre-scaled color by `(1-Frough(N,V))`. At normal incidence with scalar F0 `0.5`, the multiplier is `0.25` rather than `0.5`; at grazing it is smaller still. The [KHR specular-glossiness workflow](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Archived/KHR_materials_pbrSpecularGlossiness/README.md) defines the diffuse input after specular energy partition while allowing implementation BRDF choices. This is therefore an extra attenuation in this renderer's chosen BRDF composition, not a claim that all valid BRDFs must use one exact angular formula. It affects specular-glossiness diffuse under direct and environment lighting.

9. **Confirmed: mip generation corrupts RGBA data textures by treating alpha as color coverage.** For every non-normal texture, `TextureMipGenerator::Build` multiplies RGB by alpha and divides by averaged alpha. This is appropriate for filtering straight-alpha color, but specular-glossiness uses alpha as glossiness and RGB as specular reflectance; metallic-roughness and occlusion alpha are not RGB coverage either. These maps have their RGB values biased toward high-alpha texels in lower mips. Emissive RGB can be biased too when its unused alpha is not opaque. This affects distant/minified textured materials.

10. **Confirmed glTF import mismatch for missing normals and tangents.** The importer sums adjacent triangle face normals at shared vertices, producing smooth normals when `NORMAL` is absent; glTF requires generated flat normals. If source `TANGENT` exists while `NORMAL` is absent, it keeps that tangent even though glTF requires supplied tangents to be ignored. When tangents are absent, the custom accumulator is not MikkTSpace. This can change shading and normal-map results on imported geometry, particularly hard edges and UV seams.

11. **Confirmed: normal-map UV transforms change sampled coordinates without changing the tangent frame.** The importer accepts per-texture scale/rotation and applies the transformed UV in the pixel shader, but the TBN basis remains the original vertex tangent basis (or one generated from untransformed UVs). A UV rotation or mirrored axis changes the directions represented by map X/Y, but neither the frame nor sampled normal XY is adjusted, causing rotated or flipped lighting detail. Positive UV scaling changes pattern frequency; the appropriate slope response depends on the authored map. Other texture slots use the transform consistently for lookup. Khronos documents `KHR_texture_transform` as an affine transform on UVs and the glTF mesh rule associates tangent generation with the normal texture's coordinates ([extension](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_texture_transform/README.md), [glTF specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.pdf)).

12. **Confirmed: Ashikhmin-Shirley assumes an orthonormal tangent frame, but normal mapping breaks that assumption.** The textured shader builds `tangent` and `bitangent` around the geometric normal, changes `N` with the normal map, and then passes the old tangent/bitangent with the new normal into `EvaluateAshikhminShirleyBrdfComponents`. That evaluator dots the half-vector against all three vectors as if they formed one orthonormal basis. Once the map tilts `N`, `T` and `B` are not generally perpendicular to `N`; the evaluated anisotropic NDF coordinates are mathematically inconsistent. The PRISM branch explicitly reprojects its tangent around the mapped `N`, so this is a specific Ashikhmin + normal-map interaction.

13. **Material feature boundary: the importer does not implement all glTF material extensions.** It explicitly supports core metallic-roughness plus archived specular-glossiness and texture transforms. Other optional material extensions, including `KHR_materials_unlit`, are warned as ignored and fall back to the core PBR properties; required unsupported extensions are rejected. That fallback may be valid glTF behavior when an extension is optional, but the resulting image will not match the intended unlit/transmission/clearcoat/sheen/etc. material. Khronos defines unlit as independent of lighting and advises capable clients not to upgrade it to fully shaded PBR ([KHR_materials_unlit](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_unlit/README.md)).


## Vector and light data trace

### Vertex and material normals

- `color_vs.hlsl` transforms positions by the world matrix, normals by the supplied inverse-transpose normal matrix, and tangents by the world linear transform. Tangent handedness includes the sign of the world determinant, which is the expected correction for mirrored transforms.
- Both pixel shaders normalize the interpolated normal and Gram-Schmidt-project the tangent against it. The bitangent is `cross(N,T) * handedness`.
- The textured shader decodes a tangent-space normal from `[0,1]` to `[-1,1]`, scales only XY, and transforms it as `T*x + B*y + N*z`, followed by normalization. This is consistent with the stored tangent basis for front-facing surfaces.
- **Backface failure proof:** the raster pipeline selects `CullMode::None` when the material is double-sided (`SceneRenderer.cpp:920-922`), but neither `color_ps.hlsl` nor `color_textured_ps.hlsl` declares `SV_IsFrontFace` (repository search returned no use). The material flag is packed at `Material.h:198`, but the shaders only consume alpha-mode bits from that lane. For a backface with unflipped `N`, `dot(N,V) < 0`; `BuildBrdfQuery` clamps this to `NoV=0` (`brdf_common.hlsli:120-123`), then the common BRDF path rejects `NoV <= epsilon` (`:328`). That is a direct path to zero direct BRDF response, independent of which NDF is selected.

### Light vectors

`SceneRenderer.cpp:210-230` transforms and normalizes the authored local emission direction and stores it in `DirectionIntensity.xyz`. It separately stores world position, range, color, and point/spot cone cosines. The shader then does the following:

| Light type | Shader `L` | Attenuation / cone | Assessment |
| --- | --- | --- | --- |
| Directional | `normalize(-DirectionIntensity.xyz)` | `1` before shadow visibility | Consistent if authored direction means light emission/travel direction; the ECS comment says “Local emission direction.” |
| Point | `(lightPosition - worldPosition) / distance` | Inverse-square with a squared range fade; zero outside range | Direction and sign are consistent. The distance floor avoids the inverse-square singularity. |
| Spot | Same positional `L` | Squared radial range fade times `smoothstep(outerCos, innerCos, dot(emissionDirection, normalize(worldPosition-lightPosition)))` | Cone uses the light-to-surface direction, so the sign is consistent with emission direction. |

`BuildBrdfQuery` normalizes `N`, `V`, `L`, and `H`; computes saturated `NoV`, `NoL`, `NoH`, and `VoH`; and all direct models multiply their BRDF by `NoL * lightColor * intensity * attenuation * shadow`. For valid normalized inputs, the saturated dot products do not change positive-hemisphere values. The key exception is the unflipped double-sided backface normal described above.

Degenerate-input note: spot shading normalizes `worldPosition - lightPosition` to evaluate the cone. At exactly zero distance that vector is undefined; the positional `L` path has a distance floor, but the cone direction does not. This is an isolated center-of-light edge case, not a broad direction-sign error.

## Material, Fresnel, and direct-light equations

### Common microfacet path

The common component function implements:

```text
F0 = lerp(dielectricF0, baseColor, metallic)
F  = F0 + (1 - F0) * (1 - VoH)^5
G  = correlated Smith GGX(NoV, NoL, alpha)
fD = ((1 - metallic) * baseColor) * (1 - F) / pi
fS = F * D(NoH) * G / max(4 * NoV * NoL, epsilon)
Lo = (fD + fS) * radiance * NoL
```

The roughness convention is perceptual roughness `r` packed in `SurfaceParams.x`, then `alpha=max(r*r, 0.001)`. GGX uses `alpha² / (pi * (1 + (alpha²-1)NoH²)²)`. The IOR mode computes dielectric `F0=((ior-1)/(ior+1))²`; explicit mode uses the packed RGB F0. Both are clamped to `[0,1]` before use. Schlick Fresnel takes `VoH` for direct lighting, which is the microfacet incidence angle.

### Specular-glossiness roughness inversion proof

- Importer converts authored glossiness `g` into `properties.Roughness = 1-g` (`RenderSystem.cpp:754-755`); `BuildMaterialGpu` stores that value directly in `SurfaceParams.x` (`Material.h:162,190`).
- Untextured shader then computes `roughness = 1-SurfaceParams.x` for this workflow (`color_ps.hlsl:297`), resulting in `g` rather than `1-g`. It squares that incorrect roughness for `alpha` immediately afterward (`:298`).
- Textured shader computes `roughness = 1-(1-SurfaceParams.x)*sample.a` (`color_textured_ps.hlsl:353-355`). With the white fallback sample alpha of 1, this becomes `SurfaceParams.x`, the intended `1-g`; with a glossiness texture it becomes `1-(gFactor*gTexture)`.
- This discrepancy changes the NDF width and the environment mip chosen (`color_ps.hlsl:297-298,448`; textured `:353-356,509`). It applies to every direct BRDF selector, and to the shared environment path, rather than only to GGX.

### Specular-glossiness diffuse attenuation proof

- Untextured shader: `color_ps.hlsl:305-307` sets `diffuseColor = baseColor * (1 - max(specularF0))`.
- Textured shader: `color_textured_ps.hlsl:365-367` applies the same pre-factor after multiplying specular F0 by the specular texture.
- Both pass that `diffuseColor` as `brdfBaseColor` and `metallic=0` (`color_ps.hlsl:367-369`; textured `:427-429`).
- Shared `EvaluateMicrofacetBrdfComponents` then computes `diffuse = Lambert(diffuseColor * (1 - fresnel))` (`brdf_common.hlsli:336-340`). The split-sum IBL helper applies another `(1 - FresnelRoughness)` to the same `diffuseColor` (`brdf_common.hlsli:149-164`); the PRISM IBL diffuse branch does too (`color_ps.hlsl:456-457`).

Therefore the specular-glossiness branch applies another angular Fresnel remainder to an already partitioned diffuse input for direct and environment lighting. Scene ambient uses the pre-scaled color without this second angular factor. This is a renderer-specific extra attenuation concern; the extension permits implementation BRDF variation, so the precise expected angular diffuse model should be selected before changing it. This review does not edit implementation.

### CPU reference versus production shader

`GraphicEngineCore/modules/rendering/include/renderer/brdf/Brdf.h` computes the same Schlick specular factor but its diffuse term is `diffuseColor * (1-F0) / pi`. Production HLSL computes `diffuseColor * (1-F(V,H)) / pi`. These formulas coincide at normal incidence only when `VoH=1`; at grazing `F(V,H)` approaches one while `F0` stays fixed. The existing CPU BRDF reference test integrates the CPU formula, not the HLSL formula, so it cannot establish that production shader energy behavior matches that oracle. Treat this as a test/reference contract gap; the renderer's chosen angle-dependent formula is internally consistent with its own direct and IBL paths.

### BRDF/NDF model audit

The model selector changes `D` while sharing the same input vector set, Fresnel, visibility geometry, and `4 NoV NoL` denominator except where noted:

| Model | NDF/input | Review result |
| --- | --- | --- |
| GGX | Isotropic Trowbridge-Reitz, `D(NoH, alpha)` | Standard formula and `alpha = roughness²`; no math error found. |
| GTR | Generalized Trowbridge-Reitz with gamma | Log-domain normalization; `gamma=2` reduces to GGX. Existing reference tests numerically integrate its projected NDF. |
| Beckmann | `exp((NoH²-1)/(alpha² NoH²)) / (pi alpha² NoH⁴)` | Standard isotropic slope distribution; projected normalization is covered by reference integration. |
| Gauss | Gaussian slope distribution with `tan²=(1-NoH²)/NoH²` | Formula and projected normalization are covered by the reference test. |
| Blinn-Phong | `((n+2)/(2pi))*NoH^n`, with `n=max(2/alpha²-2,0)` | Normalized projected distribution; broad-roughness zero-exponent special case stays finite. |
| Schlick | Rational Schlick NDF; `m=clamp(alpha, .001, 1)` | Uses its own NDF with shared Smith GGX masking. That is an approximation/model choice, not an algebraic inconsistency. |
| Sawicki | Rational normalized distribution, `m=clamp(alpha, .001,1)` | Existing reference test numerically integrates and checks the rational form. |
| Ashikhmin-Shirley | `H` projected into tangent/bitangent/normal coordinates; separate U/V roughness and normalization | Anisotropic axes are used explicitly. Geometry uses `max(alphaU², alphaV²)` as a GGX Smith approximation; this is not the exact anisotropic masking term. With a normal map, the shader also passes tangent axes built around the geometric normal with a mapped shading normal, violating the orthonormal frame assumed by the NDF. |
| PRISM NDF | `H` projected to `(dot(H,T), dot(H,B))`, with disk mask, coefficient sum, signed residual, nonnegative clamp, and projected-energy normalization | Coordinate construction is consistent with unit half-vector and `NoH`; residual sign and coefficient layout agree with the documented contract. GPU direct and environment integrators use the same normalization convention. Existing tests cover independent basis parity and a constant-environment reference. |

All direct models route through the same normalized `N,V,L,H` query and the same `NoL` light multiplier. The first six non-GGX distributions and PRISM still use correlated GGX Smith masking as a shared approximation; Ashikhmin-Shirley additionally substitutes the maximum U/V slope roughness into that isotropic masking term, rather than using an anisotropic visibility function. These are model approximations. The environment dispatch is a separate mismatch: only PRISM has a distribution-specific environment integrator; the other selectable NDFs use the GGX split-sum path. Even for GGX, [EnvironmentLighting.cpp](GraphicEngineCore/modules/rendering/src/EnvironmentLighting.cpp) integrates the LUT with separable `G1(V)*G1(L)`, whereas [brdf_common.hlsli](GraphicEngineBackends/D3D11/shaders/brdf_common.hlsli) uses correlated Smith `G2` in direct lighting. No evidence in the audited source shows a sign or dot-product swap between direct NDF model branches.

### Workflow and model matrix

`MaterialWorkflow` has metallic-roughness and specular-glossiness values; `BrdfModel` has nine values (`GGX`, `GTR`, `Beckmann`, `Gauss`, `BlinnPhong`, `AshikhminShirley`, `Schlick`, `Sawicki`, `PrismNdf`). The pixel shaders contain matching selectors. Across the 18 workflow/model combinations:

| Model(s) | Direct specular | Environment specular | Difference |
| --- | --- | --- | --- |
| GGX | GGX NDF + correlated Smith G2 | GGX split-sum + separable Smith G1 product | Same NDF but visibility model differs. |
| GTR gamma 2 | GGX-equivalent GTR NDF + correlated Smith G2 | GGX split-sum + separable Smith G1 product | NDF matches; visibility model differs. |
| GTR gamma other than 2 | GTR gamma NDF | GGX split-sum | Distribution and visibility mismatch. |
| Beckmann, Gauss, Blinn-Phong, Schlick, Sawicki | Selected NDF | GGX split-sum | Distribution and visibility mismatch. |
| Ashikhmin-Shirley | Anisotropic tangent-frame NDF | Isotropic GGX split-sum | Anisotropy, distribution, and visibility differ in environment specular. |
| PRISM | Generated NDF | PRISM disk integration | Distribution-specific route exists; residual controls are intended to match. |

The same dispatch exists in both the textured and untextured pixel shaders. Textures change base color, normal, roughness/metallic or specular/glossiness factors, but do not change which environment integrator is selected. Therefore the GGX environment mismatch affects both texture variants and both workflows whenever environment lighting is enabled.

## Textures and workflows

- Metallic-roughness workflow uses blue for metallic and green for roughness (`color_textured_ps.hlsl:352-355`), multiplies factors by samples, and derives `F0` by metallic interpolation. Diffuse starts at `baseColor*(1-metallic)` and is attenuated once by the angular Fresnel term.
- Specular-glossiness workflow uses RGB for specular texture and alpha for glossiness; the CPU importer stores roughness as `1-glossiness` (`RenderSystem.cpp:755`). The textured shader reconstructs roughness as `1-(1-roughnessFactor)*glossinessSample`; the untextured shader instead complements packed roughness again, which is the confirmed inversion above. The extra Fresnel attenuation remains separate.
- Normal-map scaling and texture UV transforms do not alter the light vector conventions. Alpha mask discards before lighting. Emissive is added to diffuse lighting after direct/ambient terms, so emissive can still make an otherwise unlit backface visible.

### Texture color/data and mip audit

`ResolveImportedTexture` classifies base color and emissive as sRGB, normal/metallic-roughness/occlusion as linear data, and specular-glossiness texture as sRGB (RGB specular color is decoded while alpha glossiness remains linear in an sRGB texture format). The mip builder decodes/encodes sRGB RGB and filters normal vectors in signed space with renormalization, which are appropriate operations for those channels.

The data/alpha error is in the general non-normal filtering rule at `TextureMipGenerator.cpp:136-145`: RGB accumulation multiplies every sample by alpha, and RGB division uses summed alpha. That is premultiplied-alpha filtering. For a specular-glossiness texture, however, alpha is glossiness, not opacity. For two linear-light texels with the same area, specular RGB values `0.2` and `0.8` with glossiness alpha `0.1` and `0.9` become `0.74`, rather than the unweighted `0.5`; lower mips therefore alter F0 and diffuse partition as well as gloss. Metallic-roughness, occlusion, and emissive RGB can also be changed when their alpha channel is not fully opaque, even though that alpha is not opacity for those map uses.

This contradicts the core glTF channel rules: metallic-roughness ignores R and A, occlusion uses R and ignores GBA, and emissive ignores A. Specular-glossiness defines alpha as glossiness. The mip builder still lets those alpha values weight other channels before the shader samples them.

Base-color textures are the exception where alpha-weighted RGB filtering is appropriate for straight-alpha transparent color. The engine's alpha-coverage preservation is separately enabled only for masked base-color imports. The current `Options` structure has no channel-semantic setting to distinguish color-alpha from data-alpha, and existing [TextureMipGenerationTests.cpp](GraphicEngineCore/tests/unit/rendering/TextureMipGenerationTests.cpp) cover sRGB color, normal renormalization, and alpha coverage but not specular-glossiness/data-channel mip semantics.

### Imported normal and tangent audit

- `AssetImporter.cpp:345-363` accumulates unnormalized face cross products into each shared vertex and normalizes the sum. Shared vertices across a hard edge therefore receive averaged smooth normals. Khronos glTF specifies flat normals when normals are absent and says supplied tangents must be ignored when normals are absent ([glTF mesh rules](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.pdf)). The importer still preserves a supplied tangent and then generates normals in this case (`:519-533,590-593`).
- `AssetImporter.cpp:366-412` derives tangents with a custom triangle derivative accumulator rather than MikkTSpace. Khronos says implementations should calculate missing tangents using default MikkTSpace algorithms and the normal texture's coordinates (same glTF mesh rules). This is not evidence that every model will be visibly wrong, but it means tangent-space normal mapping can diverge on models whose tangent field is omitted.
- `KHR_texture_transform` is parsed for normal textures and changes `normalUv` in `color_textured_ps.hlsl:312-313`; the TBN at `:344-349` comes only from the original tangent/normal attributes and does not account for the UV transform's linear part. Translation has no TBN effect; rotation or mirrored axes change how sampled tangent-space X/Y align with the surface basis. The transform matrix itself is applied consistently to lookup coordinates; the missing correction is specific to normal-map basis interpretation.

### Imported material extension boundary

`AssetImporter.cpp:620-633` accepts required extensions only for `KHR_materials_pbrSpecularGlossiness` and `KHR_texture_transform`, and emits an “optional extension will be ignored” warning for other used extensions. It does not inspect a material's `KHR_materials_unlit` extension or provide a material-level unlit flag; the shader's `g_ObjectParams.y` bypass is tied to renderer wireframe mode. Optional unlit materials therefore render using their core fallback PBR parameters and lights. This is a scope limitation, not a claim of invalid glTF handling when the extension is optional.

## PRISM NDF and environment integration

- The CPU and shader enumerate 45 real Zernike terms in increasing `n`, then `m=-n,-n+2,...,n`; negative `m` selects the imaginary/sine component and nonnegative `m` the real/cosine component. The shader adds signed residual before clamping to zero and multiplies by the projected-energy normalization.
- Direct lighting evaluates the NDF at the half-vector's tangent-plane coordinates. Environment lighting samples the projected disk with `r=sqrt((i+0.5)/N)`, reconstructs `NoH=sqrt(1-r²)`, reflects `V` about `H` to obtain `L`, and accumulates the correct half-vector Jacobian form `D*G*F*VoH/(NoV*NoH)`. Uniform disk sampling contributes `pi/N`.
- The distribution texture stores coefficient-only values multiplied by normalization; the signed residual is multiplied by the same normalization in the environment shader. Their sum is clamped nonnegative, matching the direct evaluator's convention for valid positive normalization.
- **Preview parity detail:** `PrismNdfMath.cpp` computes horizon fade from the sampled disk point's `z = sqrt(1-r²)`, while both GPU shaders use `N·V`. If the preview is intended to show the exact residual seen by the shader, it needs a view-dependent input or should omit/label this view-angle fade. Existing docs currently say the preview honors the same controls, so the behavior is misleading for this option.

## Evidence and limits

Primary source locations:

- [brdf_common.hlsli](GraphicEngineBackends/D3D11/shaders/brdf_common.hlsli): query construction at lines 113-123; Fresnel at 127-137; Smith and NDF equations at 168-322; shared BRDF at 325-434.
- [color_ps.hlsl](GraphicEngineBackends/D3D11/shaders/color_ps.hlsl): PRISM environment integration at 99-156; direct vectors/material routing at 291-431; IBL and output at 436-484.
- [color_textured_ps.hlsl](GraphicEngineBackends/D3D11/shaders/color_textured_ps.hlsl): normal-map construction at 341-350; texture workflow factors at 351-368; corresponding direct and IBL paths at 371-540.
- [SceneRenderer.cpp](GraphicEngineCore/modules/rendering/src/SceneRenderer.cpp): authored light-to-GPU conversion at 210-230; double-sided raster cull selection at 920-922.
- [Material.h](GraphicEngineCore/modules/rendering/include/renderer/Material.h): GPU packing and flags at 161-200; [RenderSystem.cpp](GraphicEngineCore/modules/rendering/src/RenderSystem.cpp): imported specular-glossiness roughness conversion at 755-760.
- [AssetImporter.cpp](GraphicEngineCore/modules/assets/src/AssetImporter.cpp): generated normals/tangents at 345-412 and 590-593; extension support boundary at 620-633.
- [TextureMipGenerator.cpp](GraphicEngineCore/modules/rendering/src/TextureMipGenerator.cpp): channel filtering at 18-53 and 136-153; [RenderSystem.cpp](GraphicEngineCore/modules/rendering/src/RenderSystem.cpp): imported texture color-space classification at 804-814.
- [EnvironmentLighting.cpp](GraphicEngineCore/modules/rendering/src/EnvironmentLighting.cpp): irradiance and prefilter integrations at 133-178; [TextureMipGenerationTests.cpp](GraphicEngineCore/tests/unit/rendering/TextureMipGenerationTests.cpp): tested mip cases.
- [glTF 2.0 material rules](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#material-occlusionTexture): occlusion affects indirect lighting, and metallic-roughness/occlusion/emissive channel semantics.
- [PrismNdfMath.cpp](GraphicEngineCore/modules/rendering/src/PrismNdfMath.cpp): basis, residual, normalization, and preview equations; [MaterialSystem.cpp](GraphicEngineCore/modules/rendering/src/MaterialSystem.cpp): coefficient-only normalized distribution texture.
- [BrdfReferenceTests.cpp](GraphicEngineCore/tests/BrdfReferenceTests.cpp), [Brdf.h](GraphicEngineCore/modules/rendering/include/renderer/brdf/Brdf.h), and [PrismNdfGpuParityTests.cpp](GraphicEngineCore/tests/unit/rendering/PrismNdfGpuParityTests.cpp): existing numerical references, CPU BRDF equations, and GPU-parity coverage. These were read, not run.

Static analysis cannot establish the user's exact visible symptom or validate driver-specific rendered output, so this report cannot honestly certify 100% screen correctness. Validation was blocked locally: this checkout has no configured `out/build/x64-debug` tree or test executables, and `cmake`/`fxc` are unavailable on `PATH`; I did not install tools. The machine reports Intel and RTX adapters, but no shader compilation, WARP test, GPU capture, or rendered comparison was performed. Existing tests were inspected only. Source-proven defects and mismatches now include double-sided backface lighting, untextured specular-glossiness roughness inversion, incorrect alpha-weighted filtering of data textures, unsupported generated-normal/tangent behavior, normal-map UV rotation without tangent-frame correction, Ashikhmin-Shirley with mapped normals, and direct/IBL BRDF distribution/visibility mismatch. Specular-glossiness diffuse attenuation remains a renderer-specific model-convention concern. Full confidence requires reproducing these cases, then comparing each supported workflow/model/texture/light combination against independent numerical oracles on the production shader path; unsupported glTF extensions must be counted as unsupported rather than treated as validated material types.

## Tracking and implementation

The audit was initially requested as local, untracked analysis. Follow-up tracking was later authorized and is now organized under [epic #207](https://github.com/JGolaszewski/GrpahicEngine/issues/207) on the Graphics Engine project board. The focused work items are:

- [#208: Fix shared shader material workflow correctness](https://github.com/JGolaszewski/GrpahicEngine/issues/208) — backface orientation, specular-glossiness roughness/Fresnel composition, CPU/GPU equation parity, and mapped Ashikhmin-Shirley frames.
- [#209: Align direct, environment, and reference BRDF evaluation](https://github.com/JGolaszewski/GrpahicEngine/issues/209) — per-NDF IBL behavior, visibility differences, CPU reference fidelity, and PRISM preview parity.
- [#210: Correct imported normal and tangent frames](https://github.com/JGolaszewski/GrpahicEngine/issues/210) — missing normals/tangents and transformed normal-map coordinates.
- [#160: Fix texture mip generation](https://github.com/JGolaszewski/GrpahicEngine/issues/160) — expanded with the data-map alpha-weighting finding.

The integration branch is `epic/207-material-lighting-correctness`. No rendering fix is claimed by this audit commit; implementation commits should reference their corresponding child issue, and visual correctness remains pending the validation described above.
