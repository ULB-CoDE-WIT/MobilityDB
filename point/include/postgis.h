/*****************************************************************************
 *
 * This MobilityDB code is provided under The PostgreSQL License.
 *
 * Copyright (c) 2016-2021, Université libre de Bruxelles and MobilityDB
 * contributors
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written
 * agreement is hereby granted, provided that the above copyright notice and
 * this paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL UNIVERSITE LIBRE DE BRUXELLES BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
 * EVEN IF UNIVERSITE LIBRE DE BRUXELLES HAS BEEN ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * UNIVERSITE LIBRE DE BRUXELLES SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON
 * AN "AS IS" BASIS, AND UNIVERSITE LIBRE DE BRUXELLES HAS NO OBLIGATIONS TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS. 
 *
 *****************************************************************************/

/**
 * @file postgis.h
 * PostGIS definitions that are needed in MobilityDB but are not exported
 * in PostGIS headers
 */

#ifndef __POSTGIS_H__
#define __POSTGIS_H__

#define ACCEPT_USE_OF_DEPRECATED_PROJ_API_H 1

#include <liblwgeom.h>

/*****************************************************************************/
// Definitions needed for developing geography_line_interpolate_point

/**
* Conversion functions
*/
#define deg2rad(d) (M_PI * (d) / 180.0)
#define rad2deg(r) (180.0 * (r) / M_PI)

/**
* Point in spherical coordinates on the world. Units of radians.
*/
typedef struct
{
  double lon;
  double lat;
} GEOGRAPHIC_POINT;


/**
* Two-point great circle segment from a to b.
*/
typedef struct
{
  GEOGRAPHIC_POINT start;
  GEOGRAPHIC_POINT end;
} GEOGRAPHIC_EDGE;


extern int spheroid_init_from_srid(FunctionCallInfo fcinfo, int srid, SPHEROID *s);
extern double ptarray_length_spheroid(const POINTARRAY *pa, const SPHEROID *s);
extern int lwline_is_empty(const LWLINE *line);
extern void geographic_point_init(double lon, double lat, GEOGRAPHIC_POINT *g);
extern double sphere_distance(const GEOGRAPHIC_POINT *s, const GEOGRAPHIC_POINT *e);
extern void geog2cart(const GEOGRAPHIC_POINT *g, POINT3D *p);
extern void cart2geog(const POINT3D *p, GEOGRAPHIC_POINT *g);
extern void normalize(POINT3D *p);
extern double edge_distance_to_point(const GEOGRAPHIC_EDGE *e,
  const GEOGRAPHIC_POINT *gp, GEOGRAPHIC_POINT *closest);
extern uint32_t edge_intersects(const POINT3D *A1, const POINT3D *A2,
  const POINT3D *B1, const POINT3D *B2);
extern int edge_intersection(const GEOGRAPHIC_EDGE *e1, const GEOGRAPHIC_EDGE *e2,
  GEOGRAPHIC_POINT *g);
extern double edge_distance_to_edge(const GEOGRAPHIC_EDGE *e1, const GEOGRAPHIC_EDGE *e2,
  GEOGRAPHIC_POINT *closest1, GEOGRAPHIC_POINT *closest2);

/*****************************************************************************/

/* Definitions copied from gserialized_gist.h */

/*
 * This macro is based on PG_FREE_IF_COPY, except that it accepts two pointers.
 * See PG_FREE_IF_COPY comment in src/include/fmgr.h in postgres source code
 * for more details.
 */

#define POSTGIS_FREE_IF_COPY_P(ptrsrc, ptrori) \
  do { \
    if ((Pointer) (ptrsrc) != (Pointer) (ptrori)) \
      pfree(ptrsrc); \
  } while (0)

/* Definitions copied from measures.h */

#define DIST_MAX    -1
#define DIST_MIN    1

