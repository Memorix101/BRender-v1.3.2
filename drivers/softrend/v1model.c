/*
 * Copyright (c) 1993-1995 Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: v1model.c 1.8 1998/07/21 11:35:57 jon Exp $
 * $Locker: $
 *
 * Support routines for rendering models
 */
#include <stddef.h>
#include <string.h>

#include "drv.h"
#include "shortcut.h"
#include "brassert.h"

#include "vecifns.h"

#include "timing.h"

BR_RCS_ID("$Id: v1model.c 1.8 1998/07/21 11:35:57 jon Exp $");


static void GEOMETRY_CALL V1Faces_ScratchAllocate(struct br_geometry *self, struct br_renderer *renderer)
{
	char *sp;
	br_size_t scratch_size;

	/*
	 * Scratch space
	 */
	scratch_size  = SCRATCH_ALIGN(rend.nvertices * sizeof(*rend.vertex_counts));
	scratch_size += SCRATCH_ALIGN(rend.nfaces    * sizeof(*rend.temp_faces));
	scratch_size += SCRATCH_ALIGN(rend.nvertices * sizeof(*rend.temp_vertices));

	if(rend.block->type == BRT_LINE)
		scratch_size += SCRATCH_ALIGN(rend.nedges * sizeof(*rend.edge_flags));

	if(rend.block->type == BRT_POINT)
		scratch_size += SCRATCH_ALIGN(rend.nvertices * sizeof(*rend.vertex_flags));

	if(renderer->state.hidden.type == BRT_BUCKET_SORT &&
		(renderer->state.hidden.divert == BRT_ALL ||
		 renderer->state.hidden.divert == BRT_BLENDED && rend.block->flags & BR_PRIMF_BLENDED))

		scratch_size += SCRATCH_ALIGN(rend.nvertices * sizeof(*rend.vertex_heap_pointers));

	rend.scratch = BrScratchAllocate(scratch_size+SCRATCH_BOUNDARY);

	/*
	 * Allocate common scratch areas
	 */
	sp = rend.scratch;

	rend.vertex_counts = (void *)sp;
	sp += SCRATCH_ALIGN(rend.nvertices * sizeof(*rend.vertex_counts));

	rend.temp_faces = (void *)sp;
	sp += SCRATCH_ALIGN(rend.nfaces * sizeof(*rend.temp_faces));

	rend.temp_vertices = (void *)sp;
	sp += SCRATCH_ALIGN(rend.nvertices * sizeof(*rend.temp_vertices));

	/*
	 * Allocate optional scratch areas (edge and vertex flags)
	 */
	if(rend.block->type == BRT_LINE) {
		rend.edge_flags = (void *)sp;
		sp += SCRATCH_ALIGN(rend.nedges * sizeof(*rend.edge_flags));
		memset(rend.edge_flags, 0, rend.nedges * sizeof(*rend.edge_flags));

		/*
		 * edge 0 is never rendererd (used for internal edges)
		 */
		rend.edge_flags[0] = 1;
	}

	if(rend.block->type == BRT_POINT) {
		rend.vertex_flags = (void *)sp;
		sp += SCRATCH_ALIGN(rend.nvertices * sizeof(*rend.vertex_flags));
		memset(rend.vertex_flags,0,rend.nvertices * sizeof(*rend.vertex_flags));
	}

	if(renderer->state.hidden.type == BRT_BUCKET_SORT &&
		(renderer->state.hidden.divert == BRT_ALL ||
		 renderer->state.hidden.divert == BRT_BLENDED && rend.block->flags & BR_PRIMF_BLENDED)) {

		rend.vertex_heap_pointers = (void *)sp;
		sp += SCRATCH_ALIGN(rend.nvertices * sizeof(*rend.vertex_heap_pointers));
		memset(rend.vertex_heap_pointers,0,rend.nvertices * sizeof(*rend.vertex_heap_pointers));
	}

	/*
	 * Clear vertex counts
	 */
	memset(rend.vertex_counts,0,(rend.nvertices * sizeof(*rend.vertex_counts)));
}

/*
 * Pre Culling operations
 */
#ifndef V1Face_CullNone
static void GEOMETRY_CALL V1Face_CullNone(struct br_geometry *self, struct br_renderer *renderer)
{
	int f;
	br_uint_16 (*fp_vertices)[3] = rend.face_vertices;
	struct temp_face *tfp = rend.temp_faces;

	/*
	 * Assume all faces are visible
	 */
	for(f=0; f < rend.nfaces; f++, fp_vertices++, tfp++) {
		tfp->flag = TFF_VISIBLE;

		rend.vertex_counts[(*fp_vertices)[0]]++;
		rend.vertex_counts[(*fp_vertices)[1]]++;
		rend.vertex_counts[(*fp_vertices)[2]]++;
	}

//        rend.nvisible_faces = rend.nfaces;
}
#endif

#ifndef V1Face_OS_CullNone
static void GEOMETRY_CALL V1Face_OS_CullNone(struct br_geometry *self, struct br_renderer *renderer)
{
	int f;
	struct temp_face *tfp = rend.temp_faces;

	/*
	 * Assume all faces are visible
	 */
	for(f=0; f < rend.nfaces; f++, tfp++)
		tfp->flag = TFF_VISIBLE;

	/*
	 * Assume all vertices are visible (and that vertex counts are bytes)
	 */
	memset(rend.vertex_counts,1,rend.nvertices);

//        rend.nvisible_faces = rend.nfaces;
}
#endif

#ifndef V1Face_CullOneSidedPerspective
static void GEOMETRY_CALL V1Face_CullOneSidedPerspective(struct br_geometry *self, struct br_renderer *renderer)
{
	int f,n;
	br_uint_16 (*fp_vertices)[3] = rend.face_vertices;
	br_vector4 *fp_eqn = rend.face_eqn;
	struct temp_face *tfp = rend.temp_faces;

//        rend.nvisible_faces = 0;

 	for(f=0,n=0; f < rend.nfaces; f++, fp_vertices++, fp_eqn++, tfp++) {

		/*
		 * if Plane_Eqn . Eye <= 0, face is away from eye
		 */
		if(BrVector3Dot((br_vector3 *)fp_eqn,&scache.eye_m) < fp_eqn->v[3]) {
			tfp->flag = 0;
			continue;
		}

		tfp->flag = TFF_VISIBLE;

		rend.vertex_counts[(*fp_vertices)[0]]++;
		rend.vertex_counts[(*fp_vertices)[1]]++;
		rend.vertex_counts[(*fp_vertices)[2]]++;

//                rend.nvisible_faces++;
	}
}
#endif

