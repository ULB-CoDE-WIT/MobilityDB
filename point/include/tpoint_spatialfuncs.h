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
 * @file tpoint_spatialfuncs.h
 * Spatial functions for temporal points.
 */

#ifndef __TPOINT_SPATIALFUNCS_H__
#define __TPOINT_SPATIALFUNCS_H__

#define ACCEPT_USE_OF_DEPRECATED_PROJ_API_H 1

#include <postgres.h>
#include <liblwgeom.h>

#include "temporal.h"
#include "tpoint.h"

/*****************************************************************************/

/* Fetch from and store in the cache the fcinfo of the external function */

extern FunctionCallInfo fetch_fcinfo();
extern void store_fcinfo(FunctionCallInfo fcinfo);

/* Functions derived from PostGIS to increase floating-point precision */

extern long double closest_point2d_on_segment_ratio(const POINT2D *p,
  const POINT2D *A, const POINT2D *B, POINT2D *closest);
extern long double closest_point3dz_on_segment_ratio(const POINT3DZ *p,
  const POINT3DZ *A, const POINT3DZ *B, POINT3DZ *closest);
extern long double closest_point_on_segment_sphere(const POINT4D *p,
  const POINT4D *A, const POINT4D *B, POINT4D *closest, double *dist);
extern void interpolate_point4d_sphere(const POINT3D *p1, const POINT3D *p2,
  const POINT4D *v1, const POINT4D *v2, double f, POINT4D *p);

/* Parameter tests */

extern void ensure_spatial_validity(const Temporal *temp1, const Temporal *temp2);
extern void ensure_same_geodetic(int16 flags1, int16 flags2);
extern void ensure_same_srid_stbox(const STBOX *box1, const STBOX *box2);
extern void ensure_same_srid_tpoint(const Temporal *temp1, const Temporal *temp2);
extern void ensure_same_srid_gs(const GSERIALIZED *gs1, const GSERIALIZED *gs2);
extern void ensure_same_srid_tpoint_stbox(const Temporal *temp, const STBOX *box);
extern void ensure_same_srid_tpoint_gs(const Temporal *temp, const GSERIALIZED *gs);
extern void ensure_same_srid_stbox_gs(const STBOX *box, const GSERIALIZED *gs);
extern void ensure_same_dimensionality(int16 flags1, int16 flags2);
extern void ensure_same_spatial_dimensionality(int16 flags1, int16 flags2);
extern void ensure_same_spatial_dimensionality_stbox_gs(const STBOX *box1, const GSERIALIZED *gs);
extern void ensure_same_dimensionality_tpoint_gs(const Temporal *temp, const GSERIALIZED *gs);
extern void ensure_has_Z(int16 flags);
extern void ensure_has_not_Z(int16 flags);
extern void ensure_has_Z_gs(const GSERIALIZED *gs);
extern void ensure_has_not_Z_gs(const GSERIALIZED *gs);
extern void ensure_has_M_gs(const GSERIALIZED *gs);
extern void ensure_has_not_M_gs(const GSERIALIZED *gs);
extern void ensure_point_type(const GSERIALIZED *gs);
extern void ensure_non_empty(const GSERIALIZED *gs);

/* Utility functions */

extern const POINT2D *gs_get_point2d_p(GSERIALIZED *gs);
extern const POINT3DZ *gs_get_point3dz_p(GSERIALIZED *gs);
extern POINT2D datum_get_point2d(Datum value);
extern const POINT2D *datum_get_point2d_p(Datum value);
extern POINT3DZ datum_get_point3dz(Datum value);
extern const POINT3DZ *datum_get_point3dz_p(Datum value);
extern void datum_get_point4d(POINT4D *p, Datum value);
extern Datum point_make(double x, double y, double z, bool hasz,
  bool geodetic, int32 srid);
extern bool datum_point_eq(Datum geopoint1, Datum geopoint2);
extern GSERIALIZED *geo_serialize(LWGEOM *geom);
extern Datum datum_transform(Datum value, Datum srid);

extern datum_func2 get_distance_fn(int16 flags);
extern datum_func2 get_pt_distance_fn(int16 flags);
extern Datum geom_distance2d(Datum geom1, Datum geom2);
extern Datum geom_distance3d(Datum geom1, Datum geom2);
extern Datum geog_distance(Datum geog1, Datum geog2);
extern Datum pt_distance2d(Datum geom1, Datum geom2);
extern Datum pt_distance3d(Datum geom1, Datum geom2);

extern Datum geoseg_interpolate_point(Datum value1, Datum value2,
  long double ratio);
extern long double geoseg_locate_point(Datum start, Datum end, Datum point,
  double *dist);

