/*
 * Copyright (c) 1993-1995 Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: genrend.c 1.4 1998/10/21 11:52:23 jon Exp $
 * $Locker: $
 *
 * Geometry operations that are used for several different formats
 */
#include <stddef.h>
#include <string.h>

#include "drv.h"
#include "shortcut.h"
#include "brassert.h"

#include "vecifns.h"

#ifdef __DREAMCAST__
/* sh4zam: use the SH4's hardware 4x4 matrix register (XMTRX) and FTRV to do the
 * per-vertex model-to-screen transform in one instruction instead of twelve
 * scalar multiply-adds. The matrix is loaded into XMTRX once per vertex batch.
 *
 * Layout: BRender computes out[c] = sum_k src[k]*m[k][c] (src.w = 1). br_matrix4
 * is row-major in memory; loading it column-major into XMTRX (what
 * shz_xmtrx_load_4x4 does) transposes it, which is exactly the form FTRV needs
 * to reproduce BRender's result. C_X..C_W are the consecutive vertex components
 * 1..4, matching br_vector4 v[X..W]. */
#include <sh4zam/shz_xmtrx.h>
#include <sh4zam/shz_vector.h>

/* Master switch for the sh4zam per-vertex transform in the pure (no per-vertex
 * lighting) transform functions below. Set to 0 to fall straight back to the
 * scalar path everywhere - a one-line revert if anything regresses. Only the
 * functions with NO lighting in their loop are converted: the lit ones
 * (*Surf/*Geom, which call renderer->state.cache.vertex_fns[] per vertex)
 * are left scalar because arbitrary lighting code in the loop could clobber
 * XMTRX between the load and the FTRV. */
#define DC_FEAT_SH4ZAM_XFORM 1

#if DC_FEAT_SH4ZAM_XFORM
/* Load the model-to-screen matrix into XMTRX once, before a transform loop. */
#define DC_LOAD_XMTRX() shz_xmtrx_load_4x4((const shz_mat4x4_t *)&scache.model_to_screen)
#define DC_TRANSFORM_VERTEX(dest, src) do {                                  \
    shz_vec4_t _shz_r = shz_xmtrx_transform_vec4(                            \
        shz_vec4_init((src)->v[X], (src)->v[Y], (src)->v[Z], 1.0f));         \
    (dest)->v[X] = _shz_r.x; (dest)->v[Y] = _shz_r.y;                        \
    (dest)->v[Z] = _shz_r.z; (dest)->v[W] = _shz_r.w;                        \
} while(0)
#else
#define DC_LOAD_XMTRX() ((void)0)
#define DC_TRANSFORM_VERTEX(dest, src) \
    TRANSFORM_VERTEX((br_vector4 *)(dest), (src), &scache.model_to_screen)
#endif

/* The transform-path measurement counters have been removed now that the
 * profiling is done (they showed all vertex volume goes through the scalar
 * paths, none through the previously-sh4zam'd Bounds function - which is why
 * the pure transform functions below were converted to FTRV). Left as no-ops
 * so the call sites need not be touched. */
#define DC_XF_FAST()
#define DC_XF_SCALAR()
#else
/* Non-Dreamcast: the pure transform functions below use these too, so define
 * them as the plain scalar path (no XMTRX). */
#define DC_LOAD_XMTRX() ((void)0)
#define DC_TRANSFORM_VERTEX(dest, src) \
    TRANSFORM_VERTEX((br_vector4 *)(dest), (src), &scache.model_to_screen)
#define DC_XF_FAST()
#define DC_XF_SCALAR()
#endif

/*
 * Transform, project, outcode and update screen bounds of visible vertices
 */
#ifndef Vertex_TransformProjectOutcodeBounds
static void GEOMETRY_CALL Vertex_TransformProjectOutcodeBounds(struct br_geometry *self, struct br_renderer *renderer)
{
	int v;
	brp_vertex *tvp = rend.temp_vertices;

#ifdef __DREAMCAST__
	/* Load the model-to-screen matrix into XMTRX once for the whole batch; the
	 * per-vertex FTRV below reads it. Nothing in the loop body touches XMTRX. */
	shz_xmtrx_load_4x4((const shz_mat4x4_t *)&scache.model_to_screen);
	DC_XF_FAST();
#endif

	if (rend.block->vertex_components & CM_Q)
		for(v=0; v < rend.nvertices; v++, tvp++) {

			if(rend.vertex_counts[v] == 0)
				continue;

#ifdef __DREAMCAST__
			DC_TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),&rend.vertex_p[v]);