#ifndef V1Face_OS_CullOneSidedPerspective
#define V1Face_OS_CullOneSidedPerspective V1Face_CullOneSidedPerspective
#endif

#ifndef V1Face_CullOneSidedParallel
static void GEOMETRY_CALL V1Face_CullOneSidedParallel(struct br_geometry *self, struct br_renderer *renderer)
{
	int f,n;
	br_uint_16 (*fp_vertices)[3] = rend.face_vertices;
	br_vector4 *fp_eqn = rend.face_eqn;
	struct temp_face *tfp = rend.temp_faces;

//        rend.nvisible_faces = 0;

 	for(f=0,n=0; f < rend.nfaces; f++, fp_vertices++, fp_eqn++, tfp++) {

		/*
		 * if Plane_Eqn . Eye <= 0, face is away from eye
		 */
		if(BrVector3Dot((br_vector3 *)fp_eqn,&scache.eye_m) < S0) {
			tfp->flag = 0;
			continue;
		}

		tfp->flag = TFF_VISIBLE;

		rend.vertex_counts[(*fp_vertices)[0]]++;
		rend.vertex_counts[(*fp_vertices)[1]]++;
		rend.vertex_counts[(*fp_vertices)[2]]++;

//                rend.nvisible_faces++;
	}
}
#endif

#ifndef V1Face_OS_CullOneSidedParallel
#define V1Face_OS_CullOneSidedParallel V1Face_CullOneSidedParallel
#endif

#ifndef V1Face_CullTwoSidedPerspective
static void GEOMETRY_CALL V1Face_CullTwoSidedPerspective(struct br_geometry *self, struct br_renderer *renderer)
{
	int f,df;
	br_uint_16 (*fp_vertices)[3] = rend.face_vertices;
	br_vector4 *fp_eqn = rend.face_eqn;
	struct temp_face *tfp = rend.temp_faces;

	for(f=0; f < rend.nfaces; f++, fp_vertices++, fp_eqn++, tfp++) {

		tfp->flag = TFF_VISIBLE;
		df = TVDIR_FRONT;

		/*
		 * if Plane_Eqn . Eye <= 0, face is away from eye
		 */
		if(BrVector3Dot((br_vector3 *)fp_eqn,&scache.eye_m) < fp_eqn->v[3]) {
			tfp->flag |= TFF_REVERSED;
			df = TVDIR_BACK;
		}

		rend.vertex_counts[(*fp_vertices)[0]]++;
		rend.vertex_counts[(*fp_vertices)[1]]++;
		rend.vertex_counts[(*fp_vertices)[2]]++;

		rend.temp_vertices[(*fp_vertices)[0]].flags |= df;
		rend.temp_vertices[(*fp_vertices)[1]].flags |= df;
		rend.temp_vertices[(*fp_vertices)[2]].flags |= df;
	}

//        rend.nvisible_faces = rend.nfaces;
}
#endif

#ifndef V1Face_OS_CullTwoSidedPerspective
#define V1Face_OS_CullTwoSidedPerspective V1Face_CullTwoSidedPerspective
#endif

#ifndef V1Face_CullTwoSidedParallel
static void GEOMETRY_CALL V1Face_CullTwoSidedParallel(struct br_geometry *self, struct br_renderer *renderer)
{
	int f,df;
	br_uint_16 (*fp_vertices)[3] = rend.face_vertices;
	br_vector4 *fp_eqn = rend.face_eqn;
	struct temp_face *tfp = rend.temp_faces;

	for(f=0; f < rend.nfaces; f++, fp_vertices++, fp_eqn++, tfp++) {

		tfp->flag = TFF_VISIBLE;
		df = TVDIR_FRONT;

		/*
		 * if Plane_Eqn . Eye <= 0, face is away from eye
		 */
		if(BrVector3Dot((br_vector3 *)fp_eqn,&scache.eye_m) < S0) {
			tfp->flag |= TFF_REVERSED;
			df = TVDIR_BACK;
		}

		rend.vertex_counts[(*fp_vertices)[0]]++;
		rend.vertex_counts[(*fp_vertices)[1]]++;
		rend.vertex_counts[(*fp_vertices)[2]]++;

		rend.temp_vertices[(*fp_vertices)[0]].flags |= df;
		rend.temp_vertices[(*fp_vertices)[1]].flags |= df;
		rend.temp_vertices[(*fp_vertices)[2]].flags |= df;
	}

//        rend.nvisible_faces = rend.nfaces;
}
#endif

#ifndef V1Face_OS_CullTwoSidedParallel
#define V1Face_OS_CullTwoSidedParallel V1Face_CullTwoSidedParallel
#endif

#ifndef V1Face_CullOneSided
static void GEOMETRY_CALL V1Face_CullOneSided(struct br_geometry *self, struct br_renderer *renderer)
{
	switch(renderer->state.matrix.view_to_screen_hint) {
	case BRT_PERSPECTIVE:
		V1Face_CullOneSidedPerspective(self, renderer);
		break;

	case BRT_PARALLEL:
		V1Face_CullOneSidedParallel(self, renderer);
		break;

	default:
		V1Face_CullNone(self, renderer);
		break;
	}
}
#endif


// Special case handling for Pentium Pro
// #if BASED_FLOAT
// #ifndef V1Face_CullOneSided_P6
// static void GEOMETRY_CALL V1Face_CullOneSided_P6(struct br_geometry *self, struct br_renderer *renderer)
// {
// 	switch(renderer->state.matrix.view_to_screen_hint) {
// 	case BRT_PERSPECTIVE:
// 		V1Face_CullOneSidedPerspective_P6(self, renderer);
// 		break;

// 	case BRT_PARALLEL:
// 		V1Face_CullOneSidedParallel(self, renderer);
// 		break;

// 	default:
// 		V1Face_CullNone(self, renderer);
// 		break;
// 	}
// }
// #endif
// #endif

