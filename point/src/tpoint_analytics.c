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
 * @file tpoint_analytics.c
 * Analytic functions for temporal points and temporal floats.
 */

#include "tpoint_analytics.h"

#include <assert.h>
#include <float.h>
#include <funcapi.h>
#include <math.h>
#if MOBDB_PGSQL_VERSION < 120000
#include <access/htup_details.h>
#endif
#include <utils/builtins.h>
#include <utils/timestamp.h>
#if MOBDB_PGSQL_VERSION >= 120000
#include <utils/float.h>
#endif

#include "period.h"
#include "periodset.h"
#include "timeops.h"
#include "temporaltypes.h"
#include "tempcache.h"
#include "temporal_util.h"
#include "lifting.h"
#include "tnumber_mathfuncs.h"

#include "postgis.h"
#include "geography_funcs.h"
#include "tpoint.h"
#include "tpoint_boxops.h"
#include "tpoint_spatialrels.h"
#include "tpoint_spatialfuncs.h"

/*****************************************************************************
 * Convert a temporal point into a PostGIS trajectory geometry/geography
 * The M coordinates encode the timestamps in number of seconds since '1970-01-01'
 *****************************************************************************/

/**
 * Converts the point and the timestamp into a PostGIS geometry/geography
 * point where the M coordinate encodes the timestamp in number of seconds
 * since '1970-01-01'
 */
static LWPOINT *
point_to_trajpoint(Datum point, TimestampTz t)
{
  GSERIALIZED *gs = (GSERIALIZED *) DatumGetPointer(point);
  int32 srid = gserialized_get_srid(gs);
  /* The internal representation of timestamps in PostgreSQL is in
   * microseconds since '2000-01-01'. Therefore we need to compute
   * select date_part('epoch', timestamp '2000-01-01' - timestamp '1970-01-01')
   * which results in 946684800 */
  double epoch = ((double) t / 1e6) + 946684800;
  LWPOINT *result;
  if (FLAGS_GET_Z(gs->flags))
  {
    const POINT3DZ *point = gs_get_point3dz_p(gs);
    result = lwpoint_make4d(srid, point->x, point->y, point->z, epoch);
  }
  else
  {
    const POINT2D *point = gs_get_point2d_p(gs);
    result = lwpoint_make3dm(srid, point->x, point->y, epoch);
  }
  FLAGS_SET_GEODETIC(result->flags, FLAGS_GET_GEODETIC(gs->flags));
  return result;
}

/**
 * Converts the temporal instant point into a PostGIS trajectory
 * geometry/geography where the M coordinates encode the timestamps in
 * number of seconds since '1970-01-01'
 */
static Datum
tpointinst_to_geo(const TInstant *inst)
{
  LWPOINT *point = point_to_trajpoint(tinstant_value(inst), inst->t);
  GSERIALIZED *result = geo_serialize((LWGEOM *)point);
  pfree(point);
  return PointerGetDatum(result);
}

/**
 * Converts the temporal instant set point into a PostGIS trajectory
 * geometry/geography where the M coordinates encode the timestamps in
 * number of seconds since '1970-01-01'
 */
static Datum
tpointinstset_to_geo(const TInstantSet *ti)
{
  const TInstant *inst = tinstantset_inst_n(ti, 0);
  GSERIALIZED *gs = (GSERIALIZED *) DatumGetPointer(tinstant_value_ptr(inst));
  int32 srid = gserialized_get_srid(gs);
  LWGEOM **points = palloc(sizeof(LWGEOM *) * ti->count);
  for (int i = 0; i < ti->count; i++)
  {
    inst = tinstantset_inst_n(ti, i);
    points[i] = (LWGEOM *)point_to_trajpoint(tinstant_value(inst), inst->t);
  }
  GSERIALIZED *result;
  if (ti->count == 1)
    result = geo_serialize(points[0]);
  else
  {
    LWGEOM *mpoint = (LWGEOM *)lwcollection_construct(MULTIPOINTTYPE, srid,
      NULL, (uint32_t) ti->count, points);
    result = geo_serialize(mpoint);
    pfree(mpoint);
  }

  pfree_array((void **) points, ti->count);
  return PointerGetDatum(result);
}

/**
 * Converts the temporal sequence point into a PostGIS trajectory
 * geometry/geography where the M coordinates encode the timestamps in
 * number of seconds since '1970-01-01'
 */
static LWGEOM *
tpointseq_to_geo1(const TSequence *seq)
{
  LWGEOM **points = palloc(sizeof(LWGEOM *) * seq->count);
  for (int i = 0; i < seq->count; i++)
  {
    const TInstant *inst = tsequence_inst_n(seq, i);
    points[i] = (LWGEOM *) point_to_trajpoint(tinstant_value(inst), inst->t);
  }
  LWGEOM *result;
  /* Instantaneous sequence */
  if (seq->count == 1)
  {
    result = points[0];
    pfree(points);
  }
  else
  {
    if (MOBDB_FLAGS_GET_LINEAR(seq->flags))
      result = (LWGEOM *) lwline_from_lwgeom_array(points[0]->srid,
        (uint32_t) seq->count, points);
    else
      result = (LWGEOM *) lwcollection_construct(MULTIPOINTTYPE,
        points[0]->srid, NULL, (uint32_t) seq->count, points);
    for (int i = 0; i < seq->count; i++)
      lwpoint_free((LWPOINT *) points[i]);
    pfree(points);
  }
  return result;
}

/**
 * Converts the temporal sequence point into a PostGIS trajectory
 * geometry/geography where the M coordinates encode the timestamps in
 * number of seconds since '1970-01-01'
 */
static Datum
tpointseq_to_geo(const TSequence *seq)
{
  LWGEOM *lwgeom = tpointseq_to_geo1(seq);
  GSERIALIZED *result = geo_serialize(lwgeom);
  return PointerGetDatum(result);
}

/**
 * Converts the temporal sequence set point into a PostGIS trajectory
 * geometry/geography where the M coordinates encode the timestamps in
 * number of seconds since '1970-01-01'
 */
static Datum
tpointseqset_to_geo(const TSequenceSet *ts)
{
  const TSequence *seq;

  /* Instantaneous sequence */
  if (ts->count == 1)
  {
    seq = tsequenceset_seq_n(ts, 0);
    return tpointseq_to_geo(seq);
  }

  /* General case */
  uint32_t colltype = 0;
  LWGEOM **geoms = palloc(sizeof(LWGEOM *) * ts->count);
  for (int i = 0; i < ts->count; i++)
  {
    seq = tsequenceset_seq_n(ts, i);
    geoms[i] = tpointseq_to_geo1(seq);
    /* If output type not initialized make geom type as output type */
    if (! colltype)
      colltype = lwtype_get_collectiontype(geoms[i]->type);
    /* If geom type is not compatible with current output type
     * make output type a collection */
    else if (colltype != COLLECTIONTYPE &&
      lwtype_get_collectiontype(geoms[i]->type) != colltype)
      colltype = COLLECTIONTYPE;
  }
  // TODO add the bounding box instead of ask PostGIS to compute it again
  // GBOX *box = stbox_to_gbox(tsequence_bbox_ptr(seq));
  LWGEOM *coll = (LWGEOM *) lwcollection_construct((uint8_t) colltype,
    geoms[0]->srid, NULL, (uint32_t) ts->count, geoms);
  Datum result = PointerGetDatum(geo_serialize(coll));
  /* We cannot lwgeom_free(geoms[i] or lwgeom_free(coll) */
  pfree(geoms);
  return result;
}

/*****************************************************************************/

/**
 * Converts the temporal sequence point into a PostGIS trajectory
 * geometry/geography where the M coordinates encode the timestamps in
 * number of seconds since '1970-01-01'
 *
 * Version when the resulting value is a MultiLinestring M, where each
 * component is a segment of two points.
 *
 * @param[out] result Array on which the pointers of the newly constructed
 * sequences are stored
 * @param[in] seq Temporal point
 */
static int
tpointseq_to_geo_segmentize1(LWGEOM **result, const TSequence *seq)
{
  const TInstant *inst = tsequence_inst_n(seq, 0);
  LWPOINT *points[2];

  /* Instantaneous sequence */
  if (seq->count == 1)
  {
    result[0] = (LWGEOM *) point_to_trajpoint(tinstant_value(inst), inst->t);
    return 1;
  }

  /* General case */
  for (int i = 0; i < seq->count - 1; i++)
  {
    points[0] = point_to_trajpoint(tinstant_value(inst), inst->t);
    inst = tsequence_inst_n(seq, i + 1);
    points[1] = point_to_trajpoint(tinstant_value(inst), inst->t);
    result[i] = (LWGEOM *) lwline_from_lwgeom_array(points[0]->srid, 2,
      (LWGEOM **) points);
    lwpoint_free(points[0]); lwpoint_free(points[1]);
  }
  return seq->count - 1;
}

/**
 * Converts the temporal sequence point into a PostGIS trajectory
 * geometry/geography where the M coordinates encode the timestamps in
 * number of seconds since '1970-01-01'
 *
 * Version when the resulting value is a MultiLinestring M, where each
 * component is a segment of two points.
 */
static Datum
tpointseq_to_geo_segmentize(const TSequence *seq)
{
  int count = (seq->count == 1) ? 1 : seq->count - 1;
  LWGEOM **geoms = palloc(sizeof(LWGEOM *) * count);
  tpointseq_to_geo_segmentize1(geoms, seq);
  Datum result;
  /* Instantaneous sequence */
  if (seq->count == 1)
    result = PointerGetDatum(geo_serialize(geoms[0]));
  else
  {
    // TODO add the bounding box instead of ask PostGIS to compute it again
    // GBOX *box = stbox_to_gbox(tsequence_bbox_ptr(seq));
    LWGEOM *segcoll = (LWGEOM *) lwcollection_construct(MULTILINETYPE,
      geoms[0]->srid, NULL, (uint32_t)(seq->count - 1), geoms);
    result = PointerGetDatum(geo_serialize(segcoll));
  }
  for (int i = 0; i < count; i++)
    lwgeom_free(geoms[i]);
  pfree(geoms);
  return result;
}

/**
 * Converts the temporal sequence set point into a PostGIS trajectory
 * geometry/geography where the M coordinates encode the timestamps in
 * number of seconds since '1970-01-01'
 *
 * Version when the resulting value is a MultiLinestring M, where each
 * component is a segment of two points.
 */
