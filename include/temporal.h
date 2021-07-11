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
 * @file temporal.h
 * Basic functions for temporal types of any subtype.
 */

#ifndef __TEMPORAL_H__
#define __TEMPORAL_H__

#include <postgres.h>
#include <catalog/pg_type.h>
#include <lib/stringinfo.h>
#include <utils/array.h>
#include <utils/rangetypes.h>

#include "timetypes.h"
#include "tbox.h"
#include "stbox.h"

#if MOBDB_PGSQL_VERSION < 130000
#ifndef USE_FLOAT4_BYVAL
#error Postgres needs to be configured with USE_FLOAT4_BYVAL
#endif
#endif

#ifndef USE_FLOAT8_BYVAL
#error Postgres needs to be configured with USE_FLOAT8_BYVAL
#endif

/**
 * Floating point precision
 */
#define EPSILON         1.0E-05
/**
 * Precision for distance operations
 */
#define DIST_EPSILON    1.0E-05

/** Symbolic constants for lifting */
#define DISCONTINUOUS   true
#define CONTINUOUS      false

/** Symbolic constants for lifting */
#define INVERT          true
#define INVERT_NO       false

/** Symbolic constants for the restriction functions */
#define REST_AT         true
#define REST_MINUS      false

/** Symbolic constants for the ever/always functions */
#define EVER            true
#define ALWAYS          false

/** Symbolic constants for the restriction and the aggregation functions */
#define MIN             true
#define MAX             false

/** Symbolic constants for the synchronization and the aggregation functions */
#define CROSSINGS       true
#define CROSSINGS_NO    false

/** Symbolic constants for the synchronization and the aggregation functions */
#define BBOX_TEST       true
#define BBOX_TEST_NO    false

/** Symbolic constants for the make functions */
#define MERGE           true
#define MERGE_NO        false

#define NORMALIZE       true
#define NORMALIZE_NO    false

#define LINEAR          true
#define STEP            false

/** Symbolic constants for spatial relationships */
#define WITH_Z          true
#define NO_Z            false

/* Determine whether reduce the roundoff errors with the range operations
 * by taking the bounds instead of the projected value at the timestamp */
#define RANGE_ROUNDOFF  false

/** Enumeration for the intersection/synchronization functions */
typedef enum
{
  SYNCHRONIZE,
  SYNCHRONIZE_CROSS,
} TIntersection;

/*****************************************************************************
 * Floating comparators
 * Definitions copied from liblwgeom_internal.h
 *****************************************************************************/

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

/*****************************************************************************
 * Compatibility with older versions of PostgreSQL
 *****************************************************************************/

#define MOBDB_VERSION_STR "MobilityDB 1.0 beta 3"
#ifndef MOBDB_PGSQL_VERSION
#error Unconfigured source code -- please run cmake
#endif
#ifndef MOBDB_PGSQL_VERSION_STR
#error Unconfigured source code -- please run cmake
#endif

#define MOBDB_POSTGIS_VERSION 25
#define MOBDB_POSTGIS_VERSION_STR "PostGIS 2.5"

/*****************************************************************************
 * Concrete subtype of temporal types
 *****************************************************************************/

/**
 * Enumeration for the concrete subtype of temporal types
 */
#define ANYTEMPSUBTYPE  0
#define INSTANT         1
#define INSTANTSET      2
#define SEQUENCE        3
#define SEQUENCESET     4

#define TYPMOD_GET_SUBTYPE(typmod) ((int16) ((typmod == -1) ? (0) : (typmod & 0x0000000F)))

/**
 * Structure to represent the temporal subtype array
 */
struct tempsubtype_struct
{
  char *subtypeName;   /**< string representing the temporal type */
  int16 subtype;       /**< subtype */
};

#define TEMPSUBTYPE_STRUCT_ARRAY_LEN \
  (sizeof tempsubtype_struct_array/sizeof(struct tempsubtype_struct))
#define TEMPSUBTYPE_MAX_LEN   13

/*****************************************************************************
 * Macros for manipulating the 'flags' element with structure xGTZXLCB, where
 * x:unused bit, G:Coordinates are geodetic, T:has T coordinate,
 * Z:has Z coordinate, X:has value or X coordinate,
 * B:base type passed by value, L: Linear interpolation, C: Continuous base type
 * Notice that formally speaking the Linear interpolation flag is only needed
 * for sequence and sequence set subtypes. To facilate the transformation from
 * one subtype to another, the linear flag for instant and instant set is set
 * to the value of the continuous subtype flag.
 *****************************************************************************/

