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
 * @file temporal_tile.c
 * Bucket and tile functions for temporal types.
 * The time bucket functions are inspired from TimescaleDB.
 * https://docs.timescale.com/latest/api#time_bucket
 */

#include <postgres.h>
#include <assert.h>
#include <float.h>
#include <funcapi.h>
#include <utils/datetime.h>

#include "temporal_tile.h"
#include "oidcache.h"
#include "period.h"
#include "periodset.h"
#include "timeops.h"
#include "rangetypes_ext.h"
#include "temporal.h"
#include "temporal_util.h"

/*****************************************************************************
 * Number bucket functions
 *****************************************************************************/

/**
 * Return the initial value of the bucket in which an integer value falls.
 *
 * @param[in] value Input value
 * @param[in] width Width of the buckets
 * @param[in] origin Origin of the buckets
 */
static int
int_bucket_internal(int value, int width, int origin)
{
  assert(width > 0);
  if (value == PG_INT32_MIN || value == PG_INT32_MAX)
    return value;
  /* origin = origin % width, but use FMODULO */
  origin = origin % width;
  if ((origin > 0 && value < PG_INT32_MIN + origin) ||
    (origin < 0 && value > PG_INT32_MAX + origin))
    ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
      errmsg("integer value out of range")));

  value -= origin;
  int result = value / width;
  if (result < 0)
  {
    /*
     * need to subtract another width if remainder < 0 this only happens
     * if value is negative to begin with and there is a remainder
     * after division. Need to subtract another width since division
     * truncates toward 0 in C99.
     */
    result = (result * width) - width;
  }
  else
    result *= width;
  result += origin;
  return result;
}

/**
 * Return the initial value of the bucket in which a float value falls.
 *
 * @param[in] value Input value
 * @param[in] width Width of the buckets
 * @param[in] origin Origin of the buckets
 */
double
float_bucket_internal(double value, double width, double origin)
{
  assert(width > 0.0);
  if (value == -1 * DBL_MAX || value == DBL_MAX)
    return value;
  double result;
  /* origin = origin % width, but use FMODULO */
  FMODULO(origin, result, width);
  if ((origin > 0 && value < -1 * DBL_MAX + origin) ||
    (origin < 0 && value > DBL_MAX + origin))
    ereport(ERROR,
        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
         errmsg("value out of range")));

  value -= origin;
  /* result = (value / width) * width */
  FMODULO(value, result, width);
  if (result < 0)
  {
    /*
     * need to subtract another width if remainder < 0 this only happens
     * if value is negative to begin with and there is a remainder
     * after division. Need to subtract another width since division
     * truncates toward 0 in C99.
     */
    result = (result * width) - width;
  }
  else
    result *= width;
  result += origin;
  return result;
}

/**
 * Return the initial value of the bucket in which a number value falls.
 *
 * @param[in] value Input value
 * @param[in] width Width of the buckets
 * @param[in] origin Origin of the buckets
 * @param[in] type Oid of the values
 */
static Datum
number_bucket_internal(Datum value, Datum width, Datum origin, Oid type)
{
  ensure_tnumber_base_type(type);
  if (type == INT4OID)
    return Int32GetDatum(int_bucket_internal(DatumGetInt32(value),
      DatumGetInt32(width), DatumGetInt32(origin)));
  else
    return Float8GetDatum(float_bucket_internal(DatumGetFloat8(value),
      DatumGetFloat8(width), DatumGetFloat8(origin)));
}

PG_FUNCTION_INFO_V1(number_bucket);
/**
 * Return the initial value of the bucket in which an integer value falls.
 */
PGDLLEXPORT Datum
number_bucket(PG_FUNCTION_ARGS)
{
  Datum value = PG_GETARG_DATUM(0);
  Datum width = PG_GETARG_DATUM(1);
  Datum origin = PG_GETARG_DATUM(2);

  Oid type = get_fn_expr_argtype(fcinfo->flinfo, 0);
  ensure_positive_datum(width, type);
  Datum result = number_bucket_internal(value, width, origin, type);
  PG_RETURN_DATUM(result);
}

/*****************************************************************************
 * Timestamp bucket functions
 *****************************************************************************/

/**
 * Return the initial timestamp of the bucket in which a timestamp falls.
 *
 * @param[in] timestamp Input timestamp
 * @param[in] tunits Size of the time buckets in PostgreSQL time units
 * @param[in] torigin Origin of the buckets
 */
TimestampTz
timestamptz_bucket_internal(TimestampTz timestamp, int64 tunits,
  TimestampTz torigin)
{
  TimestampTz result;
  /* torigin = torigin % tunits, but use TMODULO */
  TMODULO(torigin, result, tunits);

  if ((torigin > 0 && timestamp < DT_NOBEGIN + torigin) ||
    (torigin < 0 && timestamp > DT_NOEND + torigin))
    ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
      errmsg("timestamp out of range")));
  timestamp -= torigin;

  /* result = (timestamp / tunits) * tunits */
  TMODULO(timestamp, result, tunits);
  if (timestamp < 0)
  {
    /*
     * need to subtract another tunits if remainder < 0 this only happens
     * if timestamp is negative to begin with and there is a remainder
     * after division. Need to subtract another tunits since division
     * truncates toward 0 in C99.
     */
    result = (result * tunits) - tunits;
  }
  else
    result *= tunits;
  result += torigin;
  return result;
}

PG_FUNCTION_INFO_V1(timestamptz_bucket);
/**
 * Return the initial timestamp of the bucket in which a timestamp falls.
 */
PGDLLEXPORT Datum
timestamptz_bucket(PG_FUNCTION_ARGS)
{
  TimestampTz timestamp = PG_GETARG_TIMESTAMPTZ(0);
  if (TIMESTAMP_NOT_FINITE(timestamp))
    PG_RETURN_TIMESTAMPTZ(timestamp);
  Interval *duration = PG_GETARG_INTERVAL_P(1);
  ensure_valid_duration(duration);
  int64 tunits = get_interval_units(duration);
  TimestampTz origin = PG_GETARG_TIMESTAMPTZ(2);

  TimestampTz result = timestamptz_bucket_internal(timestamp, tunits, origin);
  PG_RETURN_TIMESTAMPTZ(result);
}

/*****************************************************************************
 * Range bucket functions
 *****************************************************************************/

/**
 * Generate an integer or float range bucket from a bucket list
 *
 * @param[in] value Start value of the bucket
 * @param[in] width Width of the buckets
 * @param[in] origin Origin of the buckets
 * @param[in] type Oid of the data type for width and origin
 */
static RangeType *
range_bucket_get(Datum value, Datum width, Oid type)
{
  Datum lower = value;
  Datum upper = datum_add(value, width, type, type);
  return range_make(lower, upper, true, false, type);
}

/**
 * Create the initial state that persists across multiple calls of the function
 *
 * @param[in] temp Temporal number to split
 * @param[in] r Bounds for generating the bucket list
 * @param[in] width Width of the buckets
 * @param[in] origin Origin of the buckets
 *
 * @pre The width argument must be greater to 0.
 * @note The first argument is NULL when generating the bucket list, otherwise
 * it is a temporal number to be split and in this case r is the value range
 * of the temporal number
 */
static RangeBucketState *
range_bucket_state_make(Temporal *temp, RangeType *r, Datum width, Datum origin)
{
  bool intrange = (r->rangetypid == type_oid(T_INTRANGE));
  RangeBucketState *state = palloc0(sizeof(RangeBucketState));
  /* Fill in state */
  state->done = false;
  state->i = 1;
  state->temp = temp;
  state->valuetypid = intrange ? INT4OID : FLOAT8OID;
  state->width = width;
  state->origin = origin;
  Datum lower = lower_datum(r);
  Datum upper = upper_datum(r);
  state->minvalue = number_bucket_internal(lower, width, origin, state->valuetypid);
  state->maxvalue = number_bucket_internal(upper, width, origin, state->valuetypid);
  state->value = state->minvalue;
  return state;
}

/**
 * Increment the current state to the next bucket of the bucket list
 *
 * @param[in] state State to increment
 */
static void
range_bucket_state_next(RangeBucketState *state)
{
  if (!state || state->done)
    return;
  /* Move to the next bucket */
  state->i++;
  state->value = datum_add(state->value, state->width,
    state->valuetypid, state->valuetypid);
  if (datum_gt(state->value, state->maxvalue, state->valuetypid))
    state->done = true;
  return;
}

/*****************************************************************************/

PG_FUNCTION_INFO_V1(range_bucket_list);
/**
 * Generate a bucket list for ranges.
 */
