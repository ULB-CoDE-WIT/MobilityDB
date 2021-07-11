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
 * @file temporal_util.c
 * Miscellaneous utility functions for temporal types.
 */

#include "temporal_util.h"

#include <assert.h>
#include <catalog/pg_collation.h>
#include <fmgr.h>
#include <utils/builtins.h>
#include <utils/lsyscache.h>
#include <utils/timestamp.h>
#include <utils/varlena.h>

#include "period.h"
#include "temporaltypes.h"
#include "tempcache.h"
#include "doublen.h"

#include "tpoint.h"
#include "tpoint_spatialfuncs.h"

#include "tnpoint_static.h"

/*****************************************************************************
 * Temporal/base types tests
 *****************************************************************************/

/**
 * Returns true if the Oid is a EXTERNAL temporal type
 *
 * @note Function used in particular in the indexes
 */
bool
temporal_type(Oid temptypid)
{
  if (temptypid == type_oid(T_TBOOL) || temptypid == type_oid(T_TINT) ||
    temptypid == type_oid(T_TFLOAT) || temptypid == type_oid(T_TTEXT) ||
    temptypid == type_oid(T_TGEOMPOINT) || temptypid == type_oid(T_TGEOGPOINT) || 
    temptypid == type_oid(T_TNPOINT))
    return true;
  return false;
}

/**
 * Ensures that the Oid is a base type supported by MobilityDB
 */
void
ensure_temporal_base_type(Oid basetypid)
{
  if (basetypid != BOOLOID && basetypid != INT4OID &&
    basetypid != FLOAT8OID && basetypid != TEXTOID &&
    basetypid != type_oid(T_DOUBLE2) && basetypid != type_oid(T_DOUBLE3) &&
    basetypid != type_oid(T_DOUBLE4) &&
    basetypid != type_oid(T_GEOMETRY) && basetypid != type_oid(T_GEOGRAPHY) &&
    basetypid != type_oid(T_NPOINT))
    elog(ERROR, "unknown base type: %d", basetypid);
  return;
}

/**
 * Returns true if the Oid corresponds to a continuous base type
 */
bool
base_type_continuous(Oid basetypid)
{
  if (basetypid == FLOAT8OID || basetypid == type_oid(T_DOUBLE2) ||
    basetypid == type_oid(T_DOUBLE3) || basetypid == type_oid(T_DOUBLE4) ||
    basetypid == type_oid(T_GEOGRAPHY) || basetypid == type_oid(T_GEOMETRY) ||
    basetypid == type_oid(T_NPOINT))
    return true;
  return false;
}

/**
 * Ensures that the Oid is an internal or external base type that is continuous
 */
void
ensure_base_type_continuous(Temporal *temp)
{
  if (! MOBDB_FLAGS_GET_CONTINUOUS(temp->flags))
    elog(ERROR, "unknown continuous base type: %d", temp->basetypid);
  return;
}

/**
 * Returns true if the values of the type are passed by value.
 *
 * This function is called only for the base types of the temporal types
 * To avoid a call of the slow function get_typbyval (which makes a lookup
 * call), the known base types are explicitly enumerated.
 */
bool
base_type_byvalue(Oid basetypid)
{
  ensure_temporal_base_type(basetypid);
  if (basetypid == BOOLOID || basetypid == INT4OID || basetypid == FLOAT8OID)
    return true;
  return false;
}

/**
 * Returns the length of type
 *
 * This function is called only for the base types of the temporal types
 * passed by reference. To avoid a call of the slow function get_typlen
 * (which makes a lookup call), the known base types are explicitly enumerated.
 */
int16
base_type_length(Oid basetypid)
{
  ensure_temporal_base_type(basetypid);
  if (basetypid == type_oid(T_DOUBLE2))
    return 16;
  if (basetypid == type_oid(T_DOUBLE3))
    return 24;
  if (basetypid == type_oid(T_DOUBLE4))
    return 32;
  if (basetypid == TEXTOID)
    return -1;
  if (basetypid == type_oid(T_GEOMETRY) || basetypid == type_oid(T_GEOGRAPHY))
    return -1;
  if (basetypid == type_oid(T_NPOINT))
    return 16;
  elog(ERROR, "unknown base_type_length function for base type: %d", basetypid);
}

/**
 * Returns true if the Oid is a alpha base type (i.e., those whose bounding
 * box is a period) supported by MobilityDB
 */
bool
talpha_base_type(Oid basetypid)
{
  if (basetypid == BOOLOID || basetypid == TEXTOID)
    return true;
  return false;
}

/**
 * Returns true if the Oid is a temporal number type
 *
 * @note Function used in particular in the indexes
 */
bool
tnumber_type(Oid temptypid)
{
  if (temptypid == type_oid(T_TINT) || temptypid == type_oid(T_TFLOAT))
    return true;
  return false;
}

/**
 * Test whether the Oid is a number base type supported by MobilityDB
 */
bool
tnumber_base_type(Oid basetypid)
{
  if (basetypid == INT4OID || basetypid == FLOAT8OID)
    return true;
  return false;
}

/**
 * Returns true if the Oid is a number base type supported by MobilityDB
 */
void
ensure_tnumber_base_type(Oid basetypid)
{
  if (! tnumber_base_type(basetypid))
    elog(ERROR, "unknown number base type: %d", basetypid);
  return;
}

/**
 * Returns true if the Oid is a temporal number type
 *
 * @note Function used in particular in the indexes
 */
bool
tnumber_range_type(Oid rangetypid)
{
  if (rangetypid == type_oid(T_INTRANGE) || rangetypid == type_oid(T_FLOATRANGE))
    return true;
  return false;
}

/**
 * Ensures that the Oid is a range type
 */
void
ensure_tnumber_range_type(Oid rangetypid)
{
  if (! tnumber_range_type(rangetypid))
    elog(ERROR, "unknown number range type: %d", rangetypid);
  return;
}