/* The following flag is only used for TInstant */
#define MOBDB_FLAGS_GET_BYVAL(flags)      ((bool) ((flags) & 0x01))
#define MOBDB_FLAGS_GET_CONTINUOUS(flags) ((bool) (((flags) & 0x02)>>1))
#define MOBDB_FLAGS_GET_LINEAR(flags)     ((bool) (((flags) & 0x04)>>2))
#define MOBDB_FLAGS_GET_X(flags)          ((bool) (((flags) & 0x08)>>3))
#define MOBDB_FLAGS_GET_Z(flags)          ((bool) (((flags) & 0x10)>>4))
#define MOBDB_FLAGS_GET_T(flags)          ((bool) (((flags) & 0x20)>>5))
#define MOBDB_FLAGS_GET_GEODETIC(flags)   ((bool) (((flags) & 0x40)>>6))

/* The following flag is only used for TInstant */
#define MOBDB_FLAGS_SET_BYVAL(flags, value) \
  ((flags) = (value) ? ((flags) | 0x01) : ((flags) & 0xFE))
#define MOBDB_FLAGS_SET_CONTINUOUS(flags, value) \
  ((flags) = (value) ? ((flags) | 0x02) : ((flags) & 0xFD))
#define MOBDB_FLAGS_SET_LINEAR(flags, value) \
  ((flags) = (value) ? ((flags) | 0x04) : ((flags) & 0xFB))
#define MOBDB_FLAGS_SET_X(flags, value) \
  ((flags) = (value) ? ((flags) | 0x08) : ((flags) & 0xF7))
#define MOBDB_FLAGS_SET_Z(flags, value) \
  ((flags) = (value) ? ((flags) | 0x10) : ((flags) & 0xEF))
#define MOBDB_FLAGS_SET_T(flags, value) \
  ((flags) = (value) ? ((flags) | 0x20) : ((flags) & 0xDF))
#define MOBDB_FLAGS_SET_GEODETIC(flags, value) \
  ((flags) = (value) ? ((flags) | 0x40) : ((flags) & 0xBF))

/*****************************************************************************
 * Definitions for bucketing and tiling
 *****************************************************************************/

/*
 * The default origin is Monday 2000-01-03. We don't use PG epoch since it
 * starts on a saturday. This makes time-buckets by a week more intuitive and
 * aligns it with date_trunc.
 */
#define JAN_3_2000 (2 * USECS_PER_DAY)
#define DEFAULT_TIME_ORIGIN (JAN_3_2000)
#define DEFAULT_FLOATRANGE_ORIGIN (0.0)
#define DEFAULT_INTRANGE_ORIGIN (0)

/*****************************************************************************
 * Definitions for GiST indexes
 *****************************************************************************/

/* Minimum accepted ratio of split */
#define LIMIT_RATIO 0.3

/* Convenience macros for NaN-aware comparisons */
#define FLOAT8_EQ(a,b)  (float8_cmp_internal(a, b) == 0)
#define FLOAT8_LT(a,b)  (float8_cmp_internal(a, b) < 0)
#define FLOAT8_LE(a,b)  (float8_cmp_internal(a, b) <= 0)
#define FLOAT8_GT(a,b)  (float8_cmp_internal(a, b) > 0)
#define FLOAT8_GE(a,b)  (float8_cmp_internal(a, b) >= 0)
#define FLOAT8_MAX(a,b)  (FLOAT8_GT(a, b) ? (a) : (b))
#define FLOAT8_MIN(a,b)  (FLOAT8_LT(a, b) ? (a) : (b))

/*****************************************************************************
 * Additional operator strategy numbers used in the GiST and SP-GiST temporal
 * opclasses with respect to those defined in the file stratnum.h
 *****************************************************************************/

#define RTOverBeforeStrategyNumber    28    /* for &<# */
#define RTBeforeStrategyNumber        29    /* for <<# */
#define RTAfterStrategyNumber         30    /* for #>> */
#define RTOverAfterStrategyNumber     31    /* for #&> */
#define RTOverFrontStrategyNumber     32    /* for &</ */
#define RTFrontStrategyNumber         33    /* for <</ */
#define RTBackStrategyNumber          34    /* for />> */
#define RTOverBackStrategyNumber      35    /* for /&> */

