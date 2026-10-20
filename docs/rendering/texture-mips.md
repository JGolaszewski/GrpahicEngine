# Texture mip generation

Imported textures receive a complete CPU generated mip chain before GPU upload. Each destination texel uses an area weighted box filter over its footprint, so odd and non-square source dimensions contribute every texel. Color textures marked sRGB are decoded to linear light for filtering and encoded again for storage. RGB is filtered with alpha weighting to avoid transparent texel colors bleeding into visible edges.

Normal maps are interpreted as signed vectors, area averaged, normalized, and encoded back to RGB. A degenerate average falls back to the tangent-space +Z normal.

For a base-color texture used by a masked material, the mip generator scales each generated level's alpha toward the base image's coverage at the material's alpha cutoff. Coverage is discrete at small mip sizes, so the generator selects the scale with the closest representable covered-texel fraction. Material alpha mode and cutoff are part of the texture resource cache key because they affect generated mip data.

The unit suite checks deterministic mip pixels for invalid and odd-sized images, sRGB color, normal-vector averaging, and masked alpha coverage at every generated level. These CPU tests run without a graphics device. Rendering comparisons for distant foliage and normal-map detail remain dependent on a suitable D3D11 test environment.
