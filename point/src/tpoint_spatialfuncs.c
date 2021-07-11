/***********************************************************************
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
 * @file tpoint_spatialfuncs.c
 * Spatial functions for temporal points.
 */

#include "tpoint_spatialfuncs.h"

#include <assert.h>
#include <liblwgeom.h>

#include "period.h"
#include "periodset.h"
#include "timeops.h"
#include "temporaltypes.h"
#include "tempcache.h"
#include "tnumber_mathfuncs.h"

#include "postgis.h"
#include "stbox.h"
#include "tpoint.h"
#include "tpoint_boxops.h"
#include "tpoint_spatialrels.h"

/*****************************************************************************
 * PostGIS cache functions
 *****************************************************************************/

/**
 * Global variable to save the fcinfo when PostGIS functions need to access
 * the proj cache such as transform, geography_distance, or geography_azimuth
 */
FunctionCallInfo _FCINFO;

/**
 * Fetch from the cache the fcinfo of the external function
 */
FunctionCallInfo
fetch_fcinfo()
{
  assert(_FCINFO);
  return _FCINFO;
}

/**
 * Store in the cache the fcinfo of the external function
 */
void
store_fcinfo(FunctionCallInfo fcinfo)
{
  _FCINFO = fcinfo;
  return;
}

/*****************************************************************************
 * Utility functions
 *****************************************************************************/

/*
 * Obtain a geometry/geography point from the GSERIALIZED WITHOUT creating
 * the corresponding LWGEOM. These functions constitute a **SERIOUS**
 * break of encapsulation but it is the only way to achieve reasonable
 * performance when manipulating mobility data.
 * The datum_* functions suppose that the GSERIALIZED has been already
 * detoasted. This is typically the case when the datum is within a Temporal*
 * that has been already detoasted with PG_GETARG_TEMPORAL*
 * The first variant (e.g. datum_get_point2d) is slower than the second (e.g.
 * datum_get_point2d_p) since the point is passed by value and thus the bytes
 * are copied. The second version is declared const because you aren't allowed
 * to modify the values, only read them.
 */

#define GS_POINT_PTR(gs)    ((uint8_t *) gs->data + 8)

/**
 * Returns a 2D point from the datum
 */
POINT2D
datum_get_point2d(Datum geom)
{
  GSERIALIZED *gs = (GSERIALIZED *) DatumGetPointer(geom);
  POINT2D *point = (POINT2D *) GS_POINT_PTR(gs);
  return *point;
}

/**
 * Returns a pointer to a 2D point from the datum
 */
const POINT2D *
datum_get_point2d_p(Datum geom)
{
  GSERIALIZED *gs = (GSERIALIZED *) DatumGetPointer(geom);
  return (const POINT2D *) GS_POINT_PTR(gs);
}

/**
 * Returns a 2D point from the serialized geometry
 */
const POINT2D *
gs_get_point2d_p(GSERIALIZED *gs)
{
  return (POINT2D *) GS_POINT_PTR(gs);
}

/**
 * Returns a 3DZ point from the datum
 */
POINT3DZ
datum_get_point3dz(Datum geom)
{
  GSERIALIZED *gs = (GSERIALIZED *) DatumGetPointer(geom);
  POINT3DZ *point = (POINT3DZ *) GS_POINT_PTR(gs);
  return *point;
}

/**
 * Returns a pointer to a 3DZ point from the datum
 */
const POINT3DZ *
datum_get_point3dz_p(Datum geom)
{
  GSERIALIZED *gs = (GSERIALIZED *) DatumGetPointer(geom);
  return (const POINT3DZ *) GS_POINT_PTR(gs);
}

/**
 * Returns a 3DZ point from the serialized geometry
 */
const POINT3DZ *
gs_get_point3dz_p(GSERIALIZED *gs)
{
  return (const POINT3DZ *) GS_POINT_PTR(gs);
}

/**
 * Returns a 4D point from the datum
 * @note The M dimension is ignored
 */
void
datum_get_point4d(POINT4D *p, Datum geom)
{
  GSERIALIZED *gs = (GSERIALIZED *) DatumGetPointer(geom);
  memset(p, 0, sizeof(POINT4D));
  if (FLAGS_GET_Z(gs->flags))
  {
    POINT3DZ *point = (POINT3DZ *) GS_POINT_PTR(gs);
    p->x = point->x;
    p->y = point->y;
    p->z = point->z;
  }
  else
  {
    POINT2D *point = (POINT2D *) GS_POINT_PTR(gs);
    p->x = point->x;
    p->y = point->y;
  }
  return;
}

/**
 * Returns true if the two points are equal
 */
bool
datum_point_eq(Datum geopoint1, Datum geopoint2)
{
  GSERIALIZED *gs1 = (GSERIALIZED *) DatumGetPointer(geopoint1);
  GSERIALIZED *gs2 = (GSERIALIZED *) DatumGetPointer(geopoint2);
  if (gserialized_get_srid(gs1) != gserialized_get_srid(gs2) ||
    FLAGS_GET_Z(gs1->flags) != FLAGS_GET_Z(gs2->flags) ||
    FLAGS_GET_GEODETIC(gs1->flags) != FLAGS_GET_GEODETIC(gs2->flags))
    return false;
  if (FLAGS_GET_Z(gs1->flags))
  {
    const POINT3DZ *point1 = gs_get_point3dz_p(gs1);
    const POINT3DZ *point2 = gs_get_point3dz_p(gs2);
    return FP_EQUALS(point1->x, point2->x) && FP_EQUALS(point1->y, point2->y) &&
      FP_EQUALS(point1->z, point2->z);
  }
  else
  {
    const POINT2D *point1 = gs_get_point2d_p(gs1);
    const POINT2D *point2 = gs_get_point2d_p(gs2);
    return FP_EQUALS(point1->x, point2->x) && FP_EQUALS(point1->y, point2->y);
  }
}

/**
 * Serialize a geometry/geography
 *
 *@pre It is supposed that the flags such as Z and geodetic have been
 * set up before by the calling function
 */
GSERIALIZED *
geo_serialize(LWGEOM *geom)
{
  size_t size;
  GSERIALIZED *result = gserialized_from_lwgeom(geom, &size);
  SET_VARSIZE(result, size);
  return result;
}

/**
 * Call the PostGIS transform function. We need to use the fcinfo cached
 * in the external functions stbox_transform and tpoint_transform
 */
Datum
datum_transform(Datum value, Datum srid)
{
  return CallerFInfoFunctionCall2(transform, (fetch_fcinfo())->flinfo,
    InvalidOid, value, srid);
}

/*****************************************************************************
 * Generic functions
 *****************************************************************************/

/**
 * Select the appropriate distance function
 */
datum_func2
get_distance_fn(int16 flags)
{
  datum_func2 result;
  if (MOBDB_FLAGS_GET_GEODETIC(flags))
    result = &geog_distance;
  else
    result = MOBDB_FLAGS_GET_Z(flags) ?
      &geom_distance3d : &geom_distance2d;
  return result;
}

/**
 * Select the appropriate distance function
 */
datum_func2
get_pt_distance_fn(int16 flags)
{
  datum_func2 result;
  if (MOBDB_FLAGS_GET_GEODETIC(flags))
    result = &geog_distance;
  else
    result = MOBDB_FLAGS_GET_Z(flags) ?
      &pt_distance3d : &pt_distance2d;
  return result;
}

/**
 * Returns the 2D distance between the two geometries
 */
Datum
geom_distance2d(Datum geom1, Datum geom2)
{
  return call_function2(distance, geom1, geom2);
}

/**
 * Returns the 3D distance between the two geometries
 */
Datum
geom_distance3d(Datum geom1, Datum geom2)
{
  return call_function2(distance3d, geom1, geom2);
}

/**
 * Returns the distance between the two geographies
 */
Datum
geog_distance(Datum geog1, Datum geog2)
{
  return CallerFInfoFunctionCall2(geography_distance, (fetch_fcinfo())->flinfo,
    InvalidOid, geog1, geog2);
}

/**
 * Returns the 2D distance between the two geometric points
 */
Datum
pt_distance2d(Datum geom1, Datum geom2)
{
  const POINT2D *p1 = datum_get_point2d_p(geom1);
  const POINT2D *p2 = datum_get_point2d_p(geom2);
  return Float8GetDatum(distance2d_pt_pt(p1, p2));
}

/**
 * Returns the 3D distance between the two geometric points
 */
Datum
pt_distance3d(Datum geom1, Datum geom2)
{
  const POINT3DZ *p1 = datum_get_point3dz_p(geom1);
  const POINT3DZ *p2 = datum_get_point3dz_p(geom2);
  return Float8GetDatum(distance3d_pt_pt((POINT3D *) p1, (POINT3D *) p2));
}

/*****************************************************************************
 * Parameter tests
 *****************************************************************************/

/**
 * Ensure that the spatial constraints required for operating on two temporal
 * geometries are satisfied
 */
void
ensure_spatial_validity(const Temporal *temp1, const Temporal *temp2)
{
  if (tgeo_base_type(temp1->basetypid))
  {
    ensure_same_srid_tpoint(temp1, temp2);
    ensure_same_dimensionality(temp1->flags, temp2->flags);
  }
  return;
}

/**
 * Ensure that the spatiotemporal boxes have the same type of coordinates,
 * either planar or geodetic
 */
void
ensure_same_geodetic(int16 flags1, int16 flags2)
{
  if (MOBDB_FLAGS_GET_X(flags1) && MOBDB_FLAGS_GET_X(flags2) &&
    MOBDB_FLAGS_GET_GEODETIC(flags1) != MOBDB_FLAGS_GET_GEODETIC(flags2))
    elog(ERROR, "The values must be both planar or both geodetic");
  return;
}

/**
 * Ensure that the spatiotemporal boxes have the same SRID
 */
void
ensure_same_srid_stbox(const STBOX *box1, const STBOX *box2)
{
  if (MOBDB_FLAGS_GET_X(box1->flags) && MOBDB_FLAGS_GET_X(box2->flags) &&
    box1->srid != box2->srid)
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("Operation on mixed SRID")));
  return;
}

/**
 * Ensure that the temporal points have the same SRID
 */
void
ensure_same_srid_tpoint(const Temporal *temp1, const Temporal *temp2)
{
  if (tpoint_srid_internal(temp1) != tpoint_srid_internal(temp2))
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("Operation on mixed SRID")));
  return;
}

/**
 * Ensure that the temporal points have the same SRID
 */
void
ensure_same_srid_gs(const GSERIALIZED *gs1, const GSERIALIZED *gs2)
{
  if (gserialized_get_srid(gs1) != gserialized_get_srid(gs2))
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("Operation on mixed SRID")));
  return;
}

/**
 * Ensure that the temporal point and the spatiotemporal boxes have the same SRID
 */
void
ensure_same_srid_tpoint_stbox(const Temporal *temp, const STBOX *box)
{
  if (MOBDB_FLAGS_GET_X(box->flags) &&
    tpoint_srid_internal(temp) != box->srid)
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("Operation on mixed SRID")));
  return;
}

/**
 * Ensure that the temporal point and the geometry/geography have the same SRID
 */
void
ensure_same_srid_tpoint_gs(const Temporal *temp, const GSERIALIZED *gs)
{
  if (tpoint_srid_internal(temp) != gserialized_get_srid(gs))
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("Operation on mixed SRID")));
  return;
}

/**
 * Ensure that the temporal point and the geometry/geography have the same SRID
 */
void
ensure_same_srid_stbox_gs(const STBOX *box, const GSERIALIZED *gs)
{
  if (box->srid != gserialized_get_srid(gs))
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("Operation on mixed SRID")));
  return;
}

/**
 * Ensure that the temporal values have the same dimensionality
 */
void
ensure_same_dimensionality(int16 flags1, int16 flags2)
{
  if (MOBDB_FLAGS_GET_X(flags1) != MOBDB_FLAGS_GET_X(flags2) ||
    MOBDB_FLAGS_GET_Z(flags1) != MOBDB_FLAGS_GET_Z(flags2) ||
    MOBDB_FLAGS_GET_T(flags1) != MOBDB_FLAGS_GET_T(flags2))
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("The temporal values must be of the same dimensionality")));
  return;
}

/**
 * Ensure that the temporal values have the same spatial dimensionality
 */
void
ensure_same_spatial_dimensionality(int16 flags1, int16 flags2)
{
  if (MOBDB_FLAGS_GET_X(flags1) != MOBDB_FLAGS_GET_X(flags2) ||
    MOBDB_FLAGS_GET_Z(flags1) != MOBDB_FLAGS_GET_Z(flags2))
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("The temporal values must be of the same spatial dimensionality")));
  return;
}

/**
 * Ensure that the temporal point and the geometry/geography have the same dimensionality
 */
void
ensure_same_dimensionality_tpoint_gs(const Temporal *temp, const GSERIALIZED *gs)
{
  if (MOBDB_FLAGS_GET_Z(temp->flags) != FLAGS_GET_Z(gs->flags))
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("The temporal point and the geometry must be of the same dimensionality")));
  return;
}

/**
 * Ensure that the spatiotemporal boxes have the same spatial dimensionality
 */
void
ensure_same_spatial_dimensionality_stbox_gs(const STBOX *box, const GSERIALIZED *gs)
{
  if (! MOBDB_FLAGS_GET_X(box->flags) ||
      MOBDB_FLAGS_GET_Z(box->flags) != FLAGS_GET_Z(gs->flags))
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("The spatiotemporal box and the geometry must be of the same dimensionality")));
  return;
}

/**
 * Ensure that the temporal value has Z dimension
 */
void
ensure_has_Z(int16 flags)
{
  if (! MOBDB_FLAGS_GET_Z(flags))
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("The temporal value must have Z dimension")));
  return;
}

/**
 * Ensure that the temporal point has Z dimension
 */
void
ensure_has_Z_tpoint(const Temporal *temp)
{
  if (! MOBDB_FLAGS_GET_Z(temp->flags))
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("The temporal point do not have Z dimension")));
  return;
}

/**
 * Ensure that the geometry/geography has not Z dimension
 */
void
ensure_has_Z_gs(const GSERIALIZED *gs)
{
  if (! FLAGS_GET_Z(gs->flags))
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("The geometry must have Z dimension")));
  return;
}

/**
 * Ensure that the geometry/geography has not Z dimension
 */
void
ensure_has_not_Z_gs(const GSERIALIZED *gs)
{
  if (FLAGS_GET_Z(gs->flags))
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("Only geometries without Z dimension accepted")));
  return;
}

/**
 * Ensure that the geometry/geography has M dimension
 */
void
ensure_has_M_gs(const GSERIALIZED *gs)
{
  if (! FLAGS_GET_M(gs->flags))
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("Only geometries with M dimension accepted")));
  return;
}

/**
 * Ensure that the geometry/geography has not M dimension
 */
void
ensure_has_not_M_gs(const GSERIALIZED *gs)
{
  if (FLAGS_GET_M(gs->flags))
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("Only geometries without M dimension accepted")));
  return;
}

/**
 * Ensure that the geometry/geography is a point
 */
void
ensure_point_type(const GSERIALIZED *gs)
{
  if (gserialized_get_type(gs) != POINTTYPE)
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("Only point geometries accepted")));
  return;
}

/**
 * Ensure that the geometry/geography is not empty
 */
void
ensure_non_empty(const GSERIALIZED *gs)
{
  if (gserialized_is_empty(gs))
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
      errmsg("Only non-empty geometries accepted")));
  return;
}

/*****************************************************************************
 * Functions derived from PostGIS
 *****************************************************************************/

/**
 * Function derived from PostGIS file lwalgorithm.c since it is declared static
 */
static bool
lw_seg_interact(const POINT2D p1, const POINT2D p2, const POINT2D q1,
  const POINT2D q2)
{
  double minq = FP_MIN(q1.x, q2.x);
  double maxq = FP_MAX(q1.x, q2.x);
  double minp = FP_MIN(p1.x, p2.x);
  double maxp = FP_MAX(p1.x, p2.x);

  if (FP_GT(minp, maxq) || FP_LT(maxp, minq))
    return false;

  minq = FP_MIN(q1.y, q2.y);
  maxq = FP_MAX(q1.y, q2.y);
  minp = FP_MIN(p1.y, p2.y);
  maxp = FP_MAX(p1.y, p2.y);

  if (FP_GT(minp,maxq) || FP_LT(maxp,minq))
    return false;

  return true;
}

/**
 * Returns a long double between 0 and 1 representing the location of the
 * closest point on the segment to the given point, as a fraction of total
 * segment length (2D version).
 *
 * @note Function derived from the PostGIS function closest_point_on_segment.
 */
long double
closest_point2d_on_segment_ratio(const POINT2D *p, const POINT2D *A,
  const POINT2D *B, POINT2D *closest)
{
  if (FP_EQUALS(A->x, B->x) && FP_EQUALS(A->y, B->y))
  {
    *closest = *A;
    return 0.0;
  }

  /*
   * We use comp.graphics.algorithms Frequently Asked Questions method
   *
   * (1)          AC dot AB
   *         r = ----------
   *              ||AB||^2
   *  r has the following meaning:
   *  r=0 P = A
   *  r=1 P = B
   *  r<0 P is on the backward extension of AB
   *  r>1 P is on the forward extension of AB
   *  0<r<1 P is interior to AB
   *
   */
  long double r = ( (p->x-A->x) * (B->x-A->x) + (p->y-A->y) * (B->y-A->y) ) /
    ( (B->x-A->x) * (B->x-A->x) + (B->y-A->y) * (B->y-A->y) );

  if (r < 0)
  {
    *closest = *A;
    return 0.0;
  }
  if (r > 1)
  {
    *closest = *B;
    return 1.0;
  }

  closest->x = A->x + ( (B->x - A->x) * r );
  closest->y = A->y + ( (B->y - A->y) * r );
  return r;
}

/**
 * Returns a float between 0 and 1 representing the location of the closest
 * point on the segment to the given point, as a fraction of total segment
 * length (3D version).
 *
 * @note Function derived from the PostGIS function closest_point_on_segment.
 */