/*****************************************************************************
 * Struct definitions for temporal types
 *****************************************************************************/

/**
 * Structure to represent the common structure of temporal values of
 * any temporal subtype
 */
typedef struct
{
  int32         vl_len_;       /**< varlena header (do not touch directly!) */
  int16         subtype;       /**< subtype */
  int16         flags;         /**< flags */
  Oid           basetypid;     /**< base type's OID (4 bytes) */
  /* variable-length data follows, if any */
} Temporal;

/**
 * Structure to represent temporal values of instant subtype
 */
typedef struct
{
  int32         vl_len_;      /**< varlena header (do not touch directly!) */
  int16         subtype;      /**< subtype */
  int16         flags;        /**< flags */
  Oid           basetypid;    /**< base type's OID (4 bytes) */
  TimestampTz   t;            /**< timestamp (8 bytes) */
  /* variable-length data follows */
} TInstant;

/**
 * Structure to represent temporal values of instant set subtype
 */
typedef struct
{
  int32        vl_len_;       /**< varlena header (do not touch directly!) */
  int16        subtype;       /**< subtype */
  int16        flags;         /**< flags */
  Oid          basetypid;     /**< base type's OID (4 bytes) */
  int32        count;         /**< number of TInstant elements */
  size_t       offsets[1];    /**< beginning of variable-length data */
} TInstantSet;

/**
 * Structure to represent temporal values of sequence subtype
 */
typedef struct
{
  int32         vl_len_;      /**< varlena header (do not touch directly!) */
  int16         subtype;      /**< subtype */
  int16         flags;        /**< flags */
  Oid           basetypid;    /**< base type's OID (4 bytes) */
  int32         count;        /**< number of TInstant elements */
  Period        period;       /**< time span (24 bytes) */
  size_t        offsets[1];   /**< beginning of variable-length data */
} TSequence;

/**
 * Structure to represent temporal values of sequence set subtype
 */
typedef struct
{
  int32         vl_len_;      /**< varlena header (do not touch directly!) */
  int16         subtype;      /**< subtype */
  int16         flags;        /**< flags */
  Oid           basetypid;    /**< base type's OID (4 bytes) */
  int32         count;        /**< number of TSequence elements */
  int32         totalcount;   /**< total number of TInstant elements in all TSequence elements */
  size_t        offsets[1];   /**< beginning of variable-length data */
} TSequenceSet;

/**
 * Structure to represent all types of bounding boxes
 */
typedef union bboxunion
{
  Period    p;
  TBOX      b;
  STBOX     g;
} bboxunion;

/**
 * Structure to represent values of the internal type for computing aggregates
 * for temporal number types
 */
typedef struct
{
  double    a;
  double    b;
} double2;

/**
 * Structure to represent values of the internal type for computing aggregates
 * for 2D temporal point types
 */
typedef struct
{
  double    a;
  double    b;
  double    c;
} double3;

/**
 * Structure to represent values of the internal type for computing aggregates
 * for 3D temporal point types
 */
typedef struct
{
  double    a;
  double    b;
  double    c;
  double    d;
} double4;

/*****************************************************************************
 * Miscellaneous
 *****************************************************************************/

/* Definition of qsort comparator for integers */
typedef int (*qsort_comparator) (const void *a, const void *b);

/* Definition of a variadic function type for temporal lifting */
typedef Datum (*varfunc) (Datum, ...);

/* Definition of a binary function with two or three Datum arguments */
typedef Datum (*datum_func2) (Datum, Datum);
typedef Datum (*datum_func3) (Datum, Datum, Datum);

/*****************************************************************************
 * Struct definitions for GisT indexes copied from PostgreSQL
 *****************************************************************************/

/**
 * Structure to represent information about an entry that can be placed
 * to either group without affecting overlap over selected axis ("common entry").
 */
typedef struct
{
  /* Index of entry in the initial array */
  int      index;
  /* Delta between penalties of entry insertion into different groups */
  double    delta;
} CommonEntry;

/**
 * Structure to represent a projection of bounding box to an axis.
 */
typedef struct
{
  double    lower,
            upper;
} SplitInterval;

/*****************************************************************************
 * fmgr macros temporal types
 *****************************************************************************/

/* doubleN */