#ifndef V1Face_OS_CullOneSided
static void GEOMETRY_CALL V1Face_OS_CullOneSided(struct br_geometry *self, struct br_renderer *renderer)
{
	switch(renderer->state.matrix.view_to_screen_hint) {
	case BRT_PERSPECTIVE:
		V1Face_OS_CullOneSidedPerspective(self, renderer);
		break;

	case BRT_PARALLEL:
		V1Face_OS_CullOneSidedParallel(self, renderer);
		break;

	default:
		V1Face_OS_CullNone(self, renderer);
		break;
	}
}
#endif

#ifndef V1Face_CullTwoSided
static void GEOMETRY_CALL V1Face_CullTwoSided(struct br_geometry *self, struct br_renderer *renderer)
{
	switch(renderer->state.matrix.view_to_screen_hint) {
	case BRT_PERSPECTIVE:
		V1Face_CullTwoSidedPerspective(self, renderer);
		break;

	case BRT_PARALLEL:
		V1Face_CullTwoSidedParallel(self, renderer);
		break;

	default:
		V1Face_CullNone(self, renderer);
		break;
	}
}
#endif

#ifndef V1Face_OS_CullTwoSided
static void GEOMETRY_CALL V1Face_OS_CullTwoSided(struct br_geometry *self, struct br_renderer *renderer)
{
	switch(renderer->state.matrix.view_to_screen_hint) {
	case BRT_PERSPECTIVE:
		V1Face_OS_CullTwoSidedPerspective(self, renderer);
		break;

	case BRT_PARALLEL:
		V1Face_OS_CullTwoSidedParallel(self, renderer);
		break;

	default:
		V1Face_OS_CullNone(self, renderer);
		break;
	}
}
#endif

/*
 * Do per face work - find all the faces that are on screen
 */
#ifndef V1Face_Outcode
static void GEOMETRY_CALL V1Face_Outcode(struct br_geometry *self, struct br_renderer *renderer)
{
	int f;
	br_uint_16 (*fp_vertices)[3] = rend.face_vertices;
	struct temp_face *tfp = rend.temp_faces;
	br_uint_32 combined_codes;

	rend.faces_clipped = BR_FALSE;

	for(f=0; f < rend.nfaces; f++,fp_vertices++,tfp++) {

		if(!tfp->flag)
			continue;

		/*
		 * Work out AND/OR of outcodes
		 */
		combined_codes = rend.temp_vertices[(*fp_vertices)[0]].flags |
						 rend.temp_vertices[(*fp_vertices)[1]].flags |
						 rend.temp_vertices[(*fp_vertices)[2]].flags;
		/*
		 * If completely of one edge of view volume (by outcodes)
		 *	mark as not visible
		 *	continue
		 */
		if((combined_codes & OUTCODES_NOT) != OUTCODES_NOT) {
//                        rend.nvisible_faces--;

			tfp->flag = 0;

			rend.vertex_counts[(*fp_vertices)[0]]--;
			rend.vertex_counts[(*fp_vertices)[1]]--;
			rend.vertex_counts[(*fp_vertices)[2]]--;
			continue;
		}

		/*
		 * If any outcode is set - mark as needing clipping and remember combined codes
		 */
  		if(combined_codes & OUTCODES_ALL) {
			tfp->flag |= TFF_CLIPPED;
			tfp->codes = (br_uint_16)(combined_codes & OUTCODES_ALL);
			rend.faces_clipped = BR_TRUE;
		}
	}
}
#endif

#ifdef __DREAMCAST__
/*
 * Dreamcast hardware path. softrend has already transformed, lit and projected
 * the face vertices to screen space (comp_f[C_SX/C_SY], 1/w in C_Q, intensity
 * in C_I). Hand the triangle to the PowerVR (DCPVR3D_AddTri in the harness
 * dc_pvr platform) instead of running the software fill chain.
 */
/* category: 0 = opaque (OP list), 1 = punch-through / index-0 transparent
 * (PT list), 2 = blended (TR list). */
extern void DCPVR3D_AddTriTex(
    float x0, float y0, float z0, float u0, float v0, unsigned int c0,
    float x1, float y1, float z1, float u1, float v1, unsigned int c1,
    float x2, float y2, float z2, float u2, float v2, unsigned int c2,
    int texid, int category);
extern int DCPVR3D_RegisterTexture(void *pixels, int w, int h, int stride);
extern unsigned int DCPVR3D_PaletteColor(int idx);
/* Implemented in the pentprim driver (dc_pvr_texture.c) - reaches the texture
 * out of the opaque primitive state. *opaque reports PRIMF_OPAQUE_MAP. */
extern void *DC_GetCurrentTexture(void *pstate, int *w, int *h, int *stride, int *opaque);
extern void *DC_GetCurrentIndexShade(void *pstate, int *w, int *h, int *stride);

static unsigned int dc_face_colour(brp_vertex *v)
{
    int g = (int)(v->comp_f[C_I] * 255.0f);
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    return 0xFF000000u | ((unsigned int)g << 16) | ((unsigned int)g << 8) | (unsigned int)g;
}

/*
 * Terminal fill for the Dreamcast. This replaces the pentprim rasteriser
 * (rend.block->render, the leaf of the prim-block chain) rather than bypassing
 * the chain at the per-face entry. By the time control reaches here softrend has
 * already run the full chain - clip, per-surface constant evaluation and
 * texture mapping - so the vertices carry final clipped screen coordinates
 * (C_SX/C_SY), positive view-space W and mapped texture coordinates in C_U/C_V.
 *
 * The mapped U/V are in rasteriser range, i.e. the normalised 0..1 surface
 * coordinate scaled by comp_scales[] and biased by comp_offsets[] (see the SCALE
 * macros in lightmac.h). We undo that here to recover the 0..1 UV the PowerVR
 * wants. fp_eqn and tfp are NULL at this stage (the clipper passes NULL), so we
 * must not touch them.
 */
// Pipeline teardown switches for the persistent level-geometry flicker
// investigation (mirrors DC_FEAT_* in dc_pvr.c, which only gates auxiliary
// optimisations on top of an always-on core path). These gate the core path
// itself: with both 0, every triangle submitted is flat opaque white with no
// texture binding at all - pure geometry/depth/raster, nothing else. Build up
// from there one switch at a time to find which stage the bug first appears in.
#define DC_FEAT_TEXTURE 1
#define DC_FEAT_VERTEXCOLOR 0