typedef struct
{
  double distance;  /*the distance between p1 and p2*/
  POINT2D p1;
  POINT2D p2;
  int mode;  /*the direction of looking, if thedir = -1 then we look for maxdistance and if it is 1 then we look for mindistance*/
  int twisted; /*To preserve the order of incoming points to match the first and second point in shortest and longest line*/
  double tolerance; /*the tolerance for dwithin and dfullywithin*/
} DISTPTS;

extern int lw_dist2d_recursive(const LWGEOM *lw1, const LWGEOM *lw2, DISTPTS *dl);

/*  Finds the two closest points and distance between two linesegments */
extern int lw_dist2d_seg_seg(const POINT2D *A, const POINT2D *B, const POINT2D *C, const POINT2D *D, DISTPTS *dl);

/* Definitions copied from measures3d.h */

typedef struct
{
  double distance;  /*the distance between p1 and p2*/
  POINT3DZ p1;
  POINT3DZ p2;
  int mode;  /*the direction of looking, if thedir = -1 then we look for 3dmaxdistance and if it is 1 then we look for 3dmindistance*/
  int twisted; /*To preserve the order of incoming points to match the first and second point in 3dshortest and 3dlongest line*/
  double tolerance; /*the tolerance for 3ddwithin and 3ddfullywithin*/
} DISTPTS3D;

typedef struct
{
  double  x,y,z;
}
VECTOR3D;

#define DOT(u,v)   ((u).x * (v).x + (u).y * (v).y + (u).z * (v).z)
#define VECTORLENGTH(v)   sqrt(((v).x * (v).x) + ((v).y * (v).y) + ((v).z * (v).z))

extern int lw_dist3d_pt_pt(POINT3DZ *p1, POINT3DZ *p2, DISTPTS3D *dl);
extern int lw_dist3d_pt_seg(POINT3DZ *p, POINT3DZ *A, POINT3DZ *B, DISTPTS3D *dl);
extern int lw_dist3d_recursive(const LWGEOM *lwg1,const LWGEOM *lwg2, DISTPTS3D *dl);

/*  Finds the two closest points and distance between two linesegments */
extern int lw_dist3d_seg_seg(POINT3DZ *s1p1, POINT3DZ *s1p2, POINT3DZ *s2p1, POINT3DZ *s2p2, DISTPTS3D *dl);

/* Definitions copied from lwgeodetic.h */

/**
* Bitmask elements for edge_intersects() return value.
*/
#define PIR_NO_INTERACT    0x00
#define PIR_INTERSECTS     0x01
#define PIR_COLINEAR       0x02
#define PIR_A_TOUCH_RIGHT   0x04
#define PIR_A_TOUCH_LEFT  0x08
#define PIR_B_TOUCH_RIGHT   0x10
#define PIR_B_TOUCH_LEFT  0x20

extern double spheroid_distance(const GEOGRAPHIC_POINT *a, const GEOGRAPHIC_POINT *b, const SPHEROID *spheroid);
extern int geographic_point_equals(const GEOGRAPHIC_POINT *g1, const GEOGRAPHIC_POINT *g2);

/* Definitions copied from lwgeodetic_tree.h */

typedef struct circ_node
{
  GEOGRAPHIC_POINT center;
  double radius;
  uint32_t num_nodes;
  struct circ_node** nodes;
  int edge_num;
  uint32_t geom_type;
  double d;
  POINT2D pt_outside;
  POINT2D* p1;
  POINT2D* p2;
} CIRC_NODE;

extern CIRC_NODE* lwgeom_calculate_circ_tree(const LWGEOM* lwgeom);
extern void circ_tree_free(CIRC_NODE* node);

/* Definitions copied from liblwgeom_internal.h */