#define DatumGetDouble2P(X)       ((double2 *) DatumGetPointer(X))
#define Double2PGetDatum(X)       PointerGetDatum(X)
#define DatumGetDouble3P(X)       ((double3 *) DatumGetPointer(X))
#define Double3PGetDatum(X)       PointerGetDatum(X)
#define DatumGetDouble4P(X)       ((double4 *) DatumGetPointer(X))
#define Double4PGetDatum(X)       PointerGetDatum(X)

/* Temporal types */

#define DatumGetTemporal(X)       ((Temporal *) PG_DETOAST_DATUM(X))
#define DatumGetTInstant(X)       ((TInstant *) PG_DETOAST_DATUM(X))
#define DatumGetTInstantSet(X)    ((TInstantSet *) PG_DETOAST_DATUM(X))
#define DatumGetTSequence(X)      ((TSequence *) PG_DETOAST_DATUM(X))
#define DatumGetTSequenceSet(X)   ((TSequenceSet *) PG_DETOAST_DATUM(X))

#define PG_GETARG_TEMPORAL(X)     ((Temporal *) PG_GETARG_VARLENA_P(X))

#define PG_GETARG_ANYDATUM(X) (get_typlen(get_fn_expr_argtype(fcinfo->flinfo, X)) == -1 ? \
  PointerGetDatum(PG_GETARG_VARLENA_P(X)) : PG_GETARG_DATUM(X))

#define DATUM_FREE(value, basetypid) \
  do { \
    if (! base_type_byvalue(basetypid)) \
      pfree(DatumGetPointer(value)); \
  } while (0)

#define DATUM_FREE_IF_COPY(value, basetypid, n) \
  do { \
    if (! base_type_byvalue(basetypid) && DatumGetPointer(value) != PG_GETARG_POINTER(n)) \
      pfree(DatumGetPointer(value)); \
  } while (0)

/*
 * Define POSTGIS_FREE_IF_COPY_P if POSTGIS is not loaded.
 * This macro is based on PG_FREE_IF_COPY, except that it accepts two pointers.
 * See PG_FREE_IF_COPY comment in src/include/fmgr.h in postgres source code
 * for more details.
 */
#ifndef POSTGIS_FREE_IF_COPY_P
#define POSTGIS_FREE_IF_COPY_P(ptrsrc, ptrori) \
  do { \
    if ((Pointer) (ptrsrc) != (Pointer) (ptrori)) \
      pfree(ptrsrc); \
  } while (0)
#endif

/*****************************************************************************/

/* Initialization function */

extern void _PG_init(void);

/* Typmod functions */

extern const char *tempsubtype_name(int16 subtype);
extern bool tempsubtype_from_string(const char *str, int16 *subtype);

/* Parameter tests */

extern void ensure_valid_tempsubtype(int16 type);
extern void ensure_valid_tempsubtype_all(int16 type);
extern void ensure_seq_subtypes(int16 subtype);
extern void ensure_linear_interpolation(int16 flags);
extern void ensure_common_dimension(int16 flags1, int16 flags2);
extern void ensure_same_base_type(const Temporal *temp1,
  const Temporal *temp2);
extern void ensure_same_interpolation(const Temporal *temp1,
  const Temporal *temp2);
extern void ensure_increasing_timestamps(const TInstant *inst1,
  const TInstant *inst2, bool strict);
extern void ensure_valid_tinstantarr(const TInstant **instants, int count,
  bool merge, int16 subtype);
extern void ensure_valid_tsequencearr(const TSequence **sequences, int count);

extern void ensure_positive_datum(Datum size, Oid type);
extern void ensure_valid_duration(const Interval *duration);
extern void ensure_non_empty_array(ArrayType *array);

/* Miscellaneous functions */

extern Temporal *temporal_copy(const Temporal *temp);
extern Temporal *pg_getarg_temporal(const Temporal *temp);
extern bool intersection_temporal_temporal(const Temporal *temp1,
  const Temporal *temp2, TIntersection mode,
  Temporal **inter1, Temporal **inter2);

/* Version functions */

extern Datum mobilitydb_version(PG_FUNCTION_ARGS);
extern Datum mobilitydb_full_version(PG_FUNCTION_ARGS);

/* Input/output functions */

extern Datum temporal_in(PG_FUNCTION_ARGS);
extern Datum temporal_out(PG_FUNCTION_ARGS);
extern Datum temporal_send(PG_FUNCTION_ARGS);
extern Datum temporal_recv(PG_FUNCTION_ARGS);
extern Temporal* temporal_read(StringInfo buf, Oid basetypid);
extern void temporal_write(const Temporal* temp, StringInfo buf);

