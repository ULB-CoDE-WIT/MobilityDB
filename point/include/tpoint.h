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
 * @file tpoint.h
 * Functions for temporal points.
 */

#ifndef __TPOINT_H__
#define __TPOINT_H__

#define ACCEPT_USE_OF_DEPRECATED_PROJ_API_H 1

#include <postgres.h>
#include <catalog/pg_type.h>

#include "temporal.h"

#include <liblwgeom.h>
#include "stbox.h"

/*****************************************************************************
 * Macros for manipulating the 'typmod' int. An int32_t used as follows:
 * Plus/minus = Top bit.
 * Spare bits = Next 2 bits.
 * SRID = Next 21 bits.
 * TYPE = Next 6 bits.
 * ZM Flags = Bottom 2 bits.
 *****************************************************************************/

/* The following (commented out) definitions are taken from POSTGIS
#define TYPMOD_GET_SRID(typmod) ((((typmod) & 0x0FFFFF00) - ((typmod) & 0x10000000)) >> 8)
#define TYPMOD_SET_SRID(typmod, srid) ((typmod) = (((typmod) & 0xE00000FF) | ((srid & 0x001FFFFF)<<8)))
#define TYPMOD_GET_TYPE(typmod) ((typmod & 0x000000FC)>>2)
#define TYPMOD_SET_TYPE(typmod, type) ((typmod) = (typmod & 0xFFFFFF03) | ((type & 0x0000003F)<<2))
#define TYPMOD_GET_Z(typmod) ((typmod & 0x00000002)>>1)
#define TYPMOD_SET_Z(typmod) ((typmod) = typmod | 0x00000002)
#define TYPMOD_GET_M(typmod) (typmod & 0x00000001)
#define TYPMOD_SET_M(typmod) ((typmod) = typmod | 0x00000001)
#define TYPMOD_GET_NDIMS(typmod) (2+TYPMOD_GET_Z(typmod)+TYPMOD_GET_M(typmod))
*/

/* In order to reuse the above (commented out) macros for manipulating the
   typmod from POSTGIS we need to shift them to take into account that the
   first 4 bits are taken for the temporal type */

#define TYPMOD_DEL_SUBTYPE(typmod) (typmod = typmod >> 4 )
#define TYPMOD_SET_SUBTYPE(typmod, subtype) ((typmod) = typmod << 4 | subtype)

/*****************************************************************************
 * Well-Known Binary (WKB)
 *****************************************************************************/

/* Data type size */
#define WKB_TIMESTAMP_SIZE   8  /* Internal use only */
#define WKB_DOUBLE_SIZE      8  /* Internal use only */
#define WKB_INT_SIZE         4  /* Internal use only */
#define WKB_BYTE_SIZE        1  /* Internal use only */

/* Subtype */
#define MOBDB_WKB_INSTANT        1
#define MOBDB_WKB_INSTANTSET     2
#define MOBDB_WKB_SEQUENCE       3
#define MOBDB_WKB_SEQUENCESET    4

/* Period bounds */
#define MOBDB_WKB_LOWER_INC      0x01
#define MOBDB_WKB_UPPER_INC      0x02

/* Machine endianness */
#define XDR            0  /* big endian */
#define NDR            1  /* little endian */

/* Variation flags */
#define MOBDB_WKB_ZFLAG            0x10
#define MOBDB_WKB_GEODETICFLAG     0x20
#define MOBDB_WKB_SRIDFLAG         0x40
#define MOBDB_WKB_LINEAR_INTERP    0x80

/*****************************************************************************
 * Miscellaneous functions defined in TemporalPoint.c
 *****************************************************************************/

extern void temporalgeom_init();

/* Input/output functions */

extern Datum tpoint_in(PG_FUNCTION_ARGS);
extern Datum tgeompoint_typmod_in(PG_FUNCTION_ARGS);
extern Datum tgeogpoint_typmod_in(PG_FUNCTION_ARGS);
extern Datum tpoint_typmod_out(PG_FUNCTION_ARGS);
extern Datum tpoint_enforce_typmod(PG_FUNCTION_ARGS);

/* Constructor functions */

extern Datum tpointinst_constructor(PG_FUNCTION_ARGS);

/* Accessor functions */

extern Datum tpoint_stbox(PG_FUNCTION_ARGS);

/* Ever/always comparison operators */

extern Datum tpoint_values(PG_FUNCTION_ARGS);
extern Datum tpoint_stbox(PG_FUNCTION_ARGS);

/* Temporal comparisons */

extern Datum teq_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum teq_tpoint_geo(PG_FUNCTION_ARGS);
extern Datum tne_geo_tpoint(PG_FUNCTION_ARGS);
extern Datum tne_tpoint_geo(PG_FUNCTION_ARGS);

/*****************************************************************************/

#endif