static Datum
tpointseqset_to_geo_segmentize(const TSequenceSet *ts)
{
  const TSequence *seq;

  /* Instantaneous sequence */
  if (ts->count == 1)
  {
    seq = tsequenceset_seq_n(ts, 0);
    return tpointseq_to_geo_segmentize(seq);
  }

  uint8_t colltype = 0;
  LWGEOM **geoms = palloc(sizeof(LWGEOM *) * ts->totalcount);
  int k = 0;
  for (int i = 0; i < ts->count; i++)
  {
    seq = tsequenceset_seq_n(ts, i);
    k += tpointseq_to_geo_segmentize1(&geoms[k], seq);
    /* Output type not initialized */
    if (! colltype)
      colltype = (uint8_t) lwtype_get_collectiontype(geoms[k - 1]->type);
      /* Input type not compatible with output */
      /* make output type a collection */
    else if (colltype != COLLECTIONTYPE &&
      lwtype_get_collectiontype(geoms[k - 1]->type) != colltype)
      colltype = COLLECTIONTYPE;
  }
  Datum result;
  // TODO add the bounding box instead of ask PostGIS to compute it again
  // GBOX *box = stbox_to_gbox(tsequenceset_bbox_ptr(seq));
  LWGEOM *coll = (LWGEOM *) lwcollection_construct(colltype,
    geoms[0]->srid, NULL, (uint32_t) k, geoms);
  result = PointerGetDatum(geo_serialize(coll));
  for (int i = 0; i < k; i++)
    lwgeom_free(geoms[i]);
  pfree(geoms);
  return result;
}

/*****************************************************************************/

PG_FUNCTION_INFO_V1(tpoint_to_geo);
/**
 * Converts the temporal point into a PostGIS trajectory geometry/geography
 * where the M coordinates encode the timestamps in number of seconds since
 * '1970-01-01'
 */
PGDLLEXPORT Datum
tpoint_to_geo(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  bool segmentize = (PG_NARGS() == 2) ? PG_GETARG_BOOL(1) : false;
  Datum result;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT)
    result = tpointinst_to_geo((TInstant *)temp);
  else if (temp->subtype == INSTANTSET)
    result = tpointinstset_to_geo((TInstantSet *)temp);
  else if (temp->subtype == SEQUENCE)
    result = segmentize ?
         tpointseq_to_geo_segmentize((TSequence *) temp) :
         tpointseq_to_geo((TSequence *) temp);
  else /* temp->subtype == SEQUENCESET */
    result = segmentize ?
         tpointseqset_to_geo_segmentize((TSequenceSet *) temp) :
         tpointseqset_to_geo((TSequenceSet *) temp);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_DATUM(result);
}

/*****************************************************************************
 * Convert trajectory geometry/geography where the M coordinates encode the
 * timestamps in number of seconds since '1970-01-01' into a temporal point.
 *****************************************************************************/

/**
 * Converts the PostGIS trajectory geometry/geography where the M coordinates
 * encode the timestamps in number of seconds since '1970-01-01' into a
 * temporal instant point.
 */
static TInstant *
trajpoint_to_tpointinst(LWPOINT *lwpoint)
{
  bool hasz = (bool) FLAGS_GET_Z(lwpoint->flags);
  bool geodetic = (bool) FLAGS_GET_GEODETIC(lwpoint->flags);
  LWPOINT *lwpoint1;
  TimestampTz t;
  if (hasz)
  {
    POINT4D point = getPoint4d(lwpoint->point, 0);
    t = (TimestampTz) ((point.m - 946684800) * 1e6);
    lwpoint1 = lwpoint_make3dz(lwpoint->srid, point.x, point.y, point.z);
  }
  else
  {
    POINT3DM point = getPoint3dm(lwpoint->point, 0);
    t = (TimestampTz) ((point.m - 946684800) * 1e6);
    lwpoint1 = lwpoint_make2d(lwpoint->srid, point.x, point.y);
  }
  FLAGS_SET_GEODETIC(lwpoint1->flags, geodetic);
  GSERIALIZED *gs = geo_serialize((LWGEOM *)lwpoint1);
  Oid basetypid = geodetic ? type_oid(T_GEOGRAPHY) : type_oid(T_GEOMETRY);
  TInstant *result = tinstant_make(PointerGetDatum(gs), t,
    basetypid);
  pfree(gs);
  return result;
}

/**
 * Converts the PostGIS trajectory geometry/geography where the M coordinates
 * encode the timestamps in number of seconds since '1970-01-01' into a
 * temporal instant point.
 */
static TInstant *
geo_to_tpointinst(GSERIALIZED *gs)
{
  /* Geometry is a POINT */
  LWGEOM *lwgeom = lwgeom_from_gserialized(gs);
  TInstant *result = trajpoint_to_tpointinst((LWPOINT *)lwgeom);
  lwgeom_free(lwgeom);
  return result;
}

/**
 * Converts the PostGIS trajectory geometry/geography where the M coordinates
 * encode the timestamps in number of seconds since '1970-01-01' into a
 * temporal instant set point.
 */
static TInstantSet *
geo_to_tpointinstset(GSERIALIZED *gs)
{
  /* Geometry is a MULTIPOINT */
  LWGEOM *lwgeom = lwgeom_from_gserialized(gs);
  bool hasz = (bool) FLAGS_GET_Z(gs->flags);
  /* Verify that is a valid set of trajectory points */
  LWCOLLECTION *lwcoll = lwgeom_as_lwcollection(lwgeom);
  double m1 = -1 * DBL_MAX, m2;
  int npoints = lwcoll->ngeoms;
  for (int i = 0; i < npoints; i++)
  {
    LWPOINT *lwpoint = (LWPOINT *)lwcoll->geoms[i];
    if (hasz)
    {
      POINT4D point = getPoint4d(lwpoint->point, 0);
      m2 = point.m;
    }
    else
    {
      POINT3DM point = getPoint3dm(lwpoint->point, 0);
      m2 = point.m;
    }
    if (m1 >= m2)
    {
      lwgeom_free(lwgeom);
      ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
        errmsg("Trajectory must be valid")));
    }
    m1 = m2;
  }
  TInstant **instants = palloc(sizeof(TInstant *) * npoints);
  for (int i = 0; i < npoints; i++)
    instants[i] = trajpoint_to_tpointinst((LWPOINT *)lwcoll->geoms[i]);
  lwgeom_free(lwgeom);

  return tinstantset_make_free(instants, npoints, MERGE_NO);
}

/**
 * Converts the PostGIS trajectory geometry/geography where the M coordinates
 * encode the timestamps in number of seconds since '1970-01-01' into a
 * temporal sequence point.
 */
static TSequence *
geo_to_tpointseq(GSERIALIZED *gs)
{
  /* Geometry is a LINESTRING */
  bool hasz =(bool)  FLAGS_GET_Z(gs->flags);
  LWGEOM *lwgeom = lwgeom_from_gserialized(gs);
  LWLINE *lwline = lwgeom_as_lwline(lwgeom);
  int npoints = lwline->points->npoints;
  /*
   * Verify that the trajectory is valid.
   * Since calling lwgeom_is_trajectory causes discrepancies with regression
   * tests because of the error message depends on PostGIS version,
   * the verification is made here.
   */
  double m1 = -1 * DBL_MAX, m2;
  for (int i = 0; i < npoints; i++)
  {
    if (hasz)
    {
      POINT4D point = getPoint4d(lwline->points, (uint32_t) i);
      m2 = point.m;
    }
    else
    {
      POINT3DM point = getPoint3dm(lwline->points, (uint32_t) i);
      m2 = point.m;
    }
    if (m1 >= m2)
    {
      lwgeom_free(lwgeom);
      ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
        errmsg("Trajectory must be valid")));
    }
    m1 = m2;
  }
  TInstant **instants = palloc(sizeof(TInstant *) * npoints);
  for (int i = 0; i < npoints; i++)
  {
    /* Returns freshly allocated LWPOINT */
    LWPOINT *lwpoint = lwline_get_lwpoint(lwline, (uint32_t) i);
    instants[i] = trajpoint_to_tpointinst(lwpoint);
    lwpoint_free(lwpoint);
  }
  lwgeom_free(lwgeom);
  /* The resulting sequence assumes linear interpolation */
  return tsequence_make_free(instants, npoints, true, true,
    LINEAR, NORMALIZE);
}

/**
 * Converts the PostGIS trajectory geometry/geography where the M coordinates
 * encode the timestamps in number of seconds since '1970-01-01' into a
 * temporal sequence set point.
 */
static TSequenceSet *
geo_to_tpointseqset(GSERIALIZED *gs)
{
  /* Geometry is a MULTILINESTRING or a COLLECTION composed of POINT and LINESTRING */
  LWGEOM *lwgeom = lwgeom_from_gserialized(gs);
  LWCOLLECTION *lwcoll = lwgeom_as_lwcollection(lwgeom);
  int ngeoms = lwcoll->ngeoms;
  for (int i = 0; i < ngeoms; i++)
  {
    LWGEOM *lwgeom1 = lwcoll->geoms[i];
    if (lwgeom1->type != POINTTYPE && lwgeom1->type != LINETYPE)
    {
      lwgeom_free(lwgeom);
      ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
        errmsg("Component geometry/geography must be of type Point(Z)M or Linestring(Z)M")));
    }
  }

  TSequence **sequences = palloc(sizeof(TSequence *) * ngeoms);
  for (int i = 0; i < ngeoms; i++)
  {
    LWGEOM *lwgeom1 = lwcoll->geoms[i];
    GSERIALIZED *gs1 = geo_serialize(lwgeom1);
    if (lwgeom1->type == POINTTYPE)
    {
      TInstant *inst = geo_to_tpointinst(gs1);
      /* The resulting sequence assumes linear interpolation */
      sequences[i] = tinstant_to_tsequence(inst, LINEAR);
      pfree(inst);
    }
    else /* lwgeom1->type == LINETYPE */
      sequences[i] = geo_to_tpointseq(gs1);
    pfree(gs1);
  }
  lwgeom_free(lwgeom);
  /* The resulting sequence set assumes linear interpolation */
  return tsequenceset_make_free(sequences, ngeoms, NORMALIZE_NO);
}

PG_FUNCTION_INFO_V1(geo_to_tpoint);
/**
 * Converts the PostGIS trajectory geometry/geography where the M coordinates
 * encode the timestamps in number of seconds since '1970-01-01' into a
 * temporal point.
 */
PGDLLEXPORT Datum
geo_to_tpoint(PG_FUNCTION_ARGS)
{
  GSERIALIZED *gs = PG_GETARG_GSERIALIZED_P(0);
  ensure_non_empty(gs);
  ensure_has_M_gs(gs);

  Temporal *result = NULL; /* Make compiler quiet */
  if (gserialized_get_type(gs) == POINTTYPE)
    result = (Temporal *)geo_to_tpointinst(gs);
  else if (gserialized_get_type(gs) == MULTIPOINTTYPE)
    result = (Temporal *)geo_to_tpointinstset(gs);
  else if (gserialized_get_type(gs) == LINETYPE)
    result = (Temporal *)geo_to_tpointseq(gs);
  else if (gserialized_get_type(gs) == MULTILINETYPE ||
    gserialized_get_type(gs) == COLLECTIONTYPE)
    result = (Temporal *)geo_to_tpointseqset(gs);
  else
    ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
      errmsg("Invalid geometry type for trajectory")));

  PG_FREE_IF_COPY(gs, 0);
  PG_RETURN_POINTER(result);
}

/*****************************************************************************
 * Convert a temporal point into a LinestringM geometry/geography where the M
 * coordinates values are given by a temporal float.
 *****************************************************************************/

