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

void *DC_GetCurrentTexture(void *pstate_v, int *width, int *height, int *stride, int *opaque)
{
    struct br_primitive_state *ps = (struct br_primitive_state *)pstate_v;
    struct br_buffer_stored *bs;

    *opaque = 1;
    if (ps == NULL) {
        return NULL;
    }
    bs = ps->prim.colour_map.buffer;
    if (bs == NULL || bs->buffer.base == NULL) {
        return NULL;
    }
    if (bs->buffer.type != BR_PMT_INDEX_8) {
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

#endif