#else
			TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),&rend.vertex_p[v], &scache.model_to_screen);
#endif
			OUTCODE_POINT(tvp->flags, (br_vector4 *)(tvp->comp+C_X));

			if(!(tvp->flags & OUTCODES_ALL)) {
				PROJECT_VERTEX_WRITE_Q(tvp,tvp->comp[C_X],tvp->comp[C_Y],tvp->comp[C_Z],tvp->comp[C_W]);
				UPDATE_BOUNDS(tvp);
			}
		}
	else
		for(v=0; v < rend.nvertices; v++, tvp++) {

			if(rend.vertex_counts[v] == 0)
				continue;

#ifdef __DREAMCAST__
			DC_TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),&rend.vertex_p[v]);
#else
			TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),&rend.vertex_p[v], &scache.model_to_screen);
#endif
			OUTCODE_POINT(tvp->flags, (br_vector4 *)(tvp->comp+C_X));

			if(!(tvp->flags & OUTCODES_ALL)) {
				PROJECT_VERTEX(tvp,tvp->comp[C_X],tvp->comp[C_Y],tvp->comp[C_Z],tvp->comp[C_W]);
				UPDATE_BOUNDS(tvp);
			}
		}
}
#endif

/*
 * Transform, project and outcode of visible vertices
 */
#ifndef Vertex_TransformProjectOutcode
static void GEOMETRY_CALL Vertex_TransformProjectOutcode(struct br_geometry *self, struct br_renderer *renderer)
{
	int v;
	brp_vertex *tvp = rend.temp_vertices;
	DC_LOAD_XMTRX();
	DC_XF_FAST();

	if (rend.block->vertex_components & CM_Q)
		for(v=0; v < rend.nvertices; v++, tvp++) {

			if(rend.vertex_counts[v] == 0)
				continue;

			DC_TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),&rend.vertex_p[v]);
			OUTCODE_POINT(tvp->flags, (br_vector4 *)(tvp->comp+C_X));

			if(!(tvp->flags & OUTCODES_ALL)) {
				PROJECT_VERTEX_WRITE_Q(tvp,tvp->comp[C_X],tvp->comp[C_Y],tvp->comp[C_Z],tvp->comp[C_W]);
			}
		}
	else
		for(v=0; v < rend.nvertices; v++, tvp++) {

			if(rend.vertex_counts[v] == 0)
				continue;

			DC_TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),&rend.vertex_p[v]);
			OUTCODE_POINT(tvp->flags, (br_vector4 *)(tvp->comp+C_X));

			if(!(tvp->flags & OUTCODES_ALL)) {
				PROJECT_VERTEX(tvp,tvp->comp[C_X],tvp->comp[C_Y],tvp->comp[C_Z],tvp->comp[C_W]);
			}
		}
}
#endif

/*
 * Transform, project and update screen bounds of visible vertices
 */
#ifndef Vertex_OS_TransformProjectBounds
static void GEOMETRY_CALL Vertex_OS_TransformProjectBounds(struct br_geometry *self, struct br_renderer *renderer)
{
	int v;
	brp_vertex *tvp = rend.temp_vertices;
	DC_LOAD_XMTRX();
	DC_XF_FAST();

	if (rend.block->vertex_components & CM_Q)
		for(v=0; v < rend.nvertices; v++, tvp++) {

			if(rend.vertex_counts[v] == 0)
				continue;

			DC_TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),&rend.vertex_p[v]);
			PROJECT_VERTEX_WRITE_Q(tvp,tvp->comp[C_X],tvp->comp[C_Y],tvp->comp[C_Z],tvp->comp[C_W]);
			UPDATE_BOUNDS(tvp);
		}
	else
		for(v=0; v < rend.nvertices; v++, tvp++) {

			if(rend.vertex_counts[v] == 0)
				continue;

			DC_TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),&rend.vertex_p[v]);
			PROJECT_VERTEX(tvp,tvp->comp[C_X],tvp->comp[C_Y],tvp->comp[C_Z],tvp->comp[C_W]);
			UPDATE_BOUNDS(tvp);
		}
}
#endif