/**
 * Returns true if the Oid is a spatiotemporal type
 *
 * @note This function is used for features common to all spatiotemporal types,
 * in particular, all of them use the same bounding box STBOX. Therefore it is
 * used for the indexes and selectivity functions
 */
bool
tspatial_type(Oid temptypid)
{
  if (temptypid == type_oid(T_TGEOMPOINT) || temptypid == type_oid(T_TGEOGPOINT) ||
    temptypid == type_oid(T_TNPOINT))
    return true;
  return false;
}

/**
 * Returns true if the Oid is a spatiotemporal type
 *
 * @note This function is used for features common to all spatiotemporal types,
 * in particular, all of them use the same bounding box STBOX
 */
bool
tspatial_base_type(Oid basetypid)
{
  if (basetypid == type_oid(T_GEOMETRY) || basetypid == type_oid(T_GEOGRAPHY) ||
    basetypid == type_oid(T_NPOINT))
    return true;
  return false;
}

/**
 * Returns true if the Oid is a point base type supported by MobilityDB
 */
bool
tgeo_base_type(Oid basetypid)
{
  if (basetypid == type_oid(T_GEOMETRY) || basetypid == type_oid(T_GEOGRAPHY))
    return true;
  return false;
}

/**
 * Ensures that the Oid is a point base type supported by MobilityDB
 */
void
ensure_tgeo_base_type(Oid basetypid)
{
  if (! tgeo_base_type(basetypid))
    elog(ERROR, "unknown geospatial base type: %d", basetypid);
  return;
}

/**
 * Returns true if the temporal type corresponding to the Oid of the
 * base type has its trajectory precomputed
 */
bool
type_has_precomputed_trajectory(Oid basetypid)
{
  if (tgeo_base_type(basetypid))
    return true;
  return false;
}

/**
 * Returns true if the temporal type corresponding to the Oid of the
 * base type has its trajectory precomputed
 */
bool
base_type_without_bbox(Oid basetypid)
{
  if (basetypid == type_oid(T_DOUBLE2) || basetypid == type_oid(T_DOUBLE3) ||
      basetypid == type_oid(T_DOUBLE4))
    return true;
  return false;
}


/**
 * Returns the size of the bounding box
 */
size_t
temporal_bbox_size(Oid basetypid)
{
  if (talpha_base_type(basetypid))
    return sizeof(Period);
  if (tnumber_base_type(basetypid))
    return sizeof(TBOX);
  if (tspatial_base_type(basetypid))
    return sizeof(STBOX);
  /* Types without bounding box, such as tdoubleN, must be explicity stated */
  if (base_type_without_bbox(basetypid))
    return 0;
  elog(ERROR, "unknown temporal_bbox_size function for base type: %d", basetypid);
}

/*****************************************************************************
 * Oid functions
 *****************************************************************************/

/**
 * Returns the Oid of the range type corresponding to the Oid of the
 * base type
 */
Oid
range_oid_from_base(Oid basetypid)
{
  ensure_tnumber_base_type(basetypid);
  if (basetypid == INT4OID)
    return type_oid(T_INTRANGE);
  if (basetypid == FLOAT8OID)
    return type_oid(T_FLOATRANGE);
  elog(ERROR, "unknown range type for base type: %d", basetypid);
}

/**
 * Returns the Oid of the temporal type corresponding to the Oid of the
 * base type
 */
Oid
temporal_oid_from_base(Oid basetypid)
{
  ensure_temporal_base_type(basetypid);
  if (basetypid == BOOLOID)
    return type_oid(T_TBOOL);
  if (basetypid == INT4OID)
    return type_oid(T_TINT);
  if (basetypid == FLOAT8OID)
    return type_oid(T_TFLOAT);
  if (basetypid == TEXTOID)
    return type_oid(T_TTEXT);
  if (basetypid == type_oid(T_GEOMETRY))
    return type_oid(T_TGEOMPOINT);
  if (basetypid == type_oid(T_GEOGRAPHY))
    return type_oid(T_TGEOGPOINT);
  if (basetypid == type_oid(T_NPOINT))
    return type_oid(T_TNPOINT);
  elog(ERROR, "unknown temporal type for base type: %d", basetypid);
}

/**
 * Returns the Oid of the base type corresponding to the Oid of the
 * temporal type
 */
Oid
base_oid_from_temporal(Oid temptypid)
{
  assert(temporal_type(temptypid));
  if (temptypid == type_oid(T_TBOOL))
    return BOOLOID;
  if (temptypid == type_oid(T_TINT))
    return INT4OID;
  if (temptypid == type_oid(T_TFLOAT))
    return FLOAT8OID;
  if (temptypid == type_oid(T_TTEXT))
    return TEXTOID;
  if (temptypid == type_oid(T_TGEOMPOINT))
    return type_oid(T_GEOMETRY);
  if (temptypid == type_oid(T_TGEOGPOINT))
    return type_oid(T_GEOGRAPHY);
  if (temptypid == type_oid(T_TNPOINT))
    return type_oid(T_NPOINT);
  elog(ERROR, "unknown base type for temporal type: %d", temptypid);
}

/*****************************************************************************
 * Comparison functions on datums
 *****************************************************************************/

/* Version of the functions where the types of both arguments is equal */

/**
 * Returns true if the two values are equal
 */
bool
datum_eq(Datum l, Datum r, Oid type)
{
  return datum_eq2(l, r, type, type);
}

/**
 * Returns true if the two values are different
 */
bool
datum_ne(Datum l, Datum r, Oid type)
{
  return !datum_eq2(l, r, type, type);
}

/**
 * Returns true if the first value is less than the second one
 */
bool
datum_lt(Datum l, Datum r, Oid type)
{
  return datum_lt2(l, r, type, type);
}

/**
 * Returns true if the first value is less than or equal to the second one
 */
bool
datum_le(Datum l, Datum r, Oid type)
{
  return datum_eq2(l, r, type, type) || datum_lt2(l, r, type, type);
}