Datum range_bucket_list(PG_FUNCTION_ARGS)
{
  FuncCallContext *funcctx;
  RangeBucketState *state;
  bool isnull[2] = {0,0}; /* needed to say no value is null */
  Datum tuple_arr[2]; /* used to construct the composite return value */
  HeapTuple tuple;
  Datum result; /* the actual composite return value */

  /* If the function is being called for the first time */
  if (SRF_IS_FIRSTCALL())
  {
    /* Get input parameters */
#if MOBDB_PGSQL_VERSION < 110000
    RangeType *bounds = PG_GETARG_RANGE(0);
#else
    RangeType *bounds = PG_GETARG_RANGE_P(0);
#endif
    Datum width = PG_GETARG_DATUM(1);
    Datum origin = PG_GETARG_DATUM(2);

    /* Ensure parameter validity */
    Oid type = get_fn_expr_argtype(fcinfo->flinfo, 1);
    ensure_positive_datum(width, type);

    /* Initialize the FuncCallContext */
    funcctx = SRF_FIRSTCALL_INIT();
    /* Switch to memory context appropriate for multiple function calls */
    MemoryContext oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
    /* Create function state */
    funcctx->user_fctx = range_bucket_state_make(NULL, bounds, width, origin);
    /* Build a tuple description for the function output */
    get_call_result_type(fcinfo, 0, &funcctx->tuple_desc);
    BlessTupleDesc(funcctx->tuple_desc);
    MemoryContextSwitchTo(oldcontext);
  }

  /* Stuff done on every call of the function */
  funcctx = SRF_PERCALL_SETUP();
  /* Get state */
  state = funcctx->user_fctx;
  /* Stop when we've used up all buckets */
  if (state->done)
    SRF_RETURN_DONE(funcctx);

  /* Store index */
  tuple_arr[0] = Int32GetDatum(state->i);
  /* Generate bucket */
  tuple_arr[1] = PointerGetDatum(range_bucket_get(state->value, state->width,
    state->valuetypid));
  /* Advance state */
  range_bucket_state_next(state);
  /* Form tuple and return */
  tuple = heap_form_tuple(funcctx->tuple_desc, tuple_arr, isnull);
  result = HeapTupleGetDatum(tuple);
  SRF_RETURN_NEXT(funcctx, result);
}

/*****************************************************************************/

PG_FUNCTION_INFO_V1(range_bucket);
/**
 * Generate an integer or float range bucket in a bucket list for ranges.
*/
Datum range_bucket(PG_FUNCTION_ARGS)
{
  Datum value = PG_GETARG_DATUM(0);
  Datum width = PG_GETARG_DATUM(1);
  Oid type = get_fn_expr_argtype(fcinfo->flinfo, 1);
  ensure_positive_datum(width, type);
  Datum origin = PG_GETARG_DATUM(2);
  Datum value_bucket = number_bucket_internal(value, width, origin, type);
  RangeType *result = range_bucket_get(value_bucket, width, type);
  PG_RETURN_POINTER(result);
}

/*****************************************************************************
 * Range split functions for temporal numbers
 *****************************************************************************/

PG_FUNCTION_INFO_V1(tnumber_value_split);
/**
 * Split a temporal number with respect to value buckets.
 */
Datum tnumber_value_split(PG_FUNCTION_ARGS)
{
  FuncCallContext *funcctx;
  RangeBucketState *state;
  bool isnull[2] = {0,0}; /* needed to say no value is null */
  Datum tuple_arr[2]; /* used to construct the composite return value */
  HeapTuple tuple;
  Datum result; /* the actual composite return value */

  /* If the function is being called for the first time */
  if (SRF_IS_FIRSTCALL())
  {
    /* Initialize the FuncCallContext */
    funcctx = SRF_FIRSTCALL_INIT();
    /* Switch to memory context appropriate for multiple function calls */
    MemoryContext oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

    /* Get input parameters */
    Temporal *temp = PG_GETARG_TEMPORAL(0);
    Datum width = PG_GETARG_DATUM(1);
    Datum origin = PG_GETARG_DATUM(2);

    /* Ensure parameter validity */
    ensure_tnumber_base_type(temp->valuetypid);
    ensure_positive_datum(width, temp->valuetypid);
    RangeType *bounds = tnumber_value_range_internal((const Temporal *) temp);

    /* Create function state */
    funcctx->user_fctx = range_bucket_state_make(temp, bounds, width, origin);
    /* Build a tuple description for the function output */
    get_call_result_type(fcinfo, 0, &funcctx->tuple_desc);
    BlessTupleDesc(funcctx->tuple_desc);
    MemoryContextSwitchTo(oldcontext);
  }

  /* Stuff done on every call of the function */
  funcctx = SRF_PERCALL_SETUP();
  /* Get state */
  state = funcctx->user_fctx;

  /* We need to loop since the result of atRange may be NULL */
  while (true)
  {
    /* Stop when we've used up all splits */
    if (state->done)
      SRF_RETURN_DONE(funcctx);

    /* Generate bucket */
    RangeType *range = range_bucket_get(state->value, state->width,
      state->valuetypid);
    /* Advance state */
    range_bucket_state_next(state);
    /* Restrict temporal value to range */
    Temporal *atrange = tnumber_restrict_range_internal(state->temp, range,
      REST_AT);
    if (atrange != NULL)
    {
      /* Form tuple and return */
      tuple_arr[0] = lower_datum(range);
      tuple_arr[1] = PointerGetDatum(atrange);
      tuple = heap_form_tuple(funcctx->tuple_desc, tuple_arr, isnull);
      result = HeapTupleGetDatum(tuple);
      SRF_RETURN_NEXT(funcctx, result);
    }
  }
}

/*****************************************************************************
 * Period bucket functions
 *****************************************************************************/

/**
 * Generate period bucket from a bucket list
 *
 * @param[in] t Start timestamp of the bucket
 * @param[in] tunits Size of the time buckets in PostgreSQL time units
 * @param[in] torigin Origin of the buckets
 */
static Period *
period_bucket_get(TimestampTz t, int64 tunits, TimestampTz torigin)
{
  TimestampTz lower = t;
  TimestampTz upper = lower + tunits;
  return period_make(lower, upper, true, false);
}

/**
 * Create the initial state that persists across multiple calls of the function
 *
 * @param[in] p Bounds for generating the bucket list
 * @param[in] tunits Size of the time buckets in PostgreSQL time units
 * @param[in] torigin Origin of the buckets
 *
 * @pre The tunits argument must be greater to 0.
 */
static PeriodBucketState *
period_bucket_state_make(Period *p, int64 tunits, TimestampTz torigin)
{
  assert(tunits > 0);
  PeriodBucketState *state = palloc0(sizeof(PeriodBucketState));

  /* Fill in state */
  state->done = false;
  state->tunits = tunits;
  state->torigin = torigin;
  state->mint = timestamptz_bucket_internal(p->lower, tunits, torigin);
  state->maxt = timestamptz_bucket_internal(p->upper, tunits, torigin);
  state->t = state->mint;
  state->i = 1;
  return state;
}

/**
 * Increment the current state to the next bucket of the bucket list
 *
 * @param[in] state State to increment
 */
static void
period_bucket_state_next(PeriodBucketState *state)
{
  if (!state || state->done)
    return;
  /* Move to the next bucket */
  state->i++;
  state->t += state->tunits;
  if (state->t > state->maxt)
    state->done = true;
  return;
}

/*****************************************************************************/

PG_FUNCTION_INFO_V1(period_bucket_list);
/**
 * Generate a bucket list for periods.
 */
Datum period_bucket_list(PG_FUNCTION_ARGS)
{
  FuncCallContext *funcctx;
  PeriodBucketState *state;
  bool isnull[2] = {0,0}; /* needed to say no value is null */
  Datum tuple_arr[2]; /* used to construct the composite return value */
  HeapTuple tuple;
  Datum result; /* the actual composite return value */

  /* If the function is being called for the first time */
  if (SRF_IS_FIRSTCALL())
  {
    /* Get input parameters */
    Period *bounds = PG_GETARG_PERIOD(0);
    Interval *duration = PG_GETARG_INTERVAL_P(1);
    TimestampTz torigin = PG_GETARG_TIMESTAMPTZ(2);

    /* Ensure parameter validity */
    ensure_valid_duration(duration);
    int64 tunits = get_interval_units(duration);

    /* Initialize the FuncCallContext */
    funcctx = SRF_FIRSTCALL_INIT();
    /* Switch to memory context appropriate for multiple function calls */
    MemoryContext oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
    /* Create function state */
    funcctx->user_fctx = period_bucket_state_make(bounds, tunits, torigin);
    /* Build a tuple description for the function output */
    get_call_result_type(fcinfo, 0, &funcctx->tuple_desc);
    BlessTupleDesc(funcctx->tuple_desc);
    MemoryContextSwitchTo(oldcontext);
  }

  /* Stuff done on every call of the function */
  funcctx = SRF_PERCALL_SETUP();
  /* Get state */
  state = funcctx->user_fctx;
  /* Stop when we've used up all buckets */
  if (state->done)
    SRF_RETURN_DONE(funcctx);

  /* Store bucket time */
  tuple_arr[0] = Int32GetDatum(state->i);
  /* Generate bucket */
  tuple_arr[1] = PointerGetDatum(period_bucket_get(state->t, state->tunits,
    state->torigin));
  /* Advance state */
  period_bucket_state_next(state);
  /* Form tuple and return */
  tuple = heap_form_tuple(funcctx->tuple_desc, tuple_arr, isnull);
  result = HeapTupleGetDatum(tuple);
  SRF_RETURN_NEXT(funcctx, result);
}