/*
 * Transform and project visible vertices
 */
#ifndef Vertex_OS_TransformProject
static void GEOMETRY_CALL Vertex_OS_TransformProject(struct br_geometry *self, struct br_renderer *renderer)
{
	int v;
	brp_vertex *tvp = rend.temp_vertices;
	DC_LOAD_XMTRX();
	DC_XF_FAST();

	if (rend.block->vertex_components & CM_Q)
		for(v=0; v < rend.nvertices; v++, tvp++) {

			if(rend.vertex_counts[v] == 0)
				continue;

			DC_TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),&rend.vertex_p[v]);
			PROJECT_VERTEX_WRITE_Q(tvp,tvp->comp[C_X],tvp->comp[C_Y],tvp->comp[C_Z],tvp->comp[C_W]);
		}
	else
		for(v=0; v < rend.nvertices; v++, tvp++) {

			if(rend.vertex_counts[v] == 0)
				continue;

			DC_TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),&rend.vertex_p[v]);
			PROJECT_VERTEX(tvp,tvp->comp[C_X],tvp->comp[C_Y],tvp->comp[C_Z],tvp->comp[C_W]);
		}
}
#endif

/*
 * Transform, project, light and update screen bounds of visible vertices
 */
#ifndef Vertex_OS_TransformProjectBoundsSurf
static void GEOMETRY_CALL Vertex_OS_TransformProjectBoundsSurf(struct br_geometry *self, struct br_renderer *renderer)
{
	int v,i;
	brp_vertex *tvp = rend.temp_vertices;
	br_vector3 *vp_p = rend.vertex_p;
	br_vector2 *vp_map = rend.vertex_map;
	br_vector3 *vp_n = rend.vertex_n;

	/* The surface fns called per vertex below are all scalar after the
	 * DC lighting skip (UV map / unlit colour / alpha) and never touch
	 * XMTRX, so the batch-loaded matrix survives the whole loop - same
	 * pattern as the pure transform functions above. */
	DC_LOAD_XMTRX();
	rend.prelit_colours = rend.vertex_colours;

	if (rend.block->vertex_components & CM_Q)
		for(v=0; v < rend.nvertices; v++, vp_p++, vp_map++, vp_n++, tvp++) {

			if(rend.vertex_counts[v] == 0)
				continue;

			DC_TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),vp_p);
			PROJECT_VERTEX_WRITE_Q(tvp,tvp->comp[C_X],tvp->comp[C_Y],tvp->comp[C_Z],tvp->comp[C_W]);
			UPDATE_BOUNDS(tvp);

			rend.current_index = v;

			for(i=0; i < renderer->state.cache.nvertex_fns; i++)
				renderer->state.cache.vertex_fns[i](renderer, vp_p, vp_map, vp_n, scache.colour, tvp->comp);
		}
	else
		for(v=0; v < rend.nvertices; v++, vp_p++, vp_map++, vp_n++, tvp++) {

			if(rend.vertex_counts[v] == 0)
				continue;

			DC_TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),vp_p);
			PROJECT_VERTEX(tvp,tvp->comp[C_X],tvp->comp[C_Y],tvp->comp[C_Z],tvp->comp[C_W]);
			UPDATE_BOUNDS(tvp);

			rend.current_index = v;

			for(i=0; i < renderer->state.cache.nvertex_fns; i++)
				renderer->state.cache.vertex_fns[i](renderer, vp_p, vp_map, vp_n, scache.colour, tvp->comp);
		}
}
#endif

/*
 * Transform, project and light visible vertices
 */
