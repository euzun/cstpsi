Expected head-to-head table (values reported in the paper).

Per-query online time in seconds (STL = STLPSI baseline, CST = CSTPSI,
Spd = STL/CST speedup) at 1, 4, and 8 threads; communication in MiB
(Save = CSTPSI's communication saving; "-" = CSTPSI sends more).

                 |        1 thread        |        4 threads       |        8 threads       |   Comm. (MiB)
  D    Label     |  STL    CST    Spd      |  STL    CST    Spd     |  STL    CST    Spd     |  STL   CST   Save
  ---------------+------------------------+------------------------+------------------------+-------------------
  1K   23-bit    |  3.1    1.5   2.0x      |  3.0    1.5   2.0x      |  3.0    1.5   2.0x      |    6     3    50%
  1K   16-B      | 10.7    1.6   6.6x      | 10.6    1.6   6.8x      | 10.6    1.6   6.7x      |   20     3    85%
  1K   32-B      | 19.9    1.7  11.7x      | 19.7    1.6  12.3x      | 19.6    1.6  12.0x      |   36     4    89%
  1K   64-B      | 36.8    1.8  19.9x      | 36.4    1.7  21.5x      | 36.1    1.7  20.7x      |   67     5    93%
  10K  23-bit    |  3.2    1.8   1.8x      |  3.1    1.6   1.9x      |  3.1    1.6   1.9x      |    6     4    33%
  10K  16-B      | 11.2    2.1   5.2x      | 10.7    1.7   6.2x      | 10.7    1.7   6.3x      |   22     6    73%
  10K  32-B      | 20.9    2.6   8.1x      | 20.0    1.9  10.6x      | 19.8    1.8  10.9x      |   41     9    78%
  10K  64-B      | 38.5    3.4  11.3x      | 36.9    2.2  17.1x      | 36.6    2.0  17.8x      |   75    14    81%
  100K 23-bit    |  4.8    4.3   1.1x      |  3.6    2.4   1.5x      |  3.4    2.1   1.6x      |   14    16    -
  100K 16-B      | 16.7    7.9   2.1x      | 12.5    3.5   3.6x      | 11.9    2.9   4.1x      |   49    37    24%
  100K 32-B      | 31.3   12.2   2.6x      | 23.4    4.9   4.8x      | 22.2    3.9   5.7x      |   91    63    31%
  100K 64-B      | 57.7   20.2   2.9x      | 43.2    7.4   5.8x      | 41.1    5.7   7.3x      |  169   111    34%
  1M   23-bit    | 20.9   29.2   0.7x      |  8.5   10.2   0.8x      |  6.8    7.2   0.9x      |   92   132    -
  1M   16-B      | 73.1   75.5   1.0x      | 29.7   24.7   1.2x      | 23.9   17.8   1.3x      |  322   349    -
  1M   32-B      |135.8  131.0   1.0x      | 55.1   42.1   1.3x      | 44.5   30.5   1.5x      |  597   608    -
  1M   64-B      |250.6  232.8   1.1x      |101.8   74.0   1.4x      | 82.1   53.8   1.5x      | 1103  1084    2%

Notes:
  - These are the paper's empirical per-query values. run.sh measures every cell
    directly, so expect the same trends and ballpark, not identical numbers --
    especially absolute time, which depends on the machine.
  - At low query counts the per-query communication tracks these values closely;
    as the query count rises, CSTPSI's once-per-session query upload amortizes,
    so its per-query communication drops below the table.
  - FRR is 0 and FAR is 0 at T=2 in every cell, on every machine.