/*****************************************************************************/

PG_FUNCTION_INFO_V1(period_bucket);
/**
 * Generate a bucket in a bucket list for periods.
*/
Datum period_bucket(PG_FUNCTION_ARGS)
{
  TimestampTz t = PG_GETARG_TIMESTAMPTZ(0);
  Interval *interval = PG_GETARG_INTERVAL_P(1);
  ensure_valid_duration(interval);
  int64 tunits = get_interval_units(interval);
  TimestampTz torigin = PG_GETARG_TIMESTAMPTZ(2);
  TimestampTz time_bucket = timestamptz_bucket_internal(t, tunits, torigin);
  Period *result = period_bucket_get(time_bucket, tunits, torigin);
  PG_RETURN_POINTER(result);
}

/*****************************************************************************
 * Time split functions for temporal numbers
 *****************************************************************************/

/**
 * Create the initial state that persists across multiple calls of the function
 *
 * @param[in] tunits Size of the time buckets in PostgreSQL time units
 * @param[in] buckets Initial timestamps of the buckets
 * @param[in] splits Fragments of the input temporal value
 * @param[in] count Number of elements in the input arrays
 *
 * @pre count is greater than 0
 */
TimeSplitState *
time_split_state_make(int64 tunits, TimestampTz *buckets, Temporal **splits,
  int count)
{
  assert(count > 0);
  TimeSplitState *state = palloc0(sizeof(TimeSplitState));
  /* Fill in state */
  state->done = false;
  state->tunits = tunits;
  state->buckets = buckets;
  state->splits = splits;
  state->i = 0;
  state->count = count;
  return state;
}

/**
 * Increment the current state to output the next split
 *
 * @param[in] state State to increment
 */
void
time_split_state_next(TimeSplitState *state)
{
  /* Move to the next split */
  state->i++;
  if (state->i == state->count)
    state->done = true;
  return;
}

/*****************************************************************************/

/**
 * Split a temporal value into an array of splits according to time buckets.
 *
 * @param[in] inst Temporal value
 * @param[in] start,end Start and end timestamps of the buckets
 * @param[in] tunits Size of the time buckets in PostgreSQL time units
 * @param[out] buckets Start timestamp of the buckets containing a split
 * @param[out] newcount Number of values in the output array
 */
static TInstant **
tinstant_time_split(const TInstant *inst, TimestampTz start, TimestampTz end,
  int64 tunits, TimestampTz **buckets, int *newcount)
{
  TInstant **result = palloc(sizeof(TInstant *));
  TimestampTz *times = palloc(sizeof(TimestampTz));
  result[0] = (TInstant *) inst;
  TimestampTz lower = start;
  while (lower < end)
  {
    TimestampTz upper = lower + tunits;
    if (lower <= inst->t && inst->t < upper)
    {
      times[0] = lower;
      break;
    }
    lower = upper;
  }
  *buckets = times;
  *newcount = 1;
  return result;
}

/*****************************************************************************/

/**
 * Split a temporal value into an array of splits according to time buckets.
 *
 * @param[in] ti Temporal value
 * @param[in] start,end Start and end timestamps of the buckets
 * @param[in] tunits Size of the time buckets in PostgreSQL time units
 * @param[in] count Number of buckets
 * @param[out] buckets Start timestamp of the buckets containing a split
 * @param[out] newcount Number of values in the output array
 */
static TInstantSet **
tinstantset_time_split(const TInstantSet *ti, TimestampTz start, TimestampTz end,
  int64 tunits, int count, TimestampTz **buckets, int *newcount)
{
  TInstantSet **result = palloc(sizeof(TInstantSet *) * count);
  TimestampTz *times = palloc(sizeof(TimestampTz) * count);
  const TInstant **instants = palloc(sizeof(TInstant *) * ti->count);
  int i = 0,  /* counter for instants of temporal value */
      k = 0,  /* counter for instants of next split */
      l = 0;  /* counter for resulting splits */
  TimestampTz lower = start;
  TimestampTz upper = start + tunits;
  while (i < ti->count)
  {
    const TInstant *inst = tinstantset_inst_n(ti, i);
    if (lower <= inst->t && inst->t < upper)
    {
      instants[k++] = inst;
      i++;
    }
    else
    {
      if (k > 0)
      {
        times[l] = lower;
        result[l++] = tinstantset_make(instants, k, MERGE_NO);
        k = 0;
      }
      if (upper >= end)
       break;
      lower = upper;
      upper += tunits;
    }
  }
  if (k > 0)
  {
    times[l] = lower;
    result[l++] = tinstantset_make(instants, k, MERGE_NO);
  }
  pfree(instants);
  *buckets = times;
  *newcount = l;
  return result;
}

/*****************************************************************************/

/**
 * Split a temporal value into an array of splits according to period buckets.
 *
 * @param[out] result Output array of fragments of the temporal value
 * @param[out] times Output array of bucket lower bounds
 * @param[in] seq Temporal value
 * @param[in] start,end Start and end timestamps of the buckets
 * @param[in] tunits Size of the time buckets in PostgreSQL time units
 * @param[in] count Number of buckets
 *
 * @note This function is called for each sequence of a temporal sequence set
 */
static int
tsequence_time_split1(TSequence **result, TimestampTz *times, const TSequence *seq,
  TimestampTz start, TimestampTz end, int64 tunits, int count)
{
  TimestampTz lower = start;
  TimestampTz upper = lower + tunits;
  /* This loop is needed for filtering unnecesary time buckets for the sequences
   * composing a sequence set */
  /* The upper bound for the bucket is exclusive => the test below is >= */
  while (lower < end &&
    (seq->period.lower >= upper || lower > seq->period.upper ||
      (lower == seq->period.upper && ! seq->period.upper_inc)))
  {
    lower = upper;
    upper += tunits;
  }

  const TInstant **instants = palloc(sizeof(TInstant *) * seq->count * count);
  TInstant **tofree = palloc(sizeof(TInstant *) * seq->count * count);
  bool linear = MOBDB_FLAGS_GET_LINEAR(seq->flags);
  int i = 0,  /* counter for instants of temporal value */
      k = 0,  /* counter for instants of next split */
      l = 0,  /* counter for instants to free */
      m = 0;  /* counter for resulting splits */
  bool lower_inc1;
  while (i < seq->count)
  {
    const TInstant *inst = tsequence_inst_n(seq, i);
    if ((lower <= inst->t && inst->t < upper) ||
      (inst->t == upper && (linear || i == seq->count - 1)))
    {
      instants[k++] = inst;
      i++;
    }
    else
    {
      assert(k > 0);
      /* Compute the value at the end of the bucket */
      if (instants[k - 1]->t < upper)
      {
        /* The last two values of sequences with step interpolation and
         * exclusive upper bound must be equal */
        if (linear)
          tofree[l] = tsequence_at_timestamp1(instants[k - 1], inst, linear,
            upper);
        else
        {
          Datum value = tinstant_value(instants[k - 1]);
          tofree[l] = tinstant_make(value, upper, seq->valuetypid);
        }
        instants[k++] = tofree[l++];
      }
      lower_inc1 = (m == 0) ? seq->period.lower_inc : true;
      times[m] = lower;
      result[m++] = tsequence_make(instants, k, lower_inc1,
         (k > 1) ? false : true, linear, NORMALIZE);
      k = 0;
      lower = upper;
      upper += tunits;
      /* The second condition is needed for filtering unnecesary buckets for the
       * sequences composing a sequence set */
      if (lower >= end || ! contains_period_timestamp_internal(&seq->period, lower))
        break;
      /* Reuse the end value of the previous bucket for the beginning of the bucket */
      if (lower < inst->t)
        instants[k++] = tsequence_inst_n(result[m - 1], result[m - 1]->count - 1);
     }
  }
  if (k > 0)
  {
    lower_inc1 = (m == 0) ? seq->period.lower_inc : true;
    times[m] = lower;
    result[m++] = tsequence_make(instants, k, lower_inc1,
      seq->period.upper_inc, linear, NORMALIZE);
  }
  pfree_array((void **) tofree, l);
  pfree(instants);
  return m;
}