long double
closest_point3dz_on_segment_ratio(const POINT3DZ *p, const POINT3DZ *A,
  const POINT3DZ *B, POINT3DZ *closest)
{
  if (FP_EQUALS(A->x, B->x) && FP_EQUALS(A->y, B->y) &&
    FP_EQUALS(A->z, B->z))
  {
    *closest = *A;
    return 0.0;
  }

  /*
   * We use comp.graphics.algorithms Frequently Asked Questions method
   *
   * (1)          AC dot AB
   *         r = ----------
   *              ||AB||^2
   *  r has the following meaning:
   *  r=0 P = A
   *  r=1 P = B
   *  r<0 P is on the backward extension of AB
   *  r>1 P is on the forward extension of AB
   *  0<r<1 P is interior to AB
   *
   */
  long double r = ( (p->x-A->x) * (B->x-A->x) + (p->y-A->y) * (B->y-A->y) + (p->z-A->z) * (B->z-A->z) ) /
    ( (B->x-A->x) * (B->x-A->x) + (B->y-A->y) * (B->y-A->y) + (B->z-A->z) * (B->z-A->z) );

  if (r < 0)
  {
    *closest = *A;
    return 0.0;
  }
  if (r > 1)
  {
    *closest = *B;
    return 1.0;
  }

  closest->x = A->x + ( (B->x - A->x) * r );
  closest->y = A->y + ( (B->y - A->y) * r );
  closest->z = A->z + ( (B->z - A->z) * r );
  return r;
}

/**
 * Returns a float between 0 and 1 representing the location of the closest
 * point on the geography segment to the given point, as a fraction of total
 * segment length.
 *
 *@param[in] p Reference point
 *@param[in] A,B Points defining the segment
 *@param[out] closest Closest point in the segment
 *@param[out] dist Distance between the closest point and the reference point
 */
long double
closest_point_on_segment_sphere(const POINT4D *p, const POINT4D *A,
  const POINT4D *B, POINT4D *closest, double *dist)
{
  GEOGRAPHIC_EDGE e;
  GEOGRAPHIC_POINT a, proj;
  double length, /* length from A to the closest point */
    seglength; /* length of the segment AB */
  long double result; /* ratio */

  /* Initialize target point */
  geographic_point_init(p->x, p->y, &a);

  /* Initialize edge */
  geographic_point_init(A->x, A->y, &(e.start));
  geographic_point_init(B->x, B->y, &(e.end));

  /* Get the spherical distance between point and edge */
  *dist = edge_distance_to_point(&e, &a, &proj);

  /* Compute distance from beginning of the segment to closest point */
  seglength = sphere_distance(&(e.start), &(e.end));
  length = sphere_distance(&(e.start), &proj);
  result = length / seglength;

  if (closest)
  {
    /* Copy nearest into returning argument */
    closest->x = rad2deg(proj.lon);
    closest->y = rad2deg(proj.lat);

    /* Compute Z and M values for closest point */
    closest->z = A->z + ((B->z - A->z) * result);
    closest->m = A->m + ((B->m - A->m) * result);
  }
  return result;
}

/*****************************************************************************
 * Functions specializing the PostGIS functions ST_LineInterpolatePoint and
 * ST_LineLocatePoint.
 *****************************************************************************/

/**
 * Create a point
 */
Datum point_make(double x, double y, double z, bool hasz, bool geodetic,
  int32 srid)
{
  LWPOINT *lwpoint = hasz ?
    lwpoint_make3dz(srid, x, y, z) : lwpoint_make2d(srid, x, y);
  FLAGS_SET_GEODETIC(lwpoint->flags, geodetic);
  Datum result = PointerGetDatum(geo_serialize((LWGEOM *) lwpoint));
  lwpoint_free(lwpoint);
  return result;
}

/**
 * Returns a point interpolated from the geometry/geography segment with
 * respect to the fraction of its total length.
 *
 * @param[in] start,end Points defining the segment
 * @param[in] ratio Float between 0 and 1 representing the fraction of the
 * total length of the segment where the point must be located
 */
Datum
geoseg_interpolate_point(Datum start, Datum end, long double ratio)
{
  GSERIALIZED *gs = (GSERIALIZED *) DatumGetPointer(start);
  int srid = gserialized_get_srid(gs);
  POINT4D p1, p2, p;
  datum_get_point4d(&p1, start);
  datum_get_point4d(&p2, end);
  bool hasz = FLAGS_GET_Z(gs->flags);
  bool geodetic = FLAGS_GET_GEODETIC(gs->flags);
  if (geodetic)
  {
    POINT3D q1, q2;
    GEOGRAPHIC_POINT g1, g2;
    geographic_point_init(p1.x, p1.y, &g1);
    geographic_point_init(p2.x, p2.y, &g2);
    geog2cart(&g1, &q1);
    geog2cart(&g2, &q2);
    interpolate_point4d_sphere(&q1, &q2, &p1, &p2, ratio, &p);
  }
  else
  {
    /* We cannot call the PostGIS function
     * interpolate_point4d(&p1, &p2, &p, ratio);
     * since it uses a double and not a long double for the interpolation */
    p.x = p1.x + ((long double) (p2.x - p1.x) * ratio);
    p.y = p1.y + ((long double) (p2.y - p1.y) * ratio);
    p.z = p1.z + ((long double) (p2.z - p1.z) * ratio);
    p.m = 0.0;
  }

  Datum result = point_make(p.x, p.y, p.z, hasz, geodetic, srid);
  POSTGIS_FREE_IF_COPY_P(gs, DatumGetPointer(start));
  return result;
}

/**
 * Returns a float between 0 and 1 representing the location of the closest
 * point on the geometry segment to the given point, as a fraction of total
 * segment length.
 *
 *@param[in] start,end Points defining the segment
 *@param[in] point Reference point
 *@param[out] dist Distance
 */
long double
geoseg_locate_point(Datum start, Datum end, Datum point, double *dist)
{
  GSERIALIZED *gs = (GSERIALIZED *) DatumGetPointer(start);
  long double result;
  if (FLAGS_GET_GEODETIC(gs->flags))
  {
    POINT4D p1, p2, p, closest;
    datum_get_point4d(&p1, start);
    datum_get_point4d(&p2, end);
    datum_get_point4d(&p, point);
    double d;
    /* Get the closest point and the distance */
    result = closest_point_on_segment_sphere(&p, &p1, &p2, &closest, &d);
    /* For robustness, force 0/1 when closest point == start/endpoint */
    if (p4d_same(&p1, &closest))
      result = 0.0;
    else if (p4d_same(&p2, &closest))
      result = 1.0;
    /* Return the distance between the closest point and the point if requested */
    if (dist)
    {
      d = WGS84_RADIUS * d;
      /* Add to the distance the vertical displacement if we're in 3D */
      if (FLAGS_GET_Z(gs->flags))
        d = sqrt( (closest.z - p.z) * (closest.z - p.z) + d*d );
      *dist = d;
    }
  }
  else
  {
    if (FLAGS_GET_Z(gs->flags))
    {
      const POINT3DZ *p1 = datum_get_point3dz_p(start);
      const POINT3DZ *p2 = datum_get_point3dz_p(end);
      const POINT3DZ *p = datum_get_point3dz_p(point);
      POINT3DZ proj;
      result = closest_point3dz_on_segment_ratio(p, p1, p2, &proj);
      /* For robustness, force 0/1 when closest point == start/endpoint */
      if (p3d_same((POINT3D *) p1, (POINT3D *) &proj))
        result = 0.0;
      else if (p3d_same((POINT3D *) p2, (POINT3D *) &proj))
        result = 1.0;
      if (dist)
        *dist = distance3d_pt_pt((POINT3D *) p, (POINT3D *) &proj);
    }
    else
    {
      const POINT2D *p1 = datum_get_point2d_p(start);
      const POINT2D *p2 = datum_get_point2d_p(end);
      const POINT2D *p = datum_get_point2d_p(point);
      POINT2D proj;
      result = closest_point2d_on_segment_ratio(p, p1, p2, &proj);
      if (p2d_same(p1, &proj))
        result = 0.0;
      else if (p2d_same(p2, &proj))
        result = 1.0;
      if (dist)
        *dist = distance2d_pt_pt((POINT2D *) p, &proj);
    }
  }
  return result;
}

/*****************************************************************************
 * Interpolation functions defining functionality required by tsequence.c
 * that must be implemented by each temporal type
 *****************************************************************************/

/**
 * Returns true if the segment of the temporal point value intersects
 * the base value at the timestamp
 *
 * @param[in] inst1,inst2 Temporal instants defining the segment
 * @param[in] value Base value
 * @param[out] t Timestamp
 */
bool
tpointseq_intersection_value(const TInstant *inst1, const TInstant *inst2,
  Datum value, TimestampTz *t)
{
  GSERIALIZED *gs = (GSERIALIZED *) PG_DETOAST_DATUM(value);
  if (gserialized_is_empty(gs))
  {
    POSTGIS_FREE_IF_COPY_P(gs, DatumGetPointer(value));
    return false;
  }

  /* We are sure that the trajectory is a line */
  Datum start = tinstant_value(inst1);
  Datum end = tinstant_value(inst2);
  double dist;
  double fraction = geoseg_locate_point(start, end, value, &dist);
  if (fabs(dist) >= EPSILON)
    return false;

  if (t != NULL)
  {
    double duration = (inst2->t - inst1->t);
    *t = inst1->t + (TimestampTz) (duration * fraction);
    /* Note that due to roundoff errors it may be the case that the
     * resulting timestamp t may be equal to inst1->t or to inst2->t */
  }
  return true;
}

/**
 * Returns true if the two segments of the temporal geometric point
 * values intersect at the timestamp
 *
 * @param[in] start1,end1 Temporal instants defining the first segment
 * @param[in] start2,end2 Temporal instants defining the second segment
 * @param[out] t Timestamp
 * @pre The instants are synchronized, i.e., start1->t = start2->t and
 * end1->t = end2->t
 */
bool
tgeompointseq_intersection(const TInstant *start1, const TInstant *end1,
  const TInstant *start2, const TInstant *end2, TimestampTz *t)
{
  long double fraction, xfraction = 0, yfraction = 0, xdenum, ydenum;
  if (MOBDB_FLAGS_GET_Z(start1->flags)) /* 3D */
  {
    long double zfraction = 0, zdenum;
    const POINT3DZ *p1 = datum_get_point3dz_p(tinstant_value(start1));
    const POINT3DZ *p2 = datum_get_point3dz_p(tinstant_value(end1));
    const POINT3DZ *p3 = datum_get_point3dz_p(tinstant_value(start2));
    const POINT3DZ *p4 = datum_get_point3dz_p(tinstant_value(end2));
    xdenum = p2->x - p1->x - p4->x + p3->x;
    ydenum = p2->y - p1->y - p4->y + p3->y;
    zdenum = p2->z - p1->z - p4->z + p3->z;
    if (xdenum == 0 && ydenum == 0 && zdenum == 0)
      /* Parallel segments */
      return false;

    if (xdenum != 0)
    {
      xfraction = (p3->x - p1->x) / xdenum;
      if (xfraction < -1 * EPSILON || 1.0 + EPSILON < xfraction)
        /* Intersection occurs out of the period */
        return false;
    }
    if (ydenum != 0)
    {
      yfraction = (p3->y - p1->y) / ydenum;
      if (yfraction < -1 * EPSILON || 1.0 + EPSILON < yfraction)
        /* Intersection occurs out of the period */
        return false;
    }
    if (zdenum != 0)
    {
      zfraction = (p3->z - p1->z) / zdenum;
      if (zfraction < -1 * EPSILON || 1.0 + EPSILON < zfraction)
        /* Intersection occurs out of the period */
        return false;
    }
    /* If intersection occurs at different timestamps on each dimension */
    if ((xdenum != 0 && ydenum != 0 && zdenum != 0 &&
      fabsl(xfraction - yfraction) > EPSILON &&
      fabsl(xfraction - zfraction) > EPSILON) ||
      (xdenum == 0 && ydenum != 0 && zdenum != 0 &&
      fabsl(yfraction - zfraction) > EPSILON) ||
      (xdenum != 0 && ydenum == 0 && zdenum != 0 &&
      fabsl(xfraction - zfraction) > EPSILON) ||
      (xdenum != 0 && ydenum != 0 && zdenum == 0 &&
      fabsl(xfraction - yfraction) > EPSILON))
      return false;
    if (xdenum != 0)
      fraction = xfraction;
    else if (ydenum != 0)
      fraction = yfraction;
    else
      fraction = zfraction;
  }
  else /* 2D */
  {
    const POINT2D *p1 = datum_get_point2d_p(tinstant_value(start1));
    const POINT2D *p2 = datum_get_point2d_p(tinstant_value(end1));
    const POINT2D *p3 = datum_get_point2d_p(tinstant_value(start2));
    const POINT2D *p4 = datum_get_point2d_p(tinstant_value(end2));
    xdenum = p2->x - p1->x - p4->x + p3->x;
    ydenum = p2->y - p1->y - p4->y + p3->y;
    if (xdenum == 0 && ydenum == 0)
      /* Parallel segments */
      return false;

    if (xdenum != 0)
    {
      xfraction = (p3->x - p1->x) / xdenum;
      if (xfraction < -1 * EPSILON || 1.0 + EPSILON < xfraction)
        /* Intersection occurs out of the period */
        return false;
    }
    if (ydenum != 0)
    {
      yfraction = (p3->y - p1->y) / ydenum;
      if (yfraction < -1 * EPSILON || 1.0 + EPSILON < yfraction)
        /* Intersection occurs out of the period */
        return false;
    }
    /* If intersection occurs at different timestamps on each dimension */
    if (xdenum != 0 && ydenum != 0 && fabsl(xfraction - yfraction) > EPSILON)
      return false;
    fraction = xdenum != 0 ? xfraction : yfraction;
  }
  double duration = (end1->t - start1->t);
  *t = start1->t + (TimestampTz) (duration * fraction);
  /* Note that due to roundoff errors it may be the case that the
   * resulting timestamp t may be equal to inst1->t or to inst2->t */
  if (*t <= start1->t || *t >= end1->t)
    return false;
  return true;
}

/**
 * Returns true if the two segments of the temporal geographic point
 * values intersect at the timestamp
 *
 * @param[in] start1,end1 Temporal instants defining the first segment
 * @param[in] start2,end2 Temporal instants defining the second segment
 * @param[out] t Timestamp
 * @pre The instants are synchronized, i.e., start1->t = start2->t and
 * end1->t = end2->t
 */
bool
tgeogpointseq_intersection(const TInstant *start1, const TInstant *end1,
  const TInstant *start2, const TInstant *end2, TimestampTz *t)
{
  GEOGRAPHIC_EDGE e1, e2;
  POINT3D A1, A2, B1, B2;
  POINT4D p1, p2, p3, p4;

  datum_get_point4d(&p1, tinstant_value(start1));
  geographic_point_init(p1.x, p1.y, &(e1.start));
  geog2cart(&(e1.start), &A1);

  datum_get_point4d(&p2, tinstant_value(end1));
  geographic_point_init(p2.x, p2.y, &(e1.end));
  geog2cart(&(e1.end), &A2);

  datum_get_point4d(&p3, tinstant_value(start2));
  geographic_point_init(p3.x, p3.y, &(e2.start));
  geog2cart(&(e2.start), &B1);

  datum_get_point4d(&p4, tinstant_value(end2));
  geographic_point_init(p4.x, p4.y, &(e2.end));
  geog2cart(&(e2.end), &B2);

  if (! edge_intersects(&A1, &A2, &B1, &B2))
    return false;

  /* Compute the intersection point */
  GEOGRAPHIC_POINT inter;
  edge_intersection(&e1, &e2, &inter);
  /* Compute distance from beginning of the segment to closest point */
  long double seglength = sphere_distance(&(e1.start), &(e1.end));
  long double length = sphere_distance(&(e1.start), &inter);
  long double fraction = length / seglength;

  long double duration = (end1->t - start1->t);
  *t = start1->t + (TimestampTz) (duration * fraction);
  /* Note that due to roundoff errors it may be the case that the
   * resulting timestamp t may be equal to inst1->t or to inst2->t */
  if (*t <= start1->t || *t >= end1->t)
    return false;
  return true;
}

/**
 * Returns true if the three values are collinear
 *
 * @param[in] value1,value2,value3 Input values
 * @param[in] ratio Value in [0,1] representing the duration of the
 * timestamps associated to `value1` and `value2` divided by the duration
 * of the timestamps associated to `value1` and `value3`
 * @param[in] hasz True when the points have Z coordinates
 * @param[in] geodetic True for geography, false for geometry
 */
bool
geopoint_collinear(Datum value1, Datum value2, Datum value3,
  double ratio, bool hasz, bool geodetic)
{
  POINT4D p1, p2, p3, p;
  datum_get_point4d(&p1, value1);
  datum_get_point4d(&p2, value2);
  datum_get_point4d(&p3, value3);
  if (geodetic)
  {
    POINT3D q1, q3;
    GEOGRAPHIC_POINT g1, g3;
    geographic_point_init(p1.x, p1.y, &g1);
    geographic_point_init(p3.x, p3.y, &g3);
    geog2cart(&g1, &q1);
    geog2cart(&g3, &q3);
    interpolate_point4d_sphere(&q1, &q3, &p1, &p3, ratio, &p);
  }
  else
    interpolate_point4d(&p1, &p3, &p, ratio);

  bool result = hasz ?
    fabs(p2.x - p.x) <= EPSILON && fabs(p2.y - p.y) <= EPSILON &&
      fabs(p2.z - p.z) <= EPSILON :
    fabs(p2.x - p.x) <= EPSILON && fabs(p2.y - p.y) <= EPSILON;
  return result;
}

/*****************************************************************************
 * Trajectory functions.
 *****************************************************************************/

/**
 * Compute a trajectory from a set of points. The result is either a line or
 * a multipoint depending on whether the interpolation is step or linear
 *
 * @param[in] lwpoints Array of points
 * @param[in] count Number of elements in the input array
 * @param[in] linear True when the interpolation is linear
 */
LWGEOM *
lwpointarr_make_trajectory(LWGEOM **lwpoints, int count, bool linear)
{
  if (count == 1)
    return lwpoint_as_lwgeom(lwpoint_clone(lwgeom_as_lwpoint(lwpoints[0])));

  LWGEOM *result = linear ?
    (LWGEOM *) lwline_from_lwgeom_array(lwpoints[0]->srid, (uint32_t) count,
      lwpoints) :
    (LWGEOM *) lwcollection_construct(MULTIPOINTTYPE, lwpoints[0]->srid,
      NULL, (uint32_t) count, lwpoints);
  FLAGS_SET_Z(result->flags, FLAGS_GET_Z(lwpoints[0]->flags));
  FLAGS_SET_GEODETIC(result->flags, FLAGS_GET_GEODETIC(lwpoints[0]->flags));
  return result;
}