/* Constructor functions */

extern Datum tinstant_constructor(PG_FUNCTION_ARGS);
extern Datum tinstantset_constructor(PG_FUNCTION_ARGS);
extern Datum tlinearseq_constructor(PG_FUNCTION_ARGS);
extern Datum tstepseq_constructor(PG_FUNCTION_ARGS);
extern Datum tsequenceset_constructor(PG_FUNCTION_ARGS);

/* Tranformation functions */

extern Datum temporal_append_tinstant(PG_FUNCTION_ARGS);
extern Datum temporal_merge(PG_FUNCTION_ARGS);
extern Datum temporal_merge_array(PG_FUNCTION_ARGS);

extern Temporal *temporal_from_base(const Temporal *temp, Datum value,
  Oid basetypid, bool linear);

/* Cast functions */

extern Datum tint_to_tfloat(PG_FUNCTION_ARGS);
extern Datum temporal_to_period(PG_FUNCTION_ARGS);

extern Temporal *tint_to_tfloat_internal(Temporal *temp);

/* Accessor functions */

extern Datum temporal_subtype(PG_FUNCTION_ARGS);
extern Datum temporal_interpolation(PG_FUNCTION_ARGS);
extern Datum temporal_mem_size(PG_FUNCTION_ARGS);
extern Datum temporal_get_values(PG_FUNCTION_ARGS);
extern Datum temporal_get_time(PG_FUNCTION_ARGS);
extern Datum tinstant_get_value(PG_FUNCTION_ARGS);
extern Datum tnumber_to_tbox(PG_FUNCTION_ARGS);
extern Datum tnumber_value_range(PG_FUNCTION_ARGS);
extern Datum temporal_start_value(PG_FUNCTION_ARGS);
extern Datum temporal_end_value(PG_FUNCTION_ARGS);
extern Datum temporal_min_value(PG_FUNCTION_ARGS);
extern Datum temporal_max_value(PG_FUNCTION_ARGS);
extern Datum temporal_num_instants(PG_FUNCTION_ARGS);
extern Datum temporal_start_instant(PG_FUNCTION_ARGS);
extern Datum temporal_end_instant(PG_FUNCTION_ARGS);
extern Datum temporal_instant_n(PG_FUNCTION_ARGS);
extern Datum temporal_instants(PG_FUNCTION_ARGS);
extern Datum temporal_num_timestamps(PG_FUNCTION_ARGS);
extern Datum temporal_start_timestamp(PG_FUNCTION_ARGS);
extern Datum temporal_end_timestamp(PG_FUNCTION_ARGS);
extern Datum temporal_timestamp_n(PG_FUNCTION_ARGS);
extern Datum temporal_timestamps(PG_FUNCTION_ARGS);
extern Datum temporal_shift(PG_FUNCTION_ARGS);

extern ArrayType *temporal_timestamps_internal(const Temporal *temp);

extern const TInstant *tinstarr_inst_n(const Temporal *temp, int n);
extern PeriodSet *temporal_get_time_internal(const Temporal *temp);
extern Datum tfloat_ranges(const Temporal *temp);
extern const TInstant *temporal_min_instant(const Temporal *temp);
extern Datum temporal_min_value_internal(const Temporal *temp);
extern const TInstant *temporal_end_instant_internal(const Temporal *temp);
extern TimestampTz temporal_start_timestamp_internal(const Temporal *temp);
extern RangeType *tnumber_value_range_internal(const Temporal *temp);

/* Ever/always equal operators */

extern Datum temporal_ever_eq(PG_FUNCTION_ARGS);
extern Datum temporal_ever_ne(PG_FUNCTION_ARGS);
extern Datum temporal_ever_lt(PG_FUNCTION_ARGS);
extern Datum temporal_ever_le(PG_FUNCTION_ARGS);
extern Datum temporal_ever_gt(PG_FUNCTION_ARGS);
extern Datum temporal_ever_ge(PG_FUNCTION_ARGS);

extern Datum temporal_always_eq(PG_FUNCTION_ARGS);
extern Datum temporal_always_ne(PG_FUNCTION_ARGS);
extern Datum temporal_always_lt(PG_FUNCTION_ARGS);
extern Datum temporal_always_le(PG_FUNCTION_ARGS);
extern Datum temporal_always_gt(PG_FUNCTION_ARGS);
extern Datum temporal_always_ge(PG_FUNCTION_ARGS);

