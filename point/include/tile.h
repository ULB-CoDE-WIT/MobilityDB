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
 * @file stbox.h
 * Functions for spatiotemporal bounding boxes.
 */

#ifndef __TILE_H__
#define __TILE_H__

#include "temporal.h"

/*****************************************************************************/

/**
 * Struct for storing the state that persists across multiple calls generating
 * a multidimensional grid
 */
#define MAXDIMS 4
typedef struct STboxGridState
{
  bool done;
  bool hasz;
  bool hast;
  int32 srid;
  Temporal *temp;
  double size;
  int64 tsize;
  POINT3DZ sorigin;
  int64 torigin;
  int min[MAXDIMS];
  int max[MAXDIMS];
  int coords[MAXDIMS];
} STboxGridState;


/*****************************************************************************/

/* Tiling functions */

// extern STBOX *stbox_tile(bool hasz, bool hast, int32 srid, POINT3DZ sorigin,
  // TimestampTz torigin, double xsize, int64 tsize, int *coords);
// extern STboxGridState *stbox_tile_state_make(STBOX *box, double size, int64 tsize,
  // POINT3DZ sorigin, TimestampTz torigin, int32 srid);
// extern void stbox_tile_state_next(STboxGridState *state);

/*****************************************************************************/

#endif