/**
 * Compute the trajectory of a temporal instant set point.
 *
 * @param[in] ti Temporal value
 * @note Notice that this function does not remove duplicate points.
 */
Datum
tpointinstset_trajectory(const TInstantSet *ti)
{
  /* Singleton instant set */
  if (ti->count == 1)
    return tinstant_value_copy(tinstantset_inst_n(ti, 0));

  LWGEOM **points = palloc(sizeof(LWGEOM *) * ti->count);
  for (int i = 0; i < ti->count; i++)
  {
    Datum value = tinstant_value(tinstantset_inst_n(ti, i));
    GSERIALIZED *gsvalue = (GSERIALIZED *) DatumGetPointer(value);
    points[i] = lwgeom_from_gserialized(gsvalue);
  }
  LWGEOM *lwgeom = lwpointarr_make_trajectory(points, ti->count, STEP);
  Datum result = PointerGetDatum(geo_serialize(lwgeom));
  pfree(lwgeom);
  for (int i = 0; i < ti->count; i++)
    lwpoint_free((LWPOINT *) points[i]);
  pfree(points);
  return result;
}

/*****************************************************************************/

/**
 * Compute the trajectory from two geometry points
 *
 * @param[in] value1,value2 Points
 */
LWLINE *
geopoint_lwline(Datum value1, Datum value2)
{
  GSERIALIZED *gs1 = (GSERIALIZED *) DatumGetPointer(value1);
  GSERIALIZED *gs2 = (GSERIALIZED *) DatumGetPointer(value2);
  LWGEOM *geoms[2];
  geoms[0] = lwgeom_from_gserialized(gs1);
  geoms[1] = lwgeom_from_gserialized(gs2);
  LWLINE *result = lwline_from_lwgeom_array(geoms[0]->srid, 2, geoms);
  FLAGS_SET_Z(result->flags, FLAGS_GET_Z(geoms[0]->flags));
  FLAGS_SET_GEODETIC(result->flags, FLAGS_GET_GEODETIC(geoms[0]->flags));
  lwgeom_free(geoms[0]); lwgeom_free(geoms[1]);
  return result;
}

/**
 * Compute the trajectory from two points
 *
 * @param[in] value1,value2 Points
 * @note Function called during normalization for determining whether three
 * consecutive points are collinear, for computing the temporal distance,
 * the temporal spatial relationships, etc.
 */
Datum
geopoint_line(Datum value1, Datum value2)
{
  LWGEOM *traj = (LWGEOM *) geopoint_lwline(value1, value2);
  GSERIALIZED *result = geo_serialize(traj);
  lwgeom_free(traj);
  return PointerGetDatum(result);
}

/**
 * Compute the trajectory of an array of instants.
 *
 * @note This function is called by the constructor of a temporal sequence
 * and returns a single Datum which is a geometry/geography.
 * Since the composing points have been already validated in the constructor
 * there is no verification of the input in this function, in particular
 * for geographies it is supposed that the composing points are geodetic
 *
 * @param[in] instants Array of temporal instants
 * @param[in] count Number of elements in the input array
 * @param[in] linear True when the interpolation is linear
 */
Datum
tpointseq_make_trajectory(const TInstant **instants, int count, bool linear)
{
  /* Instantaneous sequence */
  if (count == 1)
    return tinstant_value_copy(instants[0]);

  LWPOINT **points = palloc(sizeof(LWPOINT *) * count);
  /* Remove two consecutive points if they are equal */
  Datum value = tinstant_value(instants[0]);
  GSERIALIZED *gs = (GSERIALIZED *) DatumGetPointer(value);
  points[0] = lwgeom_as_lwpoint(lwgeom_from_gserialized(gs));
  int k = 1;
  for (int i = 1; i < count; i++)
  {
    value = tinstant_value(instants[i]);
    gs = (GSERIALIZED *) DatumGetPointer(value);
    LWPOINT *lwpoint = lwgeom_as_lwpoint(lwgeom_from_gserialized(gs));
    if (! lwpoint_same(lwpoint, points[k - 1]))
      points[k++] = lwpoint;
  }
  LWGEOM *lwgeom = lwpointarr_make_trajectory((LWGEOM **) points, k, linear);
  Datum result = PointerGetDatum(geo_serialize(lwgeom));
  pfree(lwgeom);
  for (int i = 0; i < k; i++)
    lwpoint_free(points[i]);
  pfree(points);
  return result;
}

/**
 * Returns the precomputed trajectory of a temporal sequence point
 */
Datum
tpointseq_trajectory(const TSequence *seq)
{
  void *traj = (char *) (&seq->offsets[seq->count + 2]) +   /* start of data */
    seq->offsets[seq->count + 1];            /* offset */
  return PointerGetDatum(traj);
}

/**
 * Copy the precomputed trajectory of a temporal sequence point
 */
Datum
tpointseq_trajectory_copy(const TSequence *seq)
{
  void *traj = (char *) (&seq->offsets[seq->count + 2]) +   /* start of data */
      seq->offsets[seq->count + 1];          /* offset */
  return PointerGetDatum(gserialized_copy(traj));
}

/*****************************************************************************/

/**
 * Returns the trajectory of a temporal point with sequence set
 * type from the precomputed trajectories of its composing segments.
 *
 * @note The resulting trajectory must be freed by the calling function.
 * The function does not remove duplicates point/linestring components.
 */
Datum
tpointseqset_trajectory(const TSequenceSet *ts)
{
  /* Singleton sequence set */
  if (ts->count == 1)
    return tpointseq_trajectory_copy(tsequenceset_seq_n(ts, 0));

  bool geodetic = MOBDB_FLAGS_GET_GEODETIC(ts->flags);
  LWPOINT **points = palloc(sizeof(LWPOINT *) * ts->totalcount);
  LWGEOM **geoms = palloc(sizeof(LWGEOM *) * ts->count);
  int k = 0, l = 0;
  for (int i = 0; i < ts->count; i++)
  {
    Datum traj = tpointseq_trajectory(tsequenceset_seq_n(ts, i));
    GSERIALIZED *gstraj = (GSERIALIZED *) DatumGetPointer(traj);
    int geotype = gserialized_get_type(gstraj);
    if (geotype == POINTTYPE)
      points[l++] = lwgeom_as_lwpoint(lwgeom_from_gserialized(gstraj));
    else if (geotype == MULTIPOINTTYPE)
    {
      LWMPOINT *lwmpoint = lwgeom_as_lwmpoint(lwgeom_from_gserialized(gstraj));
      int count = lwmpoint->ngeoms;
      for (int m = 0; m < count; m++)
        points[l++] = lwmpoint->geoms[m];
    }
    /* gserialized_get_type(gstraj) == LINETYPE */
    else
      geoms[k++] = lwgeom_from_gserialized(gstraj);
  }
  Datum result;
  if (k == 0)
  {
    /* Only points */
    LWGEOM *lwgeom = lwpointarr_make_trajectory((LWGEOM **) points, l, STEP);
    result = PointerGetDatum(geo_serialize(lwgeom));
    pfree(lwgeom);
  }
  else if (l == 0)
  {
    /* Only lines */
    /* k > 1 since otherwise it is a singleton sequence set and this case
     * was taken care at the begining of the function */
    // TODO add the bounding box instead of ask PostGIS to compute it again
    // GBOX *box = stbox_to_gbox(tsequence_bbox_ptr(seq));
    LWGEOM *coll = (LWGEOM *) lwcollection_construct(MULTILINETYPE,
      geoms[0]->srid, NULL, (uint32_t) k, geoms);
    FLAGS_SET_GEODETIC(coll->flags, geodetic);
    result = PointerGetDatum(geo_serialize(coll));
    /* We cannot lwgeom_free(geoms[i] or lwgeom_free(coll) */
  }
  else
  {
    /* Both points and lines */
    if (l == 1)
      geoms[k++] = (LWGEOM *) points[0];
    else
    {
      geoms[k++] = (LWGEOM *) lwcollection_construct(MULTIPOINTTYPE,
        points[0]->srid, NULL, (uint32_t) l, (LWGEOM **) points);
      for (int i = 0; i < l; i++)
        lwpoint_free(points[i]);
    }
    // TODO add the bounding box instead of ask PostGIS to compute it again
    // GBOX *box = stbox_to_gbox(tsequence_bbox_ptr(seq));
    LWGEOM *coll = (LWGEOM *) lwcollection_construct(COLLECTIONTYPE,
      geoms[0]->srid, NULL, (uint32_t) k, geoms);
    FLAGS_SET_GEODETIC(coll->flags, geodetic);
    result = PointerGetDatum(geo_serialize(coll));
  }
  pfree(points); pfree(geoms);
  return result;
}

/*****************************************************************************/

/**
 * Returns the trajectory of a temporal point (dispatch function)
 */
Datum
tpoint_trajectory_internal(const Temporal *temp)
{
  Datum result;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT)
    result = tinstant_value_copy((TInstant *) temp);
  else if (temp->subtype == INSTANTSET)
    result = tpointinstset_trajectory((TInstantSet *) temp);
  else if (temp->subtype == SEQUENCE)
    result = tpointseq_trajectory((TSequence *) temp);
  else /* temp->subtype == SEQUENCESET */
    result = tpointseqset_trajectory((TSequenceSet *) temp);
  return result;
}

/**
 * Free the trajectory after a call to tpoint_trajectory_internal
 */
void
tpoint_trajectory_free(const Temporal *temp, Datum traj)
{
  /* We do not need to free the trajectory for temporal point sequences */
  if (temp->subtype != SEQUENCE)
    pfree(DatumGetPointer(traj));
}

/**
 * Returns the trajectory of a temporal point (dispatch function)
 */
Datum
tpoint_trajectory_external(const Temporal *temp)
{
  Datum result;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT)
    result = tinstant_value_copy((TInstant *) temp);
  else if (temp->subtype == INSTANTSET)
    result = tpointinstset_trajectory((TInstantSet *) temp);
  else if (temp->subtype == SEQUENCE)
    result = tpointseq_trajectory_copy((TSequence *) temp);
  else /* temp->subtype == SEQUENCESET */
    result = tpointseqset_trajectory((TSequenceSet *) temp);
  return result;
}

PG_FUNCTION_INFO_V1(tpoint_trajectory);
/**
 * Returns the trajectory of a temporal point
 */
PGDLLEXPORT Datum
tpoint_trajectory(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  Datum result = tpoint_trajectory_external(temp);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_DATUM(result);
}

/*****************************************************************************
 * Functions for spatial reference systems
 *****************************************************************************/

PG_FUNCTION_INFO_V1(stbox_srid);
/**
 * Returns the SRID of the spatiotemporal box
 */
PGDLLEXPORT Datum
stbox_srid(PG_FUNCTION_ARGS)
{
  STBOX *box = PG_GETARG_STBOX_P(0);
  PG_RETURN_INT32(box->srid);
}

PG_FUNCTION_INFO_V1(stbox_set_srid);
/**
 * Sets the SRID of the spatiotemporal box
 */
PGDLLEXPORT Datum
stbox_set_srid(PG_FUNCTION_ARGS)
{
  STBOX *box = PG_GETARG_STBOX_P(0);
  int32 srid = PG_GETARG_INT32(1);
  STBOX *result = stbox_copy(box);
  result->srid = srid;
  PG_RETURN_POINTER(result);
}

/*****************************************************************************/

/**
 * Returns the SRID of a temporal instant point
 */
int
tpointinst_srid(const TInstant *inst)
{
  GSERIALIZED *gs = (GSERIALIZED *) DatumGetPointer(tinstant_value_ptr(inst));
  return gserialized_get_srid(gs);
}

/**
 * Returns the SRID of a temporal instant set point
 */
int
tpointinstset_srid(const TInstantSet *ti)
{
  STBOX *box = tinstantset_bbox_ptr(ti);
  return box->srid;
}

/**
 * Returns the SRID of a temporal sequence point
 */
int
tpointseq_srid(const TSequence *seq)
{
  STBOX *box = tsequence_bbox_ptr(seq);
  return box->srid;
}

/**
 * Returns the SRID of a temporal sequence set point
 */
int
tpointseqset_srid(const TSequenceSet *ts)
{
  STBOX *box = tsequenceset_bbox_ptr(ts);
  return box->srid;
}

/**
 * Returns the SRID of a temporal point (dispatch function)
 */
int
tpoint_srid_internal(const Temporal *temp)
{
  int result;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT)
    result = tpointinst_srid((TInstant *) temp);
  else if (temp->subtype == INSTANTSET)
    result = tpointinstset_srid((TInstantSet *) temp);
  else if (temp->subtype == SEQUENCE)
    result = tpointseq_srid((TSequence *) temp);
  else /* temp->subtype == SEQUENCESET */
    result = tpointseqset_srid((TSequenceSet *) temp);
  return result;
}

PG_FUNCTION_INFO_V1(tpoint_srid);
/**
 * Returns the SRID of a temporal point
 */
PGDLLEXPORT Datum
tpoint_srid(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  int result = tpoint_srid_internal(temp);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_INT32(result);
}

/*****************************************************************************/

/**
 * Set the SRID of a temporal instant point
 */
static TInstant *
tpointinst_set_srid(TInstant *inst, int32 srid)
{
  TInstant *result = tinstant_copy(inst);
  GSERIALIZED *gs = (GSERIALIZED *) DatumGetPointer(tinstant_value_ptr(result));
  gserialized_set_srid(gs, srid);
  return result;
}

/**
 * Set the SRID of a temporal instant set point
 */
static TInstantSet *
tpointinstset_set_srid(TInstantSet *ti, int32 srid)
{
  TInstantSet *result = tinstantset_copy(ti);
  for (int i = 0; i < ti->count; i++)
  {
    const TInstant *inst = tinstantset_inst_n(result, i);
    GSERIALIZED *gs = (GSERIALIZED *) DatumGetPointer(tinstant_value_ptr(inst));
    gserialized_set_srid(gs, srid);
  }
  STBOX *box = tinstantset_bbox_ptr(result);
  box->srid = srid;
  return result;
}

/**
 * Set the SRID of a temporal sequence point
 */
static TSequence *
tpointseq_set_srid(TSequence *seq, int32 srid)
{
  GSERIALIZED *gs;
  TSequence *result = tsequence_copy(seq);
  /* Set the SRID of the composing points */
  for (int i = 0; i < seq->count; i++)
  {
    const TInstant *inst = tsequence_inst_n(result, i);
    gs = (GSERIALIZED *) DatumGetPointer(tinstant_value_ptr(inst));
    gserialized_set_srid(gs, srid);
  }
  /* Set the SRID of the precomputed trajectory */
  Datum traj = tpointseq_trajectory(result);
  gs = (GSERIALIZED *) DatumGetPointer(traj);
  gserialized_set_srid(gs, srid);
  /* Set the SRID of the bounding box */
  STBOX *box = tsequence_bbox_ptr(result);
  box->srid = srid;
  return result;
}

/**
 * Set the SRID of a temporal sequence set point
 */
static TSequenceSet *
tpointseqset_set_srid(TSequenceSet *ts, int32 srid)
{
  STBOX *box;
  TSequenceSet *result = tsequenceset_copy(ts);
  /* Loop for every composing sequence */
  for (int i = 0; i < ts->count; i++)
  {
    GSERIALIZED *gs;
    const TSequence *seq = tsequenceset_seq_n(result, i);
    for (int j = 0; j < seq->count; j++)
    {
      /* Set the SRID of the composing points */
      const TInstant *inst = tsequence_inst_n(seq, j);
      gs = (GSERIALIZED *) DatumGetPointer(tinstant_value_ptr(inst));
      gserialized_set_srid(gs, srid);
    }
    /* Set the SRID of the precomputed trajectory */
    Datum traj = tpointseq_trajectory(seq);
    gs = (GSERIALIZED *) DatumGetPointer(traj);
    gserialized_set_srid(gs, srid);
    /* Set the SRID of the bounding box */
    box = tsequence_bbox_ptr(seq);
    box->srid = srid;
  }
  /* Set the SRID of the bounding box */
  box = tsequenceset_bbox_ptr(result);
  box->srid = srid;
  return result;
}

/**
 * Set the SRID of a temporal point (dispatch function)
 */
Temporal *
tpoint_set_srid_internal(Temporal *temp, int32 srid)
{
  Temporal *result;
  if (temp->subtype == INSTANT)
    result = (Temporal *) tpointinst_set_srid((TInstant *) temp, srid);
  else if (temp->subtype == INSTANTSET)
    result = (Temporal *) tpointinstset_set_srid((TInstantSet *) temp, srid);
  else if (temp->subtype == SEQUENCE)
    result = (Temporal *) tpointseq_set_srid((TSequence *) temp, srid);
  else /* temp->subtype == SEQUENCESET */
    result = (Temporal *) tpointseqset_set_srid((TSequenceSet *) temp, srid);

  assert(result != NULL);
  return result;
}

PG_FUNCTION_INFO_V1(tpoint_set_srid);
/**
 * Set the SRID of a temporal point
 */
PGDLLEXPORT Datum
tpoint_set_srid(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  int32 srid = PG_GETARG_INT32(1);
  Temporal *result = tpoint_set_srid_internal(temp, srid);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_POINTER(result);
}

/*****************************************************************************/

PG_FUNCTION_INFO_V1(stbox_transform);
/**
 * Transform a spatiotemporal box into another spatial reference system
 */
PGDLLEXPORT Datum
stbox_transform(PG_FUNCTION_ARGS)
{
  STBOX *box = PG_GETARG_STBOX_P(0);
  Datum srid = PG_GETARG_DATUM(1);
  ensure_has_X_stbox(box);
  STBOX *result = stbox_copy(box);
  result->srid = DatumGetInt32(srid);
  bool hasz = MOBDB_FLAGS_GET_Z(box->flags);
  bool geodetic = MOBDB_FLAGS_GET_GEODETIC(box->flags);
  Datum min = point_make(box->xmin, box->ymin, box->zmin, hasz, geodetic,
    box->srid);
  Datum max = point_make(box->xmax, box->ymax, box->zmax, hasz, geodetic,
    box->srid);
  /* Store fcinfo into a global variable */
  store_fcinfo(fcinfo);
  Datum min1 = datum_transform(min, srid);
  Datum max1 = datum_transform(max, srid);
  if (hasz)
  {
    const POINT3DZ *ptmin1 = datum_get_point3dz_p(min1);
    const POINT3DZ *ptmax1 = datum_get_point3dz_p(max1);
    result->xmin = ptmin1->x;
    result->ymin = ptmin1->y;
    result->zmin = ptmin1->z;
    result->xmax = ptmax1->x;
    result->ymax = ptmax1->y;
    result->zmax = ptmax1->z;
  }
  else
  {
    const POINT2D *ptmin1 = datum_get_point2d_p(min1);
    const POINT2D *ptmax1 = datum_get_point2d_p(max1);
    result->xmin = ptmin1->x;
    result->ymin = ptmin1->y;
    result->xmax = ptmax1->x;
    result->ymax = ptmax1->y;
  }
  pfree(DatumGetPointer(min)); pfree(DatumGetPointer(max));
  pfree(DatumGetPointer(min1)); pfree(DatumGetPointer(max1));
  PG_RETURN_POINTER(result);
}