/**
* Floating point comparators.
*/
#define FP_TOLERANCE 1e-12
#define FP_IS_ZERO(A) (fabs(A) <= FP_TOLERANCE)
#define FP_MAX(A, B) (((A) > (B)) ? (A) : (B))
#define FP_MIN(A, B) (((A) < (B)) ? (A) : (B))
#define FP_ABS(a)   ((a) <  (0) ? -(a) : (a))
#define FP_EQUALS(A, B) (fabs((A)-(B)) <= FP_TOLERANCE)
#define FP_NEQUALS(A, B) (fabs((A)-(B)) > FP_TOLERANCE)
#define FP_LT(A, B) (((A) + FP_TOLERANCE) < (B))
#define FP_LTEQ(A, B) (((A) - FP_TOLERANCE) <= (B))
#define FP_GT(A, B) (((A) - FP_TOLERANCE) > (B))
#define FP_GTEQ(A, B) (((A) + FP_TOLERANCE) >= (B))
#define FP_CONTAINS_TOP(A, X, B) (FP_LT(A, X) && FP_LTEQ(X, B))
#define FP_CONTAINS_BOTTOM(A, X, B) (FP_LTEQ(A, X) && FP_LT(X, B))
#define FP_CONTAINS_INCL(A, X, B) (FP_LTEQ(A, X) && FP_LTEQ(X, B))
#define FP_CONTAINS_EXCL(A, X, B) (FP_LT(A, X) && FP_LT(X, B))
#define FP_CONTAINS(A, X, B) FP_CONTAINS_EXCL(A, X, B)

/**
* Snap-to-grid Support
*/
typedef struct gridspec_t
{
	double ipx;
	double ipy;
	double ipz;
	double ipm;
	double xsize;
	double ysize;
	double zsize;
	double msize;
}
gridspec;

extern int p4d_same(const POINT4D *p1, const POINT4D *p2);
extern int p3d_same(const POINT3D *p1, const POINT3D *p2);
extern int p2d_same(const POINT2D *p1, const POINT2D *p2);

/* PostGIS functions called by MobilityDB */

extern void srid_is_latlong(FunctionCallInfo fcinfo, int srid);
extern int clamp_srid(int srid);
extern int getSRIDbySRS(FunctionCallInfo fcinfo, const char *srs);

extern char *getSRSbySRID(FunctionCallInfo fcinfo, int32_t srid, bool short_crs);

extern int lwprint_double(double d, int maxdd, char *buf, size_t bufsize);
extern char getMachineEndian(void);
extern char lwpoint_same(const LWPOINT *p1, const LWPOINT *p2);
extern LWPOINT *lwpoint_clone(const LWPOINT *lwgeom);

extern Datum transform(PG_FUNCTION_ARGS);
extern Datum buffer(PG_FUNCTION_ARGS);
extern Datum centroid(PG_FUNCTION_ARGS);

extern Datum geography_from_geometry(PG_FUNCTION_ARGS);
extern Datum geometry_from_geography(PG_FUNCTION_ARGS);

extern Datum boundary(PG_FUNCTION_ARGS);
extern Datum contains(PG_FUNCTION_ARGS);
extern Datum containsproperly(PG_FUNCTION_ARGS);
extern Datum covers(PG_FUNCTION_ARGS);
extern Datum coveredby(PG_FUNCTION_ARGS);
extern Datum crosses(PG_FUNCTION_ARGS);
extern Datum disjoint(PG_FUNCTION_ARGS);
extern Datum ST_Equals(PG_FUNCTION_ARGS);
extern Datum intersects(PG_FUNCTION_ARGS); /* For 2D */
extern Datum intersects3d(PG_FUNCTION_ARGS); /* For 3D */
extern Datum issimple(PG_FUNCTION_ARGS);
extern Datum overlaps(PG_FUNCTION_ARGS);
extern Datum pgis_union_geometry_array(PG_FUNCTION_ARGS);
extern Datum touches(PG_FUNCTION_ARGS);
extern Datum within(PG_FUNCTION_ARGS);
extern Datum relate_full(PG_FUNCTION_ARGS);
extern Datum relate_pattern(PG_FUNCTION_ARGS);