static LWPOINT *
point_measure_to_geo_measure(Datum point, Datum measure)
{
  GSERIALIZED *gs = (GSERIALIZED *) DatumGetPointer(point);
  int32 srid = gserialized_get_srid(gs);
  double d = DatumGetFloat8(measure);
  LWPOINT *result;
  if (FLAGS_GET_Z(gs->flags))
  {
    const POINT3DZ *point = gs_get_point3dz_p(gs);
    result = lwpoint_make4d(srid, point->x, point->y, point->z, d);
  }
  else
  {
    const POINT2D *point = gs_get_point2d_p(gs);
    result = lwpoint_make3dm(srid, point->x, point->y, d);
  }
  FLAGS_SET_GEODETIC(result->flags, FLAGS_GET_GEODETIC(gs->flags));
  return result;
}

/**
 * Construct a geometry/geography with M measure from the temporal instant
 * point and the temporal float.
 *
 * @param[in] inst Temporal point
 * @param[in] measure Temporal float
 */
static Datum
tpointinst_to_geo_measure(const TInstant *inst, const TInstant *measure)
{
  LWPOINT *point = point_measure_to_geo_measure(tinstant_value(inst),
    tinstant_value(measure));
  GSERIALIZED *result = geo_serialize((LWGEOM *)point);
  pfree(point);
  return PointerGetDatum(result);
}

/**
 * Construct a geometry/geography with M measure from the temporal instant set
 * point and the temporal float.
 *
 * @param[in] ti Temporal point
 * @param[in] measure Temporal float
 */
static Datum
tpointinstset_to_geo_measure(const TInstantSet *ti, const TInstantSet *measure)
{
  LWGEOM **points = palloc(sizeof(LWGEOM *) * ti->count);
  for (int i = 0; i < ti->count; i++)
  {
    const TInstant *inst = tinstantset_inst_n(ti, i);
    const TInstant *m = tinstantset_inst_n(measure, i);
    points[i] = (LWGEOM *) point_measure_to_geo_measure(
      tinstant_value(inst), tinstant_value(m));
  }
  GSERIALIZED *result;
  if (ti->count == 1)
    result = geo_serialize(points[0]);
  else
  {
    LWGEOM *mpoint = (LWGEOM *) lwcollection_construct(MULTIPOINTTYPE,
      points[0]->srid, NULL, (uint32_t) ti->count, points);
    result = geo_serialize(mpoint);
    pfree(mpoint);
  }

  for (int i = 0; i < ti->count; i++)
    pfree(points[i]);
  pfree(points);
  return PointerGetDatum(result);
}

/**
 * Construct a geometry/geography with M measure from the temporal sequence
 * point and the temporal float. The function removes one point if two
 * consecutive points are equal
 *
 * @param[in] seq Temporal point
 * @param[in] measure Temporal float
 * @pre The temporal point and the measure are synchronized
 */
static LWGEOM *
tpointseq_to_geo_measure1(const TSequence *seq, const TSequence *measure)
{
  LWPOINT **points = palloc(sizeof(LWPOINT *) * seq->count);
  /* Remove two consecutive points if they are equal */
  const TInstant *inst = tsequence_inst_n(seq, 0);
  const TInstant *m = tsequence_inst_n(measure, 0);
  LWPOINT *value1 = point_measure_to_geo_measure(tinstant_value(inst),
    tinstant_value(m));
  points[0] = value1;
  int k = 1;
  for (int i = 1; i < seq->count; i++)
  {
    inst = tsequence_inst_n(seq, i);
    m = tsequence_inst_n(measure, i);
    LWPOINT *value2 = point_measure_to_geo_measure(tinstant_value(inst),
      tinstant_value(m));
    /* Add point only if previous point is diffrent from the current one */
    if (lwpoint_same(value1, value2) != LW_TRUE)
      points[k++] = value2;
    value1 = value2;
  }
  LWGEOM *result;
  if (k == 1)
  {
    result = (LWGEOM *) points[0];
    pfree(points);
  }
  else
  {
    result = MOBDB_FLAGS_GET_LINEAR(seq->flags) ?
      (LWGEOM *) lwline_from_lwgeom_array(points[0]->srid, (uint32_t) k,
        (LWGEOM **) points) :
      (LWGEOM *) lwcollection_construct(MULTIPOINTTYPE,
        points[0]->srid, NULL, (uint32_t) k, (LWGEOM **) points);
    for (int i = 0; i < k; i++)
      lwpoint_free(points[i]);
    pfree(points);
  }
  return result;
}

/**
 * Construct a geometry/geography with M measure from the temporal sequence
 * point and the temporal float.
 *
 * @param[in] seq Temporal point
 * @param[in] measure Temporal float
 */
static Datum
tpointseq_to_geo_measure(const TSequence *seq, const TSequence *measure)
{
  LWGEOM *lwgeom = tpointseq_to_geo_measure1(seq, measure);
  GSERIALIZED *result = geo_serialize(lwgeom);
  return PointerGetDatum(result);
}

/**
 * Construct a geometry/geography with M measure from the temporal sequence
 * point and the temporal float.
 *
 * @param[in] ts Temporal point
 * @param[in] measure Temporal float
 */
static Datum
tpointseqset_to_geo_measure(const TSequenceSet *ts, const TSequenceSet *measure)
{
  const TSequence *seq;
  const TSequence *m;

  /* Instantaneous sequence */
  if (ts->count == 1)
  {
    seq = tsequenceset_seq_n(ts, 0);
    m = tsequenceset_seq_n(measure, 0);
    return tpointseq_to_geo_measure(seq, m);
  }

  uint8_t colltype = 0;
  LWGEOM **geoms = palloc(sizeof(LWGEOM *) * ts->count);
  for (int i = 0; i < ts->count; i++)
  {
    seq = tsequenceset_seq_n(ts, i);
    m = tsequenceset_seq_n(measure, i);
    geoms[i] = tpointseq_to_geo_measure1(seq, m);
    /* Output type not initialized */
    if (! colltype)
      colltype = (uint8_t) lwtype_get_collectiontype(geoms[i]->type);
    /* Input type not compatible with output */
    /* make output type a collection */
    else if (colltype != COLLECTIONTYPE &&
      lwtype_get_collectiontype(geoms[i]->type) != colltype)
      colltype = COLLECTIONTYPE;
  }
  // TODO add the bounding box instead of ask PostGIS to compute it again
  // GBOX *box = stbox_to_gbox(tsequence_bbox_ptr(seq));
  LWGEOM *coll = (LWGEOM *) lwcollection_construct(colltype,
    geoms[0]->srid, NULL, (uint32_t) ts->count, geoms);
  Datum result = PointerGetDatum(geo_serialize(coll));
  /* We cannot lwgeom_free(geoms[i] or lwgeom_free(coll) */
  pfree(geoms);
  return result;
}

/*****************************************************************************/

/**
 * Construct a geometry/geography with M measure from the temporal sequence
 * point and the temporal float.
 *
 * Version that produces a Multilinestring when each composing linestring
 * corresponds to a segment of the orginal temporal point.
 *
 * @param[out] result Array on which the pointers of the newly constructed
 * sequences are stored
 * @param[in] seq Temporal point
 * @param[in] measure Temporal float
 */
static int
tpointseq_to_geo_measure_segmentize1(LWGEOM **result, const TSequence *seq,
  const TSequence *measure)
{
  const TInstant *inst = tsequence_inst_n(seq, 0);
  const TInstant *m = tsequence_inst_n(measure, 0);
  LWPOINT *points[2];

  /* Instantaneous sequence */
  if (seq->count == 1)
  {
    result[0] = (LWGEOM *) point_measure_to_geo_measure(
      tinstant_value(inst), tinstant_value(m));
    return 1;
  }

  /* General case */
  for (int i = 0; i < seq->count - 1; i++)
  {
    points[0] = point_measure_to_geo_measure(tinstant_value(inst),
      tinstant_value(m));
    inst = tsequence_inst_n(seq, i + 1);
    points[1] = point_measure_to_geo_measure(tinstant_value(inst),
      tinstant_value(m));
    result[i] = (LWGEOM *) lwline_from_lwgeom_array(points[0]->srid, 2,
      (LWGEOM **) points);
    lwpoint_free(points[0]); lwpoint_free(points[1]);
    m = tsequence_inst_n(measure, i + 1);
  }
  return seq->count - 1;
}

/**
 * Construct a geometry/geography with M measure from the temporal sequence
 * point and the temporal float.
 *
 * Version that produces a Multilinestring when each composing linestring
 * corresponds to a segment of the orginal temporal point.
 */
static Datum
tpointseq_to_geo_measure_segmentize(const TSequence *seq,
  const TSequence *measure)
{
  int count = (seq->count == 1) ? 1 : seq->count - 1;
  LWGEOM **geoms = palloc(sizeof(LWGEOM *) * count);
  tpointseq_to_geo_measure_segmentize1(geoms, seq, measure);
  Datum result;
  /* Instantaneous sequence */
  if (seq->count == 1)
    result = PointerGetDatum(geo_serialize(geoms[0]));
  else
  {
    // TODO add the bounding box instead of ask PostGIS to compute it again
    // GBOX *box = stbox_to_gbox(tsequence_bbox_ptr(seq));
    LWGEOM *segcoll = (LWGEOM *) lwcollection_construct(MULTILINETYPE,
      geoms[0]->srid, NULL, (uint32_t)(seq->count - 1), geoms);
    result = PointerGetDatum(geo_serialize(segcoll));
  }
  for (int i = 0; i < count; i++)
    lwgeom_free(geoms[i]);
  pfree(geoms);
  return result;
}

/**
 * Construct a geometry/geography with M measure from the temporal sequence set
 * point and the temporal float.
 *
 * Version that produces a Multilinestring when each composing linestring
 * corresponds to a segment of the orginal temporal point.
 */
static Datum
tpointseqset_to_geo_measure_segmentize(const TSequenceSet *ts,
  const TSequenceSet *measure)
{
  /* Instantaneous sequence */
  if (ts->count == 1)
  {
    const TSequence *seq1 = tsequenceset_seq_n(ts, 0);
    const TSequence *seq2 = tsequenceset_seq_n(measure, 0);
    return tpointseq_to_geo_measure_segmentize(seq1, seq2);
  }

  uint8_t colltype = 0;
  LWGEOM **geoms = palloc(sizeof(LWGEOM *) * ts->totalcount);
  int k = 0;
  for (int i = 0; i < ts->count; i++)
  {

    const TSequence *seq = tsequenceset_seq_n(ts, i);
    const TSequence *m = tsequenceset_seq_n(measure, i);
    k += tpointseq_to_geo_measure_segmentize1(&geoms[k], seq, m);
    /* Output type not initialized */
    if (! colltype)
      colltype = (uint8_t) lwtype_get_collectiontype(geoms[k - 1]->type);
      /* Input type not compatible with output */
      /* make output type a collection */
    else if (colltype != COLLECTIONTYPE &&
         lwtype_get_collectiontype(geoms[k - 1]->type) != colltype)
      colltype = COLLECTIONTYPE;
  }
  Datum result;
  // TODO add the bounding box instead of ask PostGIS to compute it again
  // GBOX *box = stbox_to_gbox(tsequenceset_bbox_ptr(seq));
  LWGEOM *coll = (LWGEOM *) lwcollection_construct(colltype,
    geoms[0]->srid, NULL, (uint32_t) k, geoms);
  result = PointerGetDatum(geo_serialize(coll));
  for (int i = 0; i < k; i++)
    lwgeom_free(geoms[i]);
  pfree(geoms);
  return result;
}