#ifndef Vertex_OS_TransformProjectSurf
static void GEOMETRY_CALL Vertex_OS_TransformProjectSurf(struct br_geometry *self, struct br_renderer *renderer)
{
	int v,i;
	brp_vertex *tvp = rend.temp_vertices;
	br_vector3 *vp_p = rend.vertex_p;
	br_vector2 *vp_map = rend.vertex_map;
	br_vector3 *vp_n = rend.vertex_n;

	/* The surface fns called per vertex below are all scalar after the
	 * DC lighting skip (UV map / unlit colour / alpha) and never touch
	 * XMTRX, so the batch-loaded matrix survives the whole loop - same
	 * pattern as the pure transform functions above. */
	DC_LOAD_XMTRX();
	rend.prelit_colours = rend.vertex_colours;

	if (rend.block->vertex_components & CM_Q)
		for(v=0; v < rend.nvertices; v++, vp_p++, vp_map++, vp_n++, tvp++) {

			if(rend.vertex_counts[v] == 0)
				continue;

			DC_TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),vp_p);
			PROJECT_VERTEX_WRITE_Q(tvp,tvp->comp[C_X],tvp->comp[C_Y],tvp->comp[C_Z],tvp->comp[C_W]);

			rend.current_index = v;

			for(i=0; i < renderer->state.cache.nvertex_fns; i++)
				renderer->state.cache.vertex_fns[i](renderer, vp_p, vp_map, vp_n, scache.colour, tvp->comp);
		}
	else
		for(v=0; v < rend.nvertices; v++, vp_p++, vp_map++, vp_n++, tvp++) {

			if(rend.vertex_counts[v] == 0)
				continue;

			DC_TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),vp_p);
			PROJECT_VERTEX(tvp,tvp->comp[C_X],tvp->comp[C_Y],tvp->comp[C_Z],tvp->comp[C_W]);

			rend.current_index = v;

			for(i=0; i < renderer->state.cache.nvertex_fns; i++)
				renderer->state.cache.vertex_fns[i](renderer, vp_p, vp_map, vp_n, scache.colour, tvp->comp);
		}
}
#endif

/*
 * Transform, project, light and update screen bounds of visible vertices
 */
#ifndef Vertex_OS_TransformProjectBoundsGeom
static void GEOMETRY_CALL Vertex_OS_TransformProjectBoundsGeom(struct br_geometry *self, struct br_renderer *renderer)
{
	int v,i;
	brp_vertex *tvp = rend.temp_vertices;
	br_vector3 *vp_p = rend.vertex_p;
	br_vector2 *vp_map = rend.vertex_map;
	br_vector3 *vp_n = rend.vertex_n;

	/* The surface fns called per vertex below are all scalar after the
	 * DC lighting skip (UV map / unlit colour / alpha) and never touch
	 * XMTRX, so the batch-loaded matrix survives the whole loop - same
	 * pattern as the pure transform functions above. */
	DC_LOAD_XMTRX();
	rend.prelit_colours = rend.vertex_colours;

	if (rend.block->vertex_components & CM_Q)
		for(v=0; v < rend.nvertices; v++, vp_p++, vp_map++, vp_n++, tvp++) {

			if(rend.vertex_counts[v] == 0)
				continue;

			DC_TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),vp_p);
			PROJECT_VERTEX_WRITE_Q(tvp,tvp->comp[C_X],tvp->comp[C_Y],tvp->comp[C_Z],tvp->comp[C_W]);
			UPDATE_BOUNDS(tvp);

			rend.current_index = v;

			for(i=0; i < renderer->state.cache.nvertex_fns; i++)
				renderer->state.cache.vertex_fns[i](renderer, vp_p, vp_map, vp_n, rend.vertex_colours[v], tvp->comp);
		}
	else
		for(v=0; v < rend.nvertices; v++, vp_p++, vp_map++, vp_n++, tvp++) {

			if(rend.vertex_counts[v] == 0)
				continue;

			DC_TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),vp_p);
			PROJECT_VERTEX(tvp,tvp->comp[C_X],tvp->comp[C_Y],tvp->comp[C_Z],tvp->comp[C_W]);
			UPDATE_BOUNDS(tvp);

			rend.current_index = v;

			for(i=0; i < renderer->state.cache.nvertex_fns; i++)
				renderer->state.cache.vertex_fns[i](renderer, vp_p, vp_map, vp_n, rend.vertex_colours[v], tvp->comp);
		}
}
#endif