/**
 * Returns true if the first value is greater than the second one
 */
bool
datum_gt(Datum l, Datum r, Oid type)
{
  return datum_lt2(r, l, type, type);
}

/**
 * Returns true if the first value is greater than or equal to the second one
 * This function is currently not used
bool
datum_ge(Datum l, Datum r, Oid type)
{
  return datum_eq2(l, r, type, type) || datum_lt2(r, l, type, type);
}
*/

/*****************************************************************************/

/*
 * Version of the functions where the types of both arguments may be different
 * but compatible, e.g., integer and float
 */

/**
 * Returns true if the two values are equal even if their type is not the same
 * (base type dispatch function)
 */
bool
datum_eq2(Datum l, Datum r, Oid typel, Oid typer)
{
  ensure_temporal_base_type(typel);
  if (typel != typer)
    ensure_temporal_base_type(typer);
  if ((typel == BOOLOID && typer == BOOLOID) ||
    (typel == INT4OID && typer == INT4OID))
    return l == r;
  if (typel == FLOAT8OID && typer == FLOAT8OID)
    return l == r;
    // return FP_EQUALS(DatumGetFloat8(l), DatumGetFloat8(r));
  if (typel == INT4OID && typer == FLOAT8OID)
    return (double) DatumGetInt32(l) == DatumGetFloat8(r);
    // return FP_EQUALS((double) DatumGetInt32(l), DatumGetFloat8(r));
  if (typel == FLOAT8OID && typer == INT4OID)
    return DatumGetFloat8(l) == (double) DatumGetInt32(r);
    // return FP_EQUALS(DatumGetFloat8(l), (double) DatumGetInt32(r));
  if (typel == TEXTOID && typer == TEXTOID)
    return text_cmp(DatumGetTextP(l), DatumGetTextP(r), DEFAULT_COLLATION_OID) == 0;
  if (typel == type_oid(T_DOUBLE2) && typel == typer)
    return double2_eq(DatumGetDouble2P(l), DatumGetDouble2P(r));
  if (typel == type_oid(T_DOUBLE3) && typel == typer)
    return double3_eq(DatumGetDouble3P(l), DatumGetDouble3P(r));
  if (typel == type_oid(T_DOUBLE4) && typel == typer)
    return double4_eq(DatumGetDouble4P(l), DatumGetDouble4P(r));
  if (typel == type_oid(T_GEOMETRY) && typel == typer)
    //  return DatumGetBool(call_function2(lwgeom_eq, l, r));
    return datum_point_eq(l, r);
  if (typel == type_oid(T_GEOGRAPHY) && typel == typer)
    //  return DatumGetBool(call_function2(geography_eq, l, r));
    return datum_point_eq(l, r);
  if (typel == type_oid(T_NPOINT) && typel == typer)
    return npoint_eq_internal(DatumGetNpoint(l), DatumGetNpoint(r));
  elog(ERROR, "unknown datum_eq2 function for base type: %d", typel);
}

/**
 * Returns true if the two values are different
 */
bool
datum_ne2(Datum l, Datum r, Oid typel, Oid typer)
{
  return !datum_eq2(l, r, typel, typer);
}

/**
 * Returns true if the first value is less than the second one
 * (base type dispatch function)
 */
bool
datum_lt2(Datum l, Datum r, Oid typel, Oid typer)
{
  ensure_temporal_base_type(typel);
  if (typel != typer)
    ensure_temporal_base_type(typer);
  if (typel == BOOLOID && typer == BOOLOID)
    return DatumGetBool(l) < DatumGetBool(r);
  if (typel == INT4OID && typer == INT4OID)
    return DatumGetInt32(l) < DatumGetInt32(r);
  if (typel == INT4OID && typer == FLOAT8OID)
    return (double) DatumGetInt32(l) < DatumGetFloat8(r);
    // return FP_LT((double) DatumGetInt32(l), DatumGetFloat8(r));
  if (typel == FLOAT8OID && typer == INT4OID)
    return DatumGetFloat8(l) < (double) DatumGetInt32(r);
    // return FP_LT(DatumGetFloat8(l), (double) DatumGetInt32(r));
  if (typel == FLOAT8OID && typer == FLOAT8OID)
    return DatumGetFloat8(l) < DatumGetFloat8(r);
    // return FP_LT(DatumGetFloat8(l), DatumGetFloat8(r));
  if (typel == TEXTOID && typer == TEXTOID)
    return text_cmp(DatumGetTextP(l), DatumGetTextP(r), DEFAULT_COLLATION_OID) < 0;
  if (typel == type_oid(T_GEOMETRY) && typel == typer)
    return DatumGetBool(call_function2(lwgeom_lt, l, r));
  if (typel == type_oid(T_GEOGRAPHY) && typel == typer)
    return DatumGetBool(call_function2(geography_lt, l, r));
  if (typel == type_oid(T_NPOINT) && typel == typer)
    return npoint_lt_internal(DatumGetNpoint(l), DatumGetNpoint(r));
  elog(ERROR, "unknown datum_lt2 function for base type: %d", typel);
}


/**
 * Returns true if the first value is less than or equal to the second one
 */
bool
datum_le2(Datum l, Datum r, Oid typel, Oid typer)
{
  return datum_eq2(l, r, typel, typer) || datum_lt2(l, r, typel, typer);
}

/**
 * Returns true if the first value is greater than the second one
 */
bool
datum_gt2(Datum l, Datum r, Oid typel, Oid typer)
{
  return datum_lt2(r, l, typer, typel);
}

/**
 * Returns true if the first value is greater than or equal to the second one
 */
bool
datum_ge2(Datum l, Datum r, Oid typel, Oid typer)
{
  return datum_eq2(l, r, typel, typer) || datum_gt2(l, r, typel, typer);
}

/*****************************************************************************/

/**
 * Returns a Datum true if the two values are equal
 */
