#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

#include <emscripten/emscripten.h>
#if defined(__wasm_simd128__)
#include <wasm_simd128.h>
#endif

namespace {

constexpr int kBoardSize7 = 7;
constexpr int kSupportedBoardCells = 49;
constexpr int kMaxBoardCells = 64;
constexpr int kMaxPlayers = 4;
constexpr int kNativePlayers = 2;
constexpr int kPlayerStateStride = 9;
constexpr int kCellFlagDead = 1 << 0;
constexpr int kCellFlagStart = 1 << 1;

constexpr int kTileNone = 0;
constexpr int kTileStraight = 1;
constexpr int kTileUTurn = 2;
constexpr int kTileCentrifugal = 3;
constexpr int kTileDiagonal = 4;
constexpr int kTileWeave = 5;
constexpr int kTileJump = 6;
constexpr int kTileDiagonalJump = 7;

constexpr int kJumpNone = 0;
constexpr int kJumpCardinal = 1;
constexpr int kJumpDiagonal = 2;
constexpr int kJumpBoth = 3;

constexpr int kStyleBalanced = 0;
constexpr int kStyleAggressive = 1;
constexpr int kStyleDefensive = 2;

constexpr int kVariantStandard = 0;
constexpr int kVariantOverload = 1;

constexpr int kNoEntry = -1;
constexpr int kPackedNoEntry = 15;
constexpr int kSearchInfinity = 2'000'000'000;
constexpr int kMaxSearchPly = 64;
constexpr int kOracleTargetDepth = 20;

enum TranspositionBound {
  kBoundExact = 0,
  kBoundLower = 1,
  kBoundUpper = 2,
};

inline int manhattan_distance(int a_row, int a_col, int b_row, int b_col) {
  return std::abs(a_row - b_row) + std::abs(a_col - b_col);
}

inline int center_bias(int board_size, int row, int col) {
  const int center = board_size / 2;
  return -manhattan_distance(row, col, center, center);
}

inline std::uint32_t hash_mix32(std::uint32_t seed, std::uint32_t value) {
  std::uint32_t h = (seed ^ (value + 0x9e3779b9u + (seed << 6) + (seed >> 2))) & 0xffffffffu;
  h ^= h >> 16;
  h = static_cast<std::uint32_t>(std::uint64_t(h) * 0x85ebca6bu);
  h ^= h >> 13;
  h = static_cast<std::uint32_t>(std::uint64_t(h) * 0xc2b2ae35u);
  return (h ^ (h >> 16)) & 0xffffffffu;
}

inline std::uint32_t hash_mix64(std::uint32_t seed, std::uint64_t value, std::uint32_t salt = 0) {
  const std::uint64_t normalized = value ^ static_cast<std::uint64_t>(salt);
  const auto low = static_cast<std::uint32_t>(normalized & 0xffffffffull);
  const auto high = static_cast<std::uint32_t>((normalized >> 32) & 0xffffffffull);
  std::uint32_t h = hash_mix32(seed, low);
  h = hash_mix32(h, high ^ (salt * 33u + 17u));
  return h;
}

inline std::uint64_t board_cell_bitmask(int row, int col, int board_size) {
  const int idx = row * board_size + col;
  if (idx < 0 || idx >= kMaxBoardCells) return 0;
  return std::uint64_t{1} << idx;
}

struct BoardBitboards {
  int size = 0;
  std::uint64_t occupied_mask = 0;
  std::uint64_t playable_mask = 0;
  std::uint64_t dead_mask = 0;
  std::uint64_t start_mask = 0;
  std::array<std::uint64_t, 8> type_masks{};
  std::array<std::uint64_t, 4> rot_masks{};
  std::array<std::uint64_t, kMaxPlayers> owner_masks{};