/*
 * Transform, project and light visible vertices
 */
#ifndef Vertex_OS_TransformProjectGeom
static void GEOMETRY_CALL Vertex_OS_TransformProjectGeom(struct br_geometry *self, struct br_renderer *renderer)
{
	int v,i;
	brp_vertex *tvp = rend.temp_vertices;
	br_vector3 *vp_p = rend.vertex_p;
	br_vector2 *vp_map = rend.vertex_map;
	br_vector3 *vp_n = rend.vertex_n;

	/* The surface fns called per vertex below are all scalar after the
	 * DC lighting skip (UV map / unlit colour / alpha) and never touch
	 * XMTRX, so the batch-loaded matrix survives the whole loop - same
	 * pattern as the pure transform functions above. */
	DC_LOAD_XMTRX();
	rend.prelit_colours = rend.vertex_colours;

	if (rend.block->vertex_components & CM_Q)
		for(v=0; v < rend.nvertices; v++, vp_p++, vp_map++, vp_n++, tvp++) {

			if(rend.vertex_counts[v] == 0)
				continue;

			DC_TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),vp_p);
			PROJECT_VERTEX_WRITE_Q(tvp,tvp->comp[C_X],tvp->comp[C_Y],tvp->comp[C_Z],tvp->comp[C_W]);

			rend.current_index = v;

			for(i=0; i < renderer->state.cache.nvertex_fns; i++)
				renderer->state.cache.vertex_fns[i](renderer, vp_p, vp_map, vp_n, rend.vertex_colours[v], tvp->comp);
		}
	else
		for(v=0; v < rend.nvertices; v++, vp_p++, vp_map++, vp_n++, tvp++) {

			if(rend.vertex_counts[v] == 0)
				continue;

			DC_TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),vp_p);
			PROJECT_VERTEX(tvp,tvp->comp[C_X],tvp->comp[C_Y],tvp->comp[C_Z],tvp->comp[C_W]);

			rend.current_index = v;

			for(i=0; i < renderer->state.cache.nvertex_fns; i++)
				renderer->state.cache.vertex_fns[i](renderer, vp_p, vp_map, vp_n, rend.vertex_colours[v], tvp->comp);
		}
}
#endif

#if 0
/*
 * Transform, project and update screen bounds of all vertives
 */
#ifndef Vertex_OSV_TransformProjectBounds
static void GEOMETRY_CALL Vertex_OSV_TransformProjectBounds(struct br_geometry *self, struct br_renderer *renderer)
{
	int v;
	brp_vertex *tvp = rend.temp_vertices;
	DC_XF_SCALAR();

	if (rend.block->vertex_components & CM_Q)
		for(v=0; v < rend.nvertices; v++, tvp++) {
			TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),&rend.vertex_p[v], &scache.model_to_screen);
			PROJECT_VERTEX_WRITE_Q(tvp,tvp->comp[C_X],tvp->comp[C_Y],tvp->comp[C_Z],tvp->comp[C_W]);
			UPDATE_BOUNDS(tvp);
		}
	else
		for(v=0; v < rend.nvertices; v++, tvp++) {
			TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),&rend.vertex_p[v], &scache.model_to_screen);
			PROJECT_VERTEX(tvp,tvp->comp[C_X],tvp->comp[C_Y],tvp->comp[C_Z],tvp->comp[C_W]);
			UPDATE_BOUNDS(tvp);
		}
}
#endif

/*
 * Transform and project all vertives
 */
#ifndef Vertex_OSV_TransformProject
static void GEOMETRY_CALL Vertex_OSV_TransformProject(struct br_geometry *self, struct br_renderer *renderer)
{
	int v;
	brp_vertex *tvp = rend.temp_vertices;
	DC_LOAD_XMTRX();

	if (rend.block->vertex_components & CM_Q)
		for(v=0; v < rend.nvertices; v++, tvp++) {
			DC_TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),&rend.vertex_p[v]);
			PROJECT_VERTEX_WRITE_Q(tvp,tvp->comp[C_X],tvp->comp[C_Y],tvp->comp[C_Z],tvp->comp[C_W]);
		}
	else
		for(v=0; v < rend.nvertices; v++, tvp++) {
			DC_TRANSFORM_VERTEX((br_vector4 *)(tvp->comp+C_X),&rend.vertex_p[v]);
			PROJECT_VERTEX(tvp,tvp->comp[C_X],tvp->comp[C_Y],tvp->comp[C_Z],tvp->comp[C_W]);
		}
}
#endif
#endif

