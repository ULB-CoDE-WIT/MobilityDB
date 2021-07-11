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
 * Bucket and tile functions for temporal types.
 * The time bucket function are inspired from TimescaleDB.
 * https://docs.timescale.com/latest/api#time_bucket
 */

/*****************************************************************************
 * Bucket functions
 *****************************************************************************/

CREATE TYPE number_intrange AS (
  index integer,
  range intrange
);
CREATE TYPE number_floatrange AS (
  index integer,
  range floatrange
);

CREATE OR REPLACE FUNCTION bucketList(bounds intrange, size integer,
  origin integer DEFAULT 0)
  RETURNS SETOF number_intrange
  AS 'MODULE_PATHNAME', 'range_bucket_list'
  LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE OR REPLACE FUNCTION bucketList(bounds floatrange, size float,
  origin float DEFAULT 0.0)
  RETURNS SETOF number_floatrange
  AS 'MODULE_PATHNAME', 'range_bucket_list'
  LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE OR REPLACE FUNCTION valueBucket("value" integer, size integer,
  origin integer DEFAULT 0)
  RETURNS integer
  AS 'MODULE_PATHNAME', 'number_bucket'
  LANGUAGE C IMMUTABLE PARALLEL SAFE STRICT;
CREATE OR REPLACE FUNCTION valueBucket("value" float, size float,
  origin float DEFAULT '0.0')
  RETURNS float
  AS 'MODULE_PATHNAME', 'number_bucket'
  LANGUAGE C IMMUTABLE PARALLEL SAFE STRICT;

CREATE OR REPLACE FUNCTION rangeBucket(value integer, size integer,
  origin integer DEFAULT 0)
  RETURNS intrange
  AS 'MODULE_PATHNAME', 'range_bucket'
  LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;
CREATE OR REPLACE FUNCTION rangeBucket(value float, size float,
  origin float DEFAULT 0.0)
  RETURNS floatrange
  AS 'MODULE_PATHNAME', 'range_bucket'
  LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

/*****************************************************************************/

CREATE TYPE time_period AS (
  index integer,
  period period
);

CREATE OR REPLACE FUNCTION bucketList(period, interval,
  timestamptz DEFAULT '2000-01-03')
  RETURNS SETOF time_period
  AS 'MODULE_PATHNAME', 'period_bucket_list'
  LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

-- bucketing of timestamptz happens at UTC time
CREATE OR REPLACE FUNCTION timeBucket("time" timestamptz, duration interval,
  origin timestamptz DEFAULT '2000-01-03')
  RETURNS timestamptz
  AS 'MODULE_PATHNAME', 'timestamptz_bucket'
  LANGUAGE C IMMUTABLE PARALLEL SAFE STRICT;

-- If an interval is given as the third argument, the bucket alignment is offset by the interval.
-- CREATE OR REPLACE FUNCTION timeBucket(ts timestamptz, size interval, "offset" interval)
  -- RETURNS timestamptz
  -- LANGUAGE SQL IMMUTABLE PARALLEL SAFE STRICT AS
-- $BODY$
    -- SELECT @extschema@.timeBucket(ts-"offset", size)+"offset";
-- $BODY$;

CREATE OR REPLACE FUNCTION periodBucket("time" timestamptz, duration interval,
  origin timestamptz DEFAULT '2000-01-03')
  RETURNS period
  AS 'MODULE_PATHNAME', 'period_bucket'
  LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

/*****************************************************************************
 * Multidimensional tiling
 *****************************************************************************/

CREATE TYPE index_tbox AS (
  index integer,
  box tbox
);

CREATE OR REPLACE FUNCTION multidimGrid(bounds tbox, size float,
  duration interval, vorigin float DEFAULT 0.0,
  torigin timestamptz DEFAULT '2000-01-03')
  RETURNS SETOF index_tbox
  AS 'MODULE_PATHNAME', 'tbox_multidim_grid'
  LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE OR REPLACE FUNCTION multidimTile("value" float, "time" timestamptz,
  size float, duration interval, vorigin float DEFAULT 0.0,
  torigin timestamptz DEFAULT '2000-01-03')
  RETURNS tbox
  AS 'MODULE_PATHNAME', 'tbox_multidim_tile'
  LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

