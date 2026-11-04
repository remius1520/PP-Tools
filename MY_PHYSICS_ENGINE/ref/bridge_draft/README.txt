DRAFT only (NOT part of the simulator, NOT tested inside the app): how the exact engine's event log (Result::tev) can be converted into the v85 tracks/finals
(pp::Result) for the in-game drawing code. It compiled and ran against the v85 types on a PC (a 10-ball break = 5 ms). Written for the abandoned "put the new engine inside v85"
route; kept as a starting point for the app layer later. It still uses the old single-table loader; use Sim::loadTable / Sim::detect now.

[merge note] In the merged tree the loader is Sim::loadTable(dir, key) with kTables keys (lvl1_4 lvl5_6 lvl7_8 lvl9 level11 lvl13) and Sim::detect(dir, pocketsXZ, radius) -> key or "".
Result::tev is filled by Sim::run(balls, shot, wantPath, wantEv=true) (tests/test_merge.cpp). pp2_bridge.hpp itself was NOT rebuilt against the merged engine (it needs the v85 types).