/*
 * Generate per-vertex surface parmeters, colour from surface
 */
#ifndef Vertex_SurfaceComponentsSurf
static void GEOMETRY_CALL Vertex_SurfaceComponentsSurf(struct br_geometry *self, struct br_renderer *renderer)
{
	union brp_vertex *tvp = rend.temp_vertices;
	br_vector3 *vp_p = rend.vertex_p;
	br_vector2 *vp_map = rend.vertex_map;
	br_vector3 *vp_n = rend.vertex_n;
	int v,i;

	rend.prelit_colours = rend.vertex_colours;

	/*
	 * Simple hack for mapping
	 */
	for(v=0; v < rend.nvertices; v++, vp_p++, vp_map++, vp_n++, tvp++) {
		if(rend.vertex_counts[v] == 0)
			continue;

		rend.current_index = v;

		for(i=0; i < renderer->state.cache.nvertex_fns; i++)
			renderer->state.cache.vertex_fns[i](renderer, vp_p, vp_map, vp_n, scache.colour, tvp->comp);
	}
}
#endif

#ifndef Vertex_SurfaceComponentsTwoSidedSurf
static void GEOMETRY_CALL Vertex_SurfaceComponentsTwoSidedSurf(struct br_geometry *self, struct br_renderer *renderer)
{
	union brp_vertex *tvp = rend.temp_vertices;
	br_vector3 *vp_p = rend.vertex_p;
	br_vector2 *vp_map = rend.vertex_map;
	br_vector3 *vp_n = rend.vertex_n;
	int v,i;
	br_vector3 rev_normal;

	rend.prelit_colours = rend.vertex_colours;

	for(v=0; v < rend.nvertices; v++, vp_p++, vp_map++, vp_n++, tvp++) {
		if(rend.vertex_counts[v] == 0)
			continue;

		rend.current_index = v;

		if(tvp->flags & TVDIR_FRONT) {
			for(i=0; i < renderer->state.cache.nvertex_fns; i++)
				renderer->state.cache.vertex_fns[i](renderer, vp_p, vp_map, vp_n, scache.colour, tvp->comp);
		} else {
			BrVector3Negate(&rev_normal, vp_n);

			for(i=0; i < renderer->state.cache.nvertex_fns; i++)
				renderer->state.cache.vertex_fns[i](renderer,  vp_p, vp_map, &rev_normal, scache.colour, tvp->comp);
		}
	}
}
#endif

/*
 * Generate per-vertex surface parmeters, colour from geometry
 */
#ifndef Vertex_SurfaceComponentsGeom
static void GEOMETRY_CALL Vertex_SurfaceComponentsGeom(struct br_geometry *self, struct br_renderer *renderer)
{
	union brp_vertex *tvp = rend.temp_vertices;
	br_vector3 *vp_p = rend.vertex_p;
	br_vector2 *vp_map = rend.vertex_map;
	br_vector3 *vp_n = rend.vertex_n;
	int v,i;

	rend.prelit_colours = rend.vertex_colours;

	for(v=0; v < rend.nvertices; v++, vp_p++, vp_map++, vp_n++, tvp++) {
		if(rend.vertex_counts[v] == 0)
			continue;

		rend.current_index = v;

		for(i=0; i < renderer->state.cache.nvertex_fns; i++)
			renderer->state.cache.vertex_fns[i](renderer, vp_p, vp_map, vp_n, rend.vertex_colours[v], tvp->comp);
	}
}
#endif