/*****************************************************************************
 * Splitting
 *****************************************************************************/

CREATE TYPE int_tint AS (
  number integer,
  tnumber tint
);
CREATE TYPE float_tfloat AS (
  number float,
  tnumber tfloat
);

CREATE OR REPLACE FUNCTION valueSplit(tint, size integer,
    origin integer DEFAULT 0)
  RETURNS SETOF int_tint
  AS 'MODULE_PATHNAME', 'tnumber_value_split'
  LANGUAGE C IMMUTABLE PARALLEL SAFE STRICT;
CREATE OR REPLACE FUNCTION valueSplit(tfloat, size float,
    origin float DEFAULT 0.0)
  RETURNS SETOF float_tfloat
  AS 'MODULE_PATHNAME', 'tnumber_value_split'
  LANGUAGE C IMMUTABLE PARALLEL SAFE STRICT;

/*****************************************************************************/

CREATE TYPE time_tbool AS (
  time timestamptz,
  temp tbool
);
CREATE TYPE time_tint AS (
  time timestamptz,
  temp tint
);
CREATE TYPE time_tfloat AS (
  time timestamptz,
  temp tfloat
);
CREATE TYPE time_ttext AS (
  time timestamptz,
  temp ttext
);

CREATE OR REPLACE FUNCTION timeSplit(tbool, size interval,
    origin timestamptz DEFAULT '2000-01-03')
  RETURNS SETOF time_tbool
  AS 'MODULE_PATHNAME', 'temporal_time_split'
  LANGUAGE C IMMUTABLE PARALLEL SAFE STRICT;
CREATE OR REPLACE FUNCTION timeSplit(tint, size interval,
    origin timestamptz DEFAULT '2000-01-03')
  RETURNS SETOF time_tint
  AS 'MODULE_PATHNAME', 'temporal_time_split'
  LANGUAGE C IMMUTABLE PARALLEL SAFE STRICT;
CREATE OR REPLACE FUNCTION timeSplit(tfloat, size interval,
    origin timestamptz DEFAULT '2000-01-03')
  RETURNS SETOF time_tfloat
  AS 'MODULE_PATHNAME', 'temporal_time_split'
  LANGUAGE C IMMUTABLE PARALLEL SAFE STRICT;
CREATE OR REPLACE FUNCTION timeSplit(ttext, size interval,
    origin timestamptz DEFAULT '2000-01-03')
  RETURNS SETOF time_ttext
  AS 'MODULE_PATHNAME', 'temporal_time_split'
  LANGUAGE C IMMUTABLE PARALLEL SAFE STRICT;

/*****************************************************************************/

CREATE TYPE int_time_tint AS (
  number integer,
  time timestamptz,
  tnumber tint
);
CREATE TYPE float_time_tfloat AS (
  number float,
  time timestamptz,
  tnumber tfloat
);

CREATE OR REPLACE FUNCTION valueTimeSplit(tint, size integer, duration interval,
    vorigin integer DEFAULT 0, torigin timestamptz DEFAULT '2000-01-03')
  RETURNS SETOF int_time_tint
  AS 'MODULE_PATHNAME', 'tnumber_value_time_split'
  LANGUAGE C IMMUTABLE PARALLEL SAFE STRICT;
CREATE OR REPLACE FUNCTION valueTimeSplit(tfloat, size float, duration interval,
    vorigin float DEFAULT 0.0, torigin timestamptz DEFAULT '2000-01-03')
  RETURNS SETOF float_time_tfloat
  AS 'MODULE_PATHNAME', 'tnumber_value_time_split'
  LANGUAGE C IMMUTABLE PARALLEL SAFE STRICT;

/*****************************************************************************/


