﻿-------------------------------------------------------------------------------
-- Tests of operators for time types.
-- File TimeOps.c
-------------------------------------------------------------------------------

-------------------------------------------------------------------------------

SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' @> timestamptz '2000-01-01';
SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' @> timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';

SELECT period '[2000-01-01, 2000-01-02]' @> timestamptz '2000-01-01';
SELECT period '[2000-01-01, 2000-01-02]' @> timestampset '{2000-01-01, 2000-01-02}';
SELECT period '(2000-01-01, 2000-01-02]' @> timestampset '{2000-01-01, 2000-01-02}';
SELECT period '[2000-01-01, 2000-01-02)' @> timestampset '{2000-01-01, 2000-01-02}';
SELECT period '(2000-01-01, 2000-01-02)' @> timestampset '{2000-01-01, 2000-01-02}';
SELECT period '[2000-01-01, 2000-01-02]' @> timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT period '[2000-01-01, 2000-01-02]' @> period '[2000-01-01, 2000-01-02]';
SELECT period '[2000-01-01, 2000-01-02]' @> periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' @> timestamptz '2000-01-01';
SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' @> timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' @> period '[2000-01-01, 2000-01-02]';
SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' @> periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

select periodset '{[2000-01-02, 2000-01-04],(2000-01-05, 2000-01-06),[2000-01-07, 2000-01-08]}' @> 
period '(2000-01-05, 2000-01-06)';
SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-05, 2000-01-06]}' @> periodset '{[2000-01-03, 2000-01-04], [2000-01-07, 2000-01-08]}';

-------------------------------------------------------------------------------

SELECT timestamptz '2000-01-01' <@ timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT timestamptz '2000-01-01' <@ period '[2000-01-01, 2000-01-02]';
SELECT timestamptz '2000-01-01' <@ periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' <@ timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' <@ period '[2000-01-01, 2000-01-02]';
SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' <@ periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';
SELECT timestampset '{2000-01-01, 2000-01-04, 2000-01-07}' && periodset '{[2000-01-02, 2000-01-03], [2000-01-05, 2000-01-06]}';

SELECT period '[2000-01-01, 2000-01-02]' <@ period '[2000-01-01, 2000-01-02]';
SELECT period '[2000-01-01, 2000-01-02]' <@ periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' <@ period '[2000-01-01, 2000-01-02]';
SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' <@ periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

-------------------------------------------------------------------------------

SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' && timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' && period '[2000-01-01, 2000-01-02]';
SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' && periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

SELECT period '[2000-01-01, 2000-01-02]' && timestampset '{2000-01-01, 2000-01-02}';
SELECT period '(2000-01-01, 2000-01-02]' && timestampset '{2000-01-01, 2000-01-02}';
SELECT period '[2000-01-01, 2000-01-02)' && timestampset '{2000-01-01, 2000-01-02}';
SELECT period '(2000-01-01, 2000-01-02)' && timestampset '{2000-01-01, 2000-01-02}';
SELECT period '[2000-01-01, 2000-01-02]' && timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT period '[2000-01-01, 2000-01-02]' && period '[2000-01-01, 2000-01-02]';
SELECT period '[2000-01-01, 2000-01-02]' && periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' && timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' && period '[2000-01-01, 2000-01-02]';
SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' && periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';
SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-05, 2000-01-06]}' && periodset '{[2000-01-03, 2000-01-04], [2000-01-07, 2000-01-08]}';

-------------------------------------------------------------------------------

SELECT timestamptz '2000-01-01' <<# timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT timestamptz '2000-01-01' <<# period '[2000-01-01, 2000-01-02]';
SELECT timestamptz '2000-01-01' <<# periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' <<# timestamptz '2000-01-01';
SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' <<# timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' <<# period '[2000-01-01, 2000-01-02]';
SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' <<# periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

SELECT period '[2000-01-01, 2000-01-02]' <<# timestamptz '2000-01-01';
SELECT period '[2000-01-01, 2000-01-02]' <<# timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT period '[2000-01-01, 2000-01-02]' <<# period '[2000-01-01, 2000-01-02]';
SELECT period '[2000-01-01, 2000-01-02]' <<# periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' <<# timestamptz '2000-01-01';
SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' <<# timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' <<# period '[2000-01-01, 2000-01-02]';
SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' <<# periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

-------------------------------------------------------------------------------

SELECT timestamptz '2000-01-01' &<# timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT timestamptz '2000-01-01' &<# period '[2000-01-01, 2000-01-02]';
SELECT timestamptz '2000-01-01' &<# periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' &<# timestamptz '2000-01-01';
SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' &<# timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' &<# period '[2000-01-01, 2000-01-02]';
SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' &<# periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

SELECT period '[2000-01-01, 2000-01-02]' &<# timestamptz '2000-01-01';
SELECT period '[2000-01-01, 2000-01-02]' &<# timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT period '[2000-01-01, 2000-01-02]' &<# period '[2000-01-01, 2000-01-02]';
SELECT period '[2000-01-01, 2000-01-02]' &<# periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' &<# timestamptz '2000-01-01';
SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' &<# timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' &<# period '[2000-01-01, 2000-01-02]';
SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' &<# periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

-------------------------------------------------------------------------------

SELECT timestamptz '2000-01-01' #>> timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT timestamptz '2000-01-01' #>> period '[2000-01-01, 2000-01-02]';
SELECT timestamptz '2000-01-01' #>> periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' #>> timestamptz '2000-01-01';
SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' #>> timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' #>> period '[2000-01-01, 2000-01-02]';
SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' #>> periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

SELECT period '[2000-01-01, 2000-01-02]' #>> timestamptz '2000-01-01';
SELECT period '[2000-01-01, 2000-01-02]' #>> timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT period '[2000-01-01, 2000-01-02]' #>> period '[2000-01-01, 2000-01-02]';
SELECT period '[2000-01-01, 2000-01-02]' #>> periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' #>> timestamptz '2000-01-01';
SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' #>> timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' #>> period '[2000-01-01, 2000-01-02]';
SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' #>> periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

-------------------------------------------------------------------------------

SELECT timestamptz '2000-01-01' #&> timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT timestamptz '2000-01-01' #&> period '[2000-01-01, 2000-01-02]';
SELECT timestamptz '2000-01-01' #&> periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' #&> timestamptz '2000-01-01';
SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' #&> timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' #&> period '[2000-01-01, 2000-01-02]';
SELECT timestampset '{2000-01-01, 2000-01-02, 2000-01-03}' #&> periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

SELECT period '[2000-01-01, 2000-01-02]' #&> timestamptz '2000-01-01';
SELECT period '[2000-01-01, 2000-01-02]' #&> timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT period '[2000-01-01, 2000-01-02]' #&> period '[2000-01-01, 2000-01-02]';
SELECT period '[2000-01-01, 2000-01-02]' #&> periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' #&> timestamptz '2000-01-01';
SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' #&> timestampset '{2000-01-01, 2000-01-02, 2000-01-03}';
SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' #&> period '[2000-01-01, 2000-01-02]';
SELECT periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}' #&> periodset '{[2000-01-01, 2000-01-02], [2000-01-03, 2000-01-04]}';

/*****************************************************************************/