  [[nodiscard]] std::uint64_t empty_playable_mask() const {
    return playable_mask & ~occupied_mask;
  }
};

struct CompactBoardWords {
  std::uint64_t occupied = 0;
  std::uint64_t blocked = 0;
  std::uint64_t tactical = 0;
};

BoardBitboards build_board_bitboards(int board_size,
                                     int cell_count,
                                     const int* cell_types,
                                     const int* cell_rots,
                                     const int* cell_owners,
                                     const int* cell_flags,
                                     int owner_slots) {
  BoardBitboards bitboards;
  bitboards.size = board_size;
  if (board_size <= 0 || board_size * board_size > kMaxBoardCells || cell_count <= 0 || !cell_flags) {
    return bitboards;
  }
  const int safe_cell_count = std::min(cell_count, std::min(board_size * board_size, kMaxBoardCells));
  const int safe_owner_slots = std::max(0, std::min(owner_slots, kMaxPlayers));
  for (int idx = 0; idx < safe_cell_count; ++idx) {
    const int row = idx / board_size;
    const int col = idx % board_size;
    const std::uint64_t bit = board_cell_bitmask(row, col, board_size);
    const int flags = cell_flags[idx];
    if (flags & kCellFlagDead) bitboards.dead_mask |= bit;
    if (flags & kCellFlagStart) bitboards.start_mask |= bit;
    if (!(flags & kCellFlagDead) && !(flags & kCellFlagStart)) bitboards.playable_mask |= bit;

    const int type_code = cell_types ? cell_types[idx] : 0;
    if (type_code <= 0) continue;
    bitboards.occupied_mask |= bit;
    if (type_code < static_cast<int>(bitboards.type_masks.size())) {
      bitboards.type_masks[type_code] |= bit;
    }
    if (cell_rots) {
      bitboards.rot_masks[cell_rots[idx] & 3] |= bit;
    }
    if (cell_owners) {
      const int owner_id = cell_owners[idx];
      if (owner_id >= 0 && owner_id < safe_owner_slots) {
        bitboards.owner_masks[owner_id] |= bit;
      }
    }
  }
  return bitboards;
}

struct PlayerNative {
  int id = 0;
  bool alive = true;
  bool has_placed_first_tile = false;
  int row = -1;
  int col = -1;
  int entrance = -1;
  int jump_left = 0;
  int diagonal_jump_left = 0;
  int combined_jump_left = 0;
};

struct ConfigNative {
  int jump_selection = kJumpNone;
  bool combined_pool = false;
  int oracle_style = kStyleBalanced;
};

struct MoveNative {
  int tile_type = kTileNone;
  int row = -1;
  int col = -1;
  int entry = kNoEntry;
};

struct ProjectionResult {
  int status = 0;
  int steps = 0;
  int row = -1;
  int col = -1;
};

struct JumpResult {
  bool eliminated = false;
  bool win = false;
  int row = -1;
  int col = -1;
  int entrance = -1;
};

struct StateNative {
  int board_size = kBoardSize7;
  int player_count = kNativePlayers;
  int current_player_idx = 0;
  bool game_over = false;
  int winner = -1;
  ConfigNative config;
  std::array<PlayerNative, kNativePlayers> players{};
  std::array<int, kSupportedBoardCells> cell_types{};
  std::array<int, kSupportedBoardCells> cell_flags{};
  std::uint64_t zobrist_hash = 0;
};

inline std::uint64_t board_extent_mask(int board_size) {
  if (board_size <= 0) return 0;
  const int cells = board_size * board_size;
  if (cells <= 0) return 0;
  if (cells >= 64) return std::numeric_limits<std::uint64_t>::max();
  return (std::uint64_t{1} << cells) - 1;
}

CompactBoardWords build_compact_board_words(const StateNative& state) {
  CompactBoardWords words;
  const int safe_cells = std::max(0, std::min(state.board_size * state.board_size, kMaxBoardCells));
  for (int idx = 0; idx < safe_cells; ++idx) {
    const std::uint64_t bit = std::uint64_t{1} << idx;
    const int flags = state.cell_flags[idx];
    if (flags & (kCellFlagDead | kCellFlagStart)) words.blocked |= bit;
    const int tile_type = state.cell_types[idx];
    if (tile_type != kTileNone) {
      words.occupied |= bit;
      if (tile_type == kTileDiagonal || tile_type == kTileWeave
          || tile_type == kTileJump || tile_type == kTileDiagonalJump) {
        words.tactical |= bit;
      }
    }
  }
  return words;
}

struct OracleTuning {
  int attack_weight = 38;
  int defense_weight = 28;
  double tactical_scale = 1.0;
};

struct OracleSignals {
  int own_jump_pool = 0;
  int opp_jump_pool = 0;
  int own_distance = 0;
  int opp_distance = 0;
  int own_projection_status = 0;
  int opp_projection_status = 0;
  int own_projection_steps = 0;
  int opp_projection_steps = 0;
  int own_projection_gain = 0;
  int opp_projection_gain = 0;
  bool own_alive = false;
  bool opp_alive = false;
  bool own_has_first_tile = false;
  bool opp_has_first_tile = false;
};

constexpr int kOracleNnueInputs = 16;
constexpr int kOracleNnueHidden = 8;
constexpr std::array<std::array<int8_t, kOracleNnueInputs>, kOracleNnueHidden> kOracleNnueW1{{
  {{  2,  3, -1,  4,  1,  2, -2,  3, -2,  6, -6,  5, -6,  2, -2,  1 }},
  {{  1, -2,  3, -3, -1,  5,  2, -6,  7, -5,  6, -4,  6, -2,  3, -2 }},
  {{  2,  4, -3,  5,  1,  6, -4,  2, -2,  5, -6,  2, -3,  4, -2,  0 }},
  {{  1, -3,  4, -4,  0, -4,  6, -4,  6, -2,  4, -5,  7, -1,  4, -3 }},
  {{  0,  2, -2,  1,  3,  2, -1,  5, -4,  3, -5,  7, -7,  6, -5,  3 }},
  {{  0, -2,  2, -1,  3, -2,  3, -4,  5, -6,  6, -5,  7, -5,  6, -3 }},
  {{  1,  3, -1,  2,  4,  4, -3,  3, -3,  4, -4,  3, -4,  5, -4,  2 }},
  {{  1, -2,  2, -2,  4, -3,  4, -3,  4, -4,  5, -4,  5, -4,  5, -4 }},
}};
constexpr std::array<int16_t, kOracleNnueHidden> kOracleNnueB1{{ 12, 11, 9, 8, 5, 4, 7, 3 }};
constexpr std::array<int8_t, kOracleNnueHidden> kOracleNnueW2{{ 7, 7, 6, 6, 5, 5, 4, 4 }};
constexpr int kOracleNnueOutBias = -64;

#if defined(__wasm_simd128__)
inline int horizontal_sum_i16x8(v128_t lanes) {
  const v128_t lo_i32 = wasm_i32x4_extend_low_i16x8(lanes);
  const v128_t hi_i32 = wasm_i32x4_extend_high_i16x8(lanes);
  const v128_t pair = wasm_i32x4_add(lo_i32, hi_i32);
  const int s0 = wasm_i32x4_extract_lane(pair, 0);
  const int s1 = wasm_i32x4_extract_lane(pair, 1);
  const int s2 = wasm_i32x4_extract_lane(pair, 2);
  const int s3 = wasm_i32x4_extract_lane(pair, 3);
  return s0 + s1 + s2 + s3;
}
#endif

struct TranspositionEntry {
  int depth = 0;
  int score = 0;
  int bound = kBoundExact;
  int best_move = 0;
};

struct SearchContext {
  int style = kStyleBalanced;
  std::unordered_map<std::uint64_t, TranspositionEntry> transposition;
  std::array<std::array<int, 2>, kMaxSearchPly> killer_moves{};
  std::unordered_map<int, int> history_heuristic;
  std::chrono::steady_clock::time_point deadline{};
  bool use_deadline = false;
  bool timed_out = false;
  int root_best_move = 0;
  std::uint64_t visited_nodes = 0;
};

std::unordered_map<std::uint64_t, int> g_oracle_root_move_cache;
std::uint64_t g_oracle_last_search_nodes = 0;
int g_oracle_last_search_elapsed_ms = 0;

struct UndoNative {
  std::array<PlayerNative, kNativePlayers> players{};
  int current_player_idx = 0;
  bool game_over = false;
  int winner = -1;
  int changed_cell_index = -1;
  int previous_cell_type = kTileNone;
  std::uint64_t zobrist_hash = 0;
};

int pack_move(const MoveNative& move);
bool packed_move_equals(const MoveNative& move, int packed_move);
void promote_packed_move_to_front(std::vector<MoveNative>& moves, int packed_move);
bool search_should_stop(SearchContext& ctx);
int score_position_for_search(const StateNative& state, int root_player_index, int style, int depth_remaining);
bool search_root_depth(StateNative& state,
                       const std::vector<MoveNative>& root_moves,
                       int root_player_index,
                       int depth,
                       int alpha,
                       int beta,
                       SearchContext& ctx,
                       MoveNative& best_move,
                       int& best_score);
void record_killer_move(SearchContext& ctx, int ply, int packed_move);
void update_history_heuristic(SearchContext& ctx, int packed_move, int depth_remaining);
int ordering_bonus(const SearchContext& ctx, int packed_move, int ply, int tt_move);
bool is_killer_move(const SearchContext& ctx, int ply, int packed_move);
bool should_prune_late_move(const SearchContext& ctx, int ply, int depth_remaining, int move_index, int packed_move, int tt_move);
int late_move_reduction(const SearchContext& ctx, int ply, int depth_remaining, int move_index, int packed_move, int tt_move);

inline std::uint64_t splitmix64(std::uint64_t x) {
  x += 0x9e3779b97f4a7c15ull;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
  return x ^ (x >> 31);
}

inline std::uint64_t zobrist_key(std::uint64_t tag, std::uint64_t value) {
  return splitmix64(tag ^ (value + 0x9e3779b97f4a7c15ull));
}

inline int board_index(int row, int col, int board_size = kBoardSize7) {
  return row * board_size + col;
}

inline bool in_bounds(int row, int col, int board_size = kBoardSize7) {
  return row >= 0 && col >= 0 && row < board_size && col < board_size;
}

inline int edge_dir(int entrance) {
  if (entrance <= 1) return 0;
  if (entrance <= 3) return 1;
  if (entrance <= 5) return 2;
  return 3;
}

inline void step_card(int row, int col, int dir, int& next_row, int& next_col) {
  if (dir == 0) { next_row = row - 1; next_col = col; return; }
  if (dir == 1) { next_row = row; next_col = col + 1; return; }
  if (dir == 2) { next_row = row + 1; next_col = col; return; }
  next_row = row;
  next_col = col - 1;
}

inline void step_diag(int row, int col, int dir, int& next_row, int& next_col) {
  if (dir == 0) { next_row = row - 1; next_col = col + 1; return; }  // NE
  if (dir == 1) { next_row = row + 1; next_col = col + 1; return; }  // SE
  if (dir == 2) { next_row = row + 1; next_col = col - 1; return; }  // SW
  next_row = row - 1;
  next_col = col - 1;  // NW
}

inline int opposite_entrance(int entrance) {
  switch (entrance) {
    case 0: return 5;
    case 1: return 4;
    case 2: return 7;
    case 3: return 6;
    case 4: return 1;
    case 5: return 0;
    case 6: return 3;
    case 7: return 2;
    default: return -1;
  }
}

int get_exit_entrance(int tile_type, int entry) {
  if (entry < 0 || entry > 7) return -1;
  switch (tile_type) {
    case kTileStraight:
    case kTileJump:
      switch (entry) {
        case 0: return 5;
        case 5: return 0;
        case 1: return 4;
        case 4: return 1;
        case 2: return 7;
        case 7: return 2;
        case 3: return 6;
        case 6: return 3;
        default: return -1;
      }
    case kTileUTurn:
      switch (entry) {
        case 0: return 1;
        case 1: return 0;
        case 2: return 3;
        case 3: return 2;
        case 4: return 5;
        case 5: return 4;
        case 6: return 7;
        case 7: return 6;
        default: return -1;
      }
    case kTileCentrifugal:
      switch (entry) {
        case 0: return 7;
        case 7: return 0;
        case 1: return 2;
        case 2: return 1;
        case 3: return 4;
        case 4: return 3;
        case 5: return 6;
        case 6: return 5;
        default: return -1;
      }
    case kTileDiagonal:
    case kTileDiagonalJump:
      switch (entry) {
        case 0: return 3;
        case 3: return 0;
        case 1: return 6;
        case 6: return 1;
        case 4: return 7;
        case 7: return 4;
        case 5: return 2;
        case 2: return 5;
        default: return -1;
      }
    case kTileWeave:
      switch (entry) {
        case 0: return 4;
        case 4: return 0;
        case 1: return 5;
        case 5: return 1;
        case 2: return 6;
        case 6: return 2;
        case 3: return 7;
        case 7: return 3;
        default: return -1;
      }
    default:
      return -1;
  }
}

int get_diagonal_direction(int entry, int exit) {
  const int key = entry * 10 + exit;
  switch (key) {
    case 3: return 1;   // 0->3 SE
    case 30: return 3;  // 3->0 NW
    case 16: return 2;  // 1->6 SW
    case 61: return 0;  // 6->1 NE
    case 52: return 0;  // 5->2 NE
    case 25: return 2;  // 2->5 SW
    case 47: return 3;  // 4->7 NW
    case 74: return 1;  // 7->4 SE
    default: return -1;
  }
}

std::pair<int, int> base_cell_for_player(int board_size, int player_id) {
  if (board_size != kBoardSize7) return {-1, -1};
  if (player_id == 0) return {0, 0};
  if (player_id == 1) return {board_size - 1, board_size - 1};
  return {-1, -1};
}

std::array<std::pair<int, int>, 2> adjacent_first_cells_for_player(int board_size, int player_id) {
  if (board_size != kBoardSize7) return {{{-1, -1}, {-1, -1}}};
  if (player_id == 0) return {{{0, 1}, {1, 0}}};
  return {{{board_size - 2, board_size - 1}, {board_size - 1, board_size - 2}}};
}

std::vector<int> first_turn_entry_choices_for_cell(int player_id, int row, int col, int board_size) {
  const auto base = base_cell_for_player(board_size, player_id);
  std::vector<int> result;
  for (int entry = 0; entry < 8; ++entry) {
    int nr = 0;
    int nc = 0;
    step_card(row, col, edge_dir(entry), nr, nc);
    if (nr == base.first && nc == base.second) result.push_back(entry);
  }
  return result;
}

inline bool player_owns_base_cell(int player_id, int row, int col, int board_size) {
  const auto base = base_cell_for_player(board_size, player_id);
  return base.first == row && base.second == col;
}

bool is_opponent_base_cell(const StateNative& state, int player_id, int row, int col) {
  for (int idx = 0; idx < state.player_count; ++idx) {
    if (state.players[idx].id == player_id) continue;
    const auto base = base_cell_for_player(state.board_size, state.players[idx].id);
    if (base.first == row && base.second == col) return true;
  }
  return false;
}

inline bool cell_is_dead(const StateNative& state, int row, int col) {
  return state.cell_flags[board_index(row, col, state.board_size)] & kCellFlagDead;
}

inline bool cell_is_start(const StateNative& state, int row, int col) {
  return state.cell_flags[board_index(row, col, state.board_size)] & kCellFlagStart;
}

inline bool cell_is_open_for_placement(const StateNative& state, int row, int col) {
  if (!in_bounds(row, col, state.board_size)) return false;
  if (cell_is_dead(state, row, col) || cell_is_start(state, row, col)) return false;
  return state.cell_types[board_index(row, col, state.board_size)] == kTileNone;
}

bool can_use_tile(const PlayerNative& player, int tile_type, const ConfigNative& config) {
  if (tile_type != kTileJump && tile_type != kTileDiagonalJump) return true;
  if (config.jump_selection == kJumpNone) return false;
  if (config.jump_selection == kJumpCardinal) return tile_type == kTileJump && player.jump_left > 0;
  if (config.jump_selection == kJumpDiagonal) return tile_type == kTileDiagonalJump && player.diagonal_jump_left > 0;
  if (config.jump_selection == kJumpBoth) {
    if (config.combined_pool) return player.combined_jump_left > 0;
    return tile_type == kTileJump ? player.jump_left > 0 : player.diagonal_jump_left > 0;
  }
  return false;
}

void consume_tile(PlayerNative& player, int tile_type, const ConfigNative& config) {
  if (tile_type != kTileJump && tile_type != kTileDiagonalJump) return;
  if (config.jump_selection == kJumpCardinal) player.jump_left--;
  else if (config.jump_selection == kJumpDiagonal) player.diagonal_jump_left--;
  else if (config.jump_selection == kJumpBoth) {
    if (config.combined_pool) player.combined_jump_left--;
    else if (tile_type == kTileJump) player.jump_left--;
    else player.diagonal_jump_left--;
  }
}

void set_winner(StateNative& state, int player_id) {
  state.game_over = true;
  state.winner = player_id;
}

void set_tie(StateNative& state) {
  state.game_over = true;
  state.winner = -1;
}

void eliminate_player(StateNative& state, int player_index) {
  state.players[player_index].alive = false;
}

void finalize_remaining_players_outcome(StateNative& state) {
  if (state.game_over) return;
  int alive_count = 0;
  int winner_id = -1;
  for (int idx = 0; idx < state.player_count; ++idx) {
    if (!state.players[idx].alive) continue;
    alive_count++;
    winner_id = state.players[idx].id;
  }
  if (alive_count == 1) {
    set_winner(state, winner_id);
  } else if (alive_count == 0) {
    set_tie(state);
  }
}

void check_position_draw(StateNative& state) {
  if (state.game_over) return;
  const auto& a = state.players[0];
  const auto& b = state.players[1];
  if (a.alive && b.alive &&
      a.has_placed_first_tile && b.has_placed_first_tile &&
      a.row == b.row && a.col == b.col && a.entrance == b.entrance &&
      a.row >= 0 && a.col >= 0 && a.entrance >= 0) {
    set_tie(state);
  }
}

void advance_to_next_alive(StateNative& state) {
  if (state.game_over) return;
  int idx = state.current_player_idx;
  for (int i = 0; i < state.player_count; ++i) {
    idx = (idx + 1) % state.player_count;
    if (state.players[idx].alive) {
      state.current_player_idx = idx;
      return;
    }
  }
}

bool is_fatal(const StateNative& state, int player_id, int row, int col) {
  if (!in_bounds(row, col, state.board_size)) return true;
  if (cell_is_dead(state, row, col)) return true;
  if (player_owns_base_cell(player_id, row, col, state.board_size)) return true;
  return false;
}

JumpResult jump_or_adjacent(const StateNative& state, int player_id, int row, int col, int entry, int exit, int tile_type) {
  JumpResult result;
  if (exit < 0) {
    result.eliminated = true;
    return result;
  }
  int next_row = -1;
  int next_col = -1;
  int next_entrance = -1;
  if (tile_type == kTileJump) {
    int skip_row = 0;
    int skip_col = 0;
    step_card(row, col, edge_dir(exit), skip_row, skip_col);
    step_card(skip_row, skip_col, edge_dir(exit), next_row, next_col);
    if (is_fatal(state, player_id, next_row, next_col)) {
      result.eliminated = true;
      return result;
    }
    if (is_opponent_base_cell(state, player_id, next_row, next_col)) {
      result.win = true;
      return result;
    }
    next_entrance = opposite_entrance(exit);
  } else if (tile_type == kTileDiagonalJump) {
    const int diag_dir = get_diagonal_direction(entry, exit);
    if (diag_dir < 0) {
      result.eliminated = true;
      return result;
    }
    step_diag(row, col, diag_dir, next_row, next_col);
    if (is_fatal(state, player_id, next_row, next_col)) {
      result.eliminated = true;
      return result;
    }
    if (is_opponent_base_cell(state, player_id, next_row, next_col)) {
      result.win = true;
      return result;
    }
    next_entrance = entry;
  } else {
    step_card(row, col, edge_dir(exit), next_row, next_col);
    if (is_fatal(state, player_id, next_row, next_col)) {
      result.eliminated = true;
      return result;
    }
    if (is_opponent_base_cell(state, player_id, next_row, next_col)) {
      result.win = true;
      return result;
    }
    next_entrance = opposite_entrance(exit);
  }
  result.row = next_row;
  result.col = next_col;
  result.entrance = next_entrance;
  return result;
}

void resolve_movement(StateNative& state, int player_index) {
  auto& player = state.players[player_index];
  bool visited[kSupportedBoardCells][8] = {};
  while (true) {
    if (!in_bounds(player.row, player.col, state.board_size) || player.entrance < 0 || player.entrance > 7) {
      eliminate_player(state, player_index);
      return;
    }
    const int idx = board_index(player.row, player.col, state.board_size);
    if (visited[idx][player.entrance]) {
      eliminate_player(state, player_index);
      return;
    }
    visited[idx][player.entrance] = true;
    const int tile_type = state.cell_types[idx];
    if (tile_type == kTileNone) return;
    const int exit = get_exit_entrance(tile_type, player.entrance);
    const JumpResult next = jump_or_adjacent(state, player.id, player.row, player.col, player.entrance, exit, tile_type);
    if (next.eliminated) {
      eliminate_player(state, player_index);
      return;
    }
    if (next.win) {
      set_winner(state, player.id);
      return;
    }
    player.row = next.row;
    player.col = next.col;
    player.entrance = next.entrance;
    if (state.cell_types[board_index(player.row, player.col, state.board_size)] == kTileNone) return;
  }
}

void execute_first_turn_movement(StateNative& state, int player_index, int tile_row, int tile_col, int entry) {
  auto& player = state.players[player_index];
  const int tile_type = state.cell_types[board_index(tile_row, tile_col, state.board_size)];
  const int exit = get_exit_entrance(tile_type, entry);
  const JumpResult next = jump_or_adjacent(state, player.id, tile_row, tile_col, entry, exit, tile_type);
  if (next.eliminated) {
    eliminate_player(state, player_index);
    return;
  }
  if (next.win) {
    set_winner(state, player.id);
    return;
  }
  player.row = next.row;
  player.col = next.col;
  player.entrance = next.entrance;
}

void resolve_after_placement(StateNative& state, int placed_row, int placed_col) {
  for (int idx = 0; idx < state.player_count; ++idx) {
    const auto& player = state.players[idx];
    if (!player.alive || !player.has_placed_first_tile) continue;
    if (player.row != placed_row || player.col != placed_col) continue;
    if (state.game_over) break;
    resolve_movement(state, idx);
  }
  check_position_draw(state);
  if (state.game_over) return;
  finalize_remaining_players_outcome(state);
}

bool same_move(const MoveNative& a, const MoveNative& b) {
  return a.tile_type == b.tile_type && a.row == b.row && a.col == b.col && a.entry == b.entry;
}

std::vector<MoveNative> collect_legal_moves(const StateNative& state, int player_index) {
  std::vector<MoveNative> moves;
  if (state.game_over || player_index < 0 || player_index >= state.player_count) return moves;
  const auto& player = state.players[player_index];
  if (!player.alive) return moves;

  std::vector<int> tile_types;
  for (int tile_type = kTileStraight; tile_type <= kTileDiagonalJump; ++tile_type) {
    if (can_use_tile(player, tile_type, state.config)) tile_types.push_back(tile_type);
  }
  if (tile_types.empty()) return moves;

  if (!player.has_placed_first_tile) {
    const auto cells = adjacent_first_cells_for_player(state.board_size, player.id);
    for (const auto& [row, col] : cells) {
      if (!cell_is_open_for_placement(state, row, col)) continue;
      const auto entries = first_turn_entry_choices_for_cell(player.id, row, col, state.board_size);
      for (const int tile_type : tile_types) {
        for (const int entry : entries) {
          moves.push_back({tile_type, row, col, entry});
        }
      }
    }
    return moves;
  }

  if (!cell_is_open_for_placement(state, player.row, player.col)) return moves;
  for (const int tile_type : tile_types) {
    moves.push_back({tile_type, player.row, player.col, kNoEntry});
  }
  return moves;
}

int distance_to_base(int board_size, int row, int col, int player_id) {
  const auto base = base_cell_for_player(board_size, player_id);
  if (base.first < 0) return board_size;
  return manhattan_distance(row, col, base.first, base.second);
}

int distance_to_nearest_opponent_base(const StateNative& state, int player_index) {
  const auto& player = state.players[player_index];
  if (player.row < 0 || player.col < 0) return state.board_size;
  int best = state.board_size;
  for (int idx = 0; idx < state.player_count; ++idx) {
    if (idx == player_index) continue;
    const auto base = base_cell_for_player(state.board_size, state.players[idx].id);
    best = std::min(best, manhattan_distance(player.row, player.col, base.first, base.second));
  }
  return best;
}

int distance_to_player_base(const StateNative& state, int source_index, int target_index) {
  const auto& source = state.players[source_index];
  if (source.row < 0 || source.col < 0) return state.board_size;
  return distance_to_base(state.board_size, source.row, source.col, state.players[target_index].id);
}

ProjectionResult project_player_along_existing_tiles(const StateNative& state, int player_index, int max_steps = 32) {
  ProjectionResult result;
  const auto& player = state.players[player_index];
  if (!player.alive || !player.has_placed_first_tile || player.row < 0 || player.col < 0 || player.entrance < 0) {
    return result;
  }
  int row = player.row;
  int col = player.col;
  int entrance = player.entrance;
  bool visited[kSupportedBoardCells][8] = {};
  int steps = 0;
  while (steps < max_steps) {
    if (!in_bounds(row, col, state.board_size) || entrance < 0 || entrance > 7) {
      result.status = 2;
      result.steps = steps;
      return result;
    }
    const int idx = board_index(row, col, state.board_size);
    if (visited[idx][entrance]) {
      result.status = 3;
      result.steps = steps;
      result.row = row;
      result.col = col;
      return result;
    }
    visited[idx][entrance] = true;
    const int tile_type = state.cell_types[idx];
    if (tile_type == kTileNone) {
      result.steps = steps;
      result.row = row;
      result.col = col;
      return result;
    }
    const int exit = get_exit_entrance(tile_type, entrance);
    const JumpResult next = jump_or_adjacent(state, player.id, row, col, entrance, exit, tile_type);
    steps++;
    if (next.win) {
      result.status = 1;
      result.steps = steps;
      return result;
    }
    if (next.eliminated) {
      result.status = 2;
      result.steps = steps;
      return result;
    }
    row = next.row;
    col = next.col;
    entrance = next.entrance;
  }
  result.status = 4;
  result.steps = steps;
  result.row = row;
  result.col = col;
  return result;
}

OracleTuning get_oracle_tuning(int style) {
  if (style == kStyleAggressive) return {48, 22, 1.2};
  if (style == kStyleDefensive) return {30, 36, 0.95};
  return {38, 28, 1.0};
}

int evaluate_oracle_nnue(const OracleSignals& signals, int style) {
  std::array<int16_t, kOracleNnueInputs> input{};
  input[0] = 16;  // bias
  input[1] = style == kStyleAggressive ? 16 : 0;
  input[2] = style == kStyleDefensive ? 16 : 0;
  input[3] = signals.own_alive ? 16 : -16;
  input[4] = signals.opp_alive ? 16 : -16;
  input[5] = signals.own_has_first_tile ? 12 : -12;
  input[6] = signals.opp_has_first_tile ? 12 : -12;
  input[7] = std::clamp(signals.own_jump_pool * 3, 0, 24);
  input[8] = std::clamp(signals.opp_jump_pool * 3, 0, 24);
  input[9] = std::clamp((14 - signals.own_distance) * 2, -24, 24);
  input[10] = std::clamp((signals.opp_distance - 4) * 2, -24, 24);
  input[11] = std::clamp(signals.own_projection_gain * 6, -30, 30);
  input[12] = std::clamp(signals.opp_projection_gain * 6, -30, 30);
  input[13] = std::clamp((signals.own_projection_status == 1 ? 24 : 0) - (signals.own_projection_status == 2 ? 24 : 0), -24, 24);
  input[14] = std::clamp((signals.opp_projection_status == 2 ? 24 : 0) - (signals.opp_projection_status == 1 ? 24 : 0), -24, 24);
  input[15] = std::clamp((signals.own_projection_steps - signals.opp_projection_steps) * 3, -30, 30);

  int output = kOracleNnueOutBias;
#if defined(__wasm_simd128__)
  const v128_t input_lo = wasm_v128_load(input.data());
  const v128_t input_hi = wasm_v128_load(input.data() + 8);
  std::array<int16_t, kOracleNnueHidden> activated_hidden{};
  for (int h = 0; h < kOracleNnueHidden; ++h) {
    const v128_t weights_i8 = wasm_v128_load(kOracleNnueW1[h].data());
    const v128_t weights_lo = wasm_i16x8_extend_low_i8x16(weights_i8);
    const v128_t weights_hi = wasm_i16x8_extend_high_i8x16(weights_i8);
    const v128_t prod_lo = wasm_i16x8_mul(weights_lo, input_lo);
    const v128_t prod_hi = wasm_i16x8_mul(weights_hi, input_hi);
    int acc = kOracleNnueB1[h]
      + horizontal_sum_i16x8(prod_lo)
      + horizontal_sum_i16x8(prod_hi);
    const int activated = std::clamp(acc, 0, 127);
    activated_hidden[h] = static_cast<int16_t>(activated);
  }
  const v128_t hidden_i16 = wasm_v128_load(activated_hidden.data());
  const v128_t out_weights_i16 = wasm_i16x8_load8x8(kOracleNnueW2.data());
  const v128_t out_prod = wasm_i16x8_mul(hidden_i16, out_weights_i16);
  output += horizontal_sum_i16x8(out_prod);
#else
  for (int h = 0; h < kOracleNnueHidden; ++h) {
    int acc = kOracleNnueB1[h];
    for (int i = 0; i < kOracleNnueInputs; ++i) {
      acc += static_cast<int>(kOracleNnueW1[h][i]) * static_cast<int>(input[i]);
    }
    const int activated = std::clamp(acc, 0, 127);
    output += activated * static_cast<int>(kOracleNnueW2[h]);
  }
#endif
  return std::clamp(output / 12, -420, 420);
}

int evaluate_state_for_player(const StateNative& state, int root_player_index, int style) {
  const auto& player = state.players[root_player_index];
  const int opponent_index = 1 - root_player_index;
  const auto& opponent = state.players[opponent_index];
  const int style_attack = style == kStyleAggressive ? 120 : style == kStyleDefensive ? 88 : 100;
  const int style_defense = style == kStyleAggressive ? 90 : style == kStyleDefensive ? 120 : 100;

  if (state.game_over) {
    if (state.winner == player.id) return 100000;
    if (state.winner < 0) return -12000;
    return -100000;
  }
  if (!player.alive) return -50000;

  int score = 0;
  const int alive_opponents = opponent.alive ? 1 : 0;
  const int own_jump_pool = player.jump_left + player.diagonal_jump_left + player.combined_jump_left;
  score += alive_opponents == 0 ? 30000 : 0;
  score += opponent.alive ? 0 : 4000;
  score += player.has_placed_first_tile ? 90 : -60;
  score += static_cast<int>(collect_legal_moves(state, root_player_index).size()) * 28;
  score += own_jump_pool * 24;
  score -= distance_to_nearest_opponent_base(state, root_player_index) * 24 * style_attack / 100;
  if (opponent.alive) {
    const int opp_jump_pool = opponent.jump_left + opponent.diagonal_jump_left + opponent.combined_jump_left;
    score -= static_cast<int>(collect_legal_moves(state, opponent_index).size()) * 16;
    score -= opp_jump_pool * 20;
    score += distance_to_player_base(state, opponent_index, root_player_index) * 12 * style_defense / 100;
  }
  return score;
}

int evaluate_state_for_oracle(const StateNative& state, int root_player_index, int style) {
  const int base_score = evaluate_state_for_player(state, root_player_index, style);
  const auto& player = state.players[root_player_index];
  if (!player.alive || state.game_over) return base_score;
  const CompactBoardWords board_words = build_compact_board_words(state);
  const std::uint64_t board_extent = board_extent_mask(state.board_size);
  const std::uint64_t open_playable =
    board_extent & ~board_words.occupied & ~board_words.blocked;
  const int tactical_tiles = std::popcount(board_words.tactical);
  const int occupied_tiles = std::popcount(board_words.occupied);
  const int open_playable_cells = std::popcount(open_playable);

  const OracleTuning tuning = get_oracle_tuning(style);
  const int opponent_index = 1 - root_player_index;
  const auto& opponent = state.players[opponent_index];
  const int own_jump_pool = player.jump_left + player.diagonal_jump_left + player.combined_jump_left;
  const int opp_jump_pool = opponent.jump_left + opponent.diagonal_jump_left + opponent.combined_jump_left;
  const int own_distance = distance_to_nearest_opponent_base(state, root_player_index);
  int opp_distance = opponent.alive
    ? distance_to_player_base(state, opponent_index, root_player_index)
    : state.board_size * 2;
  int own_projection_gain = 0;
  int opp_projection_gain = 0;
  int pressure = 0;
  pressure -= own_distance * tuning.attack_weight;
  pressure += tactical_tiles * (style == kStyleDefensive ? 6 : 8);
  pressure += open_playable_cells * (style == kStyleAggressive ? 3 : 2);
  pressure -= occupied_tiles;

  const ProjectionResult own_projection = project_player_along_existing_tiles(state, root_player_index);
  if (own_projection.status == 1) {
    pressure += 1500;
  } else if (own_projection.status == 2) {
    pressure -= 900;
  } else if (own_projection.row >= 0 && own_projection.col >= 0) {
    const int projected_distance = distance_to_base(state.board_size, own_projection.row, own_projection.col, state.players[opponent_index].id);
    own_projection_gain = own_distance - projected_distance;
    pressure += own_projection_gain * 70;
    pressure += own_projection.steps * 10;
  }

  if (opponent.alive) {
    pressure += opp_distance * tuning.defense_weight;
    const ProjectionResult opp_projection = project_player_along_existing_tiles(state, opponent_index);
    if (opp_projection.status == 1) {
      pressure -= 1200;
    } else if (opp_projection.status == 2) {
      pressure += 700;
    } else if (opp_projection.row >= 0 && opp_projection.col >= 0) {
      const int after = distance_to_base(state.board_size, opp_projection.row, opp_projection.col, player.id);
      opp_projection_gain = opp_distance - after;
      pressure += (after - opp_distance) * 54;
    }
    const OracleSignals signals{
      own_jump_pool,
      opp_jump_pool,
      own_distance,
      opp_distance,
      own_projection.status,
      opp_projection.status,
      own_projection.steps,
      opp_projection.steps,
      own_projection_gain,
      opp_projection_gain,
      player.alive,
      opponent.alive,
      player.has_placed_first_tile,
      opponent.has_placed_first_tile,
    };
    return base_score + pressure + evaluate_oracle_nnue(signals, style);
  }

  const OracleSignals signals{
    own_jump_pool,
    opp_jump_pool,
    own_distance,
    opp_distance,
    own_projection.status,
    0,
    own_projection.steps,
    0,
    own_projection_gain,
    0,
    player.alive,
    opponent.alive,
    player.has_placed_first_tile,
    opponent.has_placed_first_tile,
  };
  return base_score + pressure + evaluate_oracle_nnue(signals, style);
}

std::uint64_t cell_type_zobrist(int cell_index, int tile_type) {
  if (cell_index < 0 || cell_index >= kSupportedBoardCells || tile_type <= 0) return 0;
  return zobrist_key(0x43454c4c54595045ull, (static_cast<std::uint64_t>(cell_index) << 8) | static_cast<std::uint64_t>(tile_type & 0xff));
}

std::uint64_t player_state_zobrist(const PlayerNative& player, int slot) {
  std::uint64_t key = 0;
  key ^= zobrist_key(0x5049440000000000ull, (static_cast<std::uint64_t>(slot & 0xff) << 8) | static_cast<std::uint64_t>(player.id & 0xff));
  key ^= zobrist_key(0x50414c4956450000ull, (static_cast<std::uint64_t>(slot & 0xff) << 8) | static_cast<std::uint64_t>(player.alive ? 1 : 0));
  key ^= zobrist_key(0x5046495253540000ull, (static_cast<std::uint64_t>(slot & 0xff) << 8) | static_cast<std::uint64_t>(player.has_placed_first_tile ? 1 : 0));
  key ^= zobrist_key(0x50524f5700000000ull, (static_cast<std::uint64_t>(slot & 0xff) << 16) | static_cast<std::uint64_t>((player.row + 1) & 0xff));
  key ^= zobrist_key(0x50434f4c00000000ull, (static_cast<std::uint64_t>(slot & 0xff) << 16) | static_cast<std::uint64_t>((player.col + 1) & 0xff));
  key ^= zobrist_key(0x50454e5400000000ull, (static_cast<std::uint64_t>(slot & 0xff) << 16) | static_cast<std::uint64_t>((player.entrance + 1) & 0xff));
  key ^= zobrist_key(0x504a554d50000000ull, (static_cast<std::uint64_t>(slot & 0xff) << 16) | static_cast<std::uint64_t>(player.jump_left & 0xff));
  key ^= zobrist_key(0x50444a554d500000ull, (static_cast<std::uint64_t>(slot & 0xff) << 16) | static_cast<std::uint64_t>(player.diagonal_jump_left & 0xff));
  key ^= zobrist_key(0x50434a554d500000ull, (static_cast<std::uint64_t>(slot & 0xff) << 16) | static_cast<std::uint64_t>(player.combined_jump_left & 0xff));
  return key;
}

std::uint64_t current_player_zobrist(int current_player_idx) {
  return zobrist_key(0x43555252454e5400ull, static_cast<std::uint64_t>(current_player_idx & 0xff));
}

std::uint64_t game_state_zobrist(bool game_over, int winner) {
  return zobrist_key(0x47414d4553544154ull, (static_cast<std::uint64_t>(game_over ? 1 : 0) << 16) | static_cast<std::uint64_t>((winner + 2) & 0xff));
}

std::uint64_t config_zobrist(const ConfigNative& config) {
  std::uint64_t key = 0;
  key ^= zobrist_key(0x434f4e4649474a53ull, static_cast<std::uint64_t>(config.jump_selection & 0xff));
  key ^= zobrist_key(0x434f4e464947504full, static_cast<std::uint64_t>(config.combined_pool ? 1 : 0));
  key ^= zobrist_key(0x434f4e4649475354ull, static_cast<std::uint64_t>(config.oracle_style & 0xff));
  return key;
}

std::uint64_t compute_native_zobrist_hash(const StateNative& state) {
  std::uint64_t hash = config_zobrist(state.config);
  hash ^= current_player_zobrist(state.current_player_idx);
  hash ^= game_state_zobrist(state.game_over, state.winner);
  for (int idx = 0; idx < state.player_count; ++idx) {
    hash ^= player_state_zobrist(state.players[idx], idx);
  }
  for (int idx = 0; idx < state.board_size * state.board_size; ++idx) {
    hash ^= cell_type_zobrist(idx, state.cell_types[idx]);
  }
  return hash;
}

void refresh_zobrist_hash_after_move(StateNative& state, const UndoNative& undo) {
  std::uint64_t hash = undo.zobrist_hash;
  hash ^= current_player_zobrist(undo.current_player_idx);
  hash ^= current_player_zobrist(state.current_player_idx);
  hash ^= game_state_zobrist(undo.game_over, undo.winner);
  hash ^= game_state_zobrist(state.game_over, state.winner);
  for (int idx = 0; idx < state.player_count; ++idx) {
    hash ^= player_state_zobrist(undo.players[idx], idx);
    hash ^= player_state_zobrist(state.players[idx], idx);
  }
  if (undo.changed_cell_index >= 0) {
    hash ^= cell_type_zobrist(undo.changed_cell_index, undo.previous_cell_type);
    hash ^= cell_type_zobrist(undo.changed_cell_index, state.cell_types[undo.changed_cell_index]);
  }
  state.zobrist_hash = hash;
}

void undo_move(StateNative& state, const UndoNative& undo) {
  state.players = undo.players;
  state.current_player_idx = undo.current_player_idx;
  state.game_over = undo.game_over;
  state.winner = undo.winner;
  if (undo.changed_cell_index >= 0) {
    state.cell_types[undo.changed_cell_index] = undo.previous_cell_type;
  }
  state.zobrist_hash = undo.zobrist_hash;
}

std::uint32_t compute_native_state_hash(const StateNative& state, int player_id, int depth_remaining) {
  std::uint64_t hash = state.zobrist_hash;
  hash ^= zobrist_key(0x4450544800000000ull, static_cast<std::uint64_t>(depth_remaining & 0xff));
  hash ^= zobrist_key(0x52504c4159455200ull, static_cast<std::uint64_t>(player_id & 0xff));
  return static_cast<std::uint32_t>((hash ^ (hash >> 32)) & 0xffffffffu);
}

int terminal_score(const StateNative& state, int root_player_id, int depth_remaining, bool& is_terminal) {
  if (!state.game_over) {
    is_terminal = false;
    return 0;
  }
  is_terminal = true;
  if (state.winner == root_player_id) return 1000000 + depth_remaining;
  if (state.winner < 0) return -25000 - depth_remaining;
  return -1000000 - depth_remaining;
}

bool execute_move(StateNative& state, const MoveNative& move, UndoNative* undo = nullptr) {
  if (state.game_over) return false;
  const int player_index = state.current_player_idx;
  if (player_index < 0 || player_index >= state.player_count) return false;
  auto& player = state.players[player_index];
  if (!player.alive || !can_use_tile(player, move.tile_type, state.config)) return false;
  if (!cell_is_open_for_placement(state, move.row, move.col)) return false;
  if (undo) {
    undo->players = state.players;
    undo->current_player_idx = state.current_player_idx;
    undo->game_over = state.game_over;
    undo->winner = state.winner;
    undo->changed_cell_index = board_index(move.row, move.col, state.board_size);
    undo->previous_cell_type = state.cell_types[undo->changed_cell_index];
    undo->zobrist_hash = state.zobrist_hash;
  }

  if (!player.has_placed_first_tile) {
    bool valid_first_cell = false;
    for (const auto& [row, col] : adjacent_first_cells_for_player(state.board_size, player.id)) {
      if (row == move.row && col == move.col) {
        valid_first_cell = true;
        break;
      }
    }
    if (!valid_first_cell) return false;
    const auto entries = first_turn_entry_choices_for_cell(player.id, move.row, move.col, state.board_size);
    if (std::find(entries.begin(), entries.end(), move.entry) == entries.end()) return false;
    state.cell_types[board_index(move.row, move.col, state.board_size)] = move.tile_type;
    consume_tile(player, move.tile_type, state.config);
    execute_first_turn_movement(state, player_index, move.row, move.col, move.entry);
    player.has_placed_first_tile = true;
    finalize_remaining_players_outcome(state);
    if (!state.game_over) advance_to_next_alive(state);
    if (undo) refresh_zobrist_hash_after_move(state, *undo);
    else state.zobrist_hash = compute_native_zobrist_hash(state);
    return true;
  }

  if (move.entry != kNoEntry) return false;
  if (player.row != move.row || player.col != move.col) return false;
  state.cell_types[board_index(move.row, move.col, state.board_size)] = move.tile_type;
  consume_tile(player, move.tile_type, state.config);
  resolve_after_placement(state, move.row, move.col);
  if (!state.game_over) advance_to_next_alive(state);
  if (undo) refresh_zobrist_hash_after_move(state, *undo);
  else state.zobrist_hash = compute_native_zobrist_hash(state);
  return true;
}

std::vector<MoveNative> order_moves(StateNative& state,
                                    const std::vector<MoveNative>& moves,
                                    int root_player_index,
                                    bool maximize,
                                    int style,
                                    SearchContext* ctx = nullptr,
                                    int ply = 0,
                                    int tt_move = 0) {
  struct RankedMove {
    MoveNative move;
    int score;
  };
  std::vector<RankedMove> ranked;
  ranked.reserve(moves.size());
  for (const auto& move : moves) {
    if (ctx && search_should_stop(*ctx)) break;
    int score = -kSearchInfinity;
    UndoNative undo;
    if (execute_move(state, move, &undo)) {
      score = score_position_for_search(state, root_player_index, style, 0);
      undo_move(state, undo);
    }
    const int packed_move = pack_move(move);
    if (ctx) score += ordering_bonus(*ctx, packed_move, ply, tt_move);
    ranked.push_back({move, score});
  }
  std::sort(ranked.begin(), ranked.end(), [maximize](const RankedMove& a, const RankedMove& b) {
    return maximize ? a.score > b.score : a.score < b.score;
  });
  std::vector<MoveNative> ordered;
  ordered.reserve(ranked.size());
  for (const auto& entry : ranked) ordered.push_back(entry.move);
  return ordered;
}

int alpha_beta_search(StateNative& state, int root_player_index, int depth_remaining, int alpha, int beta, SearchContext& ctx, int ply = 0) {
  ctx.visited_nodes++;
  bool is_terminal = false;
  const int terminal = terminal_score(state, state.players[root_player_index].id, depth_remaining, is_terminal);
  if (is_terminal) return terminal;
  if (search_should_stop(ctx)) return evaluate_state_for_oracle(state, root_player_index, ctx.style);
  if (depth_remaining <= 0) return evaluate_state_for_oracle(state, root_player_index, ctx.style);

  const int actor_index = state.current_player_idx;
  if (actor_index < 0 || actor_index >= state.player_count || !state.players[actor_index].alive) {
    return evaluate_state_for_oracle(state, root_player_index, ctx.style);
  }

  const std::uint64_t tt_key = state.zobrist_hash;
  const int alpha_orig = alpha;
  const int beta_orig = beta;
  int tt_move = 0;
  if (const auto it = ctx.transposition.find(tt_key); it != ctx.transposition.end()) {
    const auto& entry = it->second;
    tt_move = entry.best_move;
    if (entry.depth >= depth_remaining) {
      if (entry.bound == kBoundExact) return entry.score;
      if (entry.bound == kBoundLower) alpha = std::max(alpha, entry.score);
      else if (entry.bound == kBoundUpper) beta = std::min(beta, entry.score);
      if (alpha >= beta) return entry.score;
    }
  }

  auto legal_moves = collect_legal_moves(state, actor_index);
  if (legal_moves.empty()) return evaluate_state_for_oracle(state, root_player_index, ctx.style);
  const bool maximize = actor_index == root_player_index;
  legal_moves = order_moves(state, legal_moves, root_player_index, maximize, ctx.style, &ctx, ply, tt_move);
  if (ctx.timed_out) return evaluate_state_for_oracle(state, root_player_index, ctx.style);

  int best = maximize ? -kSearchInfinity : kSearchInfinity;
  int best_move = 0;
  bool first_move = true;
  int move_index = 0;
  for (const auto& move : legal_moves) {
    if (search_should_stop(ctx)) break;
    const int packed_move = pack_move(move);
    ++move_index;
    if (should_prune_late_move(ctx, ply, depth_remaining, move_index, packed_move, tt_move)) continue;
    UndoNative undo;
    if (!execute_move(state, move, &undo)) continue;
    int score = 0;
    const int reduction = late_move_reduction(ctx, ply, depth_remaining, move_index, packed_move, tt_move);
    if (first_move) {
      score = alpha_beta_search(state, root_player_index, depth_remaining - 1, alpha, beta, ctx, ply + 1);
      first_move = false;
    } else if (maximize) {
      const int scout_beta = alpha >= kSearchInfinity - 1 ? kSearchInfinity : alpha + 1;
      if (reduction > 0) {
        score = alpha_beta_search(state, root_player_index, depth_remaining - 1 - reduction, alpha, scout_beta, ctx, ply + 1);
        if (score > alpha) {
          score = alpha_beta_search(state, root_player_index, depth_remaining - 1, alpha, scout_beta, ctx, ply + 1);
        }
      } else {
        score = alpha_beta_search(state, root_player_index, depth_remaining - 1, alpha, scout_beta, ctx, ply + 1);
      }
      if (score > alpha && score < beta) {
        score = alpha_beta_search(state, root_player_index, depth_remaining - 1, alpha, beta, ctx, ply + 1);
      }
    } else {
      const int scout_alpha = beta <= -kSearchInfinity + 1 ? -kSearchInfinity : beta - 1;
      if (reduction > 0) {
        score = alpha_beta_search(state, root_player_index, depth_remaining - 1 - reduction, scout_alpha, beta, ctx, ply + 1);
        if (score < beta) {
          score = alpha_beta_search(state, root_player_index, depth_remaining - 1, scout_alpha, beta, ctx, ply + 1);
        }
      } else {
        score = alpha_beta_search(state, root_player_index, depth_remaining - 1, scout_alpha, beta, ctx, ply + 1);
      }
      if (score < beta && score > alpha) {
        score = alpha_beta_search(state, root_player_index, depth_remaining - 1, alpha, beta, ctx, ply + 1);
      }
    }
    undo_move(state, undo);
    if (maximize) {
      if (score > best) {
        best = score;
        best_move = packed_move;
      }
      alpha = std::max(alpha, best);
    } else {
      if (score < best) {
        best = score;
        best_move = packed_move;
      }
      beta = std::min(beta, best);
    }
    if (ctx.timed_out) return best;
    if (beta <= alpha) {
      record_killer_move(ctx, ply, packed_move);
      update_history_heuristic(ctx, packed_move, depth_remaining);
      break;
    }
  }

  int bound = kBoundExact;
  if (best <= alpha_orig) bound = kBoundUpper;
  else if (best >= beta_orig) bound = kBoundLower;
  const auto existing = ctx.transposition.find(tt_key);
  if (existing == ctx.transposition.end() || depth_remaining >= existing->second.depth || bound == kBoundExact) {
    ctx.transposition[tt_key] = { depth_remaining, best, bound, best_move };
  }
  return best;
}

int pack_move(const MoveNative& move) {
  const int entry = move.entry < 0 ? kPackedNoEntry : move.entry;
  return ((move.tile_type & 0x0f) << 20) | ((move.row & 0x3f) << 12) | ((move.col & 0x3f) << 4) | (entry & 0x0f);
}

bool packed_move_equals(const MoveNative& move, int packed_move) {
  return packed_move > 0 && pack_move(move) == packed_move;
}

void promote_packed_move_to_front(std::vector<MoveNative>& moves, int packed_move) {
  if (packed_move <= 0 || moves.empty()) return;
  const auto it = std::find_if(moves.begin(), moves.end(), [packed_move](const MoveNative& move) {
    return packed_move_equals(move, packed_move);
  });
  if (it == moves.end() || it == moves.begin()) return;
  const MoveNative prioritized = *it;
  moves.erase(it);
  moves.insert(moves.begin(), prioritized);
}

bool search_should_stop(SearchContext& ctx) {
  if (ctx.timed_out) return true;
  if (!ctx.use_deadline) return false;
  if (std::chrono::steady_clock::now() < ctx.deadline) return false;
  ctx.timed_out = true;
  return true;
}

int score_position_for_search(const StateNative& state, int root_player_index, int style, int depth_remaining) {
  bool is_terminal = false;
  const int terminal = terminal_score(state, state.players[root_player_index].id, depth_remaining, is_terminal);
  if (is_terminal) return terminal;
  return evaluate_state_for_oracle(state, root_player_index, style);
}

bool search_root_depth(StateNative& state,
                       const std::vector<MoveNative>& root_moves,
                       int root_player_index,
                       int depth,
                       int alpha,
                       int beta,
                       SearchContext& ctx,
                       MoveNative& best_move,
                       int& best_score) {
  if (root_moves.empty()) return false;
  int local_alpha = alpha;
  bool searched_any = false;
  bool first_move = true;
  int local_best_score = -kSearchInfinity;
  MoveNative local_best_move = root_moves.front();
  for (const auto& move : root_moves) {
    if (search_should_stop(ctx)) break;
    UndoNative undo;
    if (!execute_move(state, move, &undo)) continue;
    ctx.visited_nodes++;
    searched_any = true;
    int score = 0;
    if (first_move) {
      score = alpha_beta_search(state, root_player_index, depth - 1, local_alpha, beta, ctx, 1);
      first_move = false;
    } else {
      const int scout_beta = local_alpha >= kSearchInfinity - 1 ? kSearchInfinity : local_alpha + 1;
      score = alpha_beta_search(state, root_player_index, depth - 1, local_alpha, scout_beta, ctx, 1);
      if (!ctx.timed_out && score > local_alpha && score < beta) {
        score = alpha_beta_search(state, root_player_index, depth - 1, local_alpha, beta, ctx, 1);
      }
    }
    undo_move(state, undo);
    if (score > local_best_score) {
      local_best_score = score;
      local_best_move = move;
    }
    local_alpha = std::max(local_alpha, local_best_score);
    if (ctx.timed_out) break;
    if (local_alpha >= beta) {
      const int packed_move = pack_move(move);
      record_killer_move(ctx, 0, packed_move);
      update_history_heuristic(ctx, packed_move, depth);
      break;
    }
  }
  if (!searched_any) return false;
  best_move = local_best_move;
  best_score = local_best_score;
  ctx.root_best_move = pack_move(local_best_move);
  return !ctx.timed_out;
}

void record_killer_move(SearchContext& ctx, int ply, int packed_move) {
  if (ply < 0 || ply >= kMaxSearchPly || packed_move <= 0) return;
  auto& killers = ctx.killer_moves[ply];
  if (killers[0] == packed_move) return;
  killers[1] = killers[0];
  killers[0] = packed_move;
}

void update_history_heuristic(SearchContext& ctx, int packed_move, int depth_remaining) {
  if (packed_move <= 0) return;
  ctx.history_heuristic[packed_move] += std::max(1, depth_remaining * depth_remaining);
}

int ordering_bonus(const SearchContext& ctx, int packed_move, int ply, int tt_move) {
  if (packed_move <= 0) return 0;
  int bonus = 0;
  if (tt_move > 0 && packed_move == tt_move) bonus += 1'000'000;
  if (ply >= 0 && ply < kMaxSearchPly) {
    if (ctx.killer_moves[ply][0] == packed_move) bonus += 250'000;
    else if (ctx.killer_moves[ply][1] == packed_move) bonus += 125'000;
  }
  if (const auto it = ctx.history_heuristic.find(packed_move); it != ctx.history_heuristic.end()) {
    bonus += std::min(100'000, it->second * 24);
  }
  return bonus;
}

bool is_killer_move(const SearchContext& ctx, int ply, int packed_move) {
  if (packed_move <= 0 || ply < 0 || ply >= kMaxSearchPly) return false;
  return ctx.killer_moves[ply][0] == packed_move || ctx.killer_moves[ply][1] == packed_move;
}

bool should_prune_late_move(const SearchContext& ctx, int ply, int depth_remaining, int move_index, int packed_move, int tt_move) {
  if (ply <= 0 || depth_remaining > 2 || move_index <= 10 || packed_move <= 0) return false;
  if (tt_move > 0 && packed_move == tt_move) return false;
  if (is_killer_move(ctx, ply, packed_move)) return false;
  if (const auto it = ctx.history_heuristic.find(packed_move); it != ctx.history_heuristic.end()) {
    if (it->second >= depth_remaining * depth_remaining * 8) return false;
  }
  return true;
}

int late_move_reduction(const SearchContext& ctx, int ply, int depth_remaining, int move_index, int packed_move, int tt_move) {
  if (ply <= 0 || depth_remaining < 3 || move_index <= 3 || packed_move <= 0) return 0;
  if (tt_move > 0 && packed_move == tt_move) return 0;
  if (is_killer_move(ctx, ply, packed_move)) return 0;
  if (const auto it = ctx.history_heuristic.find(packed_move); it != ctx.history_heuristic.end()) {
    if (it->second >= depth_remaining * depth_remaining * 12) return 0;
  }
  int reduction = 1;
  if (depth_remaining >= 7 && move_index >= 10) reduction = 2;
  return std::min(reduction, depth_remaining - 2);
}

bool rots_are_supported(int board_size, int cell_count, const int* cell_rots) {
  if (!cell_rots) return true;
  const int safe_cell_count = std::min(cell_count, board_size * board_size);
  for (int idx = 0; idx < safe_cell_count; ++idx) {
    if ((cell_rots[idx] & 3) != 0) return false;
  }
  return true;
}

bool build_native_state(StateNative& state,
                        int board_size,
                        int cell_count,
                        int current_player_idx,
                        int player_count,
                        int jump_selection_mode,
                        int combined_pool_mode,
                        int oracle_style,
                        int variant_mode,
                        const int* player_state,
                        const int* cell_types,
                        const int* cell_rots,
                        const int* cell_flags) {
  if (board_size != kBoardSize7 || cell_count < kSupportedBoardCells || player_count != kNativePlayers) return false;
  if (variant_mode != kVariantStandard) return false;
  if (!player_state || !cell_types || !cell_flags) return false;
  if (!rots_are_supported(board_size, cell_count, cell_rots)) return false;

  state.board_size = board_size;
  state.player_count = player_count;
  state.current_player_idx = std::max(0, std::min(current_player_idx, player_count - 1));
  state.game_over = false;
  state.winner = -1;
  state.config.jump_selection = jump_selection_mode;
  state.config.combined_pool = combined_pool_mode != 0;
  state.config.oracle_style = oracle_style;

  for (int idx = 0; idx < kSupportedBoardCells; ++idx) {
    state.cell_types[idx] = cell_types[idx];
    state.cell_flags[idx] = cell_flags[idx];
  }

  for (int idx = 0; idx < player_count; ++idx) {
    const int base = idx * kPlayerStateStride;
    auto& player = state.players[idx];
    player.id = player_state[base + 0];
    player.alive = player_state[base + 1] != 0;
    player.has_placed_first_tile = player_state[base + 2] != 0;
    player.row = player_state[base + 3];
    player.col = player_state[base + 4];
    player.entrance = player_state[base + 5];
    player.jump_left = player_state[base + 6];
    player.diagonal_jump_left = player_state[base + 7];
    player.combined_jump_left = player_state[base + 8];
  }

  finalize_remaining_players_outcome(state);
  check_position_draw(state);
  state.zobrist_hash = compute_native_zobrist_hash(state);
  return true;
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
int oracle_module_version() {
  return 9;
}

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

EMSCRIPTEN_KEEPALIVE
int oracle_count_empty_playable_cells(int board_size,
                                      int cell_count,
                                      const int* cell_types,
                                      const int* cell_flags) {
  if (board_size <= 0 || board_size * board_size > kMaxBoardCells || cell_count <= 0 || !cell_types || !cell_flags) {
    return 0;
  }
  const BoardBitboards bitboards = build_board_bitboards(
    board_size,
    cell_count,
    cell_types,
    nullptr,
    nullptr,
    cell_flags,
    0
  );
  return std::popcount(bitboards.empty_playable_mask());
}

EMSCRIPTEN_KEEPALIVE
int oracle_compute_state_hash(int board_size,
                              int cell_count,
                              int depth_remaining,
                              int current_player_idx,
                              int player_id,
                              int actor_id,
                              int game_over,
                              int winner,
                              int player_count,
                              const int* player_state,
                              const int* cell_types,
                              const int* cell_rots,
                              const int* cell_owners,
                              const int* cell_flags) {
  if (board_size <= 0 || board_size * board_size > kMaxBoardCells || cell_count <= 0 ||
      !player_state || !cell_types || !cell_rots || !cell_owners || !cell_flags) {
    return 0;
  }

  const int safe_player_count = std::max(0, std::min(player_count, kMaxPlayers));
  const BoardBitboards bitboards = build_board_bitboards(
    board_size,
    cell_count,
    cell_types,
    cell_rots,
    cell_owners,
    cell_flags,
    safe_player_count
  );

  std::uint32_t h = 2166136261u;
  h = hash_mix32(h, static_cast<std::uint32_t>((player_id + 17) * 97));
  h = hash_mix32(h, static_cast<std::uint32_t>((depth_remaining + 31) * 131));
  h = hash_mix32(h, static_cast<std::uint32_t>(((actor_id >= 0 ? actor_id : -1) + 53) * 151));
  h = hash_mix32(h, static_cast<std::uint32_t>((current_player_idx + 61) * 173));
  h = hash_mix32(h, static_cast<std::uint32_t>(game_over ? 1 : 0));
  h = hash_mix32(h, winner >= 0 ? static_cast<std::uint32_t>(winner + 7) : 0u);
  h = hash_mix32(h, static_cast<std::uint32_t>(bitboards.size + 787));

  for (int idx = 0; idx < safe_player_count; ++idx) {
    const int base = idx * kPlayerStateStride;
    const int id = player_state[base + 0];
    const int alive = player_state[base + 1];
    const int has_placed_first_tile = player_state[base + 2];
    const int row = player_state[base + 3];
    const int col = player_state[base + 4];
    const int entrance = player_state[base + 5];
    const int jump_left = player_state[base + 6];
    const int diagonal_jump_left = player_state[base + 7];
    const int combined_jump_left = player_state[base + 8];

    h = hash_mix32(h, static_cast<std::uint32_t>(id + 101));
    h = hash_mix32(h, static_cast<std::uint32_t>(alive ? 1 : 0));
    h = hash_mix32(h, static_cast<std::uint32_t>(has_placed_first_tile ? 1 : 0));
    h = hash_mix32(h, row >= 0 ? static_cast<std::uint32_t>(row + 257) : 0u);
    h = hash_mix32(h, col >= 0 ? static_cast<std::uint32_t>(col + 353) : 0u);
    h = hash_mix32(h, entrance >= 0 ? static_cast<std::uint32_t>(entrance + 449) : 0u);
    h = hash_mix32(h, static_cast<std::uint32_t>(jump_left + 557));
    h = hash_mix32(h, static_cast<std::uint32_t>(diagonal_jump_left + 653));
    h = hash_mix32(h, static_cast<std::uint32_t>(combined_jump_left + 751));
  }

  h = hash_mix64(h, bitboards.occupied_mask, 811);
  h = hash_mix64(h, bitboards.dead_mask, 887);
  h = hash_mix64(h, bitboards.start_mask, 953);
  for (int type_code = 1; type_code < static_cast<int>(bitboards.type_masks.size()); ++type_code) {
    h = hash_mix64(h, bitboards.type_masks[type_code], static_cast<std::uint32_t>(1009 + type_code * 61));
  }
  for (int rot = 0; rot < static_cast<int>(bitboards.rot_masks.size()); ++rot) {
    h = hash_mix64(h, bitboards.rot_masks[rot], static_cast<std::uint32_t>(1601 + rot * 73));
  }
  for (int owner_id = 0; owner_id < safe_player_count; ++owner_id) {
    h = hash_mix64(h, bitboards.owner_masks[owner_id], static_cast<std::uint32_t>(1901 + owner_id * 89));
  }

  return static_cast<int>(h);
}

EMSCRIPTEN_KEEPALIVE
int oracle_choose_native_move(int board_size,
                              int cell_count,
                              int current_player_idx,
                              int player_count,
                              int jump_selection_mode,
                              int combined_pool_mode,
                              int oracle_style,
                              int variant_mode,
                              int max_depth,
                              int time_budget_ms,
                              const int* player_state,
                              const int* cell_types,
                              const int* cell_rots,
                              const int* cell_flags) {
  const auto search_start = std::chrono::steady_clock::now();
  g_oracle_last_search_nodes = 0;
  g_oracle_last_search_elapsed_ms = 0;

  StateNative state;
  if (!build_native_state(
        state,
        board_size,
        cell_count,
        current_player_idx,
        player_count,
        jump_selection_mode,
        combined_pool_mode,
        oracle_style,
        variant_mode,
        player_state,
        cell_types,
        cell_rots,
        cell_flags)) {
    g_oracle_last_search_elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - search_start).count());
    return -1;
  }

  const int root_player_index = state.current_player_idx;
  const int safe_max_depth = std::max(1, std::min(max_depth, kOracleTargetDepth));
  const int safe_time_budget_ms = std::max(0, time_budget_ms);
  auto legal_moves = collect_legal_moves(state, root_player_index);
  if (legal_moves.empty()) {
    g_oracle_last_search_elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - search_start).count());
    return -1;
  }
  const std::uint64_t root_cache_key = state.zobrist_hash
    ^ zobrist_key(
      0x524f4f5443414348ull,
      (static_cast<std::uint64_t>(oracle_style & 0xff) << 24)
      | (static_cast<std::uint64_t>(safe_max_depth & 0xff) << 16)
      | static_cast<std::uint64_t>(safe_time_budget_ms & 0xffff)
    );
  if (const auto it = g_oracle_root_move_cache.find(root_cache_key); it != g_oracle_root_move_cache.end()) {
    g_oracle_last_search_elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - search_start).count());
    return it->second;
  }
  SearchContext ctx;
  ctx.style = oracle_style;
  const bool force_exact_endgame = safe_max_depth <= 6
    || (safe_max_depth <= 8 && legal_moves.size() <= 6);
  if (safe_time_budget_ms > 0 && !force_exact_endgame) {
    ctx.use_deadline = true;
    ctx.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(safe_time_budget_ms);
  }
  auto ordered = order_moves(state, legal_moves, root_player_index, true, oracle_style, &ctx, 0, 0);
  if (ordered.empty()) ordered = legal_moves;
  MoveNative best_move = ordered[0];
  int best_score = -kSearchInfinity;
  bool completed_depth = false;
  bool stop_search = false;
  for (int depth = 1; depth <= safe_max_depth; ++depth) {
    if (search_should_stop(ctx)) break;
    ordered = order_moves(state, legal_moves, root_player_index, true, oracle_style, &ctx, 0, ctx.root_best_move);
    if (ordered.empty()) ordered = legal_moves;
    promote_packed_move_to_front(ordered, ctx.root_best_move);
    if (ctx.timed_out) break;

    MoveNative depth_best_move = best_move;
    int depth_best_score = best_score;
    int aspiration = 220 + depth * 90;
    int alpha = -kSearchInfinity;
    int beta = kSearchInfinity;
    if (completed_depth && std::abs(best_score) < 900000) {
      alpha = std::max(-kSearchInfinity, best_score - aspiration);
      beta = std::min(kSearchInfinity, best_score + aspiration);
    }

    while (true) {
      const int window_alpha = alpha;
      const int window_beta = beta;
      const bool completed = search_root_depth(
        state,
        ordered,
        root_player_index,
        depth,
        window_alpha,
        window_beta,
        ctx,
        depth_best_move,
        depth_best_score
      );
      if (ctx.timed_out) {
        if (!completed_depth && depth_best_score > -kSearchInfinity) {
          best_move = depth_best_move;
          best_score = depth_best_score;
        }
        stop_search = true;
        break;
      }
      if (!completed) {
        stop_search = true;
        break;
      }
      if (completed_depth && std::abs(best_score) < 900000) {
        if (depth_best_score <= window_alpha) {
          aspiration = std::min(kSearchInfinity / 8, aspiration * 2);
          alpha = std::max(-kSearchInfinity, best_score - aspiration);
          beta = window_beta;
          continue;
        }
        if (depth_best_score >= window_beta) {
          aspiration = std::min(kSearchInfinity / 8, aspiration * 2);
          alpha = window_alpha;
          beta = std::min(kSearchInfinity, best_score + aspiration);
          continue;
        }
      }
      best_move = depth_best_move;
      best_score = depth_best_score;
      completed_depth = true;
      ctx.root_best_move = pack_move(best_move);
      break;
    }
    if (stop_search) break;
  }

  const int packed_best_move = pack_move(best_move);
  g_oracle_last_search_nodes = ctx.visited_nodes;
  g_oracle_last_search_elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - search_start).count());
  if (g_oracle_root_move_cache.size() > 200000) g_oracle_root_move_cache.clear();
  g_oracle_root_move_cache[root_cache_key] = packed_best_move;
  return packed_best_move;
}

EMSCRIPTEN_KEEPALIVE
double oracle_last_search_nodes() {
  return static_cast<double>(g_oracle_last_search_nodes);
}

EMSCRIPTEN_KEEPALIVE
int oracle_last_search_elapsed_ms() {
  return g_oracle_last_search_elapsed_ms;
}

}  // extern "C"