/*****************************************************************************/

PG_FUNCTION_INFO_V1(tpoint_to_geo_measure);
/**
 * Construct a geometry/geography with M measure from the temporal point and
 * the temporal float
 */
PGDLLEXPORT Datum
tpoint_to_geo_measure(PG_FUNCTION_ARGS)
{
  Temporal *tpoint = PG_GETARG_TEMPORAL(0);
  Temporal *measure = PG_GETARG_TEMPORAL(1);
  bool segmentize = PG_GETARG_BOOL(2);
  ensure_tgeo_base_type(tpoint->basetypid);
  ensure_tnumber_base_type(measure->basetypid);

  Temporal *sync1, *sync2;
  /* Return false if the temporal values do not intersect in time
   * The operation is synchronization without adding crossings */
  if (!intersection_temporal_temporal(tpoint, measure, SYNCHRONIZE,
    &sync1, &sync2))
  {
    PG_FREE_IF_COPY(tpoint, 0);
    PG_FREE_IF_COPY(measure, 1);
    PG_RETURN_NULL();
  }

  Temporal *result;
  ensure_valid_tempsubtype(sync1->subtype);
  if (sync1->subtype == INSTANT)
    result = (Temporal *) tpointinst_to_geo_measure(
      (TInstant *) sync1, (TInstant *) sync2);
  else if (sync1->subtype == INSTANTSET)
    result = (Temporal *) tpointinstset_to_geo_measure(
      (TInstantSet *) sync1, (TInstantSet *) sync2);
  else if (sync1->subtype == SEQUENCE)
    result = segmentize ?
      (Temporal *) tpointseq_to_geo_measure_segmentize(
        (TSequence *) sync1, (TSequence *) sync2) :
      (Temporal *) tpointseq_to_geo_measure(
        (TSequence *) sync1, (TSequence *) sync2);
  else /* sync1->subtype == SEQUENCESET */
    result = segmentize ?
      (Temporal *) tpointseqset_to_geo_measure_segmentize(
        (TSequenceSet *) sync1, (TSequenceSet *) sync2) :
      (Temporal *) tpointseqset_to_geo_measure(
        (TSequenceSet *) sync1, (TSequenceSet *) sync2);

  pfree(sync1); pfree(sync2);
  PG_FREE_IF_COPY(tpoint, 0);
  PG_FREE_IF_COPY(measure, 1);
  PG_RETURN_POINTER(result);
}

/***********************************************************************
 * Simple Douglas-Peucker-like value simplification for temporal floats.
 ***********************************************************************/

/**
 * Finds a split when simplifying the temporal sequence point using a
 * spatio-temporal extension of the Douglas-Peucker line simplification
 * algorithm.
 *
 * @param[in] seq Temporal sequence
 * @param[in] i1,i2 Indexes of the reference instants
 * @param[out] split Location of the split
 * @param[out] dist Distance at the split
 */
static void
tfloatseq_dp_findsplit(const TSequence *seq, int i1, int i2,
  int *split, double *dist)
{
  *split = i1;
  *dist = -1;
  if (i1 + 1 < i2)
  {
    const TInstant *inst1 = tsequence_inst_n(seq, i1);
    const TInstant *inst2 = tsequence_inst_n(seq, i2);
    double start = DatumGetFloat8(tinstant_value(inst1));
    double end = DatumGetFloat8(tinstant_value(inst2));
    double duration2 = (double) (inst2->t - inst1->t);
    for (int k = i1 + 1; k < i2; k++)
    {
      inst2 = tsequence_inst_n(seq, k);
      double value = DatumGetFloat8(tinstant_value(inst2));
      double duration1 = (double) (inst2->t - inst1->t);
      double ratio = duration1 / duration2;
      double value_interp = start + (end - start) * ratio;
      double d = fabs(value - value_interp);
      if (d > *dist)
      {
        /* record the maximum */
        *split = k;
        *dist = d;
      }
    }
  }
  return;
}

/**
 * Returns a negative or a positive value depending on whether the first number
 * is less than or greater than the second one
 */
int
int_cmp(const void *a, const void *b)
{
  /* casting pointer types */
  const int *ia = (const int *)a;
  const int *ib = (const int *)b;
  /* returns negative if b > a and positive if a > b */
  return *ia - *ib;
}

/**
 * Simplifies the temporal sequence number using a
 * Douglas-Peucker-like line simplification algorithm.
 *
 * @param[in] seq Temporal point
 * @param[in] eps_dist Epsilon speed
 * @param[in] minpts Minimum number of points
 */
TSequence *
tfloatseq_simplify(const TSequence *seq, double eps_dist, uint32_t minpts)
{
  static size_t stack_size = 256;
  int *stack, *outlist; /* recursion stack */
  int stack_static[stack_size];
  int outlist_static[stack_size];
  int sp = -1; /* recursion stack pointer */
  int i1, split;
  uint32_t outn = 0;
  uint32_t i;
  double dist;

  /* Do not try to simplify really short things */
  if (seq->count < 3)
    return tsequence_copy(seq);

  /* Only heap allocate book-keeping arrays if necessary */
  if ((unsigned int) seq->count > stack_size)
  {
    stack = palloc(sizeof(int) * seq->count);
    outlist = palloc(sizeof(int) * seq->count);
  }
  else
  {
    stack = stack_static;
    outlist = outlist_static;
  }

  i1 = 0;
  stack[++sp] = seq->count - 1;
  /* Add first point to output list */
  outlist[outn++] = 0;
  do
  {
    tfloatseq_dp_findsplit(seq, i1, stack[sp], &split, &dist);
    bool dosplit = (dist >= 0 &&
      (dist > eps_dist || outn + sp + 1 < minpts));
    if (dosplit)
      stack[++sp] = split;
    else
    {
      outlist[outn++] = stack[sp];
      i1 = stack[sp--];
    }
  }
  while (sp >= 0);

  /* Put list of retained points into order */
  qsort(outlist, outn, sizeof(int), int_cmp);
  /* Create new TSequence */
  const TInstant **instants = palloc(sizeof(TInstant *) * outn);
  for (i = 0; i < outn; i++)
    instants[i] = tsequence_inst_n(seq, outlist[i]);
  TSequence *result = tsequence_make((const TInstant **) instants, outn,
    seq->period.lower_inc, seq->period.upper_inc,
    MOBDB_FLAGS_GET_LINEAR(seq->flags), NORMALIZE);
  pfree(instants);

  /* Only free if arrays are on heap */
  if (stack != stack_static)
    pfree(stack);
  if (outlist != outlist_static)
    pfree(outlist);

  return result;
}

/**
 * Simplifies the temporal sequence set number using a
 * Douglas-Peucker-like line simplification algorithm.
 *
 * @param[in] ts Temporal point
 * @param[in] eps_dist Epsilon speed
 * @param[in] minpts Minimum number of points
 */
TSequenceSet *
tfloatseqset_simplify(const TSequenceSet *ts, double eps_dist, uint32_t minpts)
{
  const TSequence *seq;

  /* Singleton sequence set */
  if (ts->count == 1)
  {
    seq = tsequenceset_seq_n(ts, 0);
    TSequence *seq1 = tfloatseq_simplify(seq, eps_dist, minpts);
    TSequenceSet *result = tsequence_to_tsequenceset(seq1);
    pfree(seq1);
    return result;
  }

  /* General case */
  TSequence **sequences = palloc(sizeof(TSequence *) * ts->count);
  for (int i = 0; i < ts->count; i++)
  {
    seq = tsequenceset_seq_n(ts, i);
    sequences[i] = tfloatseq_simplify(seq, eps_dist, minpts);
  }
  return tsequenceset_make_free(sequences, ts->count, NORMALIZE);
}

PG_FUNCTION_INFO_V1(tfloat_simplify);
/**
 * Simplifies the temporal number using a
 * Douglas-Peucker-like line simplification algorithm.
 */
Datum
tfloat_simplify(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  double eps_dist = PG_GETARG_FLOAT8(1);

  Temporal *result;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT || temp->subtype == INSTANTSET ||
    ! MOBDB_FLAGS_GET_LINEAR(temp->flags))
    result = temporal_copy(temp);
  else if (temp->subtype == SEQUENCE)
    result = (Temporal *) tfloatseq_simplify((TSequence *)temp,
      eps_dist, 2);
  else /* temp->subtype == SEQUENCESET */
    result = (Temporal *) tfloatseqset_simplify((TSequenceSet *)temp,
      eps_dist, 2);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_POINTER(result);
}

/***********************************************************************
 * Simple spatio-temporal Douglas-Peucker line simplification.
 * No checks are done to avoid introduction of self-intersections.
 * No topology relations are considered.
 ***********************************************************************/

/**
 * Returns the speed of the temporal point in the segment
 *
 * @param[in] inst1, inst2 Instants defining the segment
 * @param[in] func Distance function (2D, 3D, or geodetic)
 */
static double
tpointinst_speed(const TInstant *inst1, const TInstant *inst2,
  datum_func2 func)
{
  Datum value1 = tinstant_value(inst1);
  Datum value2 = tinstant_value(inst2);
  return datum_point_eq(value1, value2) ? 0 :
    DatumGetFloat8(func(value1, value2)) /
      ((double)(inst2->t - inst1->t) / 1000000);
}

/**
 * Returns the 2D distance between the points
 */
double
dist2d_pt_pt(POINT2D *p1, POINT2D *p2)
{
  double dx = p2->x - p1->x;
  double dy = p2->y - p1->y;
  return hypot(dx, dy);
}

/**
 * Returns the 3D distance between the points
 */
double
dist3d_pt_pt(POINT3DZ *p1, POINT3DZ *p2)
{
  double dx = p2->x - p1->x;
  double dy = p2->y - p1->y;
  double dz = p2->z - p1->z;
  return hypot3d(dx, dy, dz);
}

/**
 * Returns the 4D distance between the points
 */
double
dist4d_pt_pt(POINT4D *p1, POINT4D *p2)
{
  double dx = p2->x - p1->x;
  double dy = p2->y - p1->y;
  double dz = p2->z - p1->z;
  double dm = p2->m - p1->m;
  return hypot4d(dx, dy, dz, dm);
}

/**
 * Returns the 2D distance between the point the segment
 *
 * @param[in] p Point
 * @param[in] A,B Points defining the segment
 * @see http://geomalgorithms.com/a02-_lines.html
 * @note Derived from the PostGIS function lw_dist2d_pt_seg in
 * file measures.c
 */
double
dist2d_pt_seg(POINT2D *p, POINT2D *A, POINT2D *B)
{
  POINT2D c;
  double r;
  /* If start==end, then use pt distance */
  if (A->x == B->x && A->y == B->y)
    return dist2d_pt_pt(p, A);

  r = ( (p->x-A->x) * (B->x-A->x) + (p->y-A->y) * (B->y-A->y) ) /
    ( (B->x-A->x) * (B->x-A->x) + (B->y-A->y) * (B->y-A->y) );

  if (r < 0) /* If the first vertex A is closest to the point p */
    return dist2d_pt_pt(p, A);
  if (r > 1)  /* If the second vertex B is closest to the point p */
    return dist2d_pt_pt(p, B);

  /* else if the point p is closer to some point between a and b
  then we find that point and send it to dist2d_pt_pt */
  c.x = A->x + r * (B->x - A->x);
  c.y = A->y + r * (B->y - A->y);

  return dist2d_pt_pt(p, &c);
}