static void BR_ASM_CALL dc_triangle_fill(struct brp_block *block,
    brp_vertex *v0, brp_vertex *v1, brp_vertex *v2,
    br_uint_16 *fp_vertices, br_uint_16 *fp_edges,
    br_vector4 *fp_eqn, struct temp_face *tfp)
{
    float wa = v0->comp_f[C_W];
    float wb = v1->comp_f[C_W];
    float wc = v2->comp_f[C_W];

    /* Clipping guarantees W > 0, but guard against degenerate input anyway so a
     * stray 1/w cannot feed the PowerVR garbage depth. */
    if (wa <= 1e-6f || wb <= 1e-6f || wc <= 1e-6f) {
        extern int g3d_diag_degenw;
        g3d_diag_degenw++;
        return;
    }

    {
        extern float g3d_diag_minw;
        float wmin = (wa < wb) ? wa : wb;
        if (wc < wmin) wmin = wc;
        if (wmin < g3d_diag_minw) g3d_diag_minw = wmin;
    }

    /* Diagnostic only (no effect on rendering): does comp_scales/offsets[C_I]
     * vary per material (consistent with material->index_base/index_range
     * being baked in, as the BRender material struct's "direct index ramp"
     * comment implies) or stay constant (e.g. lighting.c's generic 256/0)?
     * Settles which theory about the rainbow-coloured untextured shading is
     * right before changing the formula again. */
    {
        extern float g3d_diag_iscale_min, g3d_diag_iscale_max;
        extern float g3d_diag_ioffset_min, g3d_diag_ioffset_max;
        float is_ = rend.renderer->state.cache.comp_scales[C_I];
        float io = rend.renderer->state.cache.comp_offsets[C_I];
        if (is_ < g3d_diag_iscale_min) g3d_diag_iscale_min = is_;
        if (is_ > g3d_diag_iscale_max) g3d_diag_iscale_max = is_;
        if (io < g3d_diag_ioffset_min) g3d_diag_ioffset_min = io;
        if (io > g3d_diag_ioffset_max) g3d_diag_ioffset_max = io;
    }

    int texid = -1;
    int tw, th, ts, topaque = 1;
    float u0 = 0.f, v0v = 0.f, u1 = 0.f, v1v = 0.f, u2 = 0.f, v2v = 0.f;
    void *tpix = DC_FEAT_TEXTURE ? DC_GetCurrentTexture(rend.renderer->state.pstate, &tw, &th, &ts, &topaque) : NULL;
    if (tpix != NULL) {
        float us = rend.renderer->state.cache.comp_scales[C_U];
        float vs = rend.renderer->state.cache.comp_scales[C_V];
        float uo = rend.renderer->state.cache.comp_offsets[C_U];
        float vo = rend.renderer->state.cache.comp_offsets[C_V];

        texid = DCPVR3D_RegisterTexture(tpix, tw, th, ts);

        if (us != 0.f && vs != 0.f) {
            u0 = (v0->comp_f[C_U] - uo) / us;
            v0v = (v0->comp_f[C_V] - vo) / vs;
            u1 = (v1->comp_f[C_U] - uo) / us;
            v1v = (v1->comp_f[C_V] - vo) / vs;
            u2 = (v2->comp_f[C_U] - uo) / us;
            v2v = (v2->comp_f[C_V] - vo) / vs;
        }
    }

    /* Vertex colour. Textured surfaces modulate the texture by intensity (white
     * = full brightness) - this also covers a surface that HAS a texture but
     * whose PVR registration failed (texid < 0, texture cache full/VRAM
     * exhausted): it falls back to flat grey-by-intensity rather than a
     * texture-less draw, because comp_f[C_I] there is a 0..1 brightness, not a
     * palette index, and would otherwise pick a near-random palette entry as a
     * solid colour. Untextured flat/gouraud surfaces (tpix == NULL) have no
     * texture by design, and there comp_f[C_I] really is the final shade-ramp
     * palette index, so the real surface colour comes straight from the
     * palette - dc_face_colour (which treats C_I as 0..1) would clamp it to
     * white. */
    unsigned int c0, c1, c2;
    if (tpix != NULL) {
        c0 = dc_face_colour(v0);
        c1 = dc_face_colour(v1);
        c2 = dc_face_colour(v2);
        {
            extern float g3d_diag_minI, g3d_diag_maxI;
            float imin = v0->comp_f[C_I], imax = imin;
            if (v1->comp_f[C_I] < imin) imin = v1->comp_f[C_I];
            if (v2->comp_f[C_I] < imin) imin = v2->comp_f[C_I];
            if (v1->comp_f[C_I] > imax) imax = v1->comp_f[C_I];
            if (v2->comp_f[C_I] > imax) imax = v2->comp_f[C_I];
            if (imin < g3d_diag_minI) g3d_diag_minI = imin;
            if (imax > g3d_diag_maxI) g3d_diag_maxI = imax;
        }
        if (texid < 0) {
            extern int g3d_diag_texfail;
            g3d_diag_texfail++;
        }
    } else {
        /* No texture: flat-colour the whole face from material->index_base,
         * matching the reference OpenGL renderer's untextured path. See the
         * comment on DC_GetCurrentIndexBase for why this replaced a per-vertex
         * comp_f[C_I] palette lookup. */
        extern int DC_GetCurrentIndexBase(void *pstate);
        int idx = DC_GetCurrentIndexBase(rend.renderer->state.pstate);
        c0 = c1 = c2 = DCPVR3D_PaletteColor(idx);
        extern int g3d_diag_notex;
        g3d_diag_notex++;
    }

    if (!DC_FEAT_VERTEXCOLOR) {
        c0 = c1 = c2 = 0xFFFFFFFFu;
    }

    /* The car's drop shadow is ProcessShadow() re-rendering the ground directly
     * underneath a second time, relying on BRender's depth_shade_table being
     * temporarily pointed at a darker row to do the actual darkening (see
     * gShadow_dim_amount, graphics.c). We bypass that table entirely above, so
     * without this the "shadow" comes out at the same (or, since comp_f[C_I]
     * can exceed 1.0 and clamp to white, brighter) colour as the ground under
     * it - a light patch instead of a shadow. Approximate the missing
     * table-driven darkening with a flat multiplier derived from the same
     * gShadow_dim_amount: it ranges roughly 2.5 (barely darkened) to 7.5
     * (heavily darkened). */
    extern int g_dc_in_shadow_pass;
    if (g_dc_in_shadow_pass) {
        extern int gShadow_dim_amount;
        float scale = 1.0f - ((float)(gShadow_dim_amount - 2.5f) / 5.0f) * 0.7f;
        if (scale < 0.3f) scale = 0.3f;
        if (scale > 1.0f) scale = 1.0f;
        unsigned int sc = (unsigned int)(scale * 256.0f);
        c0 = (c0 & 0xFF000000u) | ((((c0 >> 16 & 0xFF) * sc) >> 8) << 16) | ((((c0 >> 8 & 0xFF) * sc) >> 8) << 8) | (((c0 & 0xFF) * sc) >> 8);
        c1 = (c1 & 0xFF000000u) | ((((c1 >> 16 & 0xFF) * sc) >> 8) << 16) | ((((c1 >> 8 & 0xFF) * sc) >> 8) << 8) | (((c1 & 0xFF) * sc) >> 8);
        c2 = (c2 & 0xFF000000u) | ((((c2 >> 16 & 0xFF) * sc) >> 8) << 16) | ((((c2 >> 8 & 0xFF) * sc) >> 8) << 8) | (((c2 & 0xFF) * sc) >> 8);
    }

    /* Pick the PowerVR list for this triangle:
     *  2 = blended  -> translucent list (shadows/skidmarks/shaded overlays blend
     *      over the road instead of z-fighting it); surface opacity -> alpha.
     *  1 = textured with a non-opaque map -> punch-through list, so colour index
     *      0 is keyed out (the transparent parts of fences, signs, etc).
     *  0 = everything else -> opaque list. */
    int category;
    if (rend.block->flags & BR_PRIMF_BLENDED) {
        category = 2;
        int a = (int)(rend.renderer->state.surface.opacity * 255.0f);
        if (a < 0) a = 0;
        if (a > 255) a = 255;
        unsigned int am = (unsigned int)a << 24;
        c0 = (c0 & 0x00FFFFFFu) | am;
        c1 = (c1 & 0x00FFFFFFu) | am;
        c2 = (c2 & 0x00FFFFFFu) | am;
    } else if (texid >= 0 && !topaque) {
        category = 1;
    } else {
        category = 0;
    }

    /* PowerVR depth is 1/w (larger = nearer). The shadow pass redraws the same
     * ground triangle a second time at the exact same depth as the original
     * (ProcessShadow copies list_ptr->v[0..2] verbatim) - the generic
     * per-submission-order bias below (DC3D_ZBIAS_PER_TRI) is tuned for
     * decals that are merely close to the surface they sit on, and is too
     * small to reliably win this exact-depth tie against floating point
     * noise from being computed via a separate vertex pass, causing it to
     * flicker in and out as the camera moves. Give the shadow (and oil spill
     * stains, which sit on the ground the same way - see g_dc_in_decal_pass
     * in graphics.c) a much larger, dedicated bias so they win outright
     * instead of relying on chance. */
    extern int g_dc_in_decal_pass;
    // Part of the flicker feature-isolation sweep (see DC_FEAT_* in dc_pvr.c):
    // off here as part of the all-off baseline.
#define DC_FEAT_DEDICATED_BIAS 0
    float zbias = (DC_FEAT_DEDICATED_BIAS && (g_dc_in_shadow_pass || g_dc_in_decal_pass)) ? 1.0005f : 1.0f;
    DCPVR3D_AddTriTex(
        v0->comp_f[C_SX], v0->comp_f[C_SY], zbias / wa, u0, v0v, c0,
        v1->comp_f[C_SX], v1->comp_f[C_SY], zbias / wb, u1, v1v, c1,
        v2->comp_f[C_SX], v2->comp_f[C_SY], zbias / wc, u2, v2v, c2,
        texid, category);
}
#endif

