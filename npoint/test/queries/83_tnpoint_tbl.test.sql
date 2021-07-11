-------------------------------------------------------------------------------
--
-- This MobilityDB code is provided under The PostgreSQL License.
--
-- Copyright (c) 2016-2021, Université libre de Bruxelles and MobilityDB
-- contributors
--
-- Permission to use, copy, modify, and distribute this software and its
-- documentation for any purpose, without fee, and without a written
-- agreement is hereby granted, provided that the above copyright notice and
-- this paragraph and the following two paragraphs appear in all copies.
--
-- IN NO EVENT SHALL UNIVERSITE LIBRE DE BRUXELLES BE LIABLE TO ANY PARTY FOR
-- DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
-- LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
-- EVEN IF UNIVERSITE LIBRE DE BRUXELLES HAS BEEN ADVISED OF THE POSSIBILITY
-- OF SUCH DAMAGE.
--
-- UNIVERSITE LIBRE DE BRUXELLES SPECIFICALLY DISCLAIMS ANY WARRANTIES,
-- INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
-- AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON
-- AN "AS IS" BASIS, AND UNIVERSITE LIBRE DE BRUXELLES HAS NO OBLIGATIONS TO
-- PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS. 
--
-------------------------------------------------------------------------------

-------------------------------------------------------------------------------
--  Constructors
-------------------------------------------------------------------------------

SELECT MAX(getPosition(startValue(tnpoint_inst(t1.np, t2.t)))) FROM tbl_npoint t1, tbl_timestamptz t2;

WITH test(temp) as (
SELECT tnpoint_instset(array_agg(t.inst ORDER BY getTimestamp(t.inst))) FROM tbl_tnpoint_inst t GROUP BY k%10 )
SELECT MAX(getPosition(startValue(temp))) FROM test;

WITH test(temp) as (
SELECT tnpoint_seq(array_agg(t.inst ORDER BY getTimestamp(t.inst))) FROM tbl_tnpoint_inst t GROUP BY route(t.inst) )
SELECT MAX(getPosition(startValue(temp))) FROM test;

WITH test(temp) as (
SELECT tnpoint_seqset(array_agg(t.seq ORDER BY startTimestamp(t.seq))) FROM tbl_tnpoint_seq t GROUP BY k%10 )
SELECT MAX(getPosition(startValue(temp))) FROM test;

-------------------------------------------------------------------------------
--  Transformation functions
-------------------------------------------------------------------------------

SELECT DISTINCT tempSubtype(tnpoint_inst(inst)) FROM tbl_tnpoint_inst;
SELECT DISTINCT tempSubtype(tnpoint_instset(inst)) FROM tbl_tnpoint_inst;
SELECT DISTINCT tempSubtype(tnpoint_seq(inst)) FROM tbl_tnpoint_inst;
SELECT DISTINCT tempSubtype(tnpoint_seqset(inst)) FROM tbl_tnpoint_inst;

-------------------------------------------------------------------------------

SELECT DISTINCT tempSubtype(tnpoint_inst(ti)) FROM tbl_tnpoint_instset WHERE numInstants(ti) = 1;
SELECT DISTINCT tempSubtype(tnpoint_instset(ti)) FROM tbl_tnpoint_instset;
SELECT DISTINCT tempSubtype(tnpoint_seq(ti)) FROM tbl_tnpoint_instset WHERE numInstants(ti) = 1;
SELECT DISTINCT tempSubtype(tnpoint_seqset(ti)) FROM tbl_tnpoint_instset;

-------------------------------------------------------------------------------

SELECT DISTINCT tempSubtype(tnpoint_inst(seq)) FROM tbl_tnpoint_seq WHERE numInstants(seq) = 1;
SELECT DISTINCT tempSubtype(tnpoint_instset(seq)) FROM tbl_tnpoint_seq WHERE numInstants(seq) = 1;
SELECT DISTINCT tempSubtype(tnpoint_seq(seq)) FROM tbl_tnpoint_seq;
SELECT DISTINCT tempSubtype(tnpoint_seqset(seq)) FROM tbl_tnpoint_seq;

-------------------------------------------------------------------------------