/**
 * Returns the 3D distance between the point the segment
 *
 * @param[in] p Point
 * @param[in] A,B Points defining the segment
 * @note Derived from the PostGIS function lw_dist3d_pt_seg in file
 * measures3d.c
 * @see http://geomalgorithms.com/a02-_lines.html
 */
double
dist3d_pt_seg(POINT3DZ *p, POINT3DZ *A, POINT3DZ *B)
{
  POINT3DZ c;
  double r;
  /* If start==end, then use pt distance */
  if (A->x == B->x && A->y == B->y && A->z == B->z)
    return dist3d_pt_pt(p, A);

  r = ( (p->x-A->x) * (B->x-A->x) + (p->y-A->y) * (B->y-A->y) +
      (p->z-A->z) * (B->z-A->z) ) /
    ( (B->x-A->x) * (B->x-A->x) + (B->y-A->y) * (B->y-A->y) +
      (B->z-A->z) * (B->z-A->z) );

  if (r < 0) /* If the first vertex A is closest to the point p */
    return dist3d_pt_pt(p, A);
  if (r > 1) /* If the second vertex B is closest to the point p */
    return dist3d_pt_pt(p, B);

  /* else if the point p is closer to some point between a and b
  then we find that point and send it to dist3d_pt_pt */
  c.x = A->x + r * (B->x - A->x);
  c.y = A->y + r * (B->y - A->y);
  c.z = A->z + r * (B->z - A->z);

  return dist3d_pt_pt(p, &c);
}

/**
 * Returns the 4D distance between the point the segment
 *
 * @param[in] p Point
 * @param[in] A,B Points defining the segment
 * @note Derived from the PostGIS function lw_dist3d_pt_seg in file
 * measures3d.c
 * @see http://geomalgorithms.com/a02-_lines.html
 */
double
dist4d_pt_seg(POINT4D *p, POINT4D *A, POINT4D *B)
{
  POINT4D c;
  double r;
  /* If start==end, then use pt distance */
  if (A->x == B->x && A->y == B->y && A->z == B->z && A->m == B->m)
    return dist4d_pt_pt(p, A);

  r = ( (p->x-A->x) * (B->x-A->x) + (p->y-A->y) * (B->y-A->y) +
      (p->z-A->z) * (B->z-A->z) + (p->m-A->m) * (B->m-A->m) ) /
    ( (B->x-A->x) * (B->x-A->x) + (B->y-A->y) * (B->y-A->y) +
      (B->z-A->z) * (B->z-A->z) + (B->m-A->m) * (B->m-A->m) );

  if (r < 0) /* If the first vertex A is closest to the point p */
    return dist4d_pt_pt(p, A);
  if (r > 1) /* If the second vertex B is closest to the point p */
    return dist4d_pt_pt(p, B);

  /* else if the point p is closer to some point between a and b
  then we find that point and send it to dist3d_pt_pt */
  c.x = A->x + r * (B->x - A->x);
  c.y = A->y + r * (B->y - A->y);
  c.z = A->z + r * (B->z - A->z);
  c.m = A->m + r * (B->m - A->m);

  return dist4d_pt_pt(p, &c);
}

/**
 * Finds a split when simplifying the temporal sequence point using a
 * spatio-temporal extension of the Douglas-Peucker line simplification
 * algorithm.
 *
 * @param[in] seq Temporal sequence
 * @param[in] i1,i2 Indexes of the reference instants
 * @param[in] withspeed True when the delta in the speed must be considered
 * @param[out] split Location of the split
 * @param[out] dist Distance at the split
 * @param[out] delta_speed Delta speed at the split
 */
static void
tpointseq_dp_findsplit(const TSequence *seq, int i1, int i2, bool withspeed,
  int *split, double *dist, double *delta_speed)
{
  POINT2D p2k, p2k_tmp, p2a, p2b;
  POINT3DZ p3k, p3k_tmp, p3a, p3b;
  POINT4D p4k, p4a, p4b;
  double d;
  bool hasz = MOBDB_FLAGS_GET_Z(seq->flags);
  *split = i1;
  d = -1;
  if (i1 + 1 < i2)
  {
    double speed_seg;
    datum_func2 func;
    if (withspeed)
      func = hasz ? &pt_distance3d : &pt_distance2d;
    const TInstant *inst1 = tsequence_inst_n(seq, i1);
    const TInstant *inst2 = tsequence_inst_n(seq, i2);
    if (withspeed)
      speed_seg = tpointinst_speed(inst1, inst2, func);
    if (hasz)
    {
      p3a = datum_get_point3dz(tinstant_value(inst1));
      p3b = datum_get_point3dz(tinstant_value(inst2));
      if (withspeed)
      {
        p4a.x = p3a.x; p4a.y = p3a.y;
        p4a.z = p3a.z; p4a.m = speed_seg;
        p4b.x = p3b.x; p4b.y = p3b.y;
        p4b.z = p3b.z; p4b.m = speed_seg;
      }
    }
    else
    {
      p2a = datum_get_point2d(tinstant_value(inst1));
      p2b = datum_get_point2d(tinstant_value(inst2));
      if (withspeed)
      {
        p3a.x = p2a.x; p3a.y = p2a.y; p3a.z = speed_seg;
        p3b.x = p2b.x; p3b.y = p2b.y; p3b.z = speed_seg;
      }
    }
    for (int k = i1 + 1; k < i2; k++)
    {
      double d_tmp, speed_pt;
      inst2 = tsequence_inst_n(seq, k);
      if (withspeed)
        speed_pt = tpointinst_speed(inst1, inst2, func);
      if (hasz)
      {
        p3k_tmp = datum_get_point3dz(tinstant_value(inst2));
        if (withspeed)
        {
          p4k.x = p3k_tmp.x; p4k.y = p3k_tmp.y;
          p4k.z = p3k_tmp.z; p4k.m = speed_pt;
          d_tmp = dist4d_pt_seg(&p4k, &p4a, &p4b);
        }
        else
          d_tmp = dist3d_pt_seg(&p3k_tmp, &p3a, &p3b);
      }
      else
      {
        p2k_tmp = datum_get_point2d(tinstant_value(inst2));
        if (withspeed)
        {
          p3k.x = p2k_tmp.x; p3k.y = p2k_tmp.y; p3k.z = speed_pt;
          d_tmp = dist3d_pt_seg(&p3k, &p3a, &p3b);
        }
        else
          d_tmp = dist2d_pt_seg(&p2k_tmp, &p2a, &p2b);
      }
      if (d_tmp > d)
      {
        /* record the maximum */
        d = d_tmp;
        if (hasz)
          p3k = p3k_tmp;
        else
          p2k = p2k_tmp;
        if (withspeed)
          *delta_speed = fabs(speed_seg - speed_pt);
        *split = k;
      }
      inst1 = inst2;
    }
    *dist = hasz ? dist3d_pt_seg(&p3k, &p3a, &p3b) :
      distance2d_pt_seg(&p2k, &p2a, &p2b);
  }
  else
    *dist = -1;
}

/***********************************************************************/

/**
 * Simplifies the temporal sequence point using a spatio-temporal
 * extension of the Douglas-Peucker line simplification algorithm.
 *
 * @param[in] seq Temporal point
 * @param[in] eps_dist Epsilon speed
 * @param[in] eps_speed Epsilon speed
 * @param[in] minpts Minimum number of points
 */
TSequence *
tpointseq_simplify(const TSequence *seq, double eps_dist,
  double eps_speed, uint32_t minpts)
{
  static size_t stack_size = 256;
  int *stack, *outlist; /* recursion stack */
  int stack_static[stack_size];
  int outlist_static[stack_size];
  int sp = -1; /* recursion stack pointer */
  int p1, split;
  uint32_t outn = 0;
  uint32_t i;
  double dist, delta_speed;
  bool withspeed = eps_speed > 0;

  /* Do not try to simplify really short things */
  if (seq->count < 3)
    return tsequence_copy(seq);

  /* Only heap allocate book-keeping arrays if necessary */
  if ((unsigned int) seq->count > stack_size)
  {
    stack = palloc(sizeof(int) * seq->count);
    outlist = palloc(sizeof(int) * seq->count);
  }
  else
  {
    stack = stack_static;
    outlist = outlist_static;
  }

  p1 = 0;
  stack[++sp] = seq->count - 1;
  /* Add first point to output list */
  outlist[outn++] = 0;
  do
  {
    tpointseq_dp_findsplit(seq, p1, stack[sp], withspeed, &split, &dist, &delta_speed);
    bool dosplit;
    if (withspeed)
      dosplit = (dist >= 0 &&
        (dist > eps_dist || delta_speed > eps_speed || outn + sp + 1 < minpts));
    else
      dosplit = (dist >= 0 &&
        (dist > eps_dist || outn + sp + 1 < minpts));
    if (dosplit)
      stack[++sp] = split;
    else
    {
      outlist[outn++] = stack[sp];
      p1 = stack[sp--];
    }
  }
  while (sp >= 0);

  /* Put list of retained points into order */
  qsort(outlist, outn, sizeof(int), int_cmp);
  /* Create new TSequence */
  const TInstant **instants = palloc(sizeof(TInstant *) * outn);
  for (i = 0; i < outn; i++)
    instants[i] = tsequence_inst_n(seq, outlist[i]);
  TSequence *result = tsequence_make(instants, outn,
    seq->period.lower_inc, seq->period.upper_inc,
    MOBDB_FLAGS_GET_LINEAR(seq->flags), NORMALIZE);
  pfree(instants);

  /* Only free if arrays are on heap */
  if (stack != stack_static)
    pfree(stack);
  if (outlist != outlist_static)
    pfree(outlist);

  return result;
}

/**
 * Simplifies the temporal sequence set point using a spatio-temporal
 * extension of the Douglas-Peucker line simplification algorithm.
 *
 * @param[in] ts Temporal point
 * @param[in] eps_dist Epsilon speed
 * @param[in] eps_speed Epsilon speed
 * @param[in] minpts Minimum number of points
 */
TSequenceSet *
tpointseqset_simplify(const TSequenceSet *ts, double eps_dist,
  double eps_speed, uint32_t minpts)
{
  const TSequence *seq;

  /* Singleton sequence set */
  if (ts->count == 1)
  {
    seq = tsequenceset_seq_n(ts, 0);
    TSequence *seq1 = tpointseq_simplify(seq, eps_dist, eps_speed, minpts);
    TSequenceSet *result = tsequence_to_tsequenceset(seq1);
    pfree(seq1);
    return result;
  }

  /* General case */
  TSequence **sequences = palloc(sizeof(TSequence *) * ts->count);
  for (int i = 0; i < ts->count; i++)
  {
    seq = tsequenceset_seq_n(ts, i);
    sequences[i] = tpointseq_simplify(seq, eps_dist, eps_speed, minpts);
  }
  return tsequenceset_make_free(sequences, ts->count, NORMALIZE);
}