#ifndef V1Face_Render
static void GEOMETRY_CALL V1Face_Render(struct br_geometry *self, struct br_renderer *renderer)
{
	br_uint_16 (*fp_vertices)[3] = rend.face_vertices;
	br_uint_16 (*fp_edges)[3] = rend.face_edges;
	br_vector4 *fp_eqn = rend.face_eqn;
	struct temp_face *tfp = rend.temp_faces;
	int f;
	brp_block * unclipped = renderer->state.cache.face_blocks_onscreen[renderer->state.cache.nface_blocks_onscreen].chain;
	brp_block * clipped  = renderer->state.cache.face_blocks[renderer->state.cache.nface_blocks].chain;

	rend.prelit_colours = rend.face_colours;

	/*
	 * Go through each face in loop
	 */
	for(f=0; f < rend.nfaces; f++,fp_vertices++, fp_edges++, fp_eqn++, tfp++) {

		if(!(tfp->flag & TFF_VISIBLE))
			continue;

		rend.current_index = f;

		if(tfp->flag & TFF_CLIPPED) {
			clipped->render(clipped,
				rend.temp_vertices+(*fp_vertices)[0],
				rend.temp_vertices+(*fp_vertices)[1],
				rend.temp_vertices+(*fp_vertices)[2], (br_uint_16 *)fp_vertices, (br_uint_16 *)fp_edges, fp_eqn, tfp);
		} else {
			unclipped->render(unclipped,
				rend.temp_vertices+(*fp_vertices)[0],
				rend.temp_vertices+(*fp_vertices)[1],
				rend.temp_vertices+(*fp_vertices)[2], (br_uint_16 *)fp_vertices, (br_uint_16 *)fp_edges, fp_eqn, tfp);
		}
	}
}
#endif

#ifndef V1Face_OS_Render
void GEOMETRY_CALL V1Face_OS_Render(struct br_geometry *self, struct br_renderer *renderer)
{
	br_uint_16 (*fp_vertices)[3] = rend.face_vertices;
	br_uint_16 (*fp_edges)[3] = rend.face_edges;
	br_vector4 *fp_eqn = rend.face_eqn;
	struct temp_face *tfp = rend.temp_faces;
	int f;
	brp_block * unclipped = renderer->state.cache.face_blocks_onscreen[renderer->state.cache.nface_blocks_onscreen].chain;

	rend.prelit_colours = rend.face_colours;

	/*
	 * Go through each face in loop
	 */
	for(f=0; f < rend.nfaces; f++,fp_vertices++, fp_edges++, fp_eqn++, tfp++) {
		if(tfp->flag & TFF_VISIBLE) {

			rend.current_index = f;

			unclipped->render(unclipped,
				rend.temp_vertices+(*fp_vertices)[0],
				rend.temp_vertices+(*fp_vertices)[1],
				rend.temp_vertices+(*fp_vertices)[2],
				(br_uint_16 *)fp_vertices, (br_uint_16 *)fp_edges, fp_eqn, tfp);
		}
	}
}
#endif