SELECT DISTINCT tempSubtype(tnpoint_inst(ts)) FROM tbl_tnpoint_seqset WHERE numInstants(ts) = 1;
SELECT DISTINCT tempSubtype(tnpoint_instset(ts)) FROM tbl_tnpoint_seqset WHERE duration(ts) = '00:00:00';
SELECT DISTINCT tempSubtype(tnpoint_seq(ts)) FROM tbl_tnpoint_seqset WHERE numSequences(ts) = 1;
SELECT DISTINCT tempSubtype(tnpoint_seqset(ts)) FROM tbl_tnpoint_seqset;

-------------------------------------------------------------------------------
--  Append functions
-------------------------------------------------------------------------------

SELECT MAX(numInstants(appendInstant(temp, shift(endInstant(temp), '5 min')))) FROM tbl_tnpoint;

-------------------------------------------------------------------------------
--  Cast functions
-------------------------------------------------------------------------------

SELECT count(*) FROM tbl_tnpoint where temp::tgeompoint is not null;

SELECT count(*) FROM tbl_tnpoint WHERE setPrecision(temp, 7) = setPrecision((temp::tgeompoint)::tnpoint, 7);

-------------------------------------------------------------------------------
--  Accessor functions
-------------------------------------------------------------------------------

SELECT DISTINCT tempSubtype(temp) FROM tbl_tnpoint ORDER BY 1;

SELECT MAX(memSize(temp)) FROM tbl_tnpoint;

/*
SELECT stbox(temp) FROM tbl_tnpoint;
*/

SELECT getValue(inst) FROM tbl_tnpoint_inst ORDER BY getValue(inst) LIMIT 1;

SELECT MAX(array_length(getValues(temp), 1)) FROM tbl_tnpoint;

SELECT MAX(array_length(positions(temp), 1)) FROM tbl_tnpoint;

SELECT MAX(route(inst)) FROM tbl_tnpoint_inst;

SELECT MAX(array_length(routes(temp), 1)) FROM tbl_tnpoint;

SELECT MAX(timespan(getTime(temp))) FROM tbl_tnpoint;

SELECT MAX(getTimestamp(inst)) FROM tbl_tnpoint_inst;

SELECT count(*) FROM tbl_tnpoint t1, tbl_npoint t2 WHERE t1.temp ?= t2.np;

SELECT count(*) FROM tbl_tnpoint t1, tbl_npoint t2 WHERE t1.temp %= t2.np;

SELECT count(*) FROM tbl_tnpoint_inst t1, tbl_npoint t2 WHERE ever_eq(t1.inst, t2.np);

SELECT count(*) FROM tbl_tnpoint_inst t1, tbl_npoint t2 WHERE always_eq(t1.inst, t2.np);

SELECT MAX(startTimestamp(shift(t1.temp, t2.i))) FROM tbl_tnpoint t1, tbl_interval t2;

SELECT DISTINCT MAX(getPosition(startValue(temp))) FROM tbl_tnpoint;

SELECT DISTINCT MAX(getPosition(endValue(temp))) FROM tbl_tnpoint;

SELECT MAX(timespan(temp)) FROM tbl_tnpoint;

SELECT MAX(numInstants(temp)) FROM tbl_tnpoint;

SELECT MAX(Route(startInstant(temp))) FROM tbl_tnpoint;

SELECT MAX(Route(endInstant(temp))) FROM tbl_tnpoint;

SELECT MAX(Route(instantN(temp, 1))) FROM tbl_tnpoint;

SELECT MAX(array_length(instants(temp),1)) FROM tbl_tnpoint;

SELECT MAX(numTimestamps(temp)) FROM tbl_tnpoint;

SELECT MAX(startTimestamp(temp)) FROM tbl_tnpoint;

SELECT MAX(endTimestamp(temp)) FROM tbl_tnpoint;

SELECT MAX(timestampN(temp,1)) FROM tbl_tnpoint;

SELECT MAX(array_length(timestamps(temp),1)) FROM tbl_tnpoint;

SELECT MAX(numSequences(ts)) FROM tbl_tnpoint_seqset;

SELECT MAX(timespan(startSequence(ts))) FROM tbl_tnpoint_seqset;