/**
 * Split a temporal value into an array of splits according to period buckets.
 *
 * @param[in] seq Temporal value
 * @param[in] start,end Start and end timestamps of the buckets
 * @param[in] tunits Size of the time buckets in PostgreSQL time units
 * @param[in] count Number of buckets
 * @param[out] buckets Start timestamp of the buckets containing a split
 * @param[out] newcount Number of values in the output array
 */
static TSequence **
tsequence_time_split(const TSequence *seq, TimestampTz start, TimestampTz end,
  int64 tunits, int count, TimestampTz **buckets, int *newcount)
{
  TSequence **result = palloc(sizeof(TSequence *) * count);
  TimestampTz *times = palloc(sizeof(TimestampTz) * count);
  *newcount = tsequence_time_split1(result, times, seq, start, end,
    tunits, count);
  *buckets = times;
  return result;
}

/**
 * Split a temporal value into an array of non self-intersecting splits
 *
 * @param[in] ts Temporal value
 * @param[in] start,end Start and end timestamps of the buckets
 * @param[in] tunits Size of the time buckets in PostgreSQL time units
 * @param[in] count Number of buckets
 * @param[out] buckets Start timestamp of the buckets containing a split
 * @param[out] newcount Number of values in the output array
 */
static TSequenceSet **
tsequenceset_time_split(const TSequenceSet *ts, TimestampTz start, TimestampTz end,
  int64 tunits, int count, TimestampTz **buckets, int *newcount)
{
  /* Singleton sequence set */
  if (ts->count == 1)
  {
    const TSequence *seq = tsequenceset_seq_n(ts, 0);
    TSequence **sequences = tsequence_time_split(seq, start, end, tunits,
      count, buckets, newcount);
    TSequenceSet **result = palloc(sizeof(TSequenceSet *) * *newcount);
    for (int i = 0; i < *newcount; i++)
      result[i] = tsequence_to_tsequenceset(sequences[i]);
    pfree_array((void **) sequences, *newcount);
    return result;
  }

  /* General case */
  TSequence **sequences = palloc(sizeof(TSequence *) * (ts->count + count));
  TimestampTz *times = palloc(sizeof(TimestampTz) * (ts->count + count));
  TSequence **fragments = palloc(sizeof(TSequence *) * (ts->count + count));
  TSequenceSet **result = palloc(sizeof(TSequenceSet *) * count);
  /* Variable used to adjust the start timestamp passed to the 
   * tsequence_time_split1 function in the loop */
  TimestampTz lower = start;
  int k = 0, /* Number of accumulated fragments of the current time bucket */
      m = 0; /* Number of time buckets already processed */
  for (int i = 0; i < ts->count; i++)
  {
    TimestampTz upper = lower + tunits;
    const TSequence *seq = tsequenceset_seq_n(ts, i);
    /* Output the accumulated fragments of the current time bucket (if any)
     * if the current sequence starts on the next time bucket */
    if (k > 0 && seq->period.lower >= upper)
    {
      result[m++] = tsequenceset_make((const TSequence **) fragments, k,
          NORMALIZE_NO);
      for (int j = 0; j < k; j++)
        pfree(fragments[j]);
      k = 0;
      lower += tunits;
      upper += tunits;
    }
    /* Variable keeping the number of time buckets of the current sequence */
    int l = tsequence_time_split1(sequences, &times[m], seq, lower, end,
      tunits, count);
    /* If the current sequence has produced more than two time buckets */
    if (l > 1)
    {
      /* Assemble the accumulated fragments of the first time bucket (if any)  */
      if (k > 0)
      {
        fragments[k++] = sequences[0];
        result[m++] = tsequenceset_make((const TSequence **) fragments, k,
          NORMALIZE_NO);
        for (int j = 0; j < k; j++)
          pfree(fragments[j]);
        k = 0;
      }
      else
      {
        result[m++] = tsequence_to_tsequenceset(sequences[0]);
        pfree(sequences[0]);
      }
      for (int j = 1; j < l - 2; j++)
      {
        result[m++] = tsequence_to_tsequenceset(sequences[j]);
        pfree(sequences[j]);
      }
    }
    /* Save the last fragment in case it is necessary to assemble with the
     * first one of the next sequence */
    fragments[k++] = sequences[l - 1];
    lower = times[m];
  }
  /* Process the accumulated fragments of the last time bucket */
  if (k > 0)
  {
    result[m++] = tsequenceset_make((const TSequence **) fragments, k,
        NORMALIZE_NO);
    for (int j = 0; j < k; j++)
      pfree(fragments[j]);
  }
  pfree(sequences); pfree(fragments);
  *buckets = times;
  *newcount = m;
  return result;
}

/*****************************************************************************/

Temporal **
temporal_time_split_internal(Temporal *temp, TimestampTz start, TimestampTz end,
  int64 tunits, int count, TimestampTz **buckets, int *newcount)
{
  assert(start < end);
  assert(count > 0);
  /* Split the temporal value */
  Temporal **splits;
  if (temp->temptype == INSTANT)
    splits = (Temporal **) tinstant_time_split((const TInstant *) temp,
      start, end, tunits, buckets, newcount);
  else if (temp->temptype == INSTANTSET)
    splits = (Temporal **) tinstantset_time_split((const TInstantSet *) temp,
      start, end, tunits, count, buckets, newcount);
  else if (temp->temptype == SEQUENCE)
    splits = (Temporal **) tsequence_time_split((const TSequence *) temp,
      start, end, tunits, count, buckets, newcount);
  else /* temp->temptype == SEQUENCESET */
    splits = (Temporal **) tsequenceset_time_split((const TSequenceSet *) temp,
      start, end, tunits, count, buckets, newcount);
  return splits;
}

PG_FUNCTION_INFO_V1(temporal_time_split);
/**
 * Split a temporal value into splits with respect to period buckets.
 */
Datum temporal_time_split(PG_FUNCTION_ARGS)
{
  FuncCallContext *funcctx;
  TimeSplitState *state;
  bool isnull[2] = {0,0}; /* needed to say no value is null */
  Datum tuple_arr[2]; /* used to construct the composite return value */
  HeapTuple tuple;
  Datum result; /* the actual composite return value */

  /* If the function is being called for the first time */
  if (SRF_IS_FIRSTCALL())
  {
    /* Get input parameters */
    Temporal *temp = PG_GETARG_TEMPORAL(0);
    Interval *duration = PG_GETARG_INTERVAL_P(1);
    TimestampTz torigin = PG_GETARG_TIMESTAMPTZ(2);

    /* Ensure parameter validity */
    ensure_valid_duration(duration);
    int64 tunits = get_interval_units(duration);

    /* Initialize the FuncCallContext */
    funcctx = SRF_FIRSTCALL_INIT();
    /* Switch to memory context appropriate for multiple function calls */
    MemoryContext oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

    /* Compute the bounds */
    Period p;
    temporal_period(&p, temp);
    TimestampTz start_time = p.lower;
    TimestampTz end_time = p.upper;
    TimestampTz start_bucket = timestamptz_bucket_internal(start_time, tunits,
      torigin);
    /* We need to add tunits to obtain the end timestamp of the last bucket */
    TimestampTz end_bucket = timestamptz_bucket_internal(end_time, tunits,
      torigin) + tunits;
    int count = (int) (((int64) end_bucket - (int64) start_bucket) / tunits);
    /* Split the temporal value */
    TimestampTz *buckets;
    int newcount;
    Temporal **splits = temporal_time_split_internal(temp, start_bucket, end_bucket,
      tunits, count, &buckets, &newcount);
    assert(newcount > 0);

    /* Create function state */
    funcctx->user_fctx = time_split_state_make(tunits, buckets, splits, newcount);
    /* Build a tuple description for the function output */
    get_call_result_type(fcinfo, 0, &funcctx->tuple_desc);
    BlessTupleDesc(funcctx->tuple_desc);
    MemoryContextSwitchTo(oldcontext);
  }

  /* Stuff done on every call of the function */
  funcctx = SRF_PERCALL_SETUP();
  /* Get state */
  state = funcctx->user_fctx;
  /* Stop when we've output all the splits */
  if (state->done)
    SRF_RETURN_DONE(funcctx);

  /* Store timestamp and split */
  tuple_arr[0] = TimestampTzGetDatum(state->buckets[state->i]);
  tuple_arr[1] = PointerGetDatum(state->splits[state->i]);
  /* Advance state */
  time_split_state_next(state);
  /* Form tuple and return */
  tuple = heap_form_tuple(funcctx->tuple_desc, tuple_arr, isnull);
  result = HeapTupleGetDatum(tuple);
  SRF_RETURN_NEXT(funcctx, result);
}