extern bool temporal_bbox_ev_al_eq(const Temporal *temp, Datum value,
  bool ever);
extern bool temporal_bbox_ev_al_lt_le(const Temporal *temp, Datum value,
  bool ever);
extern bool temporal_ever_eq_internal(const Temporal *temp, Datum value);

/* Restriction functions */

extern Datum temporal_at_value(PG_FUNCTION_ARGS);
extern Datum temporal_minus_value(PG_FUNCTION_ARGS);
extern Datum temporal_at_values(PG_FUNCTION_ARGS);
extern Datum tnumber_at_range(PG_FUNCTION_ARGS);
extern Datum tnumber_minus_range(PG_FUNCTION_ARGS);
extern Datum tnumber_at_ranges(PG_FUNCTION_ARGS);
extern Datum tnumber_minus_ranges(PG_FUNCTION_ARGS);
extern Datum temporal_at_min(PG_FUNCTION_ARGS);
extern Datum temporal_minus_min(PG_FUNCTION_ARGS);
extern Datum temporal_at_max(PG_FUNCTION_ARGS);
extern Datum temporal_minus_max(PG_FUNCTION_ARGS);
extern Datum temporal_at_timestamp(PG_FUNCTION_ARGS);
extern Datum temporal_minus_timestamp(PG_FUNCTION_ARGS);
extern Datum temporal_value_at_timestamp(PG_FUNCTION_ARGS);
extern Datum temporal_at_timestampset(PG_FUNCTION_ARGS);
extern Datum temporal_minus_timestampset(PG_FUNCTION_ARGS);
extern Datum temporal_at_period(PG_FUNCTION_ARGS);
extern Datum temporal_minus_period(PG_FUNCTION_ARGS);
extern Datum temporal_at_periodset(PG_FUNCTION_ARGS);
extern Datum temporal_minus_periodset(PG_FUNCTION_ARGS);
extern Datum temporal_intersects_timestamp(PG_FUNCTION_ARGS);
extern Datum temporal_intersects_timestampset(PG_FUNCTION_ARGS);
extern Datum temporal_intersects_period(PG_FUNCTION_ARGS);
extern Datum temporal_intersects_periodset(PG_FUNCTION_ARGS);

extern bool temporal_value_at_timestamp_inc(const Temporal *temp,
  TimestampTz t, Datum *value);

extern bool temporal_bbox_restrict_value(const Temporal *temp, Datum value);
extern Datum *temporal_bbox_restrict_values(const Temporal *temp,
  const Datum *values, int count, int *newcount);
extern RangeType **tnumber_bbox_restrict_ranges(const Temporal *temp,
  RangeType **ranges, int count, int *newcount);
extern Temporal *tnumber_restrict_range_internal(const Temporal *temp,
 RangeType *range, bool atfunc);

extern Temporal *temporal_restrict_timestamp_internal(const Temporal *temp,
  TimestampTz t, bool atfunc);
extern Temporal *temporal_at_period_internal(const Temporal *temp,
  const Period *ps);
extern Temporal *temporal_minus_period_internal(const Temporal *temp,
  const Period *ps);
extern Temporal *temporal_restrict_periodset_internal(const Temporal *temp,
  const PeriodSet *ps, bool atfunc);

extern void temporal_period(Period *p, const Temporal *temp);
extern char *temporal_to_string(const Temporal *temp,
  char *(*value_out)(Oid, Datum));
extern void *temporal_bbox_ptr(const Temporal *temp);
extern void temporal_bbox(void *box, const Temporal *temp);

/* Comparison functions */

extern Datum temporal_lt(PG_FUNCTION_ARGS);
extern Datum temporal_le(PG_FUNCTION_ARGS);
extern Datum temporal_eq(PG_FUNCTION_ARGS);
extern Datum temporal_ge(PG_FUNCTION_ARGS);
extern Datum temporal_gt(PG_FUNCTION_ARGS);
extern Datum temporal_cmp(PG_FUNCTION_ARGS);
extern Datum temporal_hash(PG_FUNCTION_ARGS);

extern uint32 temporal_hash_internal(const Temporal *temp);

/*****************************************************************************/

#endif