SELECT MAX(timespan(endSequence(ts))) FROM tbl_tnpoint_seqset;

SELECT MAX(timespan(sequenceN(ts, numSequences(ts)))) FROM tbl_tnpoint_seqset;

SELECT MAX(array_length(sequences(ts),1)) FROM tbl_tnpoint_seqset;

-------------------------------------------------------------------------------
--  Restriction functions
-------------------------------------------------------------------------------

SELECT COUNT(*) FROM tbl_tnpoint, tbl_npoint
WHERE atValue(temp, np) IS NOT NULL;

SELECT COUNT(*) FROM tbl_tnpoint, tbl_npoint
WHERE minusValue(temp, np) IS NOT NULL;

SELECT COUNT(*) FROM tbl_tnpoint,
( SELECT array_agg(np) AS valuearr FROM tbl_npoint) tmp
WHERE atValues(temp, valuearr) IS NOT NULL;

SELECT COUNT(*) FROM tbl_tnpoint,
( SELECT array_agg(np) AS valuearr FROM tbl_npoint) tmp
WHERE minusValues(temp, valuearr) IS NOT NULL;

SELECT COUNT(*) FROM tbl_tnpoint, tbl_timestamptz
WHERE atTimestamp(temp, t) IS NOT NULL;

SELECT COUNT(*) FROM tbl_tnpoint, tbl_timestamptz
WHERE minusTimestamp(temp, t) IS NOT NULL;

SELECT COUNT(*) FROM tbl_tnpoint, tbl_timestamptz
WHERE valueAtTimestamp(temp, t) IS NOT NULL;

SELECT COUNT(*) FROM tbl_tnpoint, tbl_timestampset
WHERE atTimestampSet(temp, ts) IS NOT NULL;

SELECT COUNT(*) FROM tbl_tnpoint, tbl_timestampset
WHERE minusTimestampSet(temp, ts) IS NOT NULL;

SELECT COUNT(*) FROM tbl_tnpoint, tbl_period
WHERE atPeriod(temp, p) IS NOT NULL;

SELECT COUNT(*) FROM tbl_tnpoint, tbl_period
WHERE minusPeriod(temp, p) IS NOT NULL;

SELECT COUNT(*) FROM tbl_tnpoint, tbl_periodset
WHERE atPeriodSet(temp, ps) IS NOT NULL;

SELECT COUNT(*) FROM tbl_tnpoint, tbl_periodset
WHERE minusPeriodSet(temp, ps) IS NOT NULL;

SELECT COUNT(*) FROM tbl_tnpoint, tbl_timestamptz
WHERE intersectsTimestamp(temp, t) IS NOT NULL;

SELECT COUNT(*) FROM tbl_tnpoint, tbl_timestampset
WHERE intersectsTimestampSet(temp, ts) IS NOT NULL;

SELECT COUNT(*) FROM tbl_tnpoint, tbl_period
WHERE intersectsPeriod(temp, p) IS NOT NULL;

SELECT COUNT(*) FROM tbl_tnpoint, tbl_periodset
WHERE intersectsPeriodSet(temp, ps) IS NOT NULL;

-------------------------------------------------------------------------------
--  Comparison functions and B-tree indexing
-------------------------------------------------------------------------------

SELECT COUNT(*) FROM tbl_tnpoint t1, tbl_tnpoint t2
WHERE t1.temp = t2.temp;
SELECT COUNT(*) FROM tbl_tnpoint t1, tbl_tnpoint t2
WHERE t1.temp <> t2.temp;
SELECT COUNT(*) FROM tbl_tnpoint t1, tbl_tnpoint t2
WHERE t1.temp < t2.temp;
SELECT COUNT(*) FROM tbl_tnpoint t1, tbl_tnpoint t2
WHERE t1.temp <= t2.temp;
SELECT COUNT(*) FROM tbl_tnpoint t1, tbl_tnpoint t2
WHERE t1.temp > t2.temp;
SELECT COUNT(*) FROM tbl_tnpoint t1, tbl_tnpoint t2
WHERE t1.temp >= t2.temp;

------------------------------------------------------------------------------