Datum
datum2_eq2(Datum l, Datum r, Oid typel, Oid typer)
{
  return BoolGetDatum(datum_eq2(l, r, typel, typer));
}

/**
 * Returns a Datum true if the two values are different
 */
Datum
datum2_ne2(Datum l, Datum r, Oid typel, Oid typer)
{
  return BoolGetDatum(datum_ne2(l, r, typel, typer));
}

/**
 * Returns a Datum true if the first value is less than the second one
 */
Datum
datum2_lt2(Datum l, Datum r, Oid typel, Oid typer)
{
  return BoolGetDatum(datum_lt2(l, r, typel, typer));
}

/**
 * Returns a Datum true if the first value is less than or equal to the second one
 */
Datum
datum2_le2(Datum l, Datum r, Oid typel, Oid typer)
{
  return BoolGetDatum(datum_le2(l, r, typel, typer));
}

/**
 * Returns a Datum true if the first value is greater than the second one
 */
Datum
datum2_gt2(Datum l, Datum r, Oid typel, Oid typer)
{
  return BoolGetDatum(datum_gt2(l, r, typel, typer));
}

/**
 * Returns a Datum true if the first value is greater than or equal to the second one
 */
Datum
datum2_ge2(Datum l, Datum r, Oid typel, Oid typer)
{
  return BoolGetDatum(datum_ge2(l, r, typel, typer));
}

/*****************************************************************************
 * Miscellaneous functions
 *****************************************************************************/

/**
 * Align to double
 */
size_t
double_pad(size_t size)
{
  if (size % 8)
    return size + (8 - size % 8);
  return size;
}

/**
 * Copy a Datum if it is passed by reference
 */
Datum
datum_copy(Datum value, Oid type)
{
  /* For types passed by value */
  if (base_type_byvalue(type))
    return value;
  /* For types passed by reference */
  int typlen = base_type_length(type);
  size_t value_size = (typlen != -1) ? (size_t) typlen : VARSIZE(value);
  void *result = palloc0(value_size);
  memcpy(result, DatumGetPointer(value), value_size);
  return PointerGetDatum(result);
}

/**
 * Convert a number to a double
 */
double
datum_double(Datum d, Oid basetypid)
{
  ensure_tnumber_base_type(basetypid);
  if (basetypid == INT4OID)
    return (double)(DatumGetInt32(d));
  else if (basetypid == FLOAT8OID)
    return DatumGetFloat8(d);
  elog(ERROR, "unknown datum_double function for base type: %d", basetypid);
}

/**
 * Convert a text value into a C string
 *
 * @note We don't include <utils/builtins.h> to avoid collisions with json-c/json.h
 * @note Function taken from PostGIS file lwgeom_in_geojson.c
 */
char *
text2cstring(const text *textptr)
{
  size_t size = VARSIZE_ANY_EXHDR(textptr);
  char *str = palloc(size + 1);
  memcpy(str, VARDATA(textptr), size);
  str[size]='\0';
  return str;
}

/*****************************************************************************
 * Call PostgreSQL functions
 *****************************************************************************/

/**
 * Call input function of the base type
 */
Datum
call_input(Oid type, char *str)
{
  Oid infunc;
  Oid basetype;
  FmgrInfo infuncinfo;
  getTypeInputInfo(type, &infunc, &basetype);
  fmgr_info(infunc, &infuncinfo);
  return InputFunctionCall(&infuncinfo, str, basetype, -1);
}

/**
 * Call output function of the base type
 */
char *
call_output(Oid type, Datum value)
{
  Oid outfunc;
  bool isvarlena;
  FmgrInfo outfuncinfo;
  getTypeOutputInfo(type, &outfunc, &isvarlena);
  fmgr_info(outfunc, &outfuncinfo);
  return OutputFunctionCall(&outfuncinfo, value);
}

/**
 * Call send function of the base type
 */
bytea *
call_send(Oid type, Datum value)
{
  Oid sendfunc;
  bool isvarlena;
  FmgrInfo sendfuncinfo;
  getTypeBinaryOutputInfo(type, &sendfunc, &isvarlena);
  fmgr_info(sendfunc, &sendfuncinfo);
  return SendFunctionCall(&sendfuncinfo, value);
}

/**
 * Call receive function of the base type
 */
Datum
call_recv(Oid type, StringInfo buf)
{
  Oid recvfunc;
  Oid basetype;
  FmgrInfo recvfuncinfo;
  getTypeBinaryInputInfo(type, &recvfunc, &basetype);
  fmgr_info(recvfunc, &recvfuncinfo);
  return ReceiveFunctionCall(&recvfuncinfo, buf, basetype, -1);
}

/**
 * Call PostgreSQL function with 1 argument
 */
#if MOBDB_PGSQL_VERSION >= 120000
Datum
call_function1(PGFunction func, Datum arg1)
{
  LOCAL_FCINFO(fcinfo, 1);
  FmgrInfo flinfo;
  memset(&flinfo, 0, sizeof(flinfo));
  flinfo.fn_mcxt = CurrentMemoryContext;
  Datum result;
  InitFunctionCallInfoData(*fcinfo, &flinfo, 1, DEFAULT_COLLATION_OID, NULL, NULL);
  fcinfo->args[0].value = arg1;
  fcinfo->args[0].isnull = false;
  result = (*func) (fcinfo);
  if (fcinfo->isnull)
    elog(ERROR, "Function %p returned NULL", (void *) func);
  return result;
}

/**
 * Call PostgreSQL function with 2 arguments
 */