#ifndef V1Face_OSV_Render
void GEOMETRY_CALL V1Face_OSV_Render(struct br_geometry *self, struct br_renderer *renderer)
{
	br_uint_16 (*fp_vertices)[3] = rend.face_vertices;
	br_uint_16 (*fp_edges)[3] = rend.face_edges;
	br_vector4 *fp_eqn = rend.face_eqn;
	struct temp_face *tfp = rend.temp_faces;
	int f;
	brp_block * unclipped = renderer->state.cache.face_blocks_onscreen[renderer->state.cache.nface_blocks_onscreen].chain;

	rend.prelit_colours = rend.face_colours;

	/*
	 * Go through each face in loop
	 */
	for(f=0; f < rend.nfaces; f++,fp_vertices++, fp_edges++, fp_eqn++, tfp++) {

		rend.current_index = f;

		unclipped->render(unclipped,
			rend.temp_vertices+(*fp_vertices)[0],
			rend.temp_vertices+(*fp_vertices)[1],
			rend.temp_vertices+(*fp_vertices)[2],
			(br_uint_16 *)fp_vertices, (br_uint_16 *)fp_edges, fp_eqn, tfp);
	}
}
#endif

#if BASED_FIXED
#define CONVERT_MASK_OTHER convert_mask_f
#endif

#if BASED_FLOAT
#define CONVERT_MASK_OTHER convert_mask_x
#endif

/*
 * Select initial culling operations
 */
static void GEOMETRY_CALL V1Faces_GeometryFnsCull(struct br_renderer *renderer)
{
	GeometryFunctionAdd(renderer, Vertex_ClearFlags);
	if(renderer->state.cull.type == BRT_TWO_SIDED)
		GeometryFunctionOnScreenAdd(renderer, Vertex_ClearFlags);

	switch(renderer->state.cull.type) {
	case BRT_ONE_SIDED:
// #if BASED_FLOAT
// 		if (renderer->device->hostInfo.processor_type == BRT_INTEL_PENTIUM_PRO)
// 			GeometryFunctionAdd(renderer, V1Face_CullOneSided_P6);
// 		else
// #endif
		GeometryFunctionAdd(renderer, V1Face_CullOneSided);
		GeometryFunctionOnScreenAdd(renderer, V1Face_OS_CullOneSided);
		break;

	case BRT_TWO_SIDED:
		GeometryFunctionAdd(renderer, V1Face_CullTwoSided);
		GeometryFunctionOnScreenAdd(renderer, V1Face_OS_CullTwoSided);
		break;

	default:
		GeometryFunctionAdd(renderer, V1Face_CullNone);
		GeometryFunctionOnScreenAdd(renderer, V1Face_OS_CullNone);
		break;
	}
}

/*
 * Generate a function to replicate constant values to all vertices
 */

static void GEOMETRY_CALL AddReplicateConstant(struct br_geometry *self, struct br_renderer *renderer)
{
   	if(rend.block->constant_mask == (1 << C_I)) {
		PrimBlockAddBoth(renderer, (brp_render_fn *)OpTriangleReplicateConstantI);
	} else if(rend.block->constant_mask == ((1 << C_R)|(1 << C_G)|(1 << C_B))) {
		PrimBlockAddBoth(renderer, (brp_render_fn *)OpTriangleReplicateConstantRGB);
	} else {
		PrimBlockAddBoth(renderer, (brp_render_fn *)OpTriangleReplicateConstant);
	}
}

/*
 * Generate the cached lists of geometry and per-face functions
 */