/*****************************************************************************
 * TBOX tile functions
 *****************************************************************************/

/**
 * Generate a tile from the a multidimensional grid
 *
 * @param[in] value Start value of the tile to output
 * @param[in] t Start timestamp of the tile to output
 * @param[in] xsize Value size of the tiles
 * @param[in] tunits Time size of the tiles in PostgreSQL time units
 */
static TBOX *
tbox_tile_get(double value, TimestampTz t, double xsize, int64 tunits)
{
  double xmin = value;
  double xmax = value + xsize;
  TimestampTz tmin = t;
  TimestampTz tmax = t + tunits;
  return (TBOX *) tbox_make(true, true, xmin, xmax, tmin, tmax);
}

/**
 * Create the initial state that persists across multiple calls of the function
 *
 * @param[in] box Bounds of the multidimensional grid
 * @param[in] xsize Value size of the tiles
 * @param[in] tunits Time size of the tiles in PostgreSQL time units
 * @param[in] xorigin Value origin of the tiles
 * @param[in] torigin Time origin of the tiles
 *
 * @pre Both xsize and tunits must be greater than 0.
 */
static TboxGridState *
tbox_tile_state_make(TBOX *box, double xsize, int64 tunits, double xorigin,
  TimestampTz torigin)
{
  assert(xsize > 0);
  assert(tunits > 0);
  TboxGridState *state = palloc0(sizeof(TboxGridState));

  /* Fill in state */
  state->done = false;
  state->i = 1;
  state->xsize = xsize;
  state->tunits = tunits;
  state->box.xmin = float_bucket_internal(box->xmin, xsize, xorigin);
  state->box.xmax = float_bucket_internal(box->xmax, xsize, xorigin);
  state->box.tmin = timestamptz_bucket_internal(box->tmin, tunits, torigin);
  state->box.tmax = timestamptz_bucket_internal(box->tmax, tunits, torigin);
  state->value = state->box.xmin;
  state->t = state->box.tmin;
  return state;
}

/**
 * Increment the current state to the next tile of the multidimensional grid
 *
 * @param[in] state State to increment
 */
static void
tbox_tile_state_next(TboxGridState *state)
{
  if (!state || state->done)
      return;
  /* Move to the next tile */
  state->i++;
  state->value += state->xsize;
  if (state->value > state->box.xmax)
  {
    state->value = state->box.xmin;
    state->t += state->tunits;
    if (state->t > state->box.tmax)
    {
      state->done = true;
      return;
    }
  }
  return;
}

/*****************************************************************************/

PG_FUNCTION_INFO_V1(tbox_multidim_grid);
/**
 * Generate a multidimensional grid for temporal numbers.
 */
Datum tbox_multidim_grid(PG_FUNCTION_ARGS)
{
  FuncCallContext *funcctx;
  TboxGridState *state;
  bool isnull[2] = {0,0}; /* needed to say no value is null */
  Datum tuple_arr[2]; /* used to construct the composite return value */
  HeapTuple tuple;
  Datum result; /* the actual composite return value */

  /* If the function is being called for the first time */
  if (SRF_IS_FIRSTCALL())
  {
    /* Get input parameters */
    TBOX *bounds = PG_GETARG_TBOX_P(0);
    double xsize = PG_GETARG_FLOAT8(1);
    Interval *duration = PG_GETARG_INTERVAL_P(2);
    double xorigin = PG_GETARG_FLOAT8(3);
    TimestampTz torigin = PG_GETARG_TIMESTAMPTZ(4);

    /* Ensure parameter validity */
    ensure_has_X_tbox(bounds);
    ensure_has_T_tbox(bounds);
    ensure_positive_datum(Float8GetDatum(xsize), FLOAT8OID);
    ensure_valid_duration(duration);
    int64 tunits = get_interval_units(duration);

    /* Initialize the FuncCallContext */
    funcctx = SRF_FIRSTCALL_INIT();
    /* Switch to memory context appropriate for multiple function calls */
    MemoryContext oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
    /* Create function state */
    funcctx->user_fctx = tbox_tile_state_make(bounds, xsize, tunits, xorigin, torigin);
    /* Build a tuple description for the function output */
    get_call_result_type(fcinfo, 0, &funcctx->tuple_desc);
    BlessTupleDesc(funcctx->tuple_desc);
    MemoryContextSwitchTo(oldcontext);
  }

  /* Stuff done on every call of the function */
  funcctx = SRF_PERCALL_SETUP();
  /* Get state */
  state = funcctx->user_fctx;
  /* Stop when we've used up all tiles */
  if (state->done)
    SRF_RETURN_DONE(funcctx);

  /* Store tile value and time */
  tuple_arr[0] = Int32GetDatum(state->i);
  /* Generate box */
  tuple_arr[1] = PointerGetDatum(tbox_tile_get(state->value, state->t,
    state->xsize, state->tunits));
  /* Advance state */
  tbox_tile_state_next(state);
  /* Form tuple and return */
  tuple = heap_form_tuple(funcctx->tuple_desc, tuple_arr, isnull);
  result = HeapTupleGetDatum(tuple);
  SRF_RETURN_NEXT(funcctx, result);
}

/*****************************************************************************/

PG_FUNCTION_INFO_V1(tbox_multidim_tile);
/**
 * Generate a tile in a multidimensional grid for temporal numbers.
*/
Datum tbox_multidim_tile(PG_FUNCTION_ARGS)
{
  double value = PG_GETARG_FLOAT8(0);
  TimestampTz t = PG_GETARG_TIMESTAMPTZ(1);
  double xsize = PG_GETARG_FLOAT8(2);
  ensure_positive_datum(Float8GetDatum(xsize), FLOAT8OID);
  Interval *interval = PG_GETARG_INTERVAL_P(3);
  ensure_valid_duration(interval);
  int64 tunits = get_interval_units(interval);
  double xorigin = PG_GETARG_FLOAT8(4);
  TimestampTz torigin = PG_GETARG_TIMESTAMPTZ(5);
  double value_bucket = float_bucket_internal(value, xsize, xorigin);
  TimestampTz time_bucket = timestamptz_bucket_internal(t, tunits, torigin);
  TBOX *result = tbox_tile_get(value_bucket, time_bucket, xsize, tunits);
  PG_RETURN_POINTER(result);
}

/*****************************************************************************
 * Value split functions for temporal numbers
 *****************************************************************************/

/**
 * Create the initial state that persists across multiple calls of the function
 *
 * @param[in] width Width of the value buckets
 * @param[in] buckets Initial values of the buckets
 * @param[in] splits Fragments of the input temporal value
 * @param[in] count Number of elements in the input arrays
 *
 * @pre count is greater than 0
 */
ValueSplitState *
value_split_state_make(Datum width, Datum *buckets, Temporal **splits,
  int count)
{
  assert(count > 0);
  ValueSplitState *state = palloc0(sizeof(ValueSplitState));
  /* Fill in state */
  state->done = false;
  state->width = width;
  state->buckets = buckets;
  state->splits = splits;
  state->i = 0;
  state->count = count;
  return state;
}

/**
 * Increment the current state to output the next split
 *
 * @param[in] state State to increment
 */
void
value_split_state_next(ValueSplitState *state)
{
  /* Move to the next split */
  state->i++;
  if (state->i == state->count)
    state->done = true;
  return;
}

/*****************************************************************************/

static int
bucket_position(Datum value, Datum width, Datum origin, Oid type)
{
  ensure_tnumber_base_type(type);
  if (type == INT4OID)
    return (DatumGetInt32(value) - DatumGetInt32(origin)) /
      DatumGetInt32(width);
  else
    return (int) floor( (DatumGetFloat8(value) - DatumGetFloat8(origin)) /
      DatumGetFloat8(width) );
}

/*****************************************************************************/

/**
 * Split a temporal value into an array of splits according to value buckets.
 *
 * @param[in] inst Temporal value
 * @param[in] width Width of the value buckets
 * @param[in] origin Origin of the value buckets
 * @param[out] buckets Start value of the buckets containing a split
 * @param[out] newcount Number of values in the output array
 */