/*****************************************************************************/

/**
 * Transform a temporal instant point into another spatial reference system
 */
TInstant *
tpointinst_transform(const TInstant *inst, Datum srid)
{
  Datum geo = datum_transform(tinstant_value(inst), srid);
  TInstant *result = tinstant_make(geo, inst->t, inst->basetypid);
  pfree(DatumGetPointer(geo));
  return result;
}

/**
 * Transform a temporal instant set point into another spatial reference system
 */
static TInstantSet *
tpointinstset_transform(const TInstantSet *ti, Datum srid)
{
  /* Singleton instant set */
  if (ti->count == 1)
  {
    TInstant *inst = tpointinst_transform(tinstantset_inst_n(ti, 0), srid);
    TInstantSet *result = tinstantset_make((const TInstant **) &inst, 1, MERGE_NO);
    pfree(inst);
    return result;
  }

  /* General case */
  Datum multipoint = tpointinstset_trajectory(ti);
  Datum transf = datum_transform(multipoint, srid);
  GSERIALIZED *gs = (GSERIALIZED *) PG_DETOAST_DATUM(transf);
  LWMPOINT *lwmpoint = lwgeom_as_lwmpoint(lwgeom_from_gserialized(gs));
  TInstant **instants = palloc(sizeof(TInstant *) * ti->count);
  for (int i = 0; i < ti->count; i++)
  {
    Datum point = PointerGetDatum(geo_serialize((LWGEOM *) (lwmpoint->geoms[i])));
    const TInstant *inst = tinstantset_inst_n(ti, i);
    instants[i] = tinstant_make(point, inst->t, inst->basetypid);
    pfree(DatumGetPointer(point));
  }
  pfree(DatumGetPointer(multipoint)); pfree(DatumGetPointer(transf));
  POSTGIS_FREE_IF_COPY_P(gs, DatumGetPointer(gs));
  lwmpoint_free(lwmpoint);

  return tinstantset_make_free(instants, ti->count, MERGE_NO);
}

/**
 * Transform a temporal sequence point into another spatial reference system
 */
static TSequence *
tpointseq_transform(const TSequence *seq, Datum srid)
{
  bool linear = MOBDB_FLAGS_GET_LINEAR(seq->flags);

  /* Instantaneous sequence */
  if (seq->count == 1)
  {
    TInstant *inst = tpointinst_transform(tsequence_inst_n(seq, 0), srid);
    TSequence *result = tinstant_to_tsequence(inst, linear);
    pfree(inst);
    return result;
  }

  /* General case */
  LWGEOM **points = palloc(sizeof(LWGEOM *) * seq->count);
  for (int i = 0; i < seq->count; i++)
  {
    Datum value = tinstant_value(tsequence_inst_n(seq, i));
    GSERIALIZED *gsvalue = (GSERIALIZED *) DatumGetPointer(value);
    points[i] = lwgeom_from_gserialized(gsvalue);
  }
  /* Last parameter set to STEP to force the function to return multipoint */
  LWGEOM *lwgeom = lwpointarr_make_trajectory(points, seq->count, STEP);
  Datum multipoint = PointerGetDatum(geo_serialize(lwgeom));
  pfree(lwgeom);
  Datum transf = datum_transform(multipoint, srid);
  GSERIALIZED *gs = (GSERIALIZED *) PG_DETOAST_DATUM(transf);
  LWMPOINT *lwmpoint = lwgeom_as_lwmpoint(lwgeom_from_gserialized(gs));
  TInstant **instants = palloc(sizeof(TInstant *) * seq->count);
  for (int i = 0; i < seq->count; i++)
  {
    Datum point = PointerGetDatum(geo_serialize((LWGEOM *) (lwmpoint->geoms[i])));
    const TInstant *inst = tsequence_inst_n(seq, i);
    instants[i] = tinstant_make(point, inst->t, inst->basetypid);
    pfree(DatumGetPointer(point));
  }

  for (int i = 0; i < seq->count; i++)
    lwpoint_free((LWPOINT *) points[i]);
  pfree(points);
  pfree(DatumGetPointer(multipoint)); pfree(DatumGetPointer(transf));
  POSTGIS_FREE_IF_COPY_P(gs, DatumGetPointer(gs));
  lwmpoint_free(lwmpoint);

  return tsequence_make_free(instants, seq->count,
    seq->period.lower_inc, seq->period.upper_inc, linear, NORMALIZE_NO);
}

/**
 * Transform a temporal sequence set point into another spatial reference system
 *
 * @note In order to do a SINGLE call to the PostGIS transform function we do
 * not iterate through the sequences and call the transform for the sequence.
 */
static TSequenceSet *
tpointseqset_transform(const TSequenceSet *ts, Datum srid)
{
  /* Singleton sequence set */
  if (ts->count == 1)
  {
    TSequence *seq = tpointseq_transform(tsequenceset_seq_n(ts, 0), srid);
    TSequenceSet *result = tsequence_to_tsequenceset(seq);
    pfree(seq);
    return result;
  }

  /* General case */
  int k = 0;
  LWGEOM **points = palloc(sizeof(LWGEOM *) * ts->totalcount);
  int maxcount = -1; /* number of instants of the longest sequence */
  for (int i = 0; i < ts->count; i++)
  {
    const TSequence *seq = tsequenceset_seq_n(ts, i);
    maxcount = Max(maxcount, seq->count);
    for (int j = 0; j < seq->count; j++)
    {
      Datum value = tinstant_value(tsequence_inst_n(seq, j));
      GSERIALIZED *gsvalue = (GSERIALIZED *) DatumGetPointer(value);
      points[k++] = lwgeom_from_gserialized(gsvalue);
    }
  }
  /* Last parameter set to STEP to force the function to return multipoint */
  LWGEOM *lwgeom = lwpointarr_make_trajectory(points, ts->totalcount, STEP);
  Datum multipoint = PointerGetDatum(geo_serialize(lwgeom));
  pfree(lwgeom);
  Datum transf = datum_transform(multipoint, srid);
  GSERIALIZED *gs = (GSERIALIZED *) PG_DETOAST_DATUM(transf);
  LWMPOINT *lwmpoint = lwgeom_as_lwmpoint(lwgeom_from_gserialized(gs));
  TSequence **sequences = palloc(sizeof(TSequence *) * ts->count);
  TInstant **instants = palloc(sizeof(TInstant *) * maxcount);
  bool linear = MOBDB_FLAGS_GET_LINEAR(ts->flags);
  k = 0;
  for (int i = 0; i < ts->count; i++)
  {
    const TSequence *seq = tsequenceset_seq_n(ts, i);
    for (int j = 0; j < seq->count; j++)
    {
      Datum point = PointerGetDatum(geo_serialize((LWGEOM *) (lwmpoint->geoms[k++])));
      const TInstant *inst = tsequence_inst_n(seq, j);
      instants[j] = tinstant_make(point, inst->t, inst->basetypid);
      pfree(DatumGetPointer(point));
    }
    sequences[i] = tsequence_make((const TInstant **) instants, seq->count,
      seq->period.lower_inc, seq->period.upper_inc, linear, NORMALIZE_NO);
    for (int j = 0; j < seq->count; j++)
      pfree(instants[j]);
  }
  TSequenceSet *result = tsequenceset_make_free(sequences, ts->count, NORMALIZE_NO);
  for (int i = 0; i < ts->totalcount; i++)
    lwpoint_free((LWPOINT *) points[i]);
  pfree(points); pfree(instants);
  pfree(DatumGetPointer(multipoint)); pfree(DatumGetPointer(transf));
  POSTGIS_FREE_IF_COPY_P(gs, DatumGetPointer(gs));
  lwmpoint_free(lwmpoint);
  return result;
}

PG_FUNCTION_INFO_V1(tpoint_transform);
/**
 * Transform a temporal point into another spatial reference system
 */
PGDLLEXPORT Datum
tpoint_transform(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  Datum srid = PG_GETARG_DATUM(1);
  /* Store fcinfo into a global variable */
  store_fcinfo(fcinfo);

  Temporal *result;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT)
    result = (Temporal *) tpointinst_transform((TInstant *) temp, srid);
  else if (temp->subtype == INSTANTSET)
    result = (Temporal *) tpointinstset_transform((TInstantSet *) temp, srid);
  else if (temp->subtype == SEQUENCE)
    result = (Temporal *) tpointseq_transform((TSequence *) temp, srid);
  else /* temp->subtype == SEQUENCESET */
    result = (Temporal *) tpointseqset_transform((TSequenceSet *) temp, srid);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_POINTER(result);
}

/*****************************************************************************
 * Cast functions
 * Notice that a geometry point and a geography point are of different size
 * since the geography point keeps a bounding box
 *****************************************************************************/

/** Symbolic constants for transforming tgeompoint <-> tgeogpoint */
#define GEOG_FROM_GEOM        true
#define GEOM_FROM_GEOG        false

/**
 * Converts the temporal point to a geometry/geography point
 */
static TInstant *
tpointinst_convert_tgeom_tgeog(const TInstant *inst, bool oper)
{
  Datum point = (oper == GEOG_FROM_GEOM) ?
    call_function1(geography_from_geometry, tinstant_value(inst)) :
    call_function1(geometry_from_geography, tinstant_value(inst));
  return tinstant_make(point, inst->t, (oper == GEOG_FROM_GEOM) ?
    type_oid(T_GEOGRAPHY) : type_oid(T_GEOMETRY));
}

/**
 * Converts the temporal point to a geometry/geography point
 */
static TInstantSet *
tpointinstset_convert_tgeom_tgeog(const TInstantSet *ti, bool oper)
{
  /* Construct a multipoint with all the points */
  LWPOINT **points = palloc(sizeof(LWPOINT *) * ti->count);
  const TInstant *inst;
  GSERIALIZED *gs;
  for (int i = 0; i < ti->count; i++)
  {
    inst = tinstantset_inst_n(ti, i);
    gs = (GSERIALIZED *) DatumGetPointer(tinstant_value_ptr(inst));
    points[i] = lwgeom_as_lwpoint(lwgeom_from_gserialized(gs));
  }
  LWGEOM *lwresult = (LWGEOM *) lwcollection_construct(MULTIPOINTTYPE,
      points[0]->srid, NULL, (uint32_t) ti->count, (LWGEOM **) points);
  Datum mpoint_orig = PointerGetDatum(geo_serialize(lwresult));
  for (int i = 0; i < ti->count; i++)
    lwpoint_free(points[i]);
  pfree(points);
  /* Convert the multipoint geometry/geography */
  Datum mpoint_trans = (oper == GEOG_FROM_GEOM) ?
      call_function1(geography_from_geometry, mpoint_orig) :
      call_function1(geometry_from_geography, mpoint_orig);
  /* Construct the resulting tpoint from the multipoint geometry/geography */
  gs = (GSERIALIZED *) DatumGetPointer(mpoint_trans);
  LWMPOINT *lwmpoint = lwgeom_as_lwmpoint(lwgeom_from_gserialized(gs));
  TInstant **instants = palloc(sizeof(TInstant *) * ti->count);
  Oid result_oid = (oper == GEOG_FROM_GEOM) ?
      type_oid(T_GEOGRAPHY) : type_oid(T_GEOMETRY);
  for (int i = 0; i < ti->count; i++)
  {
    inst = tinstantset_inst_n(ti, i);
    Datum point = PointerGetDatum(geo_serialize((LWGEOM *) (lwmpoint->geoms[i])));
    instants[i] = tinstant_make(point, inst->t, result_oid);
    pfree(DatumGetPointer(point));
  }
  lwmpoint_free(lwmpoint);
  return tinstantset_make_free(instants, ti->count, MERGE_NO);
}

/**
 * Converts the temporal point to a geometry/geography point
 */
static TSequence *
tpointseq_convert_tgeom_tgeog(const TSequence *seq, bool oper)
{
  /* Construct a multipoint with all the points */
  LWPOINT **points = palloc(sizeof(LWPOINT *) * seq->count);
  const TInstant *inst;
  GSERIALIZED *gs;
  for (int i = 0; i < seq->count; i++)
  {
    inst = tsequence_inst_n(seq, i);
    gs = (GSERIALIZED *) DatumGetPointer(tinstant_value_ptr(inst));
    points[i] = lwgeom_as_lwpoint(lwgeom_from_gserialized(gs));
  }
  LWGEOM *lwresult = (LWGEOM *) lwcollection_construct(MULTIPOINTTYPE,
      points[0]->srid, NULL, (uint32_t) seq->count, (LWGEOM **) points);
  Datum mpoint_orig = PointerGetDatum(geo_serialize(lwresult));
  for (int i = 0; i < seq->count; i++)
    lwpoint_free(points[i]);
  pfree(points);
  /* Convert the multipoint geometry/geography */
  Datum mpoint_trans = (oper == GEOG_FROM_GEOM) ?
      call_function1(geography_from_geometry, mpoint_orig) :
      call_function1(geometry_from_geography, mpoint_orig);
  /* Construct the resulting tpoint from the multipoint geometry/geography */
  gs = (GSERIALIZED *) DatumGetPointer(mpoint_trans);
  LWMPOINT *lwmpoint = lwgeom_as_lwmpoint(lwgeom_from_gserialized(gs));
  TInstant **instants = palloc(sizeof(TInstant *) * seq->count);
  Oid result_oid = (oper == GEOG_FROM_GEOM) ?
      type_oid(T_GEOGRAPHY) : type_oid(T_GEOMETRY);
  for (int i = 0; i < seq->count; i++)
  {
    inst = tsequence_inst_n(seq, i);
    Datum point = PointerGetDatum(geo_serialize((LWGEOM *) (lwmpoint->geoms[i])));
    instants[i] = tinstant_make(point, inst->t, result_oid);
    pfree(DatumGetPointer(point));
  }
  lwmpoint_free(lwmpoint);
  return tsequence_make_free(instants, seq->count, seq->period.lower_inc,
    seq->period.upper_inc, MOBDB_FLAGS_GET_LINEAR(seq->flags), NORMALIZE_NO);
}

/**
 * Converts the temporal point to a geometry/geography point
 */
static TSequenceSet *
tpointseqset_convert_tgeom_tgeog(const TSequenceSet *ts, bool oper)
{
  TSequence **sequences = palloc(sizeof(TSequence *) * ts->count);
  for (int i = 0; i < ts->count; i++)
  {
    const TSequence *seq = tsequenceset_seq_n(ts, i);
    sequences[i] = tpointseq_convert_tgeom_tgeog(seq, oper);
  }
  return tsequenceset_make_free(sequences, ts->count, NORMALIZE_NO);
}

/**
 * Converts the temporal point to a geometry/geography point
 * (dispatch function)
 */
Temporal *
tpoint_convert_tgeom_tgeog(const Temporal *temp, bool oper)
{
  Temporal *result;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT)
    result = (Temporal *) tpointinst_convert_tgeom_tgeog(
      (TInstant *) temp, oper);
  else if (temp->subtype == INSTANTSET)
    result = (Temporal *) tpointinstset_convert_tgeom_tgeog(
      (TInstantSet *) temp, oper);
  else if (temp->subtype == SEQUENCE)
    result = (Temporal *) tpointseq_convert_tgeom_tgeog(
      (TSequence *) temp, oper);
  else /* temp->subtype == SEQUENCESET */
    result = (Temporal *) tpointseqset_convert_tgeom_tgeog(
      (TSequenceSet *) temp, oper);
  return result;
}

PG_FUNCTION_INFO_V1(tgeompoint_to_tgeogpoint);
/**
 * Converts a temporal geometry point to a temporal geography point
 */