Datum
call_function2(PGFunction func, Datum arg1, Datum arg2)
{
  LOCAL_FCINFO(fcinfo, 2);
  FmgrInfo flinfo;
  memset(&flinfo, 0, sizeof(flinfo)) ;
  flinfo.fn_nargs = 2;
  flinfo.fn_mcxt = CurrentMemoryContext;
  Datum result;
  InitFunctionCallInfoData(*fcinfo, &flinfo, 2, DEFAULT_COLLATION_OID, NULL, NULL);
  fcinfo->args[0].value = arg1;
  fcinfo->args[0].isnull = false;
  fcinfo->args[1].value = arg2;
  fcinfo->args[1].isnull = false;
  result = (*func) (fcinfo);
  if (fcinfo->isnull)
    elog(ERROR, "function %p returned NULL", (void *) func);
  return result;
}

/**
 * Call PostgreSQL function with 3 arguments
 */
Datum
call_function3(PGFunction func, Datum arg1, Datum arg2, Datum arg3)
{
  LOCAL_FCINFO(fcinfo, 3);
  FmgrInfo flinfo;
  memset(&flinfo, 0, sizeof(flinfo)) ;
  flinfo.fn_mcxt = CurrentMemoryContext;
  Datum result;
  InitFunctionCallInfoData(*fcinfo, &flinfo, 3, DEFAULT_COLLATION_OID, NULL, NULL);
  fcinfo->args[0].value = arg1;
  fcinfo->args[0].isnull = false;
  fcinfo->args[1].value = arg2;
  fcinfo->args[1].isnull = false;
  fcinfo->args[2].value = arg3;
  fcinfo->args[2].isnull = false;
  result = (*func) (fcinfo);
  if (fcinfo->isnull)
    elog(ERROR, "function %p returned NULL", (void *) func);
  return result;
}
#else /* MOBDB_PGSQL_VERSION < 120000 */
/**
 * Call PostgreSQL function with 1 argument
 */
Datum
call_function1(PGFunction func, Datum arg1)
{
  FunctionCallInfoData fcinfo;
  FmgrInfo flinfo;
  memset(&flinfo, 0, sizeof(flinfo));
  flinfo.fn_mcxt = CurrentMemoryContext;
  Datum result;
  InitFunctionCallInfoData(fcinfo, &flinfo, 1, DEFAULT_COLLATION_OID, NULL, NULL);
  fcinfo.arg[0] = arg1;
  fcinfo.argnull[0] = false;
  result = (*func) (&fcinfo);
  if (fcinfo.isnull)
    elog(ERROR, "Function %p returned NULL", (void *) func);
  return result;
}

/**
 * Call PostgreSQL function with 2 arguments
 */
Datum
call_function2(PGFunction func, Datum arg1, Datum arg2)
{
  FunctionCallInfoData fcinfo;
  FmgrInfo flinfo;
  memset(&flinfo, 0, sizeof(flinfo)) ;
  flinfo.fn_mcxt = CurrentMemoryContext;
  Datum result;
  InitFunctionCallInfoData(fcinfo, &flinfo, 2, DEFAULT_COLLATION_OID, NULL, NULL);
  fcinfo.arg[0] = arg1;
  fcinfo.argnull[0] = false;
  fcinfo.arg[1] = arg2;
  fcinfo.argnull[1] = false;
  result = (*func) (&fcinfo);
  if (fcinfo.isnull)
    elog(ERROR, "function %p returned NULL", (void *) func);
  return result;
}

/**
 * Call PostgreSQL function with 3 arguments
 */
Datum
call_function3(PGFunction func, Datum arg1, Datum arg2, Datum arg3)
{
  FunctionCallInfoData fcinfo;
  FmgrInfo flinfo;
  memset(&flinfo, 0, sizeof(flinfo)) ;
  flinfo.fn_mcxt = CurrentMemoryContext;
  Datum result;
  InitFunctionCallInfoData(fcinfo, &flinfo, 3, DEFAULT_COLLATION_OID, NULL, NULL);
  fcinfo.arg[0] = arg1;
  fcinfo.argnull[0] = false;
  fcinfo.arg[1] = arg2;
  fcinfo.argnull[1] = false;
  fcinfo.arg[2] = arg3;
  fcinfo.argnull[2] = false;
  result = (*func) (&fcinfo);
  if (fcinfo.isnull)
    elog(ERROR, "function %p returned NULL", (void *) func);
  return result;
}
#endif

/*****************************************************************************/

/* CallerFInfoFunctionCall 1 to 3 are provided by PostGIS */

#if MOBDB_PGSQL_VERSION < 120000
Datum
CallerFInfoFunctionCall4(PGFunction func, FmgrInfo *flinfo, Oid collation,
  Datum arg1, Datum arg2, Datum arg3, Datum arg4)
{
  FunctionCallInfoData fcinfo;
  Datum    result;

  InitFunctionCallInfoData(fcinfo, flinfo, 3, collation, NULL, NULL);

  fcinfo.arg[0] = arg1;
  fcinfo.arg[1] = arg2;
  fcinfo.arg[2] = arg3;
  fcinfo.arg[3] = arg4;
  fcinfo.argnull[0] = false;
  fcinfo.argnull[1] = false;
  fcinfo.argnull[2] = false;
  fcinfo.argnull[3] = false;

  result = (*func) (&fcinfo);

  /* Check for null result, since caller is clearly not expecting one */
  if (fcinfo.isnull)
    elog(ERROR, "function %p returned NULL", (void *) func);

  return result;
}
#else
/* PgSQL 12+ still lacks 3-argument version of these functions */
Datum
CallerFInfoFunctionCall4(PGFunction func, FmgrInfo *flinfo, Oid collation,
  Datum arg1, Datum arg2, Datum arg3, Datum arg4)
{
    LOCAL_FCINFO(fcinfo, 4);
    Datum       result;

    InitFunctionCallInfoData(*fcinfo, flinfo, 3, collation, NULL, NULL);

    fcinfo->args[0].value = arg1;
    fcinfo->args[0].isnull = false;
    fcinfo->args[1].value = arg2;
    fcinfo->args[1].isnull = false;
    fcinfo->args[2].value = arg3;
    fcinfo->args[2].isnull = false;
    fcinfo->args[3].value = arg4;
    fcinfo->args[3].isnull = false;

    result = (*func) (fcinfo);

    /* Check for null result, since caller is clearly not expecting one */
    if (fcinfo->isnull)
      elog(ERROR, "function %p returned NULL", (void *) func);

    return result;
}
#endif