static TInstant **
tnumberinst_value_split(const TInstant *inst, Datum start_bucket, Datum width,
  Datum **buckets, int *newcount)
{
  Datum value = tinstant_value(inst);
  Oid valuetypid = inst->valuetypid;
  TInstant **result = palloc(sizeof(TInstant *));
  Datum *values = palloc(sizeof(Datum));
  result[0] = (TInstant *) inst;
  values[0] = number_bucket_internal(value, width, start_bucket, valuetypid);
  *buckets = values;
  *newcount = 1;
  return result;
}

/*****************************************************************************/

/**
 * Split a temporal value into an array of splits according to value buckets.
 *
 * @param[in] ti Temporal value
 * @param[in] width Width of the value buckets
 * @param[in] start_bucket Value of the start bucket
 * @param[in] count Number of buckets
 * @param[out] buckets Start value of the buckets containing a split
 * @param[out] newcount Number of values in the output array
 */
static TInstantSet **
tnumberinstset_value_split(const TInstantSet *ti, Datum start_bucket, Datum width,
  int count, Datum **buckets, int *newcount)
{
  Oid valuetypid = ti->valuetypid;
  /* Singleton instant set */
  if (ti->count == 1)
  {
    TInstantSet **result = palloc(sizeof(TInstantSet *));
    Datum *values = palloc(sizeof(Datum));
    result[0] = tinstantset_copy(ti);
    Datum value = tinstant_value(tinstantset_inst_n(ti, 0));
    values[0] = number_bucket_internal(value, width, start_bucket, valuetypid);
    *buckets = values;
    *newcount = 1;
    return result;
  }

  /* General case */
  const TInstant **bucketinsts = palloc(sizeof(TInstant *) * ti->count * count);
  /* palloc0 to initialize the counters to 0 */
  int *numinsts = palloc0(sizeof(int) * count);
  Datum bucket_value;
  for (int i = 0; i < ti->count; i++)
  {
    const TInstant *inst = tinstantset_inst_n(ti, i);
    Datum value = tinstant_value(inst);
    bucket_value = number_bucket_internal(value, width, start_bucket, valuetypid);
    int bucket_no = bucket_position(bucket_value, width, start_bucket, valuetypid);
    int inst_no = numinsts[bucket_no]++;
    bucketinsts[bucket_no * ti->count + inst_no] = inst;
  }
  /* Assemble the result for each value bucket */
  TInstantSet **result = palloc(sizeof(TInstantSet *) * count);
  Datum *values = palloc(sizeof(Datum) * count);
  int k = 0;
  bucket_value = start_bucket;
  for (int i = 0; i < count; i++)
  {
    if (numinsts[i] > 0)
    {
      result[k] = tinstantset_make(&bucketinsts[i * ti->count], numinsts[i], MERGE_NO);
      values[k++] = bucket_value;
    }
    bucket_value = datum_add(bucket_value, width, valuetypid, valuetypid);
  }
  pfree(bucketinsts);
  pfree(numinsts);
  *buckets = values;
  *newcount = k;
  return result;
}

/*****************************************************************************/

/**
 * Split a temporal value into an array of splits according to value buckets.
 *
 * @param[inout] result Array containing the splits of each bucket
 * @param[inout] numseqs Number of splits for each bucket
 * @param[in] seq Temporal value
 * @param[in] start_bucket Value of the start bucket
 * @param[in] width Width of the value buckets
 * @param[in] count Number of buckets
 */
static void
tnumberseq_step_value_split(TSequence **result, int *numseqs, int numcols,
  const TSequence *seq, Datum start_bucket, Datum width, int count)
{
  assert(! MOBDB_FLAGS_GET_LINEAR(seq->flags));
  Oid valuetypid = seq->valuetypid;

  /* Instantaneous sequence */
  if (seq->count == 1)
  {
    Datum value = tinstant_value(tsequence_inst_n(seq, 0));
    Datum bucket_value = number_bucket_internal(value, width, start_bucket, valuetypid);
    int bucket_no = bucket_position(bucket_value, width, start_bucket, valuetypid);
    int seq_no = numseqs[bucket_no]++;
    result[bucket_no * numcols + seq_no] = tsequence_copy(seq);
    return;
  }

  /* General case */
  TInstant **tofree = palloc(sizeof(TInstant *) * count * seq->count);
  int l = 0;   /* counter for the instants to free */
  const TInstant *inst1;
  Datum value;
  Datum bucket_value;
  int bucket_no, seq_no;
  for (int i = 1; i < seq->count; i++)
  {
    inst1 = tsequence_inst_n(seq, i - 1);
    value = tinstant_value(inst1);
    bucket_value = number_bucket_internal(value, width, start_bucket, valuetypid);
    bucket_no = bucket_position(bucket_value, width, start_bucket, valuetypid);
    seq_no = numseqs[bucket_no]++;
    const TInstant *inst2 = tsequence_inst_n(seq, i);
    bool lower_inc1 = (i == 1) ? seq->period.lower_inc : true;
    TInstant *bounds[2];
    bounds[0] = (TInstant *) inst1;
    int countinst = 1;
    if (i < seq->count)
    {
      tofree[l++] = bounds[1] = tinstant_make(value, inst2->t, valuetypid);
      countinst++;
    }
    result[bucket_no * numcols + seq_no] = tsequence_make((const TInstant **) bounds,
      countinst, lower_inc1, false, STEP, NORMALIZE_NO);
    bounds[0] = bounds[1];
    inst1 = inst2;
    lower_inc1 = true;
  }
  /* Last value if upper inclusive */
  if (seq->period.upper_inc)
  {
    inst1 = tsequence_inst_n(seq, seq->count - 1);
    value = tinstant_value(inst1);
    bucket_value = number_bucket_internal(value, width, start_bucket, valuetypid);
    bucket_no = bucket_position(bucket_value, width, start_bucket, valuetypid);
    seq_no = numseqs[bucket_no]++;
    result[bucket_no * numcols + seq_no] = tsequence_make(&inst1, 1,
      true, true, STEP, NORMALIZE_NO);
  }
  pfree_array((void **) tofree, l);
  return;
}

/*****************************************************************************/

/**
 * Split a temporal value into an array of splits according to value buckets.
 *
 * @param[inout] result Array containing the splits of each bucket
 * @param[inout] numseqs Number of splits for each bucket
 * @param[in] numcols Number of columns in the 2D pointer array. It can be
 *    seq->count for sequences or ts->totalcount for sequence sets
 * @param[in] seq Temporal value
 * @param[in] start_bucket Value of the start bucket
 * @param[in] width Width of the value buckets
 * @param[in] count Number of buckets
 */