PGDLLEXPORT Datum
tgeompoint_to_tgeogpoint(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  Temporal *result = tpoint_convert_tgeom_tgeog(temp, GEOG_FROM_GEOM);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(tgeogpoint_to_tgeompoint);
/**
 * Converts a temporal geography point to a temporal geometry point
 */
PGDLLEXPORT Datum
tgeogpoint_to_tgeompoint(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  Temporal *result = tpoint_convert_tgeom_tgeog(temp, GEOM_FROM_GEOG);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_POINTER(result);
}

/*****************************************************************************
 * Set precision of the coordinates.
 *****************************************************************************/

/**
 * Set the precision of the point coordinates to the number
 * of decimal places
 */
static Datum
datum_set_precision(Datum value, Datum prec)
{
  GSERIALIZED *gs = (GSERIALIZED *) DatumGetPointer(value);
  int srid = gserialized_get_srid(gs);
  LWPOINT *lwpoint;
  if (FLAGS_GET_Z(gs->flags))
  {
    const POINT3DZ *point = gs_get_point3dz_p(gs);
    double x = DatumGetFloat8(datum_round(Float8GetDatum(point->x), prec));
    double y = DatumGetFloat8(datum_round(Float8GetDatum(point->y), prec));
    double z = DatumGetFloat8(datum_round(Float8GetDatum(point->z), prec));
    lwpoint = lwpoint_make3dz(srid, x, y, z);
  }
  else
  {
    const POINT2D *point = gs_get_point2d_p(gs);
    double x = DatumGetFloat8(datum_round(Float8GetDatum(point->x), prec));
    double y = DatumGetFloat8(datum_round(Float8GetDatum(point->y), prec));
    lwpoint = lwpoint_make2d(srid, x, y);
  }
  bool geodetic = FLAGS_GET_GEODETIC(gs->flags);
  FLAGS_SET_GEODETIC(lwpoint->flags, geodetic);
  GSERIALIZED *result = geo_serialize((LWGEOM *) lwpoint);
  pfree(lwpoint);
  return PointerGetDatum(result);
}

PG_FUNCTION_INFO_V1(tpoint_set_precision);
/**
 * Set the precision of the coordinates of the temporal point to the number
 * of decimal places
 */
PGDLLEXPORT Datum
tpoint_set_precision(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  Datum size = PG_GETARG_DATUM(1);
  /* We only need to fill these parameters for tfunc_temporal */
  LiftedFunctionInfo lfinfo;
  lfinfo.func = (varfunc) &datum_set_precision;
  lfinfo.numparam = 2;
  lfinfo.restypid = temp->basetypid;
  Temporal *result = tfunc_temporal(temp, size, lfinfo);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_POINTER(result);
}

/*****************************************************************************
 * Functions for extracting coordinates.
 *****************************************************************************/

/**
 * Get the X coordinates of the temporal point
 */
static Datum
point_get_x(Datum point)
{
  POINT4D p;
  datum_get_point4d(&p, point);
  return Float8GetDatum(p.x);
}

PG_FUNCTION_INFO_V1(tpoint_get_x);
/**
 * Get the X coordinates of the temporal point
 */
PGDLLEXPORT Datum
tpoint_get_x(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  ensure_tgeo_base_type(temp->basetypid);
  /* We only need to fill these parameters for tfunc_temporal */
  LiftedFunctionInfo lfinfo;
  lfinfo.func = (varfunc) &point_get_x;
  lfinfo.numparam = 1;
  lfinfo.restypid = FLOAT8OID;
  Temporal *result = tfunc_temporal(temp, (Datum) NULL, lfinfo);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_POINTER(result);
}

/**
 * Get the Y coordinates of the temporal point
 */
static Datum
point_get_y(Datum point)
{
  POINT4D p;
  datum_get_point4d(&p, point);
  return Float8GetDatum(p.y);
}

PG_FUNCTION_INFO_V1(tpoint_get_y);
/**
 * Get the Y coordinates of the temporal point
 */
PGDLLEXPORT Datum
tpoint_get_y(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  ensure_tgeo_base_type(temp->basetypid);
  /* We only need to fill these parameters for tfunc_temporal */
  LiftedFunctionInfo lfinfo;
  lfinfo.func = (varfunc) &point_get_y;
  lfinfo.numparam = 1;
  lfinfo.restypid = FLOAT8OID;
  Temporal *result = tfunc_temporal(temp, (Datum) NULL, lfinfo);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_POINTER(result);
}

/**
 * Get the Z coordinates of the temporal point
 */
static Datum
point_get_z(Datum point)
{
  POINT4D p;
  datum_get_point4d(&p, point);
  return Float8GetDatum(p.z);
}

PG_FUNCTION_INFO_V1(tpoint_get_z);
/**
 * Get the Z coordinates of the temporal point
 */
PGDLLEXPORT Datum
tpoint_get_z(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  ensure_tgeo_base_type(temp->basetypid);
  ensure_has_Z_tpoint(temp);
  /* We only need to fill these parameters for tfunc_temporal */
  LiftedFunctionInfo lfinfo;
  lfinfo.func = (varfunc) &point_get_z;
  lfinfo.numparam = 1;
  lfinfo.restypid = FLOAT8OID;
  Temporal *result = tfunc_temporal(temp, (Datum) NULL, lfinfo);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_POINTER(result);
}

/*****************************************************************************
 * Length functions
 *****************************************************************************/

/**
 * Returns the length traversed by the temporal sequence point
 */
static double
tpointseq_length(const TSequence *seq)
{
  assert(MOBDB_FLAGS_GET_LINEAR(seq->flags));
  Datum traj = tpointseq_trajectory(seq);
  GSERIALIZED *gstraj = (GSERIALIZED *) DatumGetPointer(traj);
  if (gserialized_get_type(gstraj) == POINTTYPE)
    return 0;

  /* We are sure that the trajectory is a line */
  double result = MOBDB_FLAGS_GET_GEODETIC(seq->flags) ?
    DatumGetFloat8(call_function2(geography_length, traj,
      BoolGetDatum(true))) :
    /* The next function call works for 2D and 3D */
    DatumGetFloat8(call_function1(LWGEOM_length_linestring, traj));
  return result;
}

/**
 * Returns the length traversed by the temporal sequence set point
 */
static double
tpointseqset_length(const TSequenceSet *ts)
{
  assert(MOBDB_FLAGS_GET_LINEAR(ts->flags));
  double result = 0;
  for (int i = 0; i < ts->count; i++)
    result += tpointseq_length(tsequenceset_seq_n(ts, i));
  return result;
}

PG_FUNCTION_INFO_V1(tpoint_length);
/**
 * Returns the length traversed by the temporal sequence (set) point
 */
PGDLLEXPORT Datum
tpoint_length(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  double result = 0.0;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT || temp->subtype == INSTANTSET ||
    ! MOBDB_FLAGS_GET_LINEAR(temp->flags))
    ;
  else if (temp->subtype == SEQUENCE)
    result = tpointseq_length((TSequence *) temp);
  else /* temp->subtype == SEQUENCESET */
    result = tpointseqset_length((TSequenceSet *) temp);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_FLOAT8(result);
}

/*****************************************************************************/

/**
 * Returns the cumulative length traversed by the temporal instant point
 */
static TInstant *
tpointinst_cumulative_length(const TInstant *inst)
{
  return tinstant_make(Float8GetDatum(0.0), inst->t, FLOAT8OID);
}

/**
 * Returns the cumulative length traversed by the temporal instant set point
 */
static TInstantSet *
tpointinstset_cumulative_length(const TInstantSet *ti)
{
  TInstant **instants = palloc(sizeof(TInstant *) * ti->count);
  Datum length = Float8GetDatum(0.0);
  for (int i = 0; i < ti->count; i++)
  {
    const TInstant *inst = tinstantset_inst_n(ti, i);
    instants[i] = tinstant_make(length, inst->t, FLOAT8OID);
  }
  return tinstantset_make_free(instants, ti->count, MERGE_NO);
}

/**
 * Returns the cumulative length traversed by the temporal sequence point
 */
static TSequence *
tpointseq_cumulative_length(const TSequence *seq, double prevlength)
{
  bool linear = MOBDB_FLAGS_GET_LINEAR(seq->flags);
  const TInstant *inst;

  /* Instantaneous sequence */
  if (seq->count == 1)
  {
    inst = tsequence_inst_n(seq, 0);
    TInstant *inst1 = tinstant_make(Float8GetDatum(0), inst->t,
      FLOAT8OID);
    TSequence *result = tinstant_to_tsequence(inst1, linear);
    pfree(inst1);
    return result;
  }

  TInstant **instants = palloc(sizeof(TInstant *) * seq->count);
  /* Stepwise interpolation */
  if (! linear)
  {
    Datum length = Float8GetDatum(0.0);
    for (int i = 0; i < seq->count; i++)
    {
      inst = tsequence_inst_n(seq, i);
      instants[i] = tinstant_make(length, inst->t, FLOAT8OID);
    }
  }
  else
  /* Linear interpolation */
  {
    datum_func2 func = get_pt_distance_fn(seq->flags);
    const TInstant *inst1 = tsequence_inst_n(seq, 0);
    Datum value1 = tinstant_value(inst1);
    double length = prevlength;
    instants[0] = tinstant_make(Float8GetDatum(length), inst1->t,
        FLOAT8OID);
    for (int i = 1; i < seq->count; i++)
    {
      const TInstant *inst2 = tsequence_inst_n(seq, i);
      Datum value2 = tinstant_value(inst2);
      if (datum_ne(value1, value2, inst1->basetypid))
        length += DatumGetFloat8(func(value1, value2));
      instants[i] = tinstant_make(Float8GetDatum(length), inst2->t,
        FLOAT8OID);
      inst1 = inst2;
      value1 = value2;
    }
  }
  TSequence *result = tsequence_make((const TInstant **) instants, seq->count,
    seq->period.lower_inc, seq->period.upper_inc, linear, NORMALIZE);

  for (int i = 1; i < seq->count; i++)
    pfree(instants[i]);
  pfree(instants);

  return result;
}

/**
 * Returns the cumulative length traversed by the temporal sequence set point
 */
static TSequenceSet *
tpointseqset_cumulative_length(const TSequenceSet *ts)
{
  TSequence **sequences = palloc(sizeof(TSequence *) * ts->count);
  double length = 0;
  for (int i = 0; i < ts->count; i++)
  {
    const TSequence *seq = tsequenceset_seq_n(ts, i);
    sequences[i] = tpointseq_cumulative_length(seq, length);
    const TInstant *end = tsequence_inst_n(sequences[i], seq->count - 1);
    length = DatumGetFloat8(tinstant_value(end));
  }
  TSequenceSet *result = tsequenceset_make((const TSequence **) sequences,
    ts->count, NORMALIZE_NO);

  for (int i = 1; i < ts->count; i++)
    pfree(sequences[i]);
  pfree(sequences);

  return result;
}

PG_FUNCTION_INFO_V1(tpoint_cumulative_length);
/**
 * Returns the cumulative length traversed by the temporal point
 */
PGDLLEXPORT Datum
tpoint_cumulative_length(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  Temporal *result;
  ensure_valid_tempsubtype(temp->subtype);
  /* Store fcinfo into a global variable */
  store_fcinfo(fcinfo);
  if (temp->subtype == INSTANT)
    result = (Temporal *) tpointinst_cumulative_length((TInstant *) temp);
  else if (temp->subtype == INSTANTSET)
    result = (Temporal *) tpointinstset_cumulative_length((TInstantSet *) temp);
  else if (temp->subtype == SEQUENCE)
    result = (Temporal *) tpointseq_cumulative_length((TSequence *) temp, 0);
  else /* temp->subtype == SEQUENCESET */
    result = (Temporal *) tpointseqset_cumulative_length((TSequenceSet *) temp);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_POINTER(result);
}

/*****************************************************************************
 * Speed functions
 *****************************************************************************/

/**
 * Returns the speed of the temporal point
 * @pre The temporal point has linear interpolation
 */
static TSequence *
tpointseq_speed(const TSequence *seq)
{
  assert(MOBDB_FLAGS_GET_LINEAR(seq->flags));

  /* Instantaneous sequence */
  if (seq->count == 1)
    return NULL;

  /* General case */
  TInstant **instants = palloc(sizeof(TInstant *) * seq->count);
  datum_func2 func = get_pt_distance_fn(seq->flags);
  const TInstant *inst1 = tsequence_inst_n(seq, 0);
  Datum value1 = tinstant_value(inst1);
  double speed;
  for (int i = 0; i < seq->count - 1; i++)
  {
    const TInstant *inst2 = tsequence_inst_n(seq, i + 1);
    Datum value2 = tinstant_value(inst2);
    speed = datum_point_eq(value1, value2) ? 0 :
      DatumGetFloat8(func(value1, value2)) /
        ((double)(inst2->t - inst1->t) / 1000000);
    instants[i] = tinstant_make(Float8GetDatum(speed), inst1->t, FLOAT8OID);
    inst1 = inst2;
    value1 = value2;
  }
  instants[seq->count - 1] = tinstant_make(Float8GetDatum(speed),
    seq->period.upper, FLOAT8OID);
  /* The resulting sequence has step interpolation */
  TSequence *result = tsequence_make((const TInstant **) instants, seq->count,
    seq->period.lower_inc, seq->period.upper_inc, STEP, NORMALIZE);
  pfree_array((void **) instants, seq->count - 1);
  return result;
}

/**
 * Returns the speed of the temporal point
 */
static TSequenceSet *
tpointseqset_speed(const TSequenceSet *ts)
{
  TSequence **sequences = palloc(sizeof(TSequence *) * ts->count);
  int k = 0;
  for (int i = 0; i < ts->count; i++)
  {
    const TSequence *seq = tsequenceset_seq_n(ts, i);
    if (seq->count > 1)
      sequences[k++] = tpointseq_speed(seq);
  }
  /* The resulting sequence set has step interpolation */
  return tsequenceset_make_free(sequences, k, NORMALIZE);
}

PG_FUNCTION_INFO_V1(tpoint_speed);
/**
 * Returns the speed of the temporal point
 */
PGDLLEXPORT Datum
tpoint_speed(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  Temporal *result = NULL;
  /* Store fcinfo into a global variable */
  store_fcinfo(fcinfo);
  ensure_linear_interpolation(temp->flags);
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT || temp->subtype == INSTANTSET)
    ;
  else if (temp->subtype == SEQUENCE)
    result = (Temporal *) tpointseq_speed((TSequence *) temp);
  else /* temp->subtype == SEQUENCESET */
    result = (Temporal *) tpointseqset_speed((TSequenceSet *) temp);
  PG_FREE_IF_COPY(temp, 0);
  if (result == NULL)
    PG_RETURN_NULL();
  PG_RETURN_POINTER(result);
}

/*****************************************************************************
 * Time-weighed centroid for temporal geometry points
 *****************************************************************************/

/**
 * Returns the time-weighed centroid of the temporal geometry point of
 * instant set type
 */
Datum
tpointinstset_twcentroid(const TInstantSet *ti)
{
  int srid = tpointinstset_srid(ti);
  TInstant **instantsx = palloc(sizeof(TInstant *) * ti->count);
  TInstant **instantsy = palloc(sizeof(TInstant *) * ti->count);
  TInstant **instantsz = NULL; /* keep compiler quiet */
  bool hasz = MOBDB_FLAGS_GET_Z(ti->flags);
  if (hasz)
    instantsz = palloc(sizeof(TInstant *) * ti->count);

  for (int i = 0; i < ti->count; i++)
  {
    const TInstant *inst = tinstantset_inst_n(ti, i);
    POINT4D p;
    datum_get_point4d(&p, tinstant_value(inst));
    instantsx[i] = tinstant_make(Float8GetDatum(p.x), inst->t,
      FLOAT8OID);
    instantsy[i] = tinstant_make(Float8GetDatum(p.y), inst->t,
      FLOAT8OID);
    if (hasz)
      instantsz[i] = tinstant_make(Float8GetDatum(p.z), inst->t,
        FLOAT8OID);
  }
  TInstantSet *tix = tinstantset_make_free(instantsx, ti->count, MERGE_NO);
  TInstantSet *tiy = tinstantset_make_free(instantsy, ti->count, MERGE_NO);
  TInstantSet *tiz = NULL; /* keep compiler quiet */
  if (hasz)
    tiz = tinstantset_make_free(instantsz, ti->count, MERGE_NO);
  double twavgx = tnumberinstset_twavg(tix);
  double twavgy = tnumberinstset_twavg(tiy);
  double twavgz;
  if (hasz)
    twavgz = tnumberinstset_twavg(tiz);
  Datum result = point_make(twavgx, twavgy, twavgz, hasz, false, srid);

  pfree(tix); pfree(tiy);
  if (hasz)
    pfree(tiz);
  return result;
}

/**
 * Returns the time-weighed centroid of the temporal geometry point of
 * sequence type
 */
Datum
tpointseq_twcentroid(const TSequence *seq)
{
  int srid = tpointseq_srid(seq);
  TInstant **instantsx = palloc(sizeof(TInstant *) * seq->count);
  TInstant **instantsy = palloc(sizeof(TInstant *) * seq->count);
  TInstant **instantsz;
  bool hasz = MOBDB_FLAGS_GET_Z(seq->flags);
  if (hasz)
    instantsz = palloc(sizeof(TInstant *) * seq->count);

  for (int i = 0; i < seq->count; i++)
  {
    const TInstant *inst = tsequence_inst_n(seq, i);
    POINT4D p;
    datum_get_point4d(&p, tinstant_value(inst));
    instantsx[i] = tinstant_make(Float8GetDatum(p.x), inst->t,
      FLOAT8OID);
    instantsy[i] = tinstant_make(Float8GetDatum(p.y), inst->t,
      FLOAT8OID);
    if (hasz)
      instantsz[i] = tinstant_make(Float8GetDatum(p.z), inst->t,
        FLOAT8OID);
  }
  TSequence *seqx = tsequence_make_free(instantsx, seq->count,
    seq->period.lower_inc, seq->period.upper_inc,
    MOBDB_FLAGS_GET_LINEAR(seq->flags), NORMALIZE);
  TSequence *seqy = tsequence_make_free(instantsy, seq->count,
    seq->period.lower_inc, seq->period.upper_inc,
    MOBDB_FLAGS_GET_LINEAR(seq->flags), NORMALIZE);
  TSequence *seqz;
  if (hasz)
    seqz = tsequence_make_free(instantsz, seq->count, seq->period.lower_inc,
      seq->period.upper_inc, MOBDB_FLAGS_GET_LINEAR(seq->flags), NORMALIZE);
  double twavgx = tnumberseq_twavg(seqx);
  double twavgy = tnumberseq_twavg(seqy);
  double twavgz;
  if (hasz)
    twavgz = tnumberseq_twavg(seqz);
  Datum result = point_make(twavgx, twavgy, twavgz, hasz, false, srid);
  pfree(seqx); pfree(seqy);
  if (hasz)
    pfree(seqz);
  return result;
}

/**
 * Returns the time-weighed centroid of the temporal geometry point of
 * sequence set type
 */
Datum
tpointseqset_twcentroid(const TSequenceSet *ts)
{
  int srid = tpointseqset_srid(ts);
  TSequence **sequencesx = palloc(sizeof(TSequence *) * ts->count);
  TSequence **sequencesy = palloc(sizeof(TSequence *) * ts->count);
  TSequence **sequencesz = NULL; /* keep compiler quiet */
  bool hasz = MOBDB_FLAGS_GET_Z(ts->flags);
  if (hasz)
    sequencesz = palloc(sizeof(TSequence *) * ts->count);
  for (int i = 0; i < ts->count; i++)
  {
    const TSequence *seq = tsequenceset_seq_n(ts, i);
    TInstant **instantsx = palloc(sizeof(TInstant *) * seq->count);
    TInstant **instantsy = palloc(sizeof(TInstant *) * seq->count);
    TInstant **instantsz;
    if (hasz)
      instantsz = palloc(sizeof(TInstant *) * seq->count);
    for (int j = 0; j < seq->count; j++)
    {
      const TInstant *inst = tsequence_inst_n(seq, j);
      POINT4D p;
      datum_get_point4d(&p, tinstant_value(inst));
      instantsx[j] = tinstant_make(Float8GetDatum(p.x),
        inst->t, FLOAT8OID);
      instantsy[j] = tinstant_make(Float8GetDatum(p.y),
        inst->t, FLOAT8OID);
      if (hasz)
        instantsz[j] = tinstant_make(Float8GetDatum(p.z),
          inst->t, FLOAT8OID);
    }
    sequencesx[i] = tsequence_make_free(instantsx, seq->count,
      seq->period.lower_inc, seq->period.upper_inc,
      MOBDB_FLAGS_GET_LINEAR(seq->flags), NORMALIZE);
    sequencesy[i] = tsequence_make_free(instantsy,
      seq->count, seq->period.lower_inc, seq->period.upper_inc,
      MOBDB_FLAGS_GET_LINEAR(seq->flags), NORMALIZE);
    if (hasz)
      sequencesz[i] = tsequence_make_free(instantsz, seq->count,
        seq->period.lower_inc, seq->period.upper_inc,
        MOBDB_FLAGS_GET_LINEAR(seq->flags), NORMALIZE);
  }
  TSequenceSet *tsx = tsequenceset_make_free(sequencesx, ts->count, NORMALIZE);
  TSequenceSet *tsy = tsequenceset_make_free(sequencesy, ts->count, NORMALIZE);
  TSequenceSet *tsz = NULL; /* keep compiler quiet */
  if (hasz)
    tsz = tsequenceset_make_free(sequencesz, ts->count, NORMALIZE);

  double twavgx = tnumberseqset_twavg(tsx);
  double twavgy = tnumberseqset_twavg(tsy);
  double twavgz;
  if (hasz)
    twavgz = tnumberseqset_twavg(tsz);
  Datum result = point_make(twavgx, twavgy, twavgz, hasz, false, srid);
  pfree(tsx); pfree(tsy);
  if (hasz)
    pfree(tsz);
  return result;
}

