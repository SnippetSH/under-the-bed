#!/bin/bash
# 프로토타입 점검 최종(7차) 데이터 재생성 순서. 저장소 루트에서 실행: bash tools/proto_data/run_pipeline.sh
# pass3 → pass4 → pass6 순서로 data/를 다시 써요 (pass2_data.py의 SEASON_BLOCKS·BASE가 1차 설정을 담아요).
set -e
PY=${PY:-python}
$PY tools/proto_data/pass3_data.py TOP_CN=5.0 STABLE_N=200 LABILE_N=1 TOP_K=6.0 HEATER=10.0 NOV_OPEN=12 NOV_CLOSE=14
$PY tools/proto_data/pass4_data.py ABORT_WINDOW=0.35 "ABORT_CURVE=[[0,1],[0.9,0.6],[1,0.1]]" CO2_START=65 CO2_OPEN=8 CO2_CLOSE=16 COMPACTION=0.001 LUE=2.25 "WATERLOG=[[0, 0.015], [0.2, 0.006], [0.4, 0.001], [0.5, 0]]" ABORT_RATE=8.0 THIN_TICKS=1205,1994,2847,3686,4334,4861,5533
$PY tools/proto_data/pass6_data.py JITTER=4.0