static void
tnumberseq_linear_value_split(TSequence **result, int *numseqs, int numcols,
  const TSequence *seq, Datum start_bucket, Datum width, int count)
{
  assert(MOBDB_FLAGS_GET_LINEAR(seq->flags));
  Oid valuetypid = seq->valuetypid;

  /* Instantaneous sequence */
  if (seq->count == 1)
  {
    Datum value = tinstant_value(tsequence_inst_n(seq, 0));
    Datum bucket_value = number_bucket_internal(value, width, start_bucket, valuetypid);
    int bucket_no = bucket_position(bucket_value, width, start_bucket, valuetypid);
    int seq_no = numseqs[bucket_no]++;
    result[bucket_no * numcols + seq_no] = tsequence_copy(seq);
    return;
  }

  /* General case */
  TInstant **tofree = palloc(sizeof(TInstant *) * count * seq->count);
  int l = 0;   /* counter for the instants to free */
  const TInstant *inst1 = tsequence_inst_n(seq, 0);
  Datum value1 = tinstant_value(inst1);
  Datum bucket_value1 = number_bucket_internal(value1, width, start_bucket, valuetypid);
  int bucket_no1 = bucket_position(bucket_value1, width, start_bucket, valuetypid);
  for (int i = 1; i < seq->count; i++)
  {
    const TInstant *inst2 = tsequence_inst_n(seq, i);
    Datum value2 = tinstant_value(inst2);
    Datum bucket_value2 = number_bucket_internal(value2, width, start_bucket, valuetypid);
    int bucket_no2 = bucket_position(bucket_value2, width, start_bucket, valuetypid);
    /* Take into account on whether the segment is increasing or decreasing */
    Datum min_value, max_value;
    int first_bucket, last_bucket, first, last;
    TInstant *bounds[2];
    bool lower_inc_def, upper_inc_def;
    bool incr = (bucket_no1 < bucket_no2);
    if (incr)
    {
      min_value = value1;
      max_value = value2;
      first_bucket = bucket_no1;
      last_bucket = bucket_no2;
      first = 0;
      last = 1;
      lower_inc_def = true;
      upper_inc_def = false;
    }
    else
    {
      min_value = value2;
      max_value = value1;
      first_bucket = bucket_no2;
      last_bucket = bucket_no1;
      first = 1;
      last = 0;
      lower_inc_def = false;
      upper_inc_def = true;
    }
    bool lower_inc1 = lower_inc_def;
    bool upper_inc1 = upper_inc_def;
    bounds[first] = incr ? (TInstant *) inst1 : (TInstant *) inst2;
    /* Set the inclusiveness of the first or last bound */
    if (incr)
      /* If first segment */
      lower_inc1 = (i == 1) ? seq->period.lower_inc : true;
    else
      /* If last segment */
      upper_inc1 = (i == seq->count - 1) ? seq->period.upper_inc : false;
    Datum bucket_lower = incr ? bucket_value1 : bucket_value2;
    Datum bucket_upper = datum_add(bucket_lower, width, valuetypid, valuetypid);
    for (int j = first_bucket; j <= last_bucket; j++)
    {
      /* Choose between interpolate or take one of the segment ends */
      if (datum_lt(min_value, bucket_upper, valuetypid) &&
        datum_lt(bucket_upper, max_value, valuetypid))
      {
        TimestampTz t;
        tlinearseq_intersection_value(inst1, inst2, bucket_upper, valuetypid,
          NULL, &t);
        tofree[l++] = bounds[last] = tinstant_make(bucket_upper, t, valuetypid);
      }
      else
      {
        bounds[last] = incr ? (TInstant *) inst2 : (TInstant *) inst1;
        if (j == last_bucket)
        {
          if (incr)
            upper_inc1 = (i == seq->count - 1) ? seq->period.upper_inc : false;
          else
            lower_inc1 = (i == seq->count - 1) ? seq->period.lower_inc : true;
        }
      }
      /* If last bucket contains a single instant */
      int countinst = (bounds[0]->t == bounds[1]->t) ? 1 : 2;
      /* We cannot add to last bucket if last instant has exclusive bound */
      if (countinst == 1 && ! upper_inc1)
        break;
      int seq_no = numseqs[j]++;
      result[j * numcols + seq_no] = tsequence_make((const TInstant **) bounds,
        countinst, lower_inc1, upper_inc1, LINEAR, NORMALIZE_NO);
      bounds[first] = bounds[last];
      bucket_upper = datum_add(bucket_upper, width, valuetypid, valuetypid);
    }
    inst1 = inst2;
    value1 = value2;
    bucket_value1 = bucket_value2;
    bucket_no1 = bucket_no2;
    lower_inc1 = lower_inc_def;
    upper_inc1 = upper_inc_def;
  }
  pfree_array((void **) tofree, l);
  return;
}

/*****************************************************************************/

static TSequenceSet **
tnumberseq_value_split(const TSequence *seq, Datum start_bucket, Datum width,
  int count, Datum **buckets, int *newcount)
{
  Oid valuetypid = seq->valuetypid;
  /* Instantaneous sequence */
  if (seq->count == 1)
  {
    TSequenceSet **result = palloc(sizeof(TSequenceSet *));
    Datum *values = palloc(sizeof(Datum));
    result[0] = tsequence_to_tsequenceset(seq);
    Datum value = tinstant_value(tsequence_inst_n(seq, 0));
    values[0] = number_bucket_internal(value, width, start_bucket, valuetypid);
    *buckets = values;
    *newcount = 1;
    return result;
  }

  /* General case */
  TSequence **bucketseqs = palloc(sizeof(TSequence *) * seq->count * count);
  /* palloc0 to initialize the counters to 0 */
  int *numseqs = palloc0(sizeof(int) * count);
  if (MOBDB_FLAGS_GET_LINEAR(seq->flags))
    tnumberseq_linear_value_split(bucketseqs, numseqs, seq->count, seq,
      start_bucket, width, count);
  else
    tnumberseq_step_value_split(bucketseqs, numseqs, seq->count, seq,
      start_bucket, width, count);
  /* Assemble the result for each value bucket */
  TSequenceSet **result = palloc(sizeof(TSequenceSet *) * count);
  Datum *values = palloc(sizeof(Datum) * count);
  Datum bucket_value = start_bucket;
  int k = 0;
  for (int i = 0; i < count; i++)
  {
    if (numseqs[i] > 0)
    {
      result[k] = tsequenceset_make((const TSequence **)(&bucketseqs[i * seq->count]),
        numseqs[i], NORMALIZE);
      values[k++] = bucket_value;
    }
    bucket_value = datum_add(bucket_value, width, valuetypid, valuetypid);
  }
  pfree(bucketseqs);
  pfree(numseqs);
  *buckets = values;
  *newcount = k;
  return result;}

/*****************************************************************************/

/**
 * Split a temporal value into an array of splits according to value buckets.
 *
 * @param[in] ts Temporal value
 * @param[in] width Width of the value buckets
 * @param[in] start_bucket Start value of the first bucket
 * @param[in] count Number of buckets
 * @param[out] buckets Start value of the buckets containing a split
 * @param[out] newcount Number of values in the output array
 */
static TSequenceSet **
tnumberseqset_value_split(const TSequenceSet *ts, Datum start_bucket, Datum width,
  int count, Datum **buckets, int *newcount)
{
  /* Singleton sequence set */
  if (ts->count == 1)
    return tnumberseq_value_split(tsequenceset_seq_n(ts, 0), start_bucket, width,
      count, buckets, newcount);

  /* General case */
  Oid valuetypid = ts->valuetypid;
  TSequence **bucketseqs = palloc(sizeof(TSequence *) * ts->totalcount * count);
  /* palloc0 to initialize the counters to 0 */
  int *numseqs = palloc0(sizeof(int) * count);
  for (int i = 0; i < ts->count; i++)
  {
    const TSequence *seq = tsequenceset_seq_n(ts, i);
    if (MOBDB_FLAGS_GET_LINEAR(ts->flags))
      tnumberseq_linear_value_split(bucketseqs, numseqs, ts->totalcount, seq,
        start_bucket, width, count);
    else
      tnumberseq_step_value_split(bucketseqs, numseqs, ts->totalcount, seq,
        start_bucket, width, count);
  }
  /* Assemble the result for each value bucket */
  TSequenceSet **result = palloc(sizeof(TSequenceSet *) * count);
  Datum *values = palloc(sizeof(Datum) * count);
  Datum bucket_value = start_bucket;
  int k = 0;
  for (int i = 0; i < count; i++)
  {
    if (numseqs[i] > 0)
    {
      result[k] = tsequenceset_make((const TSequence **)(&bucketseqs[i * ts->totalcount]),
        numseqs[i], NORMALIZE_NO);
      values[k++] = bucket_value;
    }
    bucket_value = datum_add(bucket_value, width, valuetypid, valuetypid);
  }
  pfree(bucketseqs);
  pfree(numseqs);
  *buckets = values;
  *newcount = k;
  return result;
}

/*****************************************************************************/

Temporal **
tnumber_value_split_internal(Temporal *temp, Datum start_bucket, Datum width,
  int count, Datum **buckets, int *newcount)
{
  assert(count > 0);
  /* Split the temporal value */
  Temporal **splits;
  if (temp->temptype == INSTANT)
    splits = (Temporal **) tnumberinst_value_split((const TInstant *) temp,
      start_bucket, width, buckets, newcount);
  else if (temp->temptype == INSTANTSET)
    splits = (Temporal **) tnumberinstset_value_split((const TInstantSet *) temp,
      start_bucket, width, count, buckets, newcount);
  else if (temp->temptype == SEQUENCE)
    splits = (Temporal **) tnumberseq_value_split((const TSequence *) temp,
      start_bucket, width, count, buckets, newcount);
  else /* temp->temptype == SEQUENCESET */
    splits = (Temporal **) tnumberseqset_value_split((const TSequenceSet *) temp,
      start_bucket, width, count, buckets, newcount);
  return splits;
}

PG_FUNCTION_INFO_V1(tnumber_value_split_new);
/**
 * Split a temporal value into splits with respect to period buckets.
 */