/**
 * Returns the time-weighed centroid of the temporal geometry point
 * (dispatch function)
 */
Datum
tpoint_twcentroid_internal(Temporal *temp)
{
  Datum result;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT)
    result = tinstant_value_copy((TInstant *) temp);
  else if (temp->subtype == INSTANTSET)
    result = tpointinstset_twcentroid((TInstantSet *) temp);
  else if (temp->subtype == SEQUENCE)
    result = tpointseq_twcentroid((TSequence *) temp);
  else /* temp->subtype == SEQUENCESET */
    result = tpointseqset_twcentroid((TSequenceSet *) temp);
  return result;
}

PG_FUNCTION_INFO_V1(tpoint_twcentroid);
/**
 * Returns the time-weighed centroid of the temporal geometry point
 */
PGDLLEXPORT Datum
tpoint_twcentroid(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  Datum result = tpoint_twcentroid_internal(temp);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_DATUM(result);
}

/*****************************************************************************
 * Temporal azimuth
 *****************************************************************************/

/**
 * Returns the azimuth of the two geometry points
 */
static Datum
geom_azimuth(Datum geom1, Datum geom2)
{
  const POINT2D *p1 = datum_get_point2d_p(geom1);
  const POINT2D *p2 = datum_get_point2d_p(geom2);
  double result;
  azimuth_pt_pt(p1, p2, &result);
  return Float8GetDatum(result);
}

/**
 * Returns the azimuth the two geography points
 */
static Datum
geog_azimuth(Datum geom1, Datum geom2)
{
  return CallerFInfoFunctionCall2(geography_azimuth, (fetch_fcinfo())->flinfo,
    InvalidOid, geom1, geom2);
}

/**
 * Returns the temporal azimuth of the temporal geometry point of
 * sequence type
 *
 * @param[out] result Array on which the pointers of the newly constructed
 * sequences are stored
 * @param[in] seq Temporal value
 */
static int
tpointseq_azimuth1(TSequence **result, const TSequence *seq)
{
  /* Instantaneous sequence */
  if (seq->count == 1)
    return 0;

  /* Determine the PostGIS function to call */
  datum_func2 func = MOBDB_FLAGS_GET_GEODETIC(seq->flags) ?
    &geog_azimuth : &geom_azimuth;

  /* We are sure that there are at least 2 instants */
  TInstant **instants = palloc(sizeof(TInstant *) * seq->count);
  const TInstant *inst1 = tsequence_inst_n(seq, 0);
  Datum value1 = tinstant_value(inst1);
  int k = 0, l = 0;
  Datum azimuth = 0; /* Make the compiler quiet */
  bool lower_inc = seq->period.lower_inc, upper_inc;
  for (int i = 1; i < seq->count; i++)
  {
    const TInstant *inst2 = tsequence_inst_n(seq, i);
    Datum value2 = tinstant_value(inst2);
    upper_inc = (i == seq->count - 1) ? seq->period.upper_inc : false;
    if (datum_ne(value1, value2, seq->basetypid))
    {
      azimuth = func(value1, value2);
      instants[k++] = tinstant_make(azimuth, inst1->t, FLOAT8OID);
    }
    else
    {
      if (k != 0)
      {
        instants[k++] = tinstant_make(azimuth, inst1->t, FLOAT8OID);
        upper_inc = true;
        /* Resulting sequence has step interpolation */
        result[l++] = tsequence_make((const TInstant **) instants, k, lower_inc,
          upper_inc, STEP, NORMALIZE);
        for (int j = 0; j < k; j++)
          pfree(instants[j]);
        k = 0;
      }
      lower_inc = true;
    }
    inst1 = inst2;
    value1 = value2;
  }
  if (k != 0)
  {
    instants[k++] = tinstant_make(azimuth, inst1->t, FLOAT8OID);
    /* Resulting sequence has step interpolation */
    result[l++] = tsequence_make((const TInstant **) instants, k,
      lower_inc, upper_inc, STEP, NORMALIZE);
  }

  pfree(instants);

  return l;
}

/**
 * Returns the temporal azimuth of the temporal geometry point
 * of sequence type
 */
TSequenceSet *
tpointseq_azimuth(const TSequence *seq)
{
  TSequence **sequences = palloc(sizeof(TSequence *) * seq->count);
  int count = tpointseq_azimuth1(sequences, seq);
  /* Resulting sequence set has step interpolation */
  return tsequenceset_make_free(sequences, count, NORMALIZE);
}

/**
 * Returns the temporal azimuth of the temporal geometry point
 * of sequence set type
 */
TSequenceSet *
tpointseqset_azimuth(TSequenceSet *ts)
{
  if (ts->count == 1)
    return tpointseq_azimuth(tsequenceset_seq_n(ts, 0));

  TSequence **sequences = palloc(sizeof(TSequence *) * ts->totalcount);
  int k = 0;
  for (int i = 0; i < ts->count; i++)
  {
    const TSequence *seq = tsequenceset_seq_n(ts, i);
    k += tpointseq_azimuth1(&sequences[k], seq);
  }
  /* Resulting sequence set has step interpolation */
  return tsequenceset_make_free(sequences, k, NORMALIZE);
}

PG_FUNCTION_INFO_V1(tpoint_azimuth);
/**
 * Returns the temporal azimuth of the temporal geometry point
 */
PGDLLEXPORT Datum
tpoint_azimuth(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  /* Store fcinfo into a global variable */
  store_fcinfo(fcinfo);
  Temporal *result = NULL;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT || temp->subtype == INSTANTSET ||
    (temp->subtype == SEQUENCE && ! MOBDB_FLAGS_GET_LINEAR(temp->flags)) ||
    (temp->subtype == SEQUENCESET && ! MOBDB_FLAGS_GET_LINEAR(temp->flags)))
    ;
  else if (temp->subtype == SEQUENCE)
    result = (Temporal *) tpointseq_azimuth((TSequence *) temp);
  else /* temp->subtype == SEQUENCESET */
    result = (Temporal *) tpointseqset_azimuth((TSequenceSet *) temp);
  PG_FREE_IF_COPY(temp, 0);
  if (result == NULL)
    PG_RETURN_NULL();
  PG_RETURN_POINTER(result);
}

/*****************************************************************************
 * Functions computing the intersection of two segments derived from
 * http://www.science.smith.edu/~jorourke/books/ftp.html
 *****************************************************************************/

/*
* The possible ways a pair of segments can interact.
* Returned by the function seg2d_intersection
*/
enum {
  SEG_NO_INTERSECTION,  /* Segments do not intersect */
  SEG_OVERLAP,          /* Segments overlap */
  SEG_CROSS,            /* Segments cross */
  SEG_TOUCH,            /* Segments touch in a vertex */
} SEG_INTER_TYPE;

/**
 * Finds the UNIQUE point of intersection p between two closed
 * collinear segments ab and cd. Returns p and a SEG_INTER_TYPE value.
 * Notice that if the segments overlap no point is returned since they
 * can be an infinite number of them.
 * This function is called after verifying that the points
 * are collinear and that their bounding boxes intersect.
 */
static int
parseg2d_intersection(const POINT2D a, const POINT2D b, const POINT2D c,
  const POINT2D d, POINT2D *p)
{
  /* Compute the intersection of the bounding boxes */
  double xmin = Max(Min(a.x, b.x), Min(c.x, d.x));
  double xmax = Min(Max(a.x, b.x), Max(c.x, d.x));
  double ymin = Max(Min(a.y, b.y), Min(c.y, d.y));
  double ymax = Min(Max(a.y, b.y), Max(c.y, d.y));
  /* If the intersection of the bounding boxes is not a point */
  if (xmin < xmax || ymin < ymax )
    return SEG_OVERLAP;
  /* We are sure that the segments touch each other */
  if ((b.x == c.x && b.y == c.y) ||
      (b.x == d.x && b.y == d.y))
  {
    p->x = b.x;
    p->y = b.y;
    return SEG_TOUCH;
  }
  if ((a.x == c.x && a.y == c.y) ||
      (a.x == d.x && a.y == d.y))
  {
    p->x = a.x;
    p->y = a.y;
    return SEG_TOUCH;
  }
  /* We should never arrive here since this function is called after verifying
   * that the bounding boxes of the segments intersect */
  return SEG_NO_INTERSECTION;
}

/**
 * Finds the UNIQUE point of intersection p between two closed segments
 * ab and cd. Returns p and a SEG_INTER_TYPE value. Notice that if the
 * segments overlap no point is returned since they can be an infinite
 * number of them
 */
static int
seg2d_intersection(const POINT2D a, const POINT2D b, const POINT2D c,
  const POINT2D d, POINT2D *p)
{
  double s, t;        /* The two parameters of the parametric equations */
  double num, denom;  /* Numerator and denominator of equations */
  int result;         /* Return value characterizing the intersection */

  denom = a.x * (d.y - c.y) + b.x * (c.y - d.y) +
          d.x * (b.y - a.y) + c.x * (a.y - b.y);

  /* If denom is zero, then segments are parallel: handle separately */
  if (fabs(denom) < EPSILON)
    return parseg2d_intersection(a, b, c, d, p);

  num = a.x * (d.y - c.y) + c.x * (a.y - d.y) + d.x * (c.y - a.y);
  s = num / denom;

  num = -(a.x * (c.y - b.y) + b.x * (a.y - c.y) + c.x * (b.y - a.y));
  t = num / denom;

  if ((0.0 == s || s == 1.0) && (0.0 == t || t == 1.0))
   result = SEG_TOUCH;
  else if (0.0 <= s && s <= 1.0 && 0.0 <= t && t <= 1.0)
   result = SEG_CROSS;
  else
   result = SEG_NO_INTERSECTION;

  p->x = a.x + s * (b.x - a.x);
  p->y = a.y + s * (b.y - a.y);

  return result;
}

/*****************************************************************************
 * Non self-intersecting (a.k.a. simple) functions
 *****************************************************************************/

/**
 * Split a temporal point of subtype instant set or sequence with stepwise
 * interpolation into an array of non self-intersecting pieces
 *
 * @param[in] temp Temporal point
 * @param[out] count Number of elements in the resulting array
 * @result Boolean array determining the instant numbers at which the
 * instant set must be split.
 * @pre The temporal value has at least 2 instants
 */
static bool *
tpoint_instarr_find_splits(const Temporal *temp, int *count)
{
  assert(temp->subtype == INSTANTSET || temp->subtype == SEQUENCE);
  if (temp->subtype == SEQUENCE)
    assert(! MOBDB_FLAGS_GET_LINEAR(temp->flags));
  int count1 = (temp->subtype == INSTANTSET) ?
    ((TInstantSet *) temp)->count : ((TSequence *) temp)->count;
  assert(count1 > 1);
  /* bitarr is an array of bool for collecting the splits */
  bool *bitarr = palloc0(sizeof(bool) * count1);
  int numsplits = 0;
  int start = 0, end = count1 - 1;
  while (start < count1 - 1)
  {
    const TInstant *inst1 = tinstarr_inst_n(temp, start);
    Datum value1 = tinstant_value(inst1);
    /* Find intersections in the piece defined by start and end in a
     * breadth-first search */
    int j = start, k = start + 1;
    while (k <= end)
    {
      const TInstant *inst2 = tinstarr_inst_n(temp, k);
      Datum value2 = tinstant_value(inst2);
      if (datum_point_eq(value1, value2))
      {
        /* Set the new end */
        end = k;
        bitarr[end] = true;
        numsplits++;
        break;
      }
      if (j < k - 1)
        j++;
      else
      {
        k++;
        j = start;
      }
    }
    /* Process the next split */
    start = end;
  }
  *count = numsplits;
  return bitarr;
}

/**
 * Split a temporal point sequence with linear interpolation into an array
 * of non self-intersecting pieces. The function works only on 2D even if
 * the input points are in 3D.
 *
 * @param[in] seq Temporal point
 * @param[out] count Number of elements in the resulting array
 * @result Boolean array determining the instant numbers at which the
 * sequence must be split.
 * @pre The input sequence has at least 3 instants
 */
static bool *
tpointseq_linear_find_splits(const TSequence *seq, int *count)
{
  assert(seq->count > 2);
 /* points is an array of points in the sequence */
  POINT2D *points = palloc0(sizeof(POINT2D) * seq->count);
  /* bitarr is an array of bool for collecting the splits */
  bool *bitarr = palloc0(sizeof(bool) * seq->count);
  points[0] = datum_get_point2d(tinstant_value(tsequence_inst_n(seq, 0)));
  int numsplits = 0;
  for (int i = 1; i < seq->count; i++)
  {
    points[i] = datum_get_point2d(tinstant_value(tsequence_inst_n(seq, i)));
    /* If stationary segment we need to split the sequence */
    if (points[i - 1].x == points[i].x && points[i - 1].y == points[i].y)
    {
      if (i > 1)
      {
        bitarr[i - 1] = true;
        numsplits++;
      }
      if (i < seq->count - 1)
      {
        bitarr[i] = true;
        numsplits++;
      }
    }
  }

  /* Loop for every split due to stationary segments while adding
   * additional splits due to intersecting segments */
  int start = 0;
  while (start < seq->count - 2)
  {
    int end = start + 1;
    while (end < seq->count - 1 && ! bitarr[end])
      end++;
    if (end == start + 1)
    {
      start = end;
      continue;
    }
    /* Find intersections in the piece defined by start and end in a
     * breadth-first search */
    int i = start, j = start + 1;
    while (j < end)
    {
      /* If the bounding boxes of the segments intersect */
      if (lw_seg_interact(points[i], points[i + 1],
          points[j], points[j + 1]))
      {
        /* Candidate for intersection */
        POINT2D p;
        int intertype = seg2d_intersection(points[i], points[i + 1],
          points[j], points[j + 1], &p);
        if (intertype > 0 &&
          /* Exclude the case when two consecutive segments that
           * necessarily touch each other in their common point */
          (intertype != SEG_TOUCH || j != i + 1 ||
           p.x != points[j].x || p.y != points[j].y))
        {
          /* Set the new end */
          end = j;
          bitarr[end] = true;
          numsplits++;
          break;
        }
      }
      if (i < j - 1)
        i++;
      else
      {
        j++;
        i = start;
      }
    }
    /* Process the next split */
    start = end;
  }
  pfree(points);
  *count = numsplits;
  return bitarr;
}

/*****************************************************************************
 * Functions for testing whether a temporal point is simple and for spliting
 * a temporal point into an array of temporal points that are simple.
 * A temporal point is simple if all its components are non self-intersecting.
 * - a temporal instant point is simple
 * - a temporal instant set point is simple if it is non self-intersecting
 * - a temporal sequence point is simple if it is non self-intersecting and
 *   do not have stationary segments
 * - a temporal sequence set point is simple if every composing sequence is
 *   simple even if two composing sequences intersect
 *****************************************************************************/

/**
 * Determine whether a temporal point is self-intersecting
 *
 * @param[in] temp Temporal point
 * @param[in] count Number of instants of the temporal point
 * @pre The temporal point is of subtype instant set or sequence with stepwise
 * interpolation
 */
static bool
tpoint_instarr_is_simple(const Temporal *temp, int count)
{
  assert(count > 1);
  Datum *points = palloc(sizeof(Datum) * count);
  for (int i = 0; i < count; i++)
  {
    const TInstant *inst = tinstarr_inst_n(temp, i);
    points[i] = tinstant_value(inst);
  }
  datumarr_sort(points, count, temp->basetypid);
  bool found = false;
  for (int i = 1; i < count; i++)
  {
    if (datum_point_eq(points[i - 1], points[i]))
    {
      found = true;
      break;
    }
  }
  pfree(points);
  return ! found;
}

/**
 * Determine whether a temporal point is self-intersecting
 *
 * @param[in] ti Temporal point
 */
static bool
tpointinstset_is_simple(const TInstantSet *ti)
{
  if (ti->count == 1)
    return true;

  return tpoint_instarr_is_simple((const Temporal *) ti, ti->count);
}

/**
 * Determine whether a temporal point is self-intersecting
 *
 * @param[in] seq Temporal point
 */
static bool
tpointseq_is_simple(const TSequence *seq)
{
  if (seq->count <= 2)
    return true;

  if (! MOBDB_FLAGS_GET_LINEAR(seq->flags))
    return tpoint_instarr_is_simple((const Temporal *) seq, seq->count);

  int numsplits;
  bool *splits = tpointseq_linear_find_splits(seq, &numsplits);
  pfree(splits);
  return numsplits == 0;
}

/**
 * Determine whether a temporal point is self-intersecting
 *
 * @param[in] ts Temporal point
 */
static bool
tpointseqset_is_simple(const TSequenceSet *ts)
{
  bool result = true;
  for (int i = 0; i < ts->count; i++)
  {
    const TSequence *seq = tsequenceset_seq_n(ts, i);
    result &= tpointseq_is_simple(seq);
    if (! result)
      break;
  }
  return result;
}

PG_FUNCTION_INFO_V1(tpoint_is_simple);
/**
 * Determine whether a temporal point is self-intersecting
 */