/*****************************************************************************
 * Array functions
 *****************************************************************************/

/**
 * Free a C array of pointers
 */
void
pfree_array(void **array, int count)
{
  for (int i = 0; i < count; i++)
    pfree(array[i]);
  pfree(array);
  return;
}

/**
 * Free a C array of Datum pointers
 */
void
pfree_datumarr(Datum *array, int count)
{
  for (int i = 0; i < count; i++)
    pfree(DatumGetPointer(array[i]));
  pfree(array);
  return;
}

/**
 * Returns the string resulting from assembling the array of strings.
 * The function frees the memory of the input strings after finishing.
 */
char *
stringarr_to_string(char **strings, int count, int outlen,
  char *prefix, char open, char close)
{
  char *result = palloc(strlen(prefix) + outlen + 3);
  result[outlen] = '\0';
  size_t pos = 0;
  strcpy(result, prefix);
  pos += strlen(prefix);
  result[pos++] = open;
  for (int i = 0; i < count; i++)
  {
    strcpy(result + pos, strings[i]);
    pos += strlen(strings[i]);
    result[pos++] = ',';
    result[pos++] = ' ';
    pfree(strings[i]);
  }
  result[pos - 2] = close;
  result[pos - 1] = '\0';
  pfree(strings);
  return result;
}

/**
 * Extract a C array from a PostgreSQL array containing datums
 */
Datum *
datumarr_extract(ArrayType *array, int *count)
{
  bool byval;
  int16 typlen;
  char align;
  get_typlenbyvalalign(array->elemtype, &typlen, &byval, &align);
  Datum *result;
  deconstruct_array(array, array->elemtype, typlen, byval, align,
    &result, NULL, count);
  return result;
}

/**
 * Extract a C array from a PostgreSQL array containing timestamps
 */
TimestampTz *
timestamparr_extract(ArrayType *array, int *count)
{
  return (TimestampTz *) datumarr_extract(array, count);
}

/**
 * Extract a C array from a PostgreSQL array containing periods
 */
Period **
periodarr_extract(ArrayType *array, int *count)
{
  return (Period **) datumarr_extract(array, count);
}

/**
 * Extract a C array from a PostgreSQL array containing ranges
 */
RangeType **
rangearr_extract(ArrayType *array, int *count)
{
  return (RangeType **) datumarr_extract(array, count);
}

/**
 * Extract a C array from a PostgreSQL array containing temporal values
 */
Temporal **
temporalarr_extract(ArrayType *array, int *count)
{
  Temporal **result;
  deconstruct_array(array, array->elemtype, -1, false, 'd',
    (Datum **) &result, NULL, count);
  return result;
}

/*****************************************************************************/

/**
 * Convert a C array of datums into a PostgreSQL array
 */
ArrayType *
datumarr_to_array(Datum *values, int count, Oid type)
{
  int16 elmlen;
  bool elmbyval;
  char elmalign;
  assert(count > 0);
  get_typlenbyvalalign(type, &elmlen, &elmbyval, &elmalign);
  ArrayType *result = construct_array(values, count, type, elmlen, elmbyval, elmalign);
  return result;
}

/**
 * Convert a C array of timestamps into a PostgreSQL array
 */
ArrayType *
timestamparr_to_array(const TimestampTz *times, int count)
{
  assert(count > 0);
  ArrayType *result = construct_array((Datum *) times, count, TIMESTAMPTZOID, 8,
    true, 'd');
  return result;
}

/**
 * Convert a C array of periods into a PostgreSQL array
 */
ArrayType *
periodarr_to_array(const Period **periods, int count)
{
  assert(count > 0);
  ArrayType *result = construct_array((Datum *) periods, count,
    type_oid(T_PERIOD), sizeof(Period), false, 'd');
  return result;
}

/**
 * Convert a C array of ranges into a PostgreSQL array
 */
ArrayType *
rangearr_to_array(RangeType **ranges, int count, Oid type)
{
  assert(count > 0);
  ArrayType *result = construct_array((Datum *) ranges, count, type, -1,
    false, 'd');
  return result;
}

/**
 * Convert a C array of text values into a PostgreSQL array
 */
ArrayType *
textarr_to_array(text **textarr, int count)
{
  assert(count > 0);
  ArrayType *result = construct_array((Datum *) textarr, count, TEXTOID, -1,
    false, 'i');
  return result;
}

/**
 * Convert a C array of temporal values into a PostgreSQL array
 */
ArrayType *
temporalarr_to_array(const Temporal **temporalarr, int count)
{
  assert(count > 0);
  Oid type = temporal_oid_from_base(temporalarr[0]->basetypid);
  ArrayType *result = construct_array((Datum *) temporalarr, count, type, -1,
    false, 'd');
  return result;
}

/**
 * Convert a C array of spatiotemporal boxes into a PostgreSQL array
 */
ArrayType *
stboxarr_to_array(STBOX *boxarr, int count)
{
  assert(count > 0);
  STBOX **boxes = palloc(sizeof(STBOX *) * count);
  for (int i = 0; i < count; i++)
    boxes[i] = &boxarr[i];
  ArrayType *result = construct_array((Datum *) boxes, count,
    type_oid(T_STBOX), sizeof(STBOX), false, 'd');
  pfree(boxes);
  return result;
}

/*****************************************************************************
 * Sort functions
 *****************************************************************************/

/**
 * Comparator function for datums
 */
static int
datum_sort_cmp(const Datum *l, const Datum *r, const Oid *type)
{
  Datum x = *l;
  Datum y = *r;
  Oid t = *type;
  if (datum_eq(x, y, t))
    return 0;
  else if (datum_lt(x, y, t))
    return -1;
  else
    return 1;
}

