#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

source "$ROOT_DIR/.emsdk/emsdk_env.sh" >/dev/null

em++ native/oracle_engine.cpp \
  -O3 \
  -std=c++20 \
  -sWASM=1 \
  -sMODULARIZE=1 \
  -sEXPORT_NAME=createOracleWasmModule \
  -sALLOW_MEMORY_GROWTH=1 \
  -sENVIRONMENT=web,worker \
  -sEXPORTED_FUNCTIONS='["_oracle_module_version","_oracle_benchmark_nodes","_oracle_choose_best_opening_move","_malloc","_free"]' \
  -sEXPORTED_RUNTIME_METHODS='["cwrap","ccall","HEAP32"]' \
  -o wasm/oracle_engine.js

echo "Built wasm/oracle_engine.js and wasm/oracle_engine.wasm"