Temporal *
tpoint_simplify_internal(Temporal *temp, double eps_dist,
  double eps_speed)
{
  Temporal *result;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT || temp->subtype == INSTANTSET ||
    ! MOBDB_FLAGS_GET_LINEAR(temp->flags))
    result = temporal_copy(temp);
  else if (temp->subtype == SEQUENCE)
    result = (Temporal *) tpointseq_simplify((TSequence *)temp,
      eps_dist, eps_speed, 2);
  else /* temp->subtype == SEQUENCESET */
    result = (Temporal *) tpointseqset_simplify((TSequenceSet *)temp,
      eps_dist, eps_speed, 2);
  return result;
}

PG_FUNCTION_INFO_V1(tpoint_simplify);
/**
 * Simplifies the temporal sequence (set) point using a spatio-temporal
 * extension of the Douglas-Peucker line simplification algorithm.
 */
Datum
tpoint_simplify(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  double eps_dist = PG_GETARG_FLOAT8(1);
  double eps_speed = PG_GETARG_FLOAT8(2);

  Temporal *result = tpoint_simplify_internal(temp, eps_dist, eps_speed);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_POINTER(result);
}

/*****************************************************************************
 * Mapbox Vector Tile functions for temporal points.
 *****************************************************************************/

/**
 * Returns a temporal point with consecutive equal points removed.
 * Equality test only on x and y dimensions of input.
 */
TInstantSet *
tpointinstset_remove_repeated_points(const TInstantSet *ti, double tolerance,
  int min_points)
{
  /* No-op on short inputs */
  if (ti->count <= min_points)
    return tinstantset_copy(ti);

  double tolsq = tolerance * tolerance;
  double dsq = FLT_MAX;

  const TInstant **instants = palloc(sizeof(TInstant *) * ti->count);
  instants[0] = tinstantset_inst_n(ti, 0);
  const POINT2D *last = datum_get_point2d_p(tinstant_value(instants[0]));
  int k = 1;
  for (int i = 1; i < ti->count; i++)
  {
    bool last_point = (i == ti->count - 1);
    const TInstant *inst = tinstantset_inst_n(ti, i);
    const POINT2D *pt = datum_get_point2d_p(tinstant_value(inst));

    /* Don't drop points if we are running short of points */
    if (ti->count - k > min_points + i)
    {
      if (tolerance > 0.0)
      {
        /* Only drop points that are within our tolerance */
        dsq = distance2d_sqr_pt_pt(last, pt);
        /* Allow any point but the last one to be dropped */
        if (!last_point && dsq <= tolsq)
          continue;
      }
      else
      {
        /* At tolerance zero, only skip exact dupes */
        if (FP_EQUALS(pt->x, last->x) && FP_EQUALS(pt->y, last->y))
          continue;
      }

      /* Got to last point, and it's not very different from
       * the point that preceded it. We want to keep the last
       * point, not the second-to-last one, so we pull our write
       * index back one value */
      if (last_point && k > 1 && tolerance > 0.0 && dsq <= tolsq)
      {
        k--;
      }
    }

    /* Save the point */
    instants[k++] = inst;
    last = pt;
  }
  /* Construct the result */
  TInstantSet *result = tinstantset_make(instants, ti->count, MERGE_NO);
  pfree(instants);
  return result;
}

/**
 * Returns a temporal point with consecutive equal points removed.
 * Equality test only on x and y dimensions of input.
 */
TSequence *
tpointseq_remove_repeated_points(const TSequence *seq, double tolerance,
  int min_points)
{
  /* No-op on short inputs */
  if (seq->count <= min_points)
    return tsequence_copy(seq);

  double tolsq = tolerance * tolerance;
  double dsq = FLT_MAX;

  const TInstant **instants = palloc(sizeof(TInstant *) * seq->count);
  instants[0] = tsequence_inst_n(seq, 0);
  const POINT2D *last = datum_get_point2d_p(tinstant_value(instants[0]));
  int k = 1;
  for (int i = 1; i < seq->count; i++)
  {
    bool last_point = (i == seq->count - 1);
    const TInstant *inst = tsequence_inst_n(seq, i);
    const POINT2D *pt = datum_get_point2d_p(tinstant_value(inst));

    /* Don't drop points if we are running short of points */
    if (seq->count - i > min_points - k)
    {
      if (tolerance > 0.0)
      {
        /* Only drop points that are within our tolerance */
        dsq = distance2d_sqr_pt_pt(last, pt);
        /* Allow any point but the last one to be dropped */
        if (!last_point && dsq <= tolsq)
          continue;
      }
      else
      {
        /* At tolerance zero, only skip exact dupes */
        if (FP_EQUALS(pt->x, last->x) && FP_EQUALS(pt->y, last->y))
          continue;
      }

      /* Got to last point, and it's not very different from
       * the point that preceded it. We want to keep the last
       * point, not the second-to-last one, so we pull our write
       * index back one value */
      if (last_point && k > 1 && tolerance > 0.0 && dsq <= tolsq)
      {
        k--;
      }
    }

    /* Save the point */
    instants[k++] = inst;
    last = pt;
  }
  /* Construct the result */
  TSequence *result = tsequence_make(instants, k, seq->period.lower_inc,
    seq->period.upper_inc, MOBDB_FLAGS_GET_LINEAR(seq->flags), NORMALIZE);
  pfree(instants);
  return result;
}

/**
 * Returns a temporal point with consecutive equal points removed.
 * Equality test only on x and y dimensions of input.
 */
TSequenceSet *
tpointseqset_remove_repeated_points(const TSequenceSet *ts, double tolerance,
  int min_points)
{
  const TSequence *seq;

  /* Singleton sequence set */
  if (ts->count == 1)
  {
    seq = tsequenceset_seq_n(ts, 0);
    TSequence *seq1 = tpointseq_remove_repeated_points(seq, tolerance,
      min_points);
    TSequenceSet *result = tsequence_to_tsequenceset(seq1);
    pfree(seq1);
    return result;
  }

  /* No-op on short inputs */
  if (ts->totalcount <= min_points)
    return tsequenceset_copy(ts);

  /* General case */
  TSequence **sequences = palloc(sizeof(TSequence *) * ts->count);
  int npoints = 0;
  for (int i = 0; i < ts->count; i++)
  {
    seq = tsequenceset_seq_n(ts, i);
    /* Don't drop sequences if we are running short of points */
    if (ts->totalcount - npoints > min_points)
    {
      /* Minimum number of points set to 2 */
      sequences[i] = tpointseq_remove_repeated_points(seq, tolerance, 2);
      npoints += sequences[i]->count;
    }
    else
    {
      /* Save the sequence */
      sequences[i] = tsequence_copy(seq);
    }
  }
  return tsequenceset_make_free(sequences, ts->count, NORMALIZE);
}

/**
 * Returns a temporal point with consecutive equal points removed.
 * Equality test only on x and y dimensions of input.
 */
Temporal *
tpoint_remove_repeated_points(const Temporal *temp, double tolerance,
  int min_points)
{
  Temporal *result;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT)
    result = (Temporal *) tinstant_copy((TInstant *) temp);
  else if (temp->subtype == INSTANTSET)
    result = (Temporal *) tpointinstset_remove_repeated_points((TInstantSet *) temp,
      tolerance, min_points);
  else if (temp->subtype == SEQUENCE)
    result = (Temporal *) tpointseq_remove_repeated_points((TSequence *) temp,
      tolerance, min_points);
  else /* temp->subtype == SEQUENCESET */
    result = (Temporal *) tpointseqset_remove_repeated_points((TSequenceSet *) temp,
      tolerance, min_points);
  return result;
}

/*****************************************************************************
 * Affine functions
 *****************************************************************************/

/**
 * Affine transform a temporal point.
 */
TInstant *
tpointinst_affine(TInstant *inst, const AFFINE *a)
{
  int srid = tpointinst_srid(inst);
  Datum value = tinstant_value(inst);
  double x, y;
  LWPOINT *lwpoint;
  if (MOBDB_FLAGS_GET_Z(inst->flags))
  {
    POINT3DZ p3d = datum_get_point3dz(value);
    x = p3d.x;
    y = p3d.y;
    double z = p3d.z;
    p3d.x = a->afac * x + a->bfac * y + a->cfac * z + a->xoff;
    p3d.y = a->dfac * x + a->efac * y + a->ffac * z + a->yoff;
    p3d.z = a->gfac * x + a->hfac * y + a->ifac * z + a->zoff;
    lwpoint = lwpoint_make3dz(srid, p3d.x, p3d.y, p3d.z);
  }
  else
  {
    POINT2D p2d = datum_get_point2d(value);
    x = p2d.x;
    y = p2d.y;
    p2d.x = a->afac * x + a->bfac * y + a->xoff;
    p2d.y = a->dfac * x + a->efac * y + a->yoff;
    lwpoint = lwpoint_make2d(srid, p2d.x, p2d.y);
  }
  GSERIALIZED *gs = geo_serialize((LWGEOM *) lwpoint);
  TInstant *result = tinstant_make(PointerGetDatum(gs), inst->t,
    type_oid(T_GEOMETRY));
  lwpoint_free(lwpoint);
  pfree(gs);
  return result;
}

/**
 * Affine transform a temporal point.
 */
TInstantSet *
tpointinstset_affine(TInstantSet *ti, const AFFINE *a)
{
  int srid = tpointinstset_srid(ti);
  TInstant **instants = palloc(sizeof(TInstant *) * ti->count);
  for (int i = 0; i < ti->count; i++)
  {
    const TInstant *inst = tinstantset_inst_n(ti, i);
    Datum value = tinstant_value(inst);
    double x, y;
    LWPOINT *lwpoint;
    if (MOBDB_FLAGS_GET_Z(ti->flags))
    {
      POINT3DZ p3d = datum_get_point3dz(value);
      x = p3d.x;
      y = p3d.y;
      double z = p3d.z;
      p3d.x = a->afac * x + a->bfac * y + a->cfac * z + a->xoff;
      p3d.y = a->dfac * x + a->efac * y + a->ffac * z + a->yoff;
      p3d.z = a->gfac * x + a->hfac * y + a->ifac * z + a->zoff;
      lwpoint = lwpoint_make3dz(srid, p3d.x, p3d.y, p3d.z);
    }
    else
    {
      POINT2D p2d = datum_get_point2d(value);
      x = p2d.x;
      y = p2d.y;
      p2d.x = a->afac * x + a->bfac * y + a->xoff;
      p2d.y = a->dfac * x + a->efac * y + a->yoff;
      lwpoint = lwpoint_make2d(srid, p2d.x, p2d.y);
    }
    GSERIALIZED *gs = geo_serialize((LWGEOM *) lwpoint);
    instants[i] = tinstant_make(PointerGetDatum(gs), inst->t,
      type_oid(T_GEOMETRY));
    lwpoint_free(lwpoint);
    pfree(gs);
  }
  TInstantSet *result = tinstantset_make_free(instants, ti->count, MERGE_NO);
  return result;
}