/**
 * Comparator function for timestamps
 */
static int
timestamp_sort_cmp(const TimestampTz *l, const TimestampTz *r)
{
  TimestampTz x = *l;
  TimestampTz y = *r;
  return timestamp_cmp_internal(x, y);
}

/**
 * Comparator function for periods
 */
static int
period_sort_cmp(const Period **l, const Period **r)
{
  return period_cmp_internal(*l, *r);
}

/**
 * Comparator function for ranges
 */
static int
range_sort_cmp(const RangeType **l, const RangeType **r)
{
#if MOBDB_PGSQL_VERSION < 110000
  return DatumGetInt32(call_function2(range_cmp, RangeTypeGetDatum(*l),
    RangeTypeGetDatum(*r)));
#else
  return DatumGetInt32(call_function2(range_cmp, RangeTypePGetDatum(*l),
    RangeTypePGetDatum(*r)));
#endif
}

/**
 * Comparator function for temporal instants
 */
static int
tinstantarr_sort_cmp(const TInstant **l, const TInstant **r)
{
  return timestamp_cmp_internal((*l)->t, (*r)->t);
}

/**
 * Comparator function for temporal sequences
 */
static int
tsequencearr_sort_cmp(TSequence **l, TSequence **r)
{
  Period lp = (*l)->period;
  Period rp = (*r)->period;
  return period_cmp_internal(&lp, &rp);
}

/*****************************************************************************/

/**
 * Sort function for datums
 */
void
datumarr_sort(Datum *values, int count, Oid type)
{
  qsort_arg(values, (size_t) count, sizeof(Datum),
    (qsort_arg_comparator) &datum_sort_cmp, &type);
}

/**
 * Sort function for timestamps
 */
void
timestamparr_sort(TimestampTz *times, int count)
{
  qsort(times, (size_t) count, sizeof(TimestampTz),
    (qsort_comparator) &timestamp_sort_cmp);
}

/**
 * Sort function for double2
 * This function is currently not used
void
double2arr_sort(double2 *doubles, int count)
{
  qsort(doubles, count, sizeof(double2),
    (qsort_comparator) &double2_cmp);
}
*/

/**
 * Sort function for double3
 * This function is currently not used
void
double3arr_sort(double3 *triples, int count)
{
  qsort(triples, count, sizeof(double3),
    (qsort_comparator) &double3_cmp);
}
*/

/**
 * Sort function for periods
 */
void
periodarr_sort(Period **periods, int count)
{
  qsort(periods, (size_t) count, sizeof(Period *),
    (qsort_comparator) &period_sort_cmp);
}

/**
 * Sort function for ranges
 */
void
rangearr_sort(RangeType **ranges, int count)
{
  qsort(ranges, (size_t) count, sizeof(RangeType *),
    (qsort_comparator) &range_sort_cmp);
}

/**
 * Sort function for temporal instants
 */
void
tinstantarr_sort(TInstant **instants, int count)
{
  qsort(instants, (size_t) count, sizeof(TInstant *),
    (qsort_comparator) &tinstantarr_sort_cmp);
}

/**
 * Sort function for temporal sequences
 */
void
tsequencearr_sort(TSequence **sequences, int count)
{
  qsort(sequences, (size_t) count, sizeof(TSequence *),
    (qsort_comparator) &tsequencearr_sort_cmp);
}

/*****************************************************************************
 * Remove duplicate functions
 * These functions assume that the array has been sorted before
 *****************************************************************************/

/**
 * Remove duplicates from an array of datums
 */
int
datumarr_remove_duplicates(Datum *values, int count, Oid type)
{
  assert (count > 0);
  int newcount = 0;
  for (int i = 1; i < count; i++)
    if (datum_ne(values[newcount], values[i], type))
      values[++ newcount] = values[i];
  return newcount + 1;
}

/**
 * Remove duplicates from an array of timestamps
 */
int
timestamparr_remove_duplicates(TimestampTz *values, int count)
{
  assert (count > 0);
  int newcount = 0;
  for (int i = 1; i < count; i++)
    if (values[newcount] != values[i])
      values[++ newcount] = values[i];
  return newcount + 1;
}

/**
 * Remove duplicates from an array of temporal instants
 */
int
tinstantarr_remove_duplicates(const TInstant **instants, int count)
{
  assert(count != 0);
  int newcount = 0;
  for (int i = 1; i < count; i++)
    if (! tinstant_eq(instants[newcount], instants[i]))
      instants[++ newcount] = instants[i];
  return newcount + 1;
}

/*****************************************************************************
 * Text functions
 *****************************************************************************/

/**
 * Comparison function for text values
 *
 * @note Function copied from PostgreSQL since it is not exported
 */
int
text_cmp(text *arg1, text *arg2, Oid collid)
{
  char  *a1p,
      *a2p;
  int    len1,
      len2;

  a1p = VARDATA_ANY(arg1);
  a2p = VARDATA_ANY(arg2);

  len1 = (int) VARSIZE_ANY_EXHDR(arg1);
  len2 = (int) VARSIZE_ANY_EXHDR(arg2);

  return varstr_cmp(a1p, len1, a2p, len2, collid);
}

/*****************************************************************************
 * Arithmetic functions on datums
 * N.B. The validity of the Oids must be done in the calling function.
 *****************************************************************************/

/**
 * Returns the addition of the two numbers
 */
Datum
datum_add(Datum l, Datum r, Oid typel, Oid typer)
{
  Datum result = 0;
  if (typel == INT4OID)
  {
    if (typer == INT4OID)
      result = Int32GetDatum(DatumGetInt32(l) + DatumGetInt32(r));
    else /* typer == FLOAT8OID */
      result = Float8GetDatum(DatumGetInt32(l) + DatumGetFloat8(r));
  }
  else /* typel == FLOAT8OID */
  {
    if (typer == INT4OID)
      result = Float8GetDatum(DatumGetFloat8(l) + DatumGetInt32(r));
    else /* typer == FLOAT8OID */
      result = Float8GetDatum(DatumGetFloat8(l) + DatumGetFloat8(r));
  }
  return result;
}

