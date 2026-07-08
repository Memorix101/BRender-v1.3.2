/*
 * Dreamcast helper: expose the primitive state's current colour map (texture)
 * to the PowerVR bridge in the harness. softrend does the geometry but stores
 * the texture in the pentprim primitive state, which is opaque to it, so this
 * accessor (which has the full pentprim headers) reaches in and returns the
 * raw 8bpp texel data and dimensions. Returns NULL when there is no usable
 * 8bpp texture bound.
 */
#ifdef __DREAMCAST__

#include "drv.h"
#include "pstate.h"
#include "sbuffer.h"
#include "work.h"

#include "brender.h"

// Temporary diagnostics: tell apart the three reasons DC_GetCurrentTexture can
// report "no texture", to track down why a known-textured material (the sky
// dome) was rendering flat instead of with its texture.
int g3d_diag_nullps;
int g3d_diag_nullbuf;
int g3d_diag_wrongtype;
int g3d_diag_lasttype = -1;

void *DC_GetCurrentTexture(void *pstate_v, int *width, int *height, int *stride, int *opaque)
{
    struct br_primitive_state *ps = (struct br_primitive_state *)pstate_v;
    struct br_buffer_stored *bs;

    *opaque = 1;
    if (ps == NULL) {
        g3d_diag_nullps++;
        return NULL;
    }
    bs = ps->prim.colour_map.buffer;
    if (bs == NULL || bs->buffer.base == NULL) {
        g3d_diag_nullbuf++;
        return NULL;
    }
    if (bs->buffer.type != BR_PMT_INDEX_8) {
        g3d_diag_wrongtype++;
        g3d_diag_lasttype = bs->buffer.type;
        return NULL;
    }
    /* PRIMF_OPAQUE_MAP clear means the map keys colour index 0 as transparent,
     * so it must be drawn punch-through; set means a fully opaque texture. */
    *opaque = (ps->prim.flags & PRIMF_OPAQUE_MAP) ? 1 : 0;
    *width = (int)bs->buffer.width_p;
    *height = (int)bs->buffer.height;
    *stride = (int)bs->buffer.stride_b;
    return bs->buffer.base;
}

/* Mirrors DC_GetCurrentTexture, but for the material's index_shade table - a
 * per-material shade ramp (br_material::index_shade) that maps a lighting
 * intensity row to the actual final palette index for that material's own
 * colour. Untextured surfaces' comp_f[C_I] (after CLAMP_SCALE in lightmac.h)
 * lands in [index_base, index_base+index_range), which is a row into this
 * table, not a usable palette index by itself - using it as a palette index
 * directly scatters every material's shading across whatever unrelated colours
 * happen to live at that material's index_base in the global palette, instead
 * of that material's own ramp. Returns NULL when there is no shade table bound
 * (the caller should fall back to treating comp_f[C_I] as a direct index).
 */
void *DC_GetCurrentIndexShade(void *pstate_v, int *width, int *height, int *stride)
{
    struct br_primitive_state *ps = (struct br_primitive_state *)pstate_v;
    struct br_buffer_stored *bs;

    if (ps == NULL) {
        return NULL;
    }
    bs = ps->prim.index_shade.buffer;
    if (bs == NULL || bs->buffer.base == NULL) {
        return NULL;
    }
    *width = (int)bs->buffer.width_p;
    *height = (int)bs->buffer.height;
    *stride = (int)bs->buffer.stride_b;
    return bs->buffer.base;
}

/* Mirrors DC_GetCurrentIndexShade, but for index_blend - the table a
 * BR_PRIMF_BLENDED primitive (smoke, shaded overlays) uses to combine its
 * output with what is already on screen. For smoke this is one of the
 * generated shade tables (GenerateShadeTable in spark.c), whose columns are
 * source palette indices and whose rows tint toward a fixed reference colour
 * (black/dark-grey/grey) by an amount that increases down the rows. The last
 * row converges to (a palette match of) that reference colour for every
 * column, so reading any cell of the last row and looking up its palette RGB
 * recovers the smoke's intended tint colour - which is what the PowerVR
 * bridge needs to approximate the blend with a flat translucent quad, instead
 * of the material's index_base (which for smoke is a shade-ramp row, not the
 * tint colour). Returns NULL when no blend table is bound. */
void *DC_GetCurrentIndexBlend(void *pstate_v, int *width, int *height, int *stride)
{
    struct br_primitive_state *ps = (struct br_primitive_state *)pstate_v;
    struct br_buffer_stored *bs;

    if (ps == NULL) {
        return NULL;
    }
    bs = ps->prim.index_blend.buffer;
    if (bs == NULL || bs->buffer.base == NULL) {
        return NULL;
    }
    *width = (int)bs->buffer.width_p;
    *height = (int)bs->buffer.height;
    *stride = (int)bs->buffer.stride_b;
    return bs->buffer.base;
}

/* Untextured materials have no per-vertex shading at all in BRender's own
 * reference behaviour (confirmed against the project's OpenGL renderer,
 * gl_renderer.c's setActiveMaterial(): a material with no colour_map renders
 * the whole face as a single flat palette colour, material->index_base,
 * regardless of per-vertex lighting). comp_f[C_I] is still a valid value in
 * that case (it is the material's own shade-ramp index, offset by lighting),
 * but using it per-vertex puts the engine in unphysical territory the
 * reference renderer never visits, and after near-plane clip interpolation
 * (camera rotation is exactly when clipping geometry changes) it can land far
 * outside that shade ramp - the symptom seen on Dreamcast was per-vertex
 * rainbow colouring on level geometry as the camera rotated. Going through
 * index_base directly instead of comp_f[C_I] sidesteps that interpolation
 * entirely by never depending on a per-vertex varying value for colour. */
int DC_GetCurrentIndexBase(void *pstate_v)
{
    struct br_primitive_state *ps = (struct br_primitive_state *)pstate_v;

    if (ps == NULL) {
        return 0;
    }
    return (int)ps->prim.index_base;
}

/* Set by graphics.c around ProcessShadow's BrZbSceneRenderAdd(gShadow_actor)
 * call: the car's drop shadow is a second re-render of the ground directly
 * under the car, darkened on PC by temporarily pointing BRender's lighting
 * ramp at a darker row. dc_triangle_fill bypasses that ramp entirely, so it
 * needs to know when to darken its own output instead. */
int g_dc_in_shadow_pass;

/* Set by graphics.c around the oil-spill BrZbSceneRenderAdd(oily_actor) call:
 * oil stains sit coplanar on the ground exactly like the car's shadow, and
 * need the same dedicated depth bias (dc_triangle_fill) to stop popping in
 * and out as the camera moves - but their own material colour is already
 * correct, so unlike g_dc_in_shadow_pass this doesn't also trigger darkening. */
int g_dc_in_decal_pass;

#endif