#ifndef Vertex_SurfaceComponentsTwoSidedGeom
static void GEOMETRY_CALL Vertex_SurfaceComponentsTwoSidedGeom(struct br_geometry *self, struct br_renderer *renderer)
{
	union brp_vertex *tvp = rend.temp_vertices;
	br_vector3 *vp_p = rend.vertex_p;
	br_vector2 *vp_map = rend.vertex_map;
	br_vector3 *vp_n = rend.vertex_n;
	br_vector3 rev_normal;
	int v,i;

	DC_XF_SCALAR();

	for(v=0; v < rend.nvertices; v++, vp_p++, vp_map++, vp_n++, tvp++) {
		if(rend.vertex_counts[v] == 0)
			continue;

		rend.current_index = v;

		if(tvp->flags & TVDIR_FRONT) {
			for(i=0; i < renderer->state.cache.nvertex_fns; i++)
				renderer->state.cache.vertex_fns[i](renderer, vp_p, vp_map, vp_n, rend.vertex_colours[v], tvp->comp);
		} else {
			BrVector3Negate(&rev_normal, vp_n);

			for(i=0; i < renderer->state.cache.nvertex_fns; i++)
				renderer->state.cache.vertex_fns[i](renderer, vp_p, vp_map, &rev_normal, rend.vertex_colours[v], tvp->comp);
		}
	}
}
#endif

/*
 * Handles force front by fixing up C_Z and C_SZ
 */
#ifndef Vertex_ForceFront
static void GEOMETRY_CALL Vertex_ForceFront(struct br_geometry *self, struct br_renderer *renderer)
{
	union brp_vertex *tvp = rend.temp_vertices;
	int v;

	for(v=0; v < rend.nvertices; v++, tvp++) {
		if(rend.vertex_counts[v] == 0)
			continue;

		tvp->comp[C_SZ] = renderer->state.cache.comp_offsets[C_SZ]+renderer->state.cache.comp_scales[C_SZ];
		tvp->comp[C_Z] = tvp->comp[C_W] - BR_SCALAR_EPSILON;
	}
}
#endif

#ifndef Vertex_ForceBack
static void GEOMETRY_CALL Vertex_ForceBack(struct br_geometry *self, struct br_renderer *renderer)
{
	union brp_vertex *tvp = rend.temp_vertices;
	int v;

	for(v=0; v < rend.nvertices; v++, tvp++) {
		if(rend.vertex_counts[v] == 0)
			continue;

		tvp->comp[C_SZ] = renderer->state.cache.comp_offsets[C_SZ]-renderer->state.cache.comp_scales[C_SZ];
		tvp->comp[C_Z] = BR_SCALAR_EPSILON-tvp->comp[C_W];
	}
}
#endif

/*
 * Handles applying a depth bias (could be done more efficiently elsewhere)
 */
#ifndef Vertex_DepthBias
static void GEOMETRY_CALL Vertex_DepthBias(struct br_geometry *self, struct br_renderer *renderer)
{
	union brp_vertex *tvp = rend.temp_vertices;
	int v;
	br_scalar min, max;

	if (renderer->state.cache.comp_scales[C_SZ] < BR_SCALAR(0.0)) {

		min = renderer->state.cache.comp_offsets[C_SZ] + renderer->state.cache.comp_scales[C_SZ];
		max = renderer->state.cache.comp_offsets[C_SZ] - renderer->state.cache.comp_scales[C_SZ];

	} else {

		min = renderer->state.cache.comp_offsets[C_SZ] - renderer->state.cache.comp_scales[C_SZ];
		max = renderer->state.cache.comp_offsets[C_SZ] + renderer->state.cache.comp_scales[C_SZ];
	}

	for(v=0; v < rend.nvertices; v++, tvp++) {
		if(rend.vertex_counts[v] == 0)
			continue;

		tvp->comp[C_SZ] += renderer->state.surface.depth_bias;

		if (tvp->comp[C_SZ] < min)
			tvp->comp[C_SZ] = min;

		else if (tvp->comp[C_SZ] > max)
			tvp->comp[C_SZ] = max;
	}
}
#endif

#ifndef ScratchFree
void GEOMETRY_CALL ScratchFree(struct br_geometry *self, struct br_renderer *renderer)
{
		BrScratchFree(rend.scratch);

		rend.scratch = NULL;
}
#endif

