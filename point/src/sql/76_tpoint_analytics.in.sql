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

/*
 * tpoint_analytics.sql
 * Analytic functions for temporal points.
 */

/*****************************************************************************/
-- There are two versions of the functions since the single-argument version
-- is required for defining the casting

CREATE FUNCTION asGeometry(tgeompoint)
  RETURNS geometry
  AS 'MODULE_PATHNAME', 'tpoint_to_geo'
  LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION asGeometry(tgeompoint, boolean DEFAULT FALSE)
  RETURNS geometry
  AS 'MODULE_PATHNAME', 'tpoint_to_geo'
  LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE CAST (tgeompoint AS geometry) WITH FUNCTION asGeometry(tgeompoint);

CREATE FUNCTION asGeography(tgeogpoint)
  RETURNS geography
  AS 'MODULE_PATHNAME', 'tpoint_to_geo'
  LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE FUNCTION asGeography(tgeogpoint, boolean DEFAULT FALSE)
  RETURNS geography
  AS 'MODULE_PATHNAME', 'tpoint_to_geo'
  LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE CAST (tgeogpoint AS geography) WITH FUNCTION asGeography(tgeogpoint);

CREATE FUNCTION tgeompoint(geometry)
  RETURNS tgeompoint
  AS 'MODULE_PATHNAME', 'geo_to_tpoint'
  LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE CAST (geometry AS tgeompoint) WITH FUNCTION tgeompoint(geometry);

CREATE FUNCTION tgeogpoint(geography)
  RETURNS tgeogpoint
  AS 'MODULE_PATHNAME', 'geo_to_tpoint'
  LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE CAST (geography AS tgeogpoint) WITH FUNCTION tgeogpoint(geography);

/*****************************************************************************/

CREATE FUNCTION geoMeasure(tgeompoint, tfloat, boolean DEFAULT FALSE)
RETURNS geometry
AS 'MODULE_PATHNAME', 'tpoint_to_geo_measure'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION geoMeasure(tgeogpoint, tfloat, boolean DEFAULT FALSE)
RETURNS geography
AS 'MODULE_PATHNAME', 'tpoint_to_geo_measure'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

/*****************************************************************************/

CREATE OR REPLACE FUNCTION simplify(tfloat, float8)
RETURNS tfloat
AS 'MODULE_PATHNAME', 'tfloat_simplify'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE OR REPLACE FUNCTION simplify(tgeompoint, float8, float8 DEFAULT -1.0)
RETURNS tgeompoint
AS 'MODULE_PATHNAME', 'tpoint_simplify'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE TYPE geom_times AS (
  geom geometry,
  times timestamptz[]
);

CREATE OR REPLACE FUNCTION AsMVTGeom(tpoint tgeompoint, bounds stbox,
  extent int4 DEFAULT 4096, buffer int4 DEFAULT 256, clip_geom bool DEFAULT TRUE)
-- RETURNS tgeompoint
RETURNS geom_times
AS 'MODULE_PATHNAME','AsMVTGeom'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

/*****************************************************************************/
