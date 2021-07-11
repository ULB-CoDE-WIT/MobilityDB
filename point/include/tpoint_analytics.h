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
 * @file tpoint_analytics.h
 * Analytics functions for temporal points.
 */

#ifndef __TPOINT_ANALYTICS_H__
#define __TPOINT_ANALYTICS_H__

#include <postgres.h>
#include <fmgr.h>

#include "temporaltypes.h"

/*****************************************************************************/

/* Convert a temporal point into a PostGIS trajectory geometry/geography */

extern Datum tpoint_to_geo(PG_FUNCTION_ARGS);
extern Datum geo_to_tpoint(PG_FUNCTION_ARGS);

/* Convert a temporal point and a temporal float into a PostGIS geometry/geography */

extern Datum point_to_geo_measure(PG_FUNCTION_ARGS);

/* Simple Douglas-Peucker-like value simplification for temporal floats and
 * temporal points. */

extern Datum tfloat_simplify(PG_FUNCTION_ARGS);
extern Datum tpoint_simplify(PG_FUNCTION_ARGS);

extern Temporal *tpoint_simplify_internal(Temporal *temp, double eps_dist,
  double eps_speed);

/* Transform the temporal point to Mapbox Vector Tile format */

extern Datum AsMVT(PG_FUNCTION_ARGS);

/*****************************************************************************/

#endif