/*
 * Clear vertex flags
 */
#ifndef Vertex_ClearFlags
void GEOMETRY_CALL Vertex_ClearFlags(struct br_geometry *self, struct br_renderer *renderer)
{
	int v;

	for(v=0; v< rend.nvertices; v++)
		rend.temp_vertices[v].flags = OUTCODES_NOT;
}
#endif

/*
 * Select per vertex geometry operations
 */
void VertexGeometryFns(struct br_renderer *renderer, geometry_fn *prim_outcode)
{

	/**
 	 ** Partially on screen geometry
	 **/
	if(renderer->state.enable.flags & ENBL_BOUNDS)
		GeometryFunctionAdd(renderer, Vertex_TransformProjectOutcodeBounds);
	else
		GeometryFunctionAdd(renderer, Vertex_TransformProjectOutcode);

	/*
	 * Apply any per-primitive outcoding to further reduce the visible primitives
	 * and vertices
	 */
	if(prim_outcode)
		GeometryFunctionAdd(renderer, prim_outcode);

	if(renderer->state.cache.nvertex_fns) {
		if(renderer->state.cull.type == BRT_TWO_SIDED) {
			if(renderer->state.surface.colour_source == BRT_GEOMETRY)
				GeometryFunctionAdd(renderer, Vertex_SurfaceComponentsTwoSidedGeom);
			else
				GeometryFunctionAdd(renderer, Vertex_SurfaceComponentsTwoSidedSurf);
		} else {
			if(renderer->state.surface.colour_source == BRT_GEOMETRY)
				GeometryFunctionAdd(renderer, Vertex_SurfaceComponentsGeom);
			else
				GeometryFunctionAdd(renderer, Vertex_SurfaceComponentsSurf);
		}
	}

	/**
	 ** Fully on screen geometry
	 **/
	if(renderer->state.cache.nvertex_fns) {
		if(renderer->state.cull.type == BRT_TWO_SIDED) {
			if(renderer->state.enable.flags & ENBL_BOUNDS)
				GeometryFunctionOnScreenAdd(renderer, Vertex_OS_TransformProjectBounds);
			else
				GeometryFunctionOnScreenAdd(renderer, Vertex_OS_TransformProject);

			if(renderer->state.surface.colour_source == BRT_GEOMETRY)
				GeometryFunctionOnScreenAdd(renderer, Vertex_SurfaceComponentsTwoSidedGeom);
			else
				GeometryFunctionOnScreenAdd(renderer, Vertex_SurfaceComponentsTwoSidedSurf);

		} else {
			if(renderer->state.enable.flags & ENBL_BOUNDS) {
				if(renderer->state.surface.colour_source == BRT_GEOMETRY)
					GeometryFunctionOnScreenAdd(renderer, Vertex_OS_TransformProjectBoundsGeom);
				else
					GeometryFunctionOnScreenAdd(renderer, Vertex_OS_TransformProjectBoundsSurf);
			} else {
				if(renderer->state.surface.colour_source == BRT_GEOMETRY)
					GeometryFunctionOnScreenAdd(renderer, Vertex_OS_TransformProjectGeom);
				else
					GeometryFunctionOnScreenAdd(renderer, Vertex_OS_TransformProjectSurf);
			}
		}
	} else {
		if(renderer->state.enable.flags & ENBL_BOUNDS)
			GeometryFunctionOnScreenAdd(renderer, Vertex_OS_TransformProjectBounds);
		else
			GeometryFunctionOnScreenAdd(renderer, Vertex_OS_TransformProject);
	}

	/*
	 * Tweak for FORCE_FRONT and FORCE_BACK styles, or for applying a depth bias
	 */
	if(renderer->state.surface.force_front)
		GeometryFunctionBothAdd(renderer, Vertex_ForceFront);

	else if(renderer->state.surface.force_back)
		GeometryFunctionBothAdd(renderer, Vertex_ForceBack);

	else if(renderer->state.surface.depth_bias != BR_SCALAR(0.0))
		GeometryFunctionBothAdd(renderer, Vertex_DepthBias);
}