/**
 * Returns the subtraction of the two numbers
 */
Datum
datum_sub(Datum l, Datum r, Oid typel, Oid typer)
{
  Datum result = 0;
  if (typel == INT4OID)
  {
    if (typer == INT4OID)
      result = Int32GetDatum(DatumGetInt32(l) - DatumGetInt32(r));
    else /* typer == FLOAT8OID */
      result = Float8GetDatum(DatumGetInt32(l) - DatumGetFloat8(r));
  }
  else /* typel == FLOAT8OID */
  {
    if (typer == INT4OID)
      result = Float8GetDatum(DatumGetFloat8(l) - DatumGetInt32(r));
    else /* typer == FLOAT8OID */
      result = Float8GetDatum(DatumGetFloat8(l) - DatumGetFloat8(r));
  }
  return result;
}

/**
 * Returns the multiplication of the two numbers
 */
Datum
datum_mult(Datum l, Datum r, Oid typel, Oid typer)
{
  Datum result = 0;
  if (typel == INT4OID)
  {
    if (typer == INT4OID)
      result = Int32GetDatum(DatumGetInt32(l) * DatumGetInt32(r));
    else /* typer == FLOAT8OID */
      result = Float8GetDatum(DatumGetInt32(l) * DatumGetFloat8(r));
  }
  else /* typel == FLOAT8OID */
  {
    if (typer == INT4OID)
      result = Float8GetDatum(DatumGetFloat8(l) * DatumGetInt32(r));
    else /* typer == FLOAT8OID */
      result = Float8GetDatum(DatumGetFloat8(l) * DatumGetFloat8(r));
  }
  return result;
}

/**
 * Returns the division of the two numbers
 */
Datum
datum_div(Datum l, Datum r, Oid typel, Oid typer)
{
  Datum result = 0;
  if (typel == INT4OID)
  {
    if (typer == INT4OID)
      result = Int32GetDatum(DatumGetInt32(l) / DatumGetInt32(r));
    else /* typer == FLOAT8OID */
      result = Float8GetDatum(DatumGetInt32(l) / DatumGetFloat8(r));
  }
  else /* typel == FLOAT8OID */
  {
    if (typer == INT4OID)
      result = Float8GetDatum(DatumGetFloat8(l) / DatumGetInt32(r));
    else /* typer == FLOAT8OID */
      result = Float8GetDatum(DatumGetFloat8(l) / DatumGetFloat8(r));
  }
  return result;
}

/*****************************************************************************/

/**
 * Determine the 3D hypotenuse.
 *
 * If required, x, y, and z are swapped to make x the larger number. The
 * traditional formula of x^2+y^2+z^2 is rearranged to factor x outside the
 * sqrt. This allows computation of the hypotenuse for significantly
 * larger values, and with a higher precision than when using the naive
 * formula. In particular, this cannot overflow unless the final result
 * would be out-of-range.
 * @code
 * sqrt( x^2 + y^2 + z^2 ) = sqrt( x^2( 1 + y^2/x^2 + z^2/x^2) )
 *                         = x * sqrt( 1 + y^2/x^2 + z^2/x^2)
 *                         = x * sqrt( 1 + y/x * y/x + z/x * z/x)
 * @endcode
 */
double
hypot3d(double x, double y, double z)
{
  double yx;
  double zx;
  double temp;

  /* Handle INF and NaN properly */
  if (isinf(x) || isinf(y) || isinf(z))
    return get_float8_infinity();

  if (isnan(x) || isnan(y) || isnan(z))
    return get_float8_nan();

  /* Else, drop any minus signs */
  x = fabs(x);
  y = fabs(y);
  z = fabs(z);

  /* Swap x, y and z if needed to make x the larger one */
  if (x < y)
  {
    temp = x;
    x = y;
    y = temp;
  }
  if (x < z)
  {
    temp = x;
    x = z;
    z = temp;
  }
  /*
   * If x is zero, the hypotenuse is computed with the 2D case.
   * This test saves a few cycles in such cases, but more importantly
   * it also protects against divide-by-zero errors, since now x >= y.
   */
  if (x == 0)
    return hypot(y, z);

  /* Determine the hypotenuse */
  yx = y / x;
  zx = z / x;
  return x * sqrt(1.0 + (yx * yx) + (zx * zx));
}

/**
 * Determine the 4D hypotenuse.
 *
 * @see The function is a generalization of the 3D case in function hypot3d
 */
double
hypot4d(double x, double y, double z, double m)
{
  double yx;
  double zx;
  double mx;
  double temp;

  /* Handle INF and NaN properly */
  if (isinf(x) || isinf(y) || isinf(z) || isinf(m))
    return get_float8_infinity();

  if (isnan(x) || isnan(y) || isnan(z) || isnan(m))
    return get_float8_nan();

  /* Else, drop any minus signs */
  x = fabs(x);
  y = fabs(y);
  z = fabs(z);
  m = fabs(m);

  /* Swap x, y, z, and m if needed to make x the larger one */
  if (x < y)
  {
    temp = x;
    x = y;
    y = temp;
  }
  if (x < z)
  {
    temp = x;
    x = z;
    z = temp;
  }
  if (x < m)
  {
    temp = x;
    x = m;
    m = temp;
  }
  /*
   * If x is zero, the hypotenuse is computed with the 3D case.
   * This test saves a few cycles in such cases, but more importantly
   * it also protects against divide-by-zero errors, since now x >= y.
   */
  if (x == 0)
    return hypot3d(y, z, m);

  /* Determine the hypotenuse */
  yx = y / x;
  zx = z / x;
  mx = m / x;
  return x * sqrt(1.0 + (yx * yx) + (zx * zx) + (mx * mx));
}

/*****************************************************************************/
