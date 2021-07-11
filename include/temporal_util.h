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

#ifndef __TEMPORAL_UTIL_H__
#define __TEMPORAL_UTIL_H__

#include <postgres.h>
#include <catalog/pg_type.h>
#include <utils/array.h>
#if MOBDB_PGSQL_VERSION >= 120000
#include <utils/float.h>
#endif
#include <utils/rangetypes.h>

#include "temporal.h"
#include "postgis.h"

/*****************************************************************************/

/* Temporal/base types tests */

extern bool temporal_type(Oid temptypid);
extern void ensure_temporal_base_type(Oid basetypid);
extern bool base_type_continuous(Oid basetypid);
extern void ensure_base_type_continuous(Temporal *temp);
extern bool base_type_byvalue(Oid basetypid);
extern int16 base_type_length(Oid basetypid);
extern bool talpha_base_type(Oid basetypid);
extern bool tnumber_type(Oid temptypid);
extern bool tnumber_base_type(Oid basetypid);
extern void ensure_tnumber_base_type(Oid basetypid);
extern bool tnumber_range_type(Oid rangetype);
extern void ensure_tnumber_range_type(Oid rangetype);
extern bool tspatial_type(Oid temptypid);
extern bool tspatial_base_type(Oid basetypid);
extern bool tgeo_base_type(Oid basetypid);
extern void ensure_tgeo_base_type(Oid basetypid);
extern bool type_has_precomputed_trajectory(Oid basetypid);
extern size_t temporal_bbox_size(Oid basetypid);

/* Oid functions */

extern Oid range_oid_from_base(Oid basetypid);
extern Oid temporal_oid_from_base(Oid basetypid);
extern Oid base_oid_from_temporal(Oid temptypid);

/* Miscellaneous functions */

extern size_t double_pad(size_t size);
extern Datum datum_copy(Datum value, Oid type);
extern double datum_double(Datum d, Oid basetypid);
extern char *text2cstring(const text *textptr);

/* PostgreSQL call helpers */

extern Datum call_input(Oid type, char *str);
extern char *call_output(Oid type, Datum value);
extern bytea *call_send(Oid type, Datum value);
extern Datum call_recv(Oid type, StringInfo buf);
extern Datum call_function1(PGFunction func, Datum arg1);
extern Datum call_function2(PGFunction func, Datum arg1, Datum arg2);
extern Datum call_function3(PGFunction func, Datum arg1, Datum arg2,
  Datum arg3);
extern Datum call_function4(PGFunction func, Datum arg1, Datum arg2,
  Datum arg3, Datum arg4);

extern Datum CallerFInfoFunctionCall4(PGFunction func, FmgrInfo *flinfo,
  Oid collation, Datum arg1, Datum arg2, Datum arg3, Datum arg4);

extern Datum CallerFInfoFunctionCall4(PGFunction func, FmgrInfo *flinfo,
    Oid collation, Datum arg1, Datum arg2, Datum arg3, Datum arg4);

/* Array functions */

extern void pfree_array(void **array, int count);
extern void pfree_datumarr(Datum *array, int count);
extern char *stringarr_to_string(char **strings, int count, int outlen,
  char *prefix, char open, char close);
extern Datum *datumarr_extract(ArrayType *array, int *count);
extern TimestampTz *timestamparr_extract(ArrayType *array, int *count);
extern Period **periodarr_extract(ArrayType *array, int *count);
extern RangeType **rangearr_extract(ArrayType *array, int *count);
extern Temporal **temporalarr_extract(ArrayType *array, int *count);

extern ArrayType *datumarr_to_array(Datum *values, int count, Oid type);
extern ArrayType *timestamparr_to_array(const TimestampTz *times, int count);
extern ArrayType *periodarr_to_array(const Period **periods, int count);
extern ArrayType *rangearr_to_array(RangeType **ranges, int count, Oid type);
extern ArrayType *textarr_to_array(text **textarr, int count);
extern ArrayType *temporalarr_to_array(const Temporal **temporal, int count);
extern ArrayType *stboxarr_to_array(STBOX *boxarr, int count);

/* Sort functions */

extern void datumarr_sort(Datum *values, int count, Oid basetypid);
extern void timestamparr_sort(TimestampTz *times, int count);
extern void double2arr_sort(double2 *doubles, int count);
extern void double3arr_sort(double3 *triples, int count);
extern void periodarr_sort(Period **periods, int count);
extern void rangearr_sort(RangeType **ranges, int count);
extern void tinstantarr_sort(TInstant **instants, int count);
extern void tsequencearr_sort(TSequence **sequences, int count);

/* Remove duplicate functions */

extern int datumarr_remove_duplicates(Datum *values, int count,
  Oid basetypid);
extern int timestamparr_remove_duplicates(TimestampTz *values, int count);
extern int tinstantarr_remove_duplicates(const TInstant **instants, int count);

/* Text functions */

extern int text_cmp(text *arg1, text *arg2, Oid collid);

/* Arithmetic functions */

extern Datum datum_add(Datum l, Datum r, Oid typel, Oid typer);
extern Datum datum_sub(Datum l, Datum r, Oid typel, Oid typer);
extern Datum datum_mult(Datum l, Datum r, Oid typel, Oid typer);
extern Datum datum_div(Datum l, Datum r, Oid typel, Oid typer);

/* Comparison functions on datums */

extern bool datum_eq(Datum l, Datum r, Oid type);
extern bool datum_ne(Datum l, Datum r, Oid type);
extern bool datum_lt(Datum l, Datum r, Oid type);
extern bool datum_le(Datum l, Datum r, Oid type);
extern bool datum_gt(Datum l, Datum r, Oid type);
extern bool datum_ge(Datum l, Datum r, Oid type);

extern bool datum_eq2(Datum l, Datum r, Oid typel, Oid typer);
extern bool datum_ne2(Datum l, Datum r, Oid typel, Oid typer);
extern bool datum_lt2(Datum l, Datum r, Oid typel, Oid typer);
extern bool datum_le2(Datum l, Datum r, Oid typel, Oid typer);
extern bool datum_gt2(Datum l, Datum r, Oid typel, Oid typer);
extern bool datum_ge2(Datum l, Datum r, Oid typel, Oid typer);

extern Datum datum2_eq2(Datum l, Datum r, Oid typel, Oid typer);
extern Datum datum2_ne2(Datum l, Datum r, Oid typel, Oid typer);
extern Datum datum2_lt2(Datum l, Datum r, Oid typel, Oid typer);
extern Datum datum2_le2(Datum l, Datum r, Oid typel, Oid typer);
extern Datum datum2_gt2(Datum l, Datum r, Oid typel, Oid typer);
extern Datum datum2_ge2(Datum l, Datum r, Oid typel, Oid typer);

/* Hypothenuse functions */

extern double hypot3d(double x, double y, double z);
extern double hypot4d(double x, double y, double z, double m);

/*****************************************************************************/

#endif
