#include <cmath>
#include <cstdint>
#include <limits>

#include <emscripten/emscripten.h>

namespace {

inline int manhattan_distance(int a_row, int a_col, int b_row, int b_col) {
  return std::abs(a_row - b_row) + std::abs(a_col - b_col);
}

inline int center_bias(int board_size, int row, int col) {
  const int center = board_size / 2;
  return -manhattan_distance(row, col, center, center);
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
int oracle_module_version() {
  return 1;
}

// Synthetic throughput benchmark for validating the wasm toolchain and JS bridge.
EMSCRIPTEN_KEEPALIVE
int oracle_benchmark_nodes(int iterations) {
  volatile std::uint64_t state = 0x9E3779B97F4A7C15ULL;
  for (int i = 0; i < iterations; ++i) {
    state ^= static_cast<std::uint64_t>(i) + 0xBF58476D1CE4E5B9ULL + (state << 6) + (state >> 2);
    state *= 0x94D049BB133111EBULL;
    state ^= state >> 31;
  }
  return iterations;
}

// Small utility that can score a batch of opening placements from JS.
// This is intentionally narrow and acts as a first step toward moving Oracle
// helpers into wasm without changing the JS engine yet.
EMSCRIPTEN_KEEPALIVE
int oracle_choose_best_opening_move(int board_size,
                                    int move_count,
                                    const int* move_rows,
                                    const int* move_cols,
                                    int opponent_base_row,
                                    int opponent_base_col) {
  if (board_size <= 0 || move_count <= 0 || !move_rows || !move_cols) return -1;

  int best_index = 0;
  int best_score = std::numeric_limits<int>::min();
  for (int i = 0; i < move_count; ++i) {
    const int row = move_rows[i];
    const int col = move_cols[i];
    const int progress = -manhattan_distance(row, col, opponent_base_row, opponent_base_col) * 100;
    const int center = center_bias(board_size, row, col) * 9;
    const int score = progress + center;
    if (score > best_score) {
      best_score = score;
      best_index = i;
    }
  }
  return best_index;
}

}  // extern "C"