static void GEOMETRY_CALL V1Faces_GeometryFnsUpdate(struct br_geometry *self, struct br_renderer *renderer, br_boolean divert)
{
	/*
	 * Reset function lists
	 */
	GeometryFunctionReset(renderer);
	PrimBlockReset(renderer);

	/*
	 * Initially sharing of vertices in the primitive heap is allowed if no constant components are
	 * stored there
	 */
	renderer->state.cache.share_vertex_0 = rend.block->constant_components == 0;
	renderer->state.cache.share_other_vertices = !(rend.block->flags & BR_PRIMF_CONST_DUPLICATE);

	/**
	 ** Geometry operations
	 **/

	/*
	 * Setup
	 */
	GeometryFunctionBothAdd(renderer, V1Faces_ScratchAllocate);

	/*
	 * Model space Culling
	 */
	V1Faces_GeometryFnsCull(renderer);

	/*
	 * Vertex transform project, outcodes, bounds, & lighting
	 */
	VertexGeometryFns(renderer, V1Face_Outcode);

	/*
	 * Rendering
	 */
	GeometryFunctionAdd(renderer, V1Face_Render);

	if(renderer->state.cull.type == BRT_ONE_SIDED)
		GeometryFunctionOnScreenAdd(renderer, V1Face_OS_Render);
	else
		GeometryFunctionOnScreenAdd(renderer, V1Face_OSV_Render);

	/*
	 * Clean up
	 */
	GeometryFunctionBothAdd(renderer, ScratchFree);

	/**
	 ** Generate primitive output chain
	 **/

	/*
	 * Divert primitives to order table if required
	 */
	if(divert) {
#if BASED_FLOAT
		if(rend.block->convert_mask_i || rend.block->convert_mask_x) {
#endif
#if BASED_FIXED
		if ((rend.block->constant_components | rend.block->vertex_components) & CM_Q?
			rend.block->convert_mask_i || (rend.block->convert_mask_f & ~(1 << C_Q)) || (rend.block->convert_mask_x & (1 << C_Q)):
			rend.block->convert_mask_i || rend.block->convert_mask_f) {
#endif
			switch(rend.block->type) {
			case BRT_POINT:
				PrimBlockAddBoth(renderer, (brp_render_fn *)OpHeapAddPointConvert);
				break;
			case BRT_LINE:
				PrimBlockAddBoth(renderer, (brp_render_fn *)OpHeapAddLineConvert);
				break;
			case BRT_TRIANGLE:
				PrimBlockAddBoth(renderer, (brp_render_fn *)OpHeapAddTriangleConvert);
				break;
			default:
				break;
			}
		} else {
			switch(rend.block->type) {
			case BRT_POINT:
				PrimBlockAddBoth(renderer, (brp_render_fn *)OpHeapAddPoint);
				break;
			case BRT_LINE:
				PrimBlockAddBoth(renderer, (brp_render_fn *)OpHeapAddLine);
				break;
			case BRT_TRIANGLE:
				PrimBlockAddBoth(renderer, (brp_render_fn *)OpHeapAddTriangle);
				break;
			default:
				break;
			}
		}
	} else {

		/*
		 * Convertion of parameters
		 */
#if BASED_FLOAT
		if(rend.block->convert_mask_i || rend.block->convert_mask_x) {
#endif
#if BASED_FIXED
		if ((rend.block->constant_components | rend.block->vertex_components) & CM_Q?
			rend.block->convert_mask_i || (rend.block->convert_mask_f & ~(1 << C_Q)) || (rend.block->convert_mask_x & (1 << C_Q)):
			rend.block->convert_mask_i || rend.block->convert_mask_f) {
#endif
			switch(rend.block->type) {
			case BRT_POINT:
				PrimBlockAddBoth(renderer, (brp_render_fn *)RenderConvert1);
				break;
			case BRT_LINE:
				PrimBlockAddBoth(renderer, (brp_render_fn *)RenderConvert2);
				break;
			case BRT_TRIANGLE:
				PrimBlockAddBoth(renderer, (brp_render_fn *)RenderConvert3);
				break;
			default:
				break;
			}
		}
	}

	/*
	 * Clipping and constant parameter evalation, and optional fragmentation of triangles into lines or points
	 */
	switch(rend.block->type) {
	case BRT_POINT:
		PrimBlockAdd(renderer, (brp_render_fn *)OpTriangleToPoints);
		PrimBlockOnScreenAdd(renderer, (brp_render_fn *)OpTriangleToPoints_OS);

		if(renderer->state.cache.nconstant_fns) {
			AddReplicateConstant(self, renderer);
			if(renderer->state.cull.type == BRT_TWO_SIDED)
				PrimBlockAddBoth(renderer, (brp_render_fn *)OpTriangleTwoSidedConstantSurf);
			else
				PrimBlockAddBoth(renderer, (brp_render_fn *)OpTriangleConstantSurf);
		}
		break;

	case BRT_LINE:

		PrimBlockAdd(renderer, (brp_render_fn *)OpLineClip);
		PrimBlockAddBoth(renderer, (brp_render_fn *)OpTriangleToLines);

		if(renderer->state.cache.nconstant_fns) {
			AddReplicateConstant(self, renderer);

			if(renderer->state.cull.type == BRT_TWO_SIDED)
				PrimBlockAddBoth(renderer, (brp_render_fn *)OpTriangleTwoSidedConstantSurf);
			else
				PrimBlockAddBoth(renderer, (brp_render_fn *)OpTriangleConstantSurf);
		}
		break;

	case BRT_TRIANGLE:

		if(renderer->state.cache.nconstant_fns) {
			if(rend.block->flags & BR_PRIMF_CONST_DUPLICATE)
				AddReplicateConstant(self, renderer);

			PrimBlockAdd(renderer, (brp_render_fn *)OpTriangleClipConstantSurf);

			if(renderer->state.cull.type == BRT_TWO_SIDED)
				PrimBlockOnScreenAdd(renderer, (brp_render_fn *)OpTriangleTwoSidedConstantSurf);
			else
				PrimBlockOnScreenAdd(renderer, (brp_render_fn *)OpTriangleConstantSurf);
		} else
		   PrimBlockAdd(renderer, (brp_render_fn *)OpTriangleClip);

        /*
         * Optional subdivision if the primitive requests it
         */
        if(rend.block->flags & BR_PRIMF_SUBDIVIDE) {
    		PrimBlockAdd(renderer, (brp_render_fn *)OpTriangleSubdivide);
	    	PrimBlockOnScreenAdd(renderer, (brp_render_fn *)OpTriangleSubdivideOnScreen);
			SubdivideSetThreshold(rend.block->subdivide_tolerance);
        }
		break;

	default:
		break;
	}

	/*
	 * Two-sided relighting of vertices
	 */
	if((renderer->state.cull.type == BRT_TWO_SIDED) && renderer->state.cache.nvertex_fns)
	   PrimBlockAddBoth(renderer, (brp_render_fn *)OpTriangleRelightTwoSided);

	/*
	 * Environment wrapping fixup
	 */
	if((renderer->state.surface.mapping_source == BRT_ENVIRONMENT_INFINITE)||
	   (renderer->state.surface.mapping_source == BRT_ENVIRONMENT_LOCAL))
	   PrimBlockAddBoth(renderer, (brp_render_fn *)OpTriangleMappingWrapFix);

	/*
	 * Quad based texture mapping
	 */
	if(renderer->state.surface.mapping_source == BRT_QUAD) {

		PrimBlockAddBoth(renderer, (brp_render_fn *)OpTriangleMapQuad);

		/*
		 * Sharing of vertices is not possible once they have been quad mapped
		 */
		renderer->state.cache.share_vertex_0 = BR_FALSE;
		renderer->state.cache.share_other_vertices = BR_FALSE;
	}

	/*
	 * Update cache comparison info.
	 */
	renderer->state.cache.format = self;
}


static br_error V1Model_Render
		(struct br_geometry *self, struct br_renderer *renderer, struct v11model *model,
		struct br_renderer_state_stored *default_state,
		br_token type, br_boolean on_screen)
{
	int i,g;
	struct br_renderer_state_stored *state;
	br_error r;
	br_boolean z_sort, z_sort_blended, divert;

	/*
	 */
	CheckPrimitiveState(renderer);

	/*
	 * Remember current renderer and format in use
	 */
	rend.geometry = self;
	rend.renderer = renderer;

	rend.v11model = model;

	/*
	 * Make local copy of bounds
	 */
	scache.min = renderer->state.bounds.min;
	scache.max = renderer->state.bounds.max;

	/*
	 * Do per-scene and per-model updates if necessary
	 */
	if(!scache.valid_per_scene) {
		StaticCacheUpdate_PerScene(renderer);
		scache.valid_per_scene = BR_TRUE;
	}

	if(!scache.valid_per_model) {
		StaticCacheUpdate_PerModel(renderer);
		scache.valid_per_model = BR_TRUE;
	}

	/*
	 * Go through each group
	 */
	for(g=0; g < model->ngroups; g++) {
#if 1
		rend.face_vertices = (br_uint_16 (*)[3])model->groups[g].vertex_numbers;
		rend.face_edges = (br_uint_16 (*)[3])model->groups[g].edges;
		rend.face_eqn = model->groups[g].eqn;
#else
		rend.faces = NULL;//model->groups[g].faces;
#endif
#if 1
		rend.vertex_p = model->groups[g].position;
		rend.vertex_map = model->groups[g].map;
		rend.vertex_n = model->groups[g].normal;
#else
		rend.vertices = NULL;//model->groups[g].vertices;
#endif
		rend.face_colours = model->groups[g].face_colours;
		rend.vertex_colours = model->groups[g].vertex_colours;
		rend.face_flags = model->groups[g].face_flags;
		rend.nfaces = model->groups[g].nfaces;
		rend.nvertices = model->groups[g].nvertices;
		rend.nedges = model->groups[g].nedges;

		/*
	 	 * Restore state for group
		 */
		state = model->groups[g].stored?model->groups[g].stored:default_state;

		if(state)
			RendererStateRestore(renderer, state, (br_uint_32)BR_STATE_ALL);

		/*
		 * Synchronise with primitives library
		 */

		z_sort = (renderer->state.hidden.type == BRT_BUCKET_SORT &&
				 renderer->state.hidden.divert == BRT_ALL &&
				 renderer->state.hidden.order_table != NULL &&
				 scache.valid_primitive_heap);

		z_sort_blended = (renderer->state.hidden.type == BRT_BUCKET_SORT &&
				 renderer->state.hidden.divert == BRT_BLENDED &&
				 renderer->state.hidden.order_table != NULL &&
				 scache.valid_primitive_heap);

		r = PrimitiveStateRenderBegin(renderer->state.pstate,
			&rend.block, &rend.block_changed, &rend.range_changed, z_sort, type);

		if(r != BRE_OK)
			return r;

		/*
		 * Invalidate cache if divert status has changed
		 */
		divert = z_sort || z_sort_blended && rend.block->flags & BR_PRIMF_BLENDED;

		if (divert != renderer->state.cache.divert) {

			renderer->state.cache.divert = divert;
			renderer->state.cache.valid = BR_FALSE;
		}

		/*
		 * Calculate working data
		 */

		/*
		 * Merge opacity into alpha byte of colour
		 */
		scache.colour = renderer->state.surface.colour & 0xFFFFFF;
		scache.colour |= (br_uint_32)BrScalarToInt(BR_CONST_MUL(renderer->state.surface.opacity,256)) << 24;

		/*
		 * Make sure base primitive block is hooked up to the right place
		 */
		renderer->state.cache.face_blocks[0].chain = rend.block;
		renderer->state.cache.face_blocks_onscreen[0].chain = rend.block;

		if(rend.block_changed || rend.range_changed || !renderer->state.cache.valid) {

			CacheUpdate(renderer);

			if (rend.block_changed || !renderer->state.cache.valid)
				V1Faces_GeometryFnsUpdate(self, renderer, divert);

			renderer->state.cache.valid = BR_TRUE;
			renderer->state.timestamp_cache = Timestamp();
		}

#if BASED_FIXED
		if(on_screen) {
			scache.scale_x = renderer->state.cache.comp_scales[C_SX];
			scache.scale_y = renderer->state.cache.comp_scales[C_SY];
			scache.scale_z = renderer->state.cache.comp_scales[C_SZ];

			scache.offset_x = renderer->state.cache.comp_offsets[C_SX];
			scache.offset_y = renderer->state.cache.comp_offsets[C_SY];
			scache.offset_z = renderer->state.cache.comp_offsets[C_SZ];

			ModelToViewportUpdate();
		}
#endif
#ifdef __DREAMCAST__
		/*
		 * Dreamcast hardware path. The leaf of the prim-block chain is the
		 * pentprim rasteriser, which is x86 assembly and cannot run on the SH4.
		 * Redirect it to our PowerVR terminal fill. Everything ahead of it -
		 * culling, transform, clip, surface evaluation, texture mapping - is
		 * portable C and runs unchanged, so the fill receives fully processed,
		 * clipped, correctly-mapped triangles. Set per group because
		 * PrimitiveStateRenderBegin re-arms rend.block->render each time.
		 */
		rend.block->render = dc_triangle_fill;
#endif

		/*
		 * Invoke the current set of renderer functions on the group
		 */
		if(on_screen) {
			for(i=0; i < renderer->state.cache.ngeometry_fns_onscreen; i++)
				renderer->state.cache.geometry_fns_onscreen[i](self,renderer);
		} else {
			for(i=0; i < renderer->state.cache.ngeometry_fns; i++)
				renderer->state.cache.geometry_fns[i](self,renderer);
		}

		/*
		 * Save any cached state
		 */
		if(state)
			RendererStateSave(renderer, state, BR_STATE_CACHE);

		/*
		 * Finish with primitives
		 */
		PrimitiveStateRenderEnd(renderer->state.pstate,rend.block);
	}

	/*
	 * Put updated bounds back
	 */
	renderer->state.bounds.min = scache.min;
	renderer->state.bounds.max = scache.max;
	renderer->state.bounds.timestamp = Timestamp();

	return BRE_OK;
}

br_error BR_CMETHOD_DECL(br_geometry_v1_model_soft, render)
		(struct br_geometry *self, struct br_renderer *renderer, struct v11model *model,
		struct br_renderer_state_stored *default_state,
		br_token type)
{
	br_error r;

	r = V1Model_Render(self, renderer, model, default_state, type, BR_FALSE);

	return r;
}

br_error BR_CMETHOD_DECL(br_geometry_v1_model_soft, renderOnScreen)
		(struct br_geometry *self, struct br_renderer *renderer, struct v11model *model,
		struct br_renderer_state_stored *default_state,
		br_token type)
{
	br_error r;

	r = V1Model_Render(self, renderer, model, default_state, type, BR_TRUE);

	return r;
}