Datum tnumber_value_split_new(PG_FUNCTION_ARGS)
{
  FuncCallContext *funcctx;
  ValueSplitState *state;
  bool isnull[2] = {0,0}; /* needed to say no value is null */
  Datum tuple_arr[2]; /* used to construct the composite return value */
  HeapTuple tuple;
  Datum result; /* the actual composite return value */

  /* If the function is being called for the first time */
  if (SRF_IS_FIRSTCALL())
  {
    /* Get input parameters */
    Temporal *temp = PG_GETARG_TEMPORAL(0);
    Datum width = PG_GETARG_DATUM(1);
    Datum origin = PG_GETARG_DATUM(2);

    /* Ensure parameter validity */
    Oid valuetypid = temp->valuetypid;
    ensure_positive_datum(width, valuetypid);

    /* Initialize the FuncCallContext */
    funcctx = SRF_FIRSTCALL_INIT();
    /* Switch to memory context appropriate for multiple function calls */
    MemoryContext oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

    /* Compute the value bounds */
    RangeType *range = tnumber_value_range_internal((const Temporal *) temp);
    Datum start_value = lower_datum(range);
    /* We need to add width to obtain the end value of the last bucket */
    Datum end_value = datum_add(upper_datum(range), width, valuetypid, valuetypid);
    Datum start_bucket = number_bucket_internal(start_value, width, origin, valuetypid);
    Datum end_bucket = number_bucket_internal(end_value, width, origin, valuetypid);
    int count = (valuetypid == INT4OID) ?
      (DatumGetInt32(end_bucket) - DatumGetInt32(start_bucket)) /
        DatumGetInt32(width) :
      floor((DatumGetFloat8(end_bucket) - DatumGetFloat8(start_bucket)) /
        DatumGetFloat8(width));

    /* Split the temporal value */
    Datum *buckets;
    int newcount;
    Temporal **splits = tnumber_value_split_internal(temp, start_bucket, width,
      count, &buckets, &newcount);
    assert(newcount > 0);

    /* Create function state */
    funcctx->user_fctx = value_split_state_make(width, buckets, splits, newcount);
    /* Build a tuple description for the function output */
    get_call_result_type(fcinfo, 0, &funcctx->tuple_desc);
    BlessTupleDesc(funcctx->tuple_desc);
    MemoryContextSwitchTo(oldcontext);
  }

  /* Stuff done on every call of the function */
  funcctx = SRF_PERCALL_SETUP();
  /* Get state */
  state = funcctx->user_fctx;
  /* Stop when we've output all the splits */
  if (state->done)
    SRF_RETURN_DONE(funcctx);

  /* Store timestamp and split */
  tuple_arr[0] = state->buckets[state->i];
  tuple_arr[1] = PointerGetDatum(state->splits[state->i]);
  /* Advance state */
  value_split_state_next(state);
  /* Form tuple and return */
  tuple = heap_form_tuple(funcctx->tuple_desc, tuple_arr, isnull);
  result = HeapTupleGetDatum(tuple);
  SRF_RETURN_NEXT(funcctx, result);
}

/*****************************************************************************
 * Value and time split functions for temporal numbers
 *****************************************************************************/

/**
 * Create the initial state that persists across multiple calls of the function
 *
 * @param[in] value_buckets Initial values of the tiles
 * @param[in] time_buckets Initial timestamps of the tiles
 * @param[in] splits Fragments of the input temporal value
 * @param[in] count Number of elements in the input arrays
 *
 * @pre count is greater than 0
 */
ValueTimeSplitState *
value_time_split_state_make(Datum *value_buckets, TimestampTz *time_buckets,
  Temporal **splits, int count)
{
  assert(count > 0);
  ValueTimeSplitState *state = palloc0(sizeof(ValueTimeSplitState));
  /* Fill in state */
  state->done = false;
  state->value_buckets = value_buckets;
  state->time_buckets = time_buckets;
  state->splits = splits;
  state->i = 0;
  state->count = count;
  return state;
}

/**
 * Increment the current state to the next tile of the multidimensional grid
 *
 * @param[in] state State to increment
 */
void
value_time_split_state_next(ValueTimeSplitState *state)
{
  /* Move to the next split */
  state->i++;
  if (state->i == state->count)
    state->done = true;
  return;
}

/*****************************************************************************/

PG_FUNCTION_INFO_V1(tnumber_value_time_split);
/**
 * Split a temporal value into splits with respect to period tiles.
 */
Datum tnumber_value_time_split(PG_FUNCTION_ARGS)
{
  FuncCallContext *funcctx;
  ValueTimeSplitState *state;
  bool isnull[3] = {0,0,0}; /* needed to say no value is null */
  Datum tuple_arr[3]; /* used to construct the composite return value */
  HeapTuple tuple;
  Datum result; /* the actual composite return value */

  /* If the function is being called for the first time */
  if (SRF_IS_FIRSTCALL())
  {
    /* Get input parameters */
    Temporal *temp = PG_GETARG_TEMPORAL(0);
    Datum width = PG_GETARG_DATUM(1);
    Interval *duration = PG_GETARG_INTERVAL_P(2);
    Datum origin = PG_GETARG_DATUM(3);
    TimestampTz torigin = PG_GETARG_TIMESTAMPTZ(4);

    /* Ensure parameter validity */
    Oid valuetypid = temp->valuetypid;
    ensure_positive_datum(width, valuetypid);
    ensure_valid_duration(duration);
    int64 tunits = get_interval_units(duration);

    /* Initialize the FuncCallContext */
    funcctx = SRF_FIRSTCALL_INIT();
    /* Switch to memory context appropriate for multiple function calls */
    MemoryContext oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

    /* Compute the value bounds */
    RangeType *range = tnumber_value_range_internal((const Temporal *) temp);
    Datum start_value = lower_datum(range);
    /* We need to add width to obtain the end value of the last bucket */
    Datum end_value = datum_add(upper_datum(range), width, valuetypid, valuetypid);
    Datum start_bucket = number_bucket_internal(start_value, width, origin, valuetypid);
    Datum end_bucket = number_bucket_internal(end_value, width, origin, valuetypid);
    int value_count = (valuetypid == INT4OID) ?
      (DatumGetInt32(end_bucket) - DatumGetInt32(start_bucket)) / DatumGetInt32(width) :
      floor((DatumGetFloat8(end_bucket) - DatumGetFloat8(start_bucket)) / DatumGetFloat8(width));

    /* Compute the time bounds */
    Period p;
    temporal_period(&p, temp);
    TimestampTz start_time = p.lower;
    TimestampTz end_time = p.upper;
    TimestampTz start_time_bucket = timestamptz_bucket_internal(start_time,
      tunits, torigin);
    /* We need to add tunits to obtain the end timestamp of the last bucket */
    TimestampTz end_time_bucket = timestamptz_bucket_internal(end_time, tunits,
      torigin) + tunits;
    int time_count = (int) (((int64) end_time_bucket - (int64) start_time_bucket) / tunits);

    /* Compute the number of tiles */
    int count = value_count * time_count;

    /* Split the temporal value */
    Datum *value_buckets = palloc(sizeof(Datum) * count);
    TimestampTz *time_buckets = palloc(sizeof(TimestampTz) * count);
    Temporal **splits = palloc(sizeof(Temporal *) * count);
    int k = 0;
    Datum lower_value = start_bucket;
    while (datum_lt(lower_value, end_bucket, valuetypid))
    {
      Datum upper_value = datum_add(lower_value, width, valuetypid, valuetypid);
      range = range_make(lower_value, upper_value, true, false, valuetypid);
      Temporal *atrange = tnumber_restrict_range_internal(temp, range, REST_AT);
      if (atrange != NULL)
      {
        int num_time_splits;
        TimestampTz *times;
        Temporal **time_splits = temporal_time_split_internal(atrange,
          start_time_bucket, end_time_bucket, tunits, time_count, &times,
          &num_time_splits);
        for (int i = 0; i < num_time_splits; i++)
        {
          value_buckets[i + k] = lower_value;
          time_buckets[i + k] = times[i];
          splits[i + k] = time_splits[i];
        }
        k += num_time_splits;
        pfree(time_splits);
        pfree(times);
      }
      pfree(range);
      lower_value = upper_value;
    }

    assert(k > 0);
    /* Create function state */
    funcctx->user_fctx = value_time_split_state_make(value_buckets,
      time_buckets, splits, k);
    /* Build a tuple description for the function output */
    get_call_result_type(fcinfo, 0, &funcctx->tuple_desc);
    BlessTupleDesc(funcctx->tuple_desc);
    MemoryContextSwitchTo(oldcontext);
  }

  /* stuff done on every call of the function */
  funcctx = SRF_PERCALL_SETUP();
  /* get state */
  state = funcctx->user_fctx;
  /* Stop when we've output all the splits */
  if (state->done)
    SRF_RETURN_DONE(funcctx);

  /* Store value, timestamp, and split */
  tuple_arr[0] = state->value_buckets[state->i];
  tuple_arr[1] = TimestampTzGetDatum(state->time_buckets[state->i]);
  tuple_arr[2] = PointerGetDatum(state->splits[state->i]);
  /* Advance state */
  value_time_split_state_next(state);
  /* Form tuple and return */
  tuple = heap_form_tuple(funcctx->tuple_desc, tuple_arr, isnull);
  result = HeapTupleGetDatum(tuple);
  SRF_RETURN_NEXT(funcctx, result);
}

/*****************************************************************************/