extern Datum intersection(PG_FUNCTION_ARGS);
extern Datum distance(PG_FUNCTION_ARGS); /* For 2D */
extern Datum distance3d(PG_FUNCTION_ARGS); /* For 3D */

extern Datum BOX2D_to_LWGEOM(PG_FUNCTION_ARGS);
extern Datum BOX3D_to_LWGEOM(PG_FUNCTION_ARGS);

extern Datum LWGEOM_addpoint(PG_FUNCTION_ARGS);
extern Datum LWGEOM_azimuth(PG_FUNCTION_ARGS);
extern Datum LWGEOM_closestpoint(PG_FUNCTION_ARGS); /* For 2D */
extern Datum LWGEOM_closestpoint3d(PG_FUNCTION_ARGS); /* For 3D */
extern Datum LWGEOM_collect_garray(PG_FUNCTION_ARGS);
extern Datum LWGEOM_dwithin(PG_FUNCTION_ARGS); /* For 2D */
extern Datum LWGEOM_dwithin3d(PG_FUNCTION_ARGS); /* For 3D */
extern Datum LWGEOM_geometryn_collection(PG_FUNCTION_ARGS);
extern Datum LWGEOM_get_srid(PG_FUNCTION_ARGS);  /* also for geography */
extern Datum LWGEOM_set_srid(PG_FUNCTION_ARGS);
extern Datum LWGEOM_isempty(PG_FUNCTION_ARGS);
extern Datum LWGEOM_length_linestring(PG_FUNCTION_ARGS);
extern Datum LWGEOM_line_locate_point(PG_FUNCTION_ARGS);
extern Datum LWGEOM_line_interpolate_point(PG_FUNCTION_ARGS);
extern Datum LWGEOM_line_substring(PG_FUNCTION_ARGS);
extern Datum LWGEOM_makepoint(PG_FUNCTION_ARGS);
extern Datum LWGEOM_numgeometries_collection(PG_FUNCTION_ARGS);
extern Datum LWGEOM_numpoints_linestring(PG_FUNCTION_ARGS);
extern Datum LWGEOM_pointn_linestring(PG_FUNCTION_ARGS);
extern Datum LWGEOM_reverse(PG_FUNCTION_ARGS);
extern Datum LWGEOM_setpoint_linestring(PG_FUNCTION_ARGS);
extern Datum LWGEOM_shortestline2d(PG_FUNCTION_ARGS); /* For 2D */
extern Datum LWGEOM_shortestline3d(PG_FUNCTION_ARGS); /* For 3D */

extern Datum lwgeom_eq(PG_FUNCTION_ARGS);
extern Datum lwgeom_lt(PG_FUNCTION_ARGS);
extern Datum lwgeom_hash(PG_FUNCTION_ARGS);

extern Datum geography_covers(PG_FUNCTION_ARGS);
extern Datum geography_length(PG_FUNCTION_ARGS);
extern Datum geography_dwithin(PG_FUNCTION_ARGS);
extern Datum geography_distance(PG_FUNCTION_ARGS);
extern Datum geography_azimuth(PG_FUNCTION_ARGS);
extern Datum geography_bestsrid(PG_FUNCTION_ARGS);

extern Datum geography_eq(PG_FUNCTION_ARGS);
extern Datum geography_lt(PG_FUNCTION_ARGS);

#define PG_GETARG_GSERIALIZED_P(varno) ((GSERIALIZED *)PG_DETOAST_DATUM(PG_GETARG_DATUM(varno)))
#define PG_GETARG_GSERIALIZED_P_COPY(varno) ((GSERIALIZED *)PG_DETOAST_DATUM_COPY(PG_GETARG_DATUM(varno)))

#include "temporal.h"
#include <liblwgeom.h>

#endif /* __TEMPORAL_POSTGIS_H__ */
/*****************************************************************************/
