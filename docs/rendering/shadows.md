# Shadow caster behavior

Opaque materials cast solid shadows when their entity has `CastShadows` enabled. Masked materials sample base-color alpha and use the material alpha cutoff, so cutouts match forward rendering. Blended materials do not cast shadows; they represent partial coverage that this depth-only shadow map cannot model. Shadow culling follows the material's double-sided setting.