/**
 * Affine transform a temporal point.
 */
TSequence *
tpointseq_affine(const TSequence *seq, const AFFINE *a)
{
  int srid = tpointseq_srid(seq);
  TInstant **instants = palloc(sizeof(TInstant *) * seq->count);
  for (int i = 0; i < seq->count; i++)
  {
    const TInstant *inst = tsequence_inst_n(seq, i);
    Datum value = tinstant_value(inst);
    double x, y;
    LWPOINT *lwpoint;
    if (MOBDB_FLAGS_GET_Z(seq->flags))
    {
      POINT3DZ p3d = datum_get_point3dz(value);
      x = p3d.x;
      y = p3d.y;
      double z = p3d.z;
      p3d.x = a->afac * x + a->bfac * y + a->cfac * z + a->xoff;
      p3d.y = a->dfac * x + a->efac * y + a->ffac * z + a->yoff;
      p3d.z = a->gfac * x + a->hfac * y + a->ifac * z + a->zoff;
      lwpoint = lwpoint_make3dz(srid, p3d.x, p3d.y, p3d.z);
    }
    else
    {
      POINT2D p2d = datum_get_point2d(value);
      x = p2d.x;
      y = p2d.y;
      p2d.x = a->afac * x + a->bfac * y + a->xoff;
      p2d.y = a->dfac * x + a->efac * y + a->yoff;
      lwpoint = lwpoint_make2d(srid, p2d.x, p2d.y);
    }
    GSERIALIZED *gs = geo_serialize((LWGEOM *) lwpoint);
    instants[i] = tinstant_make(PointerGetDatum(gs), inst->t,
      type_oid(T_GEOMETRY));
    lwpoint_free(lwpoint);
    pfree(gs);
  }
  /* Construct the result */
  return tsequence_make_free(instants, seq->count, seq->period.lower_inc,
    seq->period.upper_inc, MOBDB_FLAGS_GET_LINEAR(seq->flags), NORMALIZE);
}

/**
 * Affine transform a temporal point.
 *
 * @param[in] ts Temporal point
 * @param[in] a Affine transformation
 */
TSequenceSet *
tpointseqset_affine(const TSequenceSet *ts, const AFFINE *a)
{
  /* Singleton sequence set */
  if (ts->count == 1)
  {
    TSequence *seq = tpointseq_affine(tsequenceset_seq_n(ts, 0), a);
    TSequenceSet *result = tsequence_to_tsequenceset(seq);
    pfree(seq);
    return result;
  }

  /* General case */
  TSequence **sequences = palloc(sizeof(TSequence *) * ts->count);
  for (int i = 0; i < ts->count; i++)
    sequences[i] = tpointseq_affine(tsequenceset_seq_n(ts, i), a);
  return tsequenceset_make_free(sequences, ts->count, NORMALIZE);
}

/**
 * Affine transform a temporal point.
 */
Temporal *
tpoint_affine(const Temporal *temp, const AFFINE *a)
{
  Temporal *result;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT)
    result = (Temporal *) tpointinst_affine((TInstant *) temp, a);
  else if (temp->subtype == INSTANTSET)
    result = (Temporal *) tpointinstset_affine((TInstantSet *) temp, a);
  else if (temp->subtype == SEQUENCE)
    result = (Temporal *) tpointseq_affine((TSequence *) temp, a);
  else /* temp->subtype == SEQUENCESET */
    result = (Temporal *) tpointseqset_affine((TSequenceSet *) temp, a);
  return result;
}

/*****************************************************************************
 * Grid functions
 *****************************************************************************/

/**
 * Stick a temporal point to the given grid specification.
 */
TInstant *
tpointinst_grid(const TInstant *inst, const gridspec *grid)
{
  bool has_z = MOBDB_FLAGS_GET_Z(inst->flags);
  if (grid->xsize == 0 && grid->ysize == 0 && (has_z ? grid->zsize == 0 : 1))
    return tinstant_copy(inst);

  int srid = tpointinst_srid(inst);
  Datum value = tinstant_value(inst);

  /* Read and round point */
  POINT4D p;
  datum_get_point4d(&p, value);
  double x = p.x;
  double y = p.y;
  double z = has_z ? p.z : 0;
  if (grid->xsize > 0)
    x = rint((p.x - grid->ipx) / grid->xsize) * grid->xsize + grid->ipx;
  if (grid->ysize > 0)
    y = rint((p.y - grid->ipy) / grid->ysize) * grid->ysize + grid->ipy;
  if (has_z && grid->zsize > 0)
    z = rint((p.z - grid->ipz) / grid->zsize) * grid->zsize + grid->ipz;

  /* Write rounded values into the next instant */
  LWPOINT *lwpoint = has_z ?
    lwpoint_make3dz(srid, x, y, z) : lwpoint_make2d(srid, x, y);
  GSERIALIZED *gs = geo_serialize((LWGEOM *) lwpoint);
  lwpoint_free(lwpoint);

  /* Construct the result */
  TInstant *result = tinstant_make(PointerGetDatum(gs), inst->t,
    type_oid(T_GEOMETRY));
  lwpoint_free(lwpoint);
  pfree(gs);
  return result;
}

/**
 * Stick a temporal point to the given grid specification.
 */
TInstantSet *
tpointinstset_grid(const TInstantSet *ti, const gridspec *grid)
{
  bool has_z = MOBDB_FLAGS_GET_Z(ti->flags);
  int srid = tpointinstset_srid(ti);
  TInstant **instants = palloc(sizeof(TInstant *) * ti->count);
  int k = 0;
  for (int i = 0; i < ti->count; i++)
  {
    POINT4D p, prev_p;
    const TInstant *inst = tinstantset_inst_n(ti, i);
    Datum value = tinstant_value(inst);

    /* Read and round point */
    datum_get_point4d(&p, value);
    double x = p.x;
    double y = p.y;
    double z = has_z ? p.z : 0;
    if (grid->xsize > 0)
      x = rint((p.x - grid->ipx) / grid->xsize) * grid->xsize + grid->ipx;
    if (grid->ysize > 0)
      y = rint((p.y - grid->ipy) / grid->ysize) * grid->ysize + grid->ipy;
    if (has_z && grid->zsize > 0)
      z = rint((p.z - grid->ipz) / grid->zsize) * grid->zsize + grid->ipz;

    /* Skip duplicates */
    if (i > 1 && prev_p.x == x && prev_p.y == y && (has_z ? prev_p.z == z : 1))
      continue;

    /* Write rounded values into the next instant */
    LWPOINT *lwpoint = has_z ?
      lwpoint_make3dz(srid, x, y, z) : lwpoint_make2d(srid, x, y);
    GSERIALIZED *gs = geo_serialize((LWGEOM *) lwpoint);
    instants[k++] = tinstant_make(PointerGetDatum(gs), inst->t,
      type_oid(T_GEOMETRY));
    lwpoint_free(lwpoint);
    pfree(gs);
    memcpy(&prev_p, &p, sizeof(POINT4D));
  }
  /* Construct the result */
  return tinstantset_make_free(instants, k, MERGE_NO);
}

/**
 * Stick a temporal point to the given grid specification.
 */
TSequence *
tpointseq_grid(const TSequence *seq, const gridspec *grid)
{
  bool has_z = MOBDB_FLAGS_GET_Z(seq->flags);
  int srid = tpointseq_srid(seq);
  TInstant **instants = palloc(sizeof(TInstant *) * seq->count);
  int k = 0;
  for (int i = 0; i < seq->count; i++)
  {
    POINT4D p, prev_p;
    const TInstant *inst = tsequence_inst_n(seq, i);
    Datum value = tinstant_value(inst);

    /* Read and round point */
    datum_get_point4d(&p, value);
    double x = p.x;
    double y = p.y;
    double z = has_z ? p.z : 0;
    if (grid->xsize > 0)
      x = rint((p.x - grid->ipx) / grid->xsize) * grid->xsize + grid->ipx;
    if (grid->ysize > 0)
      y = rint((p.y - grid->ipy) / grid->ysize) * grid->ysize + grid->ipy;
    if (has_z && grid->zsize > 0)
      z = rint((p.z - grid->ipz) / grid->zsize) * grid->zsize + grid->ipz;

    /* Skip duplicates */
    if (i > 1 && prev_p.x == x && prev_p.y == y && (has_z ? prev_p.z == z : 1))
      continue;

    /* Write rounded values into the next instant */
    LWPOINT *lwpoint = has_z ?
      lwpoint_make3dz(srid, x, y, z) : lwpoint_make2d(srid, x, y);
    GSERIALIZED *gs = geo_serialize((LWGEOM *) lwpoint);
    instants[k++] = tinstant_make(PointerGetDatum(gs), inst->t,
      type_oid(T_GEOMETRY));
    lwpoint_free(lwpoint);
    pfree(gs);
    memcpy(&prev_p, &p, sizeof(POINT4D));
  }
  /* Construct the result */
  return tsequence_make_free(instants, k, k > 1 ? seq->period.lower_inc : true,
    k > 1 ? seq->period.upper_inc : true, MOBDB_FLAGS_GET_LINEAR(seq->flags),
    NORMALIZE);
}

/**
 * Stick a temporal point to the given grid specification.
 */
TSequenceSet *
tpointseqset_grid(const TSequenceSet *ts, const gridspec *grid)
{
  /* Singleton sequence set */
  if (ts->count == 1)
  {
    TSequence *seq = tpointseq_grid(tsequenceset_seq_n(ts, 0), grid);
    TSequenceSet *result = tsequence_to_tsequenceset(seq);
    pfree(seq);
    return result;
  }

  /* General case */
  TSequence **sequences = palloc(sizeof(TSequence *) * ts->count);
  for (int i = 0; i < ts->count; i++)
    sequences[i] = tpointseq_grid(tsequenceset_seq_n(ts, i), grid);
  return tsequenceset_make_free(sequences, ts->count, NORMALIZE);
}

/**
 * Stick a temporal point to the given grid specification.
 *
 * Only the x, y, and possible z dimensions are gridded, the timestamp is
 * kept unmodified. Two consecutive instants falling on the same grid cell
 * are collapsed into one single instant.
 */
Temporal *
tpoint_grid(const Temporal *temp, const gridspec *grid)
{
  Temporal *result;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT)
    result = (Temporal *) tpointinst_grid((TInstant *) temp, grid);
  else if (temp->subtype == INSTANTSET)
    result = (Temporal *) tpointinstset_grid((TInstantSet *) temp, grid);
  else if (temp->subtype == SEQUENCE)
    result = (Temporal *) tpointseq_grid((TSequence *) temp, grid);
  else /* temp->subtype == SEQUENCESET */
    result = (Temporal *) tpointseqset_grid((TSequenceSet *) temp, grid);
  return result;
}

/*****************************************************************************/

/**
 * Transform a temporal point into vector tile coordinate space.
 *
 * @param[in] tpoint Temporal point
 * @param[in] box Geometric bounds of the tile contents without buffer
 * @param[in] extent Tile extent in tile coordinate space
 * @param[in] buffer Buffer distance in tile coordinate space
 * @param[in] clip_geom True if temporal point should be clipped
 */