PGDLLEXPORT Datum
tpoint_is_simple(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  bool result;
  if (temp->subtype == INSTANT)
    result = true;
  else if (temp->subtype == INSTANTSET)
    result = tpointinstset_is_simple((TInstantSet *) temp);
  else if (temp->subtype == SEQUENCE)
    result = tpointseq_is_simple((TSequence *) temp);
  else /* temp->subtype == SEQUENCESET */
    result = tpointseqset_is_simple((TSequenceSet *) temp);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_BOOL(result);
}

/*****************************************************************************/

/**
 * Split a temporal point into an array of non self-intersecting pieces
 *
 * @param[in] ti Temporal point
 * @param[in] splits Bool array stating the splits
 * @param[in] count Number of elements in the resulting array
 * @pre The instant set has at least two instants
 */
static TInstantSet **
tpointinstset_split(const TInstantSet *ti, bool *splits, int count)
{
  assert(ti->count > 1);
  const TInstant **instants = palloc(sizeof(TInstant *) * ti->count);
  TInstantSet **result = palloc(sizeof(TInstantSet *) * count);
  /* Create the splits */
  int start = 0, k = 0;
  while (start < ti->count)
  {
    int end = start + 1;
    while (end < ti->count && ! splits[end])
      end++;
    /* Construct piece from start to end */
    for (int j = 0; j < end - start; j++)
      instants[j] = tinstantset_inst_n(ti, j + start);
    result[k++] = tinstantset_make(instants, end - start, MERGE_NO);
    /* Continue with the next split */
    start = end;
  }
  pfree(instants);
  return result;
}

/**
 * Split a temporal point into an array of non self-intersecting pieces
 *
 * @param[in] ti Temporal point
 * @param[in] count Number of elements in the resulting array
 */
TInstantSet **
tpointinstset_make_simple1(const TInstantSet *ti, int *count)
{
  TInstantSet **result;
  /* Special case when the input instant set has 1 instant */
  if (ti->count == 1)
  {
    result = palloc(sizeof(TInstantSet *));
    result[0] = tinstantset_copy(ti);
    *count = 1;
    return result;
  }

  int numsplits;
  bool *splits = tpoint_instarr_find_splits((const Temporal *) ti,
    &numsplits);
  if (numsplits == 0)
  {
    result = palloc(sizeof(TInstantSet *));
    result[0] = tinstantset_copy(ti);
    pfree(splits);
    *count = 1;
    return result;
  }

  result = tpointinstset_split(ti, splits, numsplits + 1);
  pfree(splits);
  *count = numsplits + 1;
  return result;
}

/**
 * Split a temporal point into an array of non self-intersecting pieces
 *
 * @param[in] ti Temporal point
 */
static ArrayType *
tpointinstset_make_simple(const TInstantSet *ti)
{
  int count;
  TInstantSet **instsets = tpointinstset_make_simple1(ti, &count);
  ArrayType *result = temporalarr_to_array((const Temporal **) instsets, count);
  pfree_array((void **) instsets, count);
  return result;
}

/**
 * Split a temporal point into an array of non self-intersecting pieces
 *
 * @param[in] seq Temporal point
 * @param[in] splits Bool array stating the splits
 * @param[in] count Number of elements in the resulting array
 * @note This function is called for each sequence of a sequence set
 */
static TSequence **
tpointseq_split(const TSequence *seq, bool *splits, int count)
{
  assert(seq->count > 2);
  bool linear = MOBDB_FLAGS_GET_LINEAR(seq->flags);
  TInstant **instants = palloc(sizeof(TInstant *) * seq->count);
  TSequence **result = palloc(sizeof(TSequence *) * count);
  /* Create the splits */
  int start = 0, k = 0;
  while (start < seq->count - 1)
  {
    int end = start + 1;
    while (end < seq->count - 1 && ! splits[end])
      end++;
    /* Construct piece from start to end inclusive */
    for (int j = 0; j <= end - start; j++)
      instants[j] = (TInstant *) tsequence_inst_n(seq, j + start);
    bool lower_inc1 = (start == 0) ? seq->period.lower_inc : true;
    bool upper_inc1 = (end == seq->count - 1) ?
      seq->period.upper_inc && ! splits[seq->count - 1] : false;
    /* The last two values of sequences with step interpolation and
     * exclusive upper bound must be equal */
    bool tofree = false;
    if (! linear &&
      ! datum_point_eq(tinstant_value(instants[end - start - 1]),
      tinstant_value(instants[end - start])))
    {
      Datum value = tinstant_value(instants[end - start - 1]);
      TimestampTz t = tsequence_inst_n(seq, end - start)->t;
      instants[end - start] = tinstant_make(value, t, seq->basetypid);
      tofree = true;
      upper_inc1 = false;
    }
    result[k++] = tsequence_make((const TInstant **) instants, end - start + 1,
      lower_inc1, upper_inc1, linear, NORMALIZE_NO);
    if (tofree)
    {
      /* Free the last instant created for the step interpolation */
      pfree(instants[end - start]);
      tofree = false;
    }
    /* Continue with the next split */
    start = end;
  }
  if (seq->period.upper_inc && splits[seq->count - 1])
  {
    /* Construct piece containing last instant of sequence */
    instants[0] = (TInstant *) tsequence_inst_n(seq, seq->count - 1);
    result[k++] = tsequence_make((const TInstant **) instants, seq->count - start,
      true, seq->period.upper_inc, linear, NORMALIZE_NO);
  }
  pfree(instants);
  return result;
}

/**
 * Split a temporal point into an array of non self-intersecting pieces
 *
 * @param[in] seq Temporal point
 * @param[out] count Number of elements in the resulting array
 * @note This function is called for each sequence of a sequence set
 */
TSequence **
tpointseq_make_simple1(const TSequence *seq, int *count)
{
  bool linear = MOBDB_FLAGS_GET_LINEAR(seq->flags);
  TSequence **result;
  /* Special cases when the input sequence has 1 or 2 instants */
  if (seq->count <= 2)
  {
    result = palloc(sizeof(TSequence *));
    result[0] = tsequence_copy(seq);
    *count = 1;
    return result;
  }

  int numsplits;
  bool *splits = linear ?
    tpointseq_linear_find_splits(seq, &numsplits) :
    tpoint_instarr_find_splits((const Temporal *) seq, &numsplits);
  if (numsplits == 0)
  {
    result = palloc(sizeof(TSequence *));
    result[0] = tsequence_copy(seq);
    pfree(splits);
    *count = 1;
    return result;
  }

  result = tpointseq_split(seq, splits, numsplits + 1);
  pfree(splits);
  *count = numsplits + 1;
  return result;
}

/**
 * Split a temporal point into an array of non self-intersecting pieces
 *
 * @param[in] seq Temporal point
 */
static ArrayType *
tpointseq_make_simple(const TSequence *seq)
{
  int count;
  TSequence **sequences = tpointseq_make_simple1(seq, &count);
  ArrayType *result = temporalarr_to_array((const Temporal **) sequences, count);
  pfree_array((void **) sequences, count);
  return result;
}

/**
 * Split a temporal point into an array of non self-intersecting pieces
 *
 * @param[in] ts Temporal point
 */
static ArrayType *
tpointseqset_make_simple(const TSequenceSet *ts)
{
  /* Singleton sequence set */
  if (ts->count == 1)
    return tpointseq_make_simple(tsequenceset_seq_n(ts, 0));

  /* General case */
  TSequence ***sequences = palloc0(sizeof(TSequence **) * ts->count);
  int *countseqs = palloc0(sizeof(int) * ts->count);
  int totalcount = 0;
  for (int i = 0; i < ts->count; i++)
  {
    const TSequence *seq = tsequenceset_seq_n(ts, i);
    sequences[i] = tpointseq_make_simple1(seq, &countseqs[i]);
    totalcount += countseqs[i];
  }
  assert(totalcount > 0);
  TSequence **allseqs = tsequencearr2_to_tsequencearr(sequences, countseqs,
    ts->count, totalcount);
  ArrayType *result = temporalarr_to_array((const Temporal **) allseqs,
    totalcount);
  pfree_array((void **) allseqs, totalcount);
  return result;
}

PG_FUNCTION_INFO_V1(tpoint_make_simple);
/**
 * Split a temporal point into an array of non self-intersecting pieces
 */
PGDLLEXPORT Datum
tpoint_make_simple(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  ArrayType *result;
  if (temp->subtype == INSTANT)
    result = temporalarr_to_array((const Temporal **) &temp, 1);
  else if (temp->subtype == INSTANTSET)
    result = tpointinstset_make_simple((TInstantSet *) temp);
  else if (temp->subtype == SEQUENCE)
    result = tpointseq_make_simple((TSequence *) temp);
  else /* temp->subtype == SEQUENCESET */
    result = tpointseqset_make_simple((TSequenceSet *) temp);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_POINTER(result);
}

/*****************************************************************************
 * Restriction functions
 * N.B. In the PostGIS version currently used by MobilityDB (2.5) there is no
 * true ST_Intersection function for geography
 *****************************************************************************/

/**
 * Restricts the temporal instant point to the (complement of the) geometry
 */
static TInstant *
tpointinst_restrict_geometry(const TInstant *inst, Datum geom, bool atfunc)
{
  bool inter = DatumGetBool(call_function2(intersects, tinstant_value(inst), geom));
  if ((atfunc && !inter) || (!atfunc && inter))
    return NULL;
  return tinstant_copy(inst);
}

/**
 * Restricts the temporal instant set point to the (complement of the) geometry
 */
static TInstantSet *
tpointinstset_restrict_geometry(const TInstantSet *ti, Datum geom, bool atfunc)
{
  const TInstant **instants = palloc(sizeof(TInstant *) * ti->count);
  int k = 0;
  for (int i = 0; i < ti->count; i++)
  {
    const TInstant *inst = tinstantset_inst_n(ti, i);
    bool inter = DatumGetBool(call_function2(intersects, tinstant_value(inst), geom));
    if ((atfunc && inter) || (!atfunc && !inter))
      instants[k++] = inst;
  }
  TInstantSet *result = NULL;
  if (k != 0)
    result = tinstantset_make(instants, k, MERGE_NO);
  pfree(instants);
  return result;
}

/**
 * Get the intersection points
 */
static int
gsinter_get_points(Datum *result, GSERIALIZED *gsinter)
{
  int type = gserialized_get_type(gsinter);
  /* The gserialized is of point or multipoint type */
  assert(type == POINTTYPE || type == MULTIPOINTTYPE);
  int k = 0;
  if (type == POINTTYPE)
  {
    result[k++] = PointerGetDatum(gserialized_copy(gsinter));
  }
  else
  /* It is a collection of type MULTIPOINTTYPE */
  {
    LWGEOM *lwinter = lwgeom_from_gserialized(gsinter);
    LWCOLLECTION *coll = lwgeom_as_lwcollection(lwinter);
    int countinter = coll->ngeoms;
    for (int i = 0; i < countinter; i++)
    {
      /* Find the i-th intersection */
      result[k++] = PointerGetDatum(geo_serialize(coll->geoms[i]));
    }
    lwgeom_free(lwinter);
  }
  return k;
}

/**
 * Restricts the temporal sequence point with step interpolation to the geometry
 *
 * @param[in] seq Temporal point
 * @param[in] geom Geometry
 * @param[out] count Number of elements in the resulting array
 */
static TSequence **
tpointseq_step_at_geometry(const TSequence *seq, Datum geom, int *count)
{
  /* Split the temporal point in an array of non self-intersecting
   * temporal points */
  int countsimple;
  TSequence **simpleseqs = tpointseq_make_simple1(seq, &countsimple);
  /* Allocate memory for the intersection points */
  Datum *points = palloc(sizeof(Datum) * countsimple * seq->count);
  int k = 0;
  /* Loop for every simple piece of the sequence */
  for (int i = 0; i < countsimple; i++)
  {
    Datum traj = tpointseq_trajectory(simpleseqs[i]);
    Datum inter = call_function2(intersection, traj, geom);
    GSERIALIZED *gsinter = (GSERIALIZED *) PG_DETOAST_DATUM(inter);
    if (! gserialized_is_empty(gsinter))
      k += gsinter_get_points(&points[k], gsinter);
    pfree(DatumGetPointer(inter));
    POSTGIS_FREE_IF_COPY_P(gsinter, DatumGetPointer(gsinter));
  }
  pfree_array((void **) simpleseqs, countsimple);
  if (k == 0)
  {
    *count = 0;
    return NULL;
  }
  TSequence **result = palloc(sizeof(TSequence *) * seq->count);
  int newcount = tsequence_at_values1(result, seq, points, k);
  pfree_datumarr(points, k);
  *count = newcount;
  return result;
}

/*****************************************************************************/

/**
 * Returns the timestamp at which the segment of the temporal value takes
 * the base value
 *
 * This function must take into account the roundoff errors and thus it uses
 * the datum_point_eq_eps for comparing two values so the coordinates of the
 * values may differ by EPSILON. Furthermore, we must drop the Z values since
 * this function may be called for finding the intersection of a sequence and
 * a geometry and the Z values crrently given by PostGIS/GEOS are not necessarily
 * extact, they "are copied, averaged or interpolated" as stated in
 * https://postgis.net/docs/ST_Intersection.html
 *
 * @param[in] inst1,inst2 Temporal values
 * @param[in] value Base value
 * @param[out] t Timestamp
 * @result Returns true if the base value is found in the temporal value
 * @pre The segment is not constant and has linear interpolation
 * @note The resulting timestamp may be at an exclusive bound.
 */
static bool
tpointseq_timestamp_at_value1(const TInstant *inst1, const TInstant *inst2,
  Datum value, TimestampTz *t)
{
  bool hasz = MOBDB_FLAGS_GET_Z(inst1->flags);
  Datum value1, value2, val;
  GSERIALIZED *gs1, *gs2, *gs;
  if (hasz)
  {
    /* We need to do the comparison in 2D since the Z values returned by PostGIS
     * may not be correct */
    gs1 = (GSERIALIZED *) DatumGetPointer(tinstant_value_copy(inst1));
    gs2 = (GSERIALIZED *) DatumGetPointer(tinstant_value_copy(inst2));
    gs = gserialized_copy((GSERIALIZED *) DatumGetPointer(value));
    FLAGS_SET_Z(gs1->flags, false);
    FLAGS_SET_Z(gs2->flags, false);
    FLAGS_SET_Z(gs->flags, false);
    value1 = PointerGetDatum(gs1);
    value2 = PointerGetDatum(gs2);
    val = PointerGetDatum(gs);
  }
  else
  {
    value1 = tinstant_value(inst1);
    value2 = tinstant_value(inst2);
    val = value;
  }
  /* Is the lower bound the answer? */
  bool result = true;
  if (datum_point_eq(value1, val))
    *t = inst1->t;
  /* Is the upper bound the answer? */
  else if (datum_point_eq(value2, value))
    *t = inst2->t;
  else
  {
    double dist;
    double fraction = geoseg_locate_point(value1, value2, val, &dist);
    if (fabs(dist) >= EPSILON)
      result = false;
    else
    {
      double duration = (inst2->t - inst1->t);
      *t = inst1->t + (TimestampTz) (duration * fraction);
    }
  }
  if (hasz)
  {
    pfree(gs1); pfree(gs2); pfree(gs);
  }
  return result;
}

/**
 * Returns the timestamp at which the temporal value takes the base value.
 *
 * This function is called by the atGeometry function to find the timestamp
 * at which an intersection point found by PostGIS is located. This function
 * differs from function tpointseq_intersection_value in particular since the
 * latter is used for finding crossings during synchronization and thus it is
 * required that the timestamp in strictly between the timestamps of a segment.
 *
 * @param[in] seq Temporal value
 * @param[in] value Base value
 * @param[out] t Timestamp
 * @result Returns true if the base value is found in the temporal value
 * @pre The base value is known to belong to the temporal sequence (taking
 * into account roundoff errors), the temporal sequence has linear
 * interpolation, and it simple
 * @note The resulting timestamp may be at an exclusive bound.
 */
bool
tpointseq_timestamp_at_value(const TSequence *seq, Datum value,
  TimestampTz *t)
{
  assert(seq->count >= 1);
  const TInstant *inst1 = tsequence_inst_n(seq, 0);
  for (int i = 1; i < seq->count; i++)
  {
    const TInstant *inst2 = tsequence_inst_n(seq, i);
    /* We are sure that the segment is not constant since the
     * sequence is simple */
    if (tpointseq_timestamp_at_value1(inst1, inst2, value, t))
      return true;
    inst1 = inst2;
  }
  /* We should never arrive here */
  elog(ERROR, "The value has not been found due to roundoff errors");
  return false;
}

/**
 * Get the periods at which the temporal sequence point with linear interpolation
 * intersects the geometry
 *
 * @param[in] seq Temporal point
 * @param[in] gsinter Intersection of the temporal point and the geometry
 * @param[out] count Number of elements in the resulting array
 * @pre The temporal sequence is simple, that is, non self-intersecting and
 * the intersection is non empty
 */