extern bool tpointseq_intersection_value(const TInstant *inst1,
  const TInstant *inst2, Datum value, TimestampTz *t);
extern bool tgeompointseq_intersection(const TInstant *start1, const TInstant *end1,
  const TInstant *start2, const TInstant *end2, TimestampTz *t);
extern bool tgeogpointseq_intersection(const TInstant *start1, const TInstant *end1,
  const TInstant *start2, const TInstant *end2, TimestampTz *t);

extern bool geopoint_collinear(Datum value1, Datum value2, Datum value3,
  double ratio, bool hasz, bool geodetic);

extern void spheroid_init(SPHEROID *s, double a, double b);
extern void geography_interpolate_point4d(const POINT3D *p1, const POINT3D *p2,
  const POINT4D *v1, const POINT4D *v2, double f, POINT4D *p);

/* Functions for spatial reference systems */

extern Datum tpoint_srid(PG_FUNCTION_ARGS);
extern Datum tpoint_set_srid(PG_FUNCTION_ARGS);

extern Temporal *tpoint_set_srid_internal(Temporal *temp, int32 srid) ;
extern int tpointinst_srid(const TInstant *inst);
extern int tpointinstset_srid(const TInstantSet *ti);
extern int tpointseq_srid(const TSequence *seq);
extern int tpointseqset_srid(const TSequenceSet *ts);
extern int tpoint_srid_internal(const Temporal *t);
extern TInstant *tpointinst_transform(const TInstant *inst, Datum srid);

/* Cast functions */

extern Datum tgeompoint_to_tgeogpoint(PG_FUNCTION_ARGS);
extern Datum tgeogpoint_to_tgeompoint(PG_FUNCTION_ARGS);

extern TInstant *tgeogpointinst_to_tgeompointinst(const TInstant *inst);
extern TSequence *tgeogpointseq_to_tgeompointseq(const TSequence *seq);
extern TSequenceSet *tgeogpoints_to_tgeompoints(const TSequenceSet *ts);

/* Trajectory functions */

extern Datum tpoint_trajectory(PG_FUNCTION_ARGS);

extern LWGEOM *lwpointarr_make_trajectory(LWGEOM **lwpoints, int count, bool linear);
extern Datum tpointinstset_trajectory(const TInstantSet *ti);
extern Datum tpoint_trajectory_internal(const Temporal *temp);
extern void tpoint_trajectory_free(const Temporal *temp, Datum traj);
extern Datum tpoint_trajectory_external(const Temporal *temp);
extern Datum tpointseq_make_trajectory(const TInstant **instants, int count, bool linear);

extern Datum geopoint_line(Datum value1, Datum value2);
extern LWLINE *geopoint_lwline(Datum value1, Datum value2);

extern Datum tpointseq_trajectory(const TSequence *seq);
extern Datum tpointseq_trajectory_copy(const TSequence *seq);
extern Datum tpointseqset_trajectory(const TSequenceSet *ts);

/* Length, speed, time-weighted centroid, and temporal azimuth functions */

extern Datum tpoint_length(PG_FUNCTION_ARGS);
extern Datum tpoint_cumulative_length(PG_FUNCTION_ARGS);
extern Datum tpoint_speed(PG_FUNCTION_ARGS);
extern Datum tgeompoint_twcentroid(PG_FUNCTION_ARGS);
extern Datum tpoint_azimuth(PG_FUNCTION_ARGS);

extern Datum tpointinstset_twcentroid(const TInstantSet *ti);
extern Datum tpointseq_twcentroid(const TSequence *seq);
extern Datum tpointseqset_twcentroid(const TSequenceSet *ts);
extern Datum tpoint_twcentroid_internal(Temporal *temp);

/* Restriction functions */

extern Datum tpoint_at_geometry(PG_FUNCTION_ARGS);
extern Datum tpoint_at_stbox(PG_FUNCTION_ARGS);

extern Datum tpoint_minus_geometry(PG_FUNCTION_ARGS);
extern Datum tpoint_minus_stbox(PG_FUNCTION_ARGS);

extern TSequence **tpointseq_at_geometry(const TSequence *seq, Datum geo,
  int *count);
extern Temporal *tpoint_restrict_geometry_internal(const Temporal *temp,
  Datum geom, bool atfunc);
extern Temporal *tpoint_at_stbox_internal(const Temporal *temp, const STBOX *box);

extern TInstantSet **tpointinstset_make_simple1(const TInstantSet *ti, int *count);
extern TSequence **tpointseq_make_simple1(const TSequence *seq, int *count);
extern Period **tpointseq_interperiods(const TSequence *seq,
  GSERIALIZED *gsinter, int *count);

/*****************************************************************************/

#endif