Temporal *
tpoint_mvt(const Temporal *tpoint, const STBOX *box, uint32_t extent,
  uint32_t buffer, bool clip_geom)
{
  AFFINE affine = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  gridspec grid = {0, 0, 0, 0, 1, 1, 0, 0};
  double width = box->xmax - box->xmin;
  double height = box->ymax - box->ymin;
  double resx, resy, res, fx, fy;

  resx = width / extent;
  resy = height / extent;
  res = (resx < resy ? resx : resy)/2;
  fx = extent / width;
  fy = -(extent / height);

  /* Remove all non-essential points (under the output resolution) */
  Temporal *tpoint1 = tpoint_remove_repeated_points(tpoint, res, 2);
  /* Epsilon speed is not taken into account, i.e., parameter set to 0 */
  Temporal *tpoint2 = tpoint_simplify_internal(tpoint1, res, 0);

  /* If geometry has disappeared, you're done */
  if (tpoint2 == NULL)
    return NULL;

  /* Transform to tile coordinate space */
  affine.afac = fx;
  affine.efac = fy;
  affine.ifac = 1;
  affine.xoff = -box->xmin * fx;
  affine.yoff = -box->ymax * fy;
  Temporal *tpoint3 = tpoint_affine(tpoint2, &affine);

  /* Snap to integer precision, removing duplicate points */
  Temporal *tpoint4 = tpoint_grid(tpoint3, &grid);

  pfree(tpoint1); pfree(tpoint2); pfree(tpoint3);
  return tpoint4;
}

/*****************************************************************************/

/**
 * Decouple the points and the timestamps of a temporal point.
 *
 * @note The function does not remove consecutive points/instants that are equal.
 * @param[in] inst Temporal point
 * @param[out] timesarr Array of timestamps
 */
Datum
tpointinst_decouple(const TInstant *inst, ArrayType **timesarr)
{
  *timesarr = timestamparr_to_array(&inst->t, 1);
  return tinstant_value_copy(inst);
}

/**
 * Decouple the points and the timestamps of a temporal point.
 *
 * @note The function does not remove consecutive points/instants that are equal.
 * @param[in] ti Temporal point
 * @param[out] timesarr Array of timestamps
 */
Datum
tpointinstset_decouple(const TInstantSet *ti, ArrayType **timesarr)
{
  /* Instantaneous sequence */
  if (ti->count == 1)
    return tpointinst_decouple(tinstantset_inst_n(ti, 0), timesarr);

  /* General case */
  LWGEOM **points = palloc(sizeof(LWGEOM *) * ti->count);
  TimestampTz *times = palloc(sizeof(TimestampTz) * ti->count);
  for (int i = 0; i < ti->count; i++)
  {
    const TInstant *inst = tinstantset_inst_n(ti, i);
    Datum value = tinstant_value(inst);
    GSERIALIZED *gs = (GSERIALIZED *) DatumGetPointer(value);
    points[i] = lwgeom_from_gserialized(gs);
    times[i] = inst->t;
  }
  LWGEOM *lwgeom = lwpointarr_make_trajectory(points, ti->count,
    MOBDB_FLAGS_GET_LINEAR(ti->flags));
  Datum result = PointerGetDatum(geo_serialize(lwgeom));
  pfree(lwgeom);
  *timesarr = timestamparr_to_array(times, ti->count);
  for (int i = 0; i < ti->count; i++)
    lwpoint_free((LWPOINT *) points[i]);
  pfree(points);
  pfree(times);
  return result;
}

/**
 * Decouple the points and the timestamps of a temporal point.
 *
 * @note The function does not remove consecutive points/instants that are equal.
 * @param[in] seq Temporal point
 * @param[out] times Array of timestamps
 */
LWGEOM *
tpointseq_decouple1(const TSequence *seq, TimestampTz *times)
{
  /* General case */
  LWGEOM **points = palloc(sizeof(LWGEOM *) * seq->count);
  for (int i = 0; i < seq->count; i++)
  {
    const TInstant *inst = tsequence_inst_n(seq, i);
    Datum value = tinstant_value(inst);
    GSERIALIZED *gs = (GSERIALIZED *) DatumGetPointer(value);
    points[i] = lwgeom_from_gserialized(gs);
    times[i] = inst->t;
  }
  LWGEOM *result = lwpointarr_make_trajectory(points, seq->count,
    MOBDB_FLAGS_GET_LINEAR(seq->flags));
  for (int i = 0; i < seq->count; i++)
    lwpoint_free((LWPOINT *) points[i]);
  pfree(points);
  return result;
}

/**
 * Decouple the points and the timestamps of a temporal point.
 *
 * @note The function does not remove consecutive points/instants that are equal.
 * @param[in] seq Temporal point
 * @param[out] timesarr Array of timestamps
 */
Datum
tpointseq_decouple(const TSequence *seq, ArrayType **timesarr)
{
  TimestampTz *times = palloc(sizeof(TimestampTz) * seq->count);
  LWGEOM *lwgeom = tpointseq_decouple1(seq, times);
  Datum result = PointerGetDatum(geo_serialize(lwgeom));
  pfree(lwgeom);
  *timesarr = timestamparr_to_array(times, seq->count);
  pfree(times);
  return result;
}

/**
 * Decouple the points and the timestamps of a temporal point.
 *
 * @note The function does not remove consecutive points/instants that are equal.
 * @param[in] ts Temporal point
 * @param[out] timesarr Array of timestamps
 */
Datum
tpointseqset_decouple(const TSequenceSet *ts, ArrayType **timesarr)
{
  /* General case */
  uint32_t colltype = 0;
  LWGEOM **geoms = palloc(sizeof(LWGEOM *) * ts->count);
  TimestampTz *times = palloc(sizeof(TimestampTz) * ts->totalcount);
  int k = 0;
  for (int i = 0; i < ts->count; i++)
  {
    const TSequence *seq = tsequenceset_seq_n(ts, i);
    geoms[i] = tpointseq_decouple1(seq, &times[k]);
    k += seq->count;
    /* If output type not initialized make geom type as output type */
    if (! colltype)
      colltype = lwtype_get_collectiontype(geoms[i]->type);
    /* If geom type is not compatible with current output type
     * make output type a collection */
    else if (colltype != COLLECTIONTYPE &&
      lwtype_get_collectiontype(geoms[i]->type) != colltype)
      colltype = COLLECTIONTYPE;
  }
  LWGEOM *coll = (LWGEOM *) lwcollection_construct((uint8_t) colltype,
    geoms[0]->srid, NULL, (uint32_t) ts->count, geoms);
  Datum result = PointerGetDatum(geo_serialize(coll));
  *timesarr = timestamparr_to_array(times, ts->totalcount);
  /* We cannot lwgeom_free(geoms[i] or lwgeom_free(coll) */
  pfree(geoms);
  pfree(times);
  return result;
}

/**
 * Decouple the points and the timestamps of a temporal point.
 *
 * @param[in] temp Temporal point
 * @param[out] timesarr Array of timestamps
 */
Datum
tpoint_decouple(const Temporal *temp, ArrayType **timesarr)
{
  Datum result;
  ensure_valid_tempsubtype(temp->subtype);
  if (temp->subtype == INSTANT)
    result = tpointinst_decouple((TInstant *) temp, timesarr);
  else if (temp->subtype == INSTANTSET)
    result = tpointinstset_decouple((TInstantSet *) temp, timesarr);
  else if (temp->subtype == SEQUENCE)
    result = tpointseq_decouple((TSequence *) temp, timesarr);
  else /* temp->subtype == SEQUENCESET */
    result = tpointseqset_decouple((TSequenceSet *) temp, timesarr);
  return result;
}

/*****************************************************************************/

PG_FUNCTION_INFO_V1(AsMVTGeom);
/**
 * Transform the temporal point to Mapbox Vector Tile format
 */
Datum
AsMVTGeom(PG_FUNCTION_ARGS)
{
  Temporal *temp = PG_GETARG_TEMPORAL(0);
  STBOX *bounds = PG_GETARG_STBOX_P(1);
  if (bounds->xmax - bounds->xmin <= 0 || bounds->ymax - bounds->ymin <= 0)
  {
    elog(ERROR, "%s: Geometric bounds are too small", __func__);
    PG_RETURN_NULL();
  }
  int32_t extent = PG_GETARG_INT32(2);
  if (extent <= 0)
  {
    elog(ERROR, "%s: Extent must be greater than 0", __func__);
    PG_RETURN_NULL();
  }
  int32_t buffer = PG_GETARG_INT32(3);
  bool clip_geom = PG_GETARG_BOOL(4);

  /* Clip temporal point immediately, to reduce the number of instants to
   * be processed. In PostGIS this is done at the last step */

  Temporal *temp1;
  if (clip_geom)
  {
    temp1 = tpoint_at_stbox_internal(temp, bounds);
    if (temp1 == NULL)
    {
      PG_FREE_IF_COPY(temp, 0);
      PG_RETURN_NULL();
    }
  }
  else
    temp1 = temp;

  /* Bounding box test to drop geometries smaller than the resolution */
  STBOX box;
  temporal_bbox(&box, temp1);
  double tpoint_width = box.xmax - box.xmin;
  double tpoint_height = box.ymax - box.ymin;
  /* We use half of the square height and width as limit: We use this
   * and not area so it works properly with lines */
  double bounds_width = ((bounds->xmax - bounds->xmin) / extent) / 2.0;
  double bounds_height = ((bounds->ymax - bounds->ymin) / extent) / 2.0;
  if (tpoint_width < bounds_width && tpoint_height < bounds_height)
  {
    if (clip_geom)
      pfree(temp1);
    PG_FREE_IF_COPY(temp, 0);
    PG_RETURN_NULL();
  }

  Temporal *temp2 = tpoint_mvt(temp1, bounds, extent, buffer, clip_geom);
  if (temp2 == NULL)
  {
    if (clip_geom)
      pfree(temp1);
    PG_FREE_IF_COPY(temp, 0);
    PG_RETURN_NULL();
  }

  /* Decouple the geometry and the timestamps */
  ArrayType *timesarr;
  Datum geom = tpoint_decouple(temp2, &timesarr);

  /* Build a tuple description for the function output */
  TupleDesc resultTupleDesc;
  get_call_result_type(fcinfo, NULL, &resultTupleDesc);
  BlessTupleDesc(resultTupleDesc);

  /* Construct the result */
  HeapTuple resultTuple;
  bool result_is_null[2] = {0,0}; /* needed to say no value is null */
  Datum result_values[2]; /* used to construct the composite return value */
  Datum result; /* the actual composite return value */
  /* Store geometry */
  result_values[0] = geom;
  /* Store timestamp array */
  result_values[1] = PointerGetDatum(timesarr);
  /* Form tuple and return */
  resultTuple = heap_form_tuple(resultTupleDesc, result_values, result_is_null);
  result = HeapTupleGetDatum(resultTuple);

  if (clip_geom)
    pfree(temp1);
  pfree(temp2);
  PG_FREE_IF_COPY(temp, 0);
  PG_RETURN_DATUM(result);
}

/*****************************************************************************/