Period **
tpointseq_interperiods(const TSequence *seq, GSERIALIZED *gsinter,
  int *count)
{
  /* The temporal sequence has at least 2 instants since
   * (1) the test for instantaneous full sequence is done in the calling function.
   * (2) the simple components of a non self-intersecting sequence have at least
   *     two instants */
  assert(seq->count > 1);
  const TInstant *start = tsequence_inst_n(seq, 0);
  const TInstant *end = tsequence_inst_n(seq, seq->count - 1);
  Period **result;

  /* If the sequence is stationary the whole sequence intersects with the
   * geometry since gsinter is not empty */
  if (seq->count == 2 &&
    datum_point_eq(tinstant_value(start), tinstant_value(end)))
  {
    result = palloc(sizeof(Period *));
    result[0] = period_copy(&seq->period);
    *count = 1;
    return result;
  }

  /* General case */
  LWGEOM *lwgeom_inter = lwgeom_from_gserialized(gsinter);
  int type = lwgeom_inter->type;
  int countinter;
  LWPOINT *lwpoint_inter;
  LWLINE *lwline_inter;
  LWCOLLECTION *coll;
  if (type == POINTTYPE)
  {
    countinter = 1;
    lwpoint_inter = lwgeom_as_lwpoint(lwgeom_inter);
  }
  else if (type == LINETYPE)
  {
    countinter = 1;
    lwline_inter = lwgeom_as_lwline(lwgeom_inter);
  }
  else
  /* It is a collection of type MULTIPOINTTYPE, MULTILINETYPE, or
   * COLLECTIONTYPE */
  {
    coll = lwgeom_as_lwcollection(lwgeom_inter);
    countinter = coll->ngeoms;
  }
  Period **periods = palloc(sizeof(Period *) * countinter);
  int k = 0;
  for (int i = 0; i < countinter; i++)
  {
    if (countinter > 1)
    {
      /* Find the i-th intersection */
      LWGEOM *subgeom = coll->geoms[i];
      if (subgeom->type == POINTTYPE)
        lwpoint_inter = lwgeom_as_lwpoint(subgeom);
      else /* type == LINETYPE */
        lwline_inter = lwgeom_as_lwline(subgeom);
      type = subgeom->type;
    }
    TimestampTz t1, t2;
    GSERIALIZED *gspoint;
    /* Each intersection is either a point or a linestring */
    if (type == POINTTYPE)
    {
      gspoint = geo_serialize((LWGEOM *) lwpoint_inter);
      tpointseq_timestamp_at_value(seq, PointerGetDatum(gspoint), &t1);
      pfree(gspoint);
      /* If the intersection is not at an exclusive bound */
      if ((seq->period.lower_inc || t1 > start->t) &&
        (seq->period.upper_inc || t1 < end->t))
      {
        periods[k++] = period_make(t1, t1, true, true);
      }
    }
    else
    {
      /* Get the fraction of the start point of the intersecting line */
      LWPOINT *lwpoint = lwline_get_lwpoint(lwline_inter, 0);
      gspoint = geo_serialize((LWGEOM *) lwpoint);
      tpointseq_timestamp_at_value(seq, PointerGetDatum(gspoint), &t1);
      pfree(gspoint);
      /* Get the fraction of the end point of the intersecting line */
      lwpoint = lwline_get_lwpoint(lwline_inter, lwline_inter->points->npoints - 1);
      gspoint = geo_serialize((LWGEOM *) lwpoint);
      tpointseq_timestamp_at_value(seq, PointerGetDatum(gspoint), &t2);
      pfree(gspoint);
      /* If t1 == t2 and the intersection is not at an exclusive bound */
      if (t1 == t2 && (seq->period.lower_inc || t1 > start->t) &&
        (seq->period.upper_inc || t1 < end->t))
      {
        periods[k++] = period_make(t1, t1, true, true);
      }
      else
      {
        TimestampTz lower1 = Min(t1, t2);
        TimestampTz upper1 = Max(t1, t2);
        bool lower_inc1 = (lower1 == start->t) ? seq->period.lower_inc : true;
        bool upper_inc1 = (upper1 == end->t) ? seq->period.upper_inc : true;
        periods[k++] = period_make(lower1, upper1, lower_inc1, upper_inc1);
      }
    }
  }
  lwgeom_free(lwgeom_inter);

  if (k == 0)
  {
    *count = k;
    pfree(periods);
    return NULL;
  }
  if (k == 1)
  {
    *count = k;
    return periods;
  }

  /* It is necessary to normalize the periods due to roundoff errors */
  int newcount;
  result = periodarr_normalize(periods, k, &newcount);
  *count = newcount;
  return result;
}

/**
 * Restricts the temporal sequence point with linear interpolation to the geometry
 *
 * @param[in] seq Temporal point
 * @param[in] geom Geometry
 * @param[out] count Number of elements in the resulting array
 */
static TSequence **
tpointseq_linear_at_geometry(const TSequence *seq, Datum geom, int *count)
{
  /* Split the temporal point in an array of non self-intersecting
   * temporal points */
  int countsimple;
  TSequence **simpleseqs = tpointseq_make_simple1(seq, &countsimple);
  /* Allocate memory for the result */
  Period ***periods = palloc(sizeof(Period *) * countsimple);
  int *countpers = palloc0(sizeof(int) * countsimple);
  int totalcount = 0;
  /* Loop for every simple piece of the sequence */
  for (int i = 0; i < countsimple; i++)
  {
    Datum traj = tpointseq_trajectory(simpleseqs[i]);
    Datum inter = call_function2(intersection, traj, geom);
    GSERIALIZED *gsinter = (GSERIALIZED *) PG_DETOAST_DATUM(inter);
    if (! gserialized_is_empty(gsinter))
    {
      periods[i] = tpointseq_interperiods(simpleseqs[i], gsinter,
        &countpers[i]);
      totalcount += countpers[i];
    }
    pfree(DatumGetPointer(inter));
    POSTGIS_FREE_IF_COPY_P(gsinter, DatumGetPointer(gsinter));
  }
  pfree_array((void **) simpleseqs, countsimple);
  if (totalcount == 0)
  {
    *count = 0;
    return NULL;
  }
  /* Assemble the periods into a single array */
  Period **allperiods = palloc(sizeof(Period *) * totalcount);
  int k = 0;
  for (int i = 0; i < countsimple; i++)
  {
    for (int j = 0; j < countpers[i]; j++)
      allperiods[k++] = periods[i][j];
    if (countpers[i] != 0)
      pfree(periods[i]);
  }
  /* It is necessary to sort the periods */
  periodarr_sort(allperiods, totalcount);
  PeriodSet *ps = periodset_make_free(allperiods, totalcount, NORMALIZE);
  TSequence **result = palloc(sizeof(TSequence *) * totalcount);
  *count = tsequence_at_periodset(result, seq, ps);
  pfree(ps);
  return result;
}

/**
 * Restricts the temporal sequence point to the geometry.
 *
 * The function splits the temporal point in an array of temporal point
 * sequences that are simple (that is, not self-intersecting) and loops
 * for each piece.
 * @param[in] seq Temporal point
 * @param[in] geom Geometry
 * @param[out] count Number of elements in the resulting array
 * @pre The temporal sequence is simple, that is, not self-intersecting
 */
TSequence **
tpointseq_at_geometry(const TSequence *seq, Datum geom, int *count)
{
  /* Instantaneous sequence */
  if (seq->count == 1)
  {
    TInstant *inst = tpointinst_restrict_geometry(tsequence_inst_n(seq, 0),
      geom, REST_AT);
    if (inst == NULL)
    {
      *count = 0;
      return NULL;
    }
    TSequence **result = palloc(sizeof(TSequence *));
    result[0] = tsequence_copy(seq);
    *count = 1;
    return result;
  }
  if (MOBDB_FLAGS_GET_LINEAR(seq->flags))
    return tpointseq_linear_at_geometry(seq, geom, count);
  else
    return tpointseq_step_at_geometry(seq, geom, count);
}

/**
 * Restrict the temporal sequence point to the complement of the geometry
 *
 * It is not possible to use a similar approach as for tpointseq_at_geometry1
 * where instead of computing the intersections we compute the difference since
 * in PostGIS the following query
 * @code
 * select st_astext(st_difference(geometry 'Linestring(0 0,3 3)',
 *     geometry 'MultiPoint((1 1),(2 2),(3 3))'))
 * @endcode
 * returns `LINESTRING(0 0,3 3)`. Therefore we compute tpointseq_at_geometry1
 * and then compute the complement of the value obtained.
 *
 * @param[in] seq Temporal point
 * @param[in] geom Geometry
 * @param[out] count Number of elements in the resulting array
 */
static TSequence **
tpointseq_minus_geometry1(const TSequence *seq, Datum geom, int *count)
{
  int countinter;
  TSequence **sequences = tpointseq_at_geometry(seq, geom, &countinter);
  if (countinter == 0)
  {
    TSequence **result = palloc(sizeof(TSequence *));
    result[0] = tsequence_copy(seq);
    *count = 1;
    return result;
  }

  const Period **periods = palloc(sizeof(Period) * countinter);
  for (int i = 0; i < countinter; i++)
    periods[i] = &sequences[i]->period;
  PeriodSet *ps1 = periodset_make(periods, countinter, NORMALIZE_NO);
  PeriodSet *ps2 = minus_period_periodset_internal(&seq->period, ps1);
  pfree(ps1); pfree(periods);
  if (ps2 == NULL)
  {
    *count = 0;
    return NULL;
  }
  TSequence **result = palloc(sizeof(TSequence *) * ps2->count);
  *count = tsequence_at_periodset(result, seq, ps2);
  pfree(ps2);
  return result;
}

/**
 * Restricts the temporal sequence point to the (complement of the) geometry
 *
 @note The test for instantaneous sequences is done at the function
 tpointseq_at_geometry since the latter function is called for each sequence
 of a sequence set.
 */
static TSequenceSet *
tpointseq_restrict_geometry(const TSequence *seq, Datum geom, bool atfunc)
{
  int count;
  TSequence **sequences = atfunc ? tpointseq_at_geometry(seq, geom, &count) :
    tpointseq_minus_geometry1(seq, geom, &count);
  if (sequences == NULL)
    return NULL;

  /* It is necessary to sort the sequences */
  tsequencearr_sort((TSequence **) sequences, count);
  return tsequenceset_make_free(sequences, count, NORMALIZE);
}

/**
 * Restricts the temporal sequence set point to the (complement of the) geometry
 *
 * @param[in] ts Temporal point
 * @param[in] geom Geometry
 * @param[in] box Bounding box of the geometry
 * @param[in] atfunc True when the restriction is at, false for minus
 */
static TSequenceSet *
tpointseqset_restrict_geometry(const TSequenceSet *ts, Datum geom,
  const STBOX *box, bool atfunc)
{
  /* Singleton sequence set */
  if (ts->count == 1)
    return tpointseq_restrict_geometry(tsequenceset_seq_n(ts, 0), geom, atfunc);

  /* palloc0 used due to the bounding box test in the for loop below */
  TSequence ***sequences = palloc0(sizeof(TSequence *) * ts->count);
  int *countseqs = palloc0(sizeof(int) * ts->count);
  int totalcount = 0;
  for (int i = 0; i < ts->count; i++)
  {
    const TSequence *seq = tsequenceset_seq_n(ts, i);
    /* Bounding box test */
    STBOX *box1 = tsequence_bbox_ptr(seq);
    bool overlaps = overlaps_stbox_stbox_internal(box1, box);
    if (atfunc)
    {
      if (overlaps)
      {
        sequences[i] = tpointseq_at_geometry(seq, geom, &countseqs[i]);
        totalcount += countseqs[i];
      }
    }
    else
    {
      if (!overlaps)
      {
        sequences[i] = palloc(sizeof(TSequence *));
        sequences[i][0] = tsequence_copy(seq);
        countseqs[i] = 1;
        totalcount ++;
      }
      else
      {
        sequences[i] = tpointseq_minus_geometry1(seq, geom, &countseqs[i]);
        totalcount += countseqs[i];
      }
    }
  }
  if (totalcount == 0)
  {
    pfree(sequences); pfree(countseqs);
    return NULL;
  }
  TSequence **allseqs = tsequencearr2_to_tsequencearr(sequences,
    countseqs, ts->count, totalcount);
  return tsequenceset_make_free(allseqs, totalcount, NORMALIZE);
}

/**
 * Restricts the temporal point to the (complement of the) geometry
 * (dispatch function)
 *
 * @pre The arguments are of the same dimensionality, have the same SRID,
 * and the geometry is not empty
 */
Temporal *
tpoint_restrict_geometry_internal(const Temporal *temp, Datum geom, bool atfunc)
{
  /* Bounding box test */
  STBOX box1, box2;
  memset(&box1, 0, sizeof(STBOX));
  memset(&box2, 0, sizeof(STBOX));
  temporal_bbox(&box1, temp);
  /* Non-empty geometries have a bounding box */
  assert(geo_to_stbox_internal(&box2, (GSERIALIZED *) DatumGetPointer(geom)));
  if (!overlaps_stbox_stbox_internal(&box1, &box2))
    return atfunc ? NULL : temporal_copy(temp);

  Temporal *result;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT)
    result = (Temporal *) tpointinst_restrict_geometry((TInstant *) temp,
      geom, atfunc);
  else if (temp->subtype == INSTANTSET)
    result = (Temporal *) tpointinstset_restrict_geometry((TInstantSet *) temp,
      geom, atfunc);
  else if (temp->subtype == SEQUENCE)
    result = (Temporal *) tpointseq_restrict_geometry((TSequence *) temp,
      geom, atfunc);
  else /* temp->subtype == SEQUENCESET */
    result = (Temporal *) tpointseqset_restrict_geometry((TSequenceSet *) temp,
      geom, &box2, atfunc);

  return result;
}

/**
 * Restricts the temporal point to the (complement of the) geometry
 *
 * Mixing 2D/3D is enabled to compute, for example, 2.5D operations
 */
static Datum
tpoint_restrict_geometry(FunctionCallInfo fcinfo, bool atfunc)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  GSERIALIZED *gs = PG_GETARG_GSERIALIZED_P(1);
  if (gserialized_is_empty(gs))
  {
    PG_FREE_IF_COPY(gs, 1);
    if (atfunc)
    {
      PG_FREE_IF_COPY(temp, 0);
      PG_RETURN_NULL();
    }
    else
    {
      Temporal *result = temporal_copy(temp);
      PG_FREE_IF_COPY(temp, 0);
      PG_RETURN_POINTER(result);
    }
  }
  ensure_same_srid_tpoint_gs(temp, gs);
  Temporal *result = tpoint_restrict_geometry_internal(temp,
    PointerGetDatum(gs), atfunc);
  PG_FREE_IF_COPY(temp, 0);
  PG_FREE_IF_COPY(gs, 1);
  if (result == NULL)
    PG_RETURN_NULL();
  PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(tpoint_at_geometry);
/**
 * Restricts the temporal point to the geometry
 */
PGDLLEXPORT Datum
tpoint_at_geometry(PG_FUNCTION_ARGS)
{
  return tpoint_restrict_geometry(fcinfo, REST_AT);
}

PG_FUNCTION_INFO_V1(tpoint_minus_geometry);
/**
 * Restrict the temporal point to the complement of the geometry
 */
PGDLLEXPORT Datum
tpoint_minus_geometry(PG_FUNCTION_ARGS)
{
  return tpoint_restrict_geometry(fcinfo, REST_MINUS);
}

/*****************************************************************************/

/**
 * Restrict the temporal point to the spatiotemporal box
 *
 * @pre The arguments are of the same dimensionality and
 * have the same SRID
 */
Temporal *
tpoint_at_stbox_internal(const Temporal *temp, const STBOX *box)
{
  /* At least one of MOBDB_FLAGS_GET_X and MOBDB_FLAGS_GET_T is true */
  bool hasx = MOBDB_FLAGS_GET_X(box->flags);
  bool hast = MOBDB_FLAGS_GET_T(box->flags);
  assert(hasx || hast);

  /* Bounding box test */
  STBOX box1;
  memset(&box1, 0, sizeof(STBOX));
  temporal_bbox(&box1, temp);
  if (!overlaps_stbox_stbox_internal(box, &box1))
    return NULL;

  Temporal *temp1;
  if (hast)
  {
    Period p;
    period_set(&p, box->tmin, box->tmax, true, true);
    temp1 = temporal_at_period_internal(temp, &p);
    /* Despite the bounding box test above, temp1 may be NULL due to
     * exclusive bounds */
    if (temp1 == NULL)
      return NULL;
  }
  else
    temp1 = (Temporal *) temp;

  Temporal *result;
  if (hasx)
  {
    Datum gbox = PointerGetDatum(stbox_to_gbox(box));
    Datum geom = MOBDB_FLAGS_GET_Z(box->flags) ?
      call_function1(BOX3D_to_LWGEOM, gbox) :
      call_function1(BOX2D_to_LWGEOM, gbox);
    Datum geom1 = call_function2(LWGEOM_set_srid, geom,
      Int32GetDatum(box->srid));
    result = tpoint_restrict_geometry_internal(temp1, geom1, REST_AT);
    pfree(DatumGetPointer(gbox)); pfree(DatumGetPointer(geom));
    pfree(DatumGetPointer(geom1));
    if (hast)
      pfree(temp1);
  }
  else
    result = temp1;
  return result;
}

/**
 * Restrict the temporal point to the complement of the spatiotemporal box.
 * (internal function).
 * We cannot make the difference from each dimension separately, i.e.,
 * restrict at the period and then restrict to the polygon. Therefore, we
 * compute the atStbox and then compute the complement of the value obtained.
 *
 * @pre The arguments are of the same dimensionality and have the same SRID
 */
static Temporal *
tpoint_minus_stbox_internal(const Temporal *temp, const STBOX *box)
{
  /* Bounding box test */
  STBOX box1;
  memset(&box1, 0, sizeof(STBOX));
  temporal_bbox(&box1, temp);
  if (!overlaps_stbox_stbox_internal(box, &box1))
    return temporal_copy(temp);

  Temporal *result = NULL;
  Temporal *temp1 = tpoint_at_stbox_internal(temp, box);
  if (temp1 != NULL)
  {
    PeriodSet *ps1 = temporal_get_time_internal(temp);
    PeriodSet *ps2 = temporal_get_time_internal(temp1);
    PeriodSet *ps = minus_periodset_periodset_internal(ps1, ps2);
    if (ps != NULL)
    {
      result = temporal_restrict_periodset_internal(temp, ps, true);
      pfree(ps);
    }
    pfree(temp1); pfree(ps1); pfree(ps2);
  }
  return result;
}

/**
 * Restrict the temporal point to (the complement of) the spatiotemporal box
 *
 * Mixing 2D/3D is enabled to compute, for example, 2.5D operations
 */
static Datum
tpoint_restrict_stbox(FunctionCallInfo fcinfo, bool atfunc)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  STBOX *box = PG_GETARG_STBOX_P(1);
  ensure_common_dimension(temp->flags, box->flags);
  if (MOBDB_FLAGS_GET_X(box->flags))
  {
    ensure_same_geodetic(temp->flags, box->flags);
    ensure_same_srid_tpoint_stbox(temp, box);
  }
  Temporal *result = atfunc ? tpoint_at_stbox_internal(temp, box) :
    tpoint_minus_stbox_internal(temp, box);
  PG_FREE_IF_COPY(temp, 0);
  if (result == NULL)
    PG_RETURN_NULL();
  PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(tpoint_at_stbox);
/**
 * Restricts the temporal value to the spatiotemporal box
 */
PGDLLEXPORT Datum
tpoint_at_stbox(PG_FUNCTION_ARGS)
{
  return tpoint_restrict_stbox(fcinfo, REST_AT);
}

PG_FUNCTION_INFO_V1(tpoint_minus_stbox);
/**
 * Restricts the temporal value to the complement of the spatiotemporal box
 */
PGDLLEXPORT Datum
tpoint_minus_stbox(PG_FUNCTION_ARGS)
{
  return tpoint_restrict_stbox(fcinfo, REST_MINUS);
}

/*****************************************************************************/
