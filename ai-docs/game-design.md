# Tile Path Game - Design Docs

This document was split into two focused markdown files:

- `ai-docs/game-rules.md` - gameplay rules, tile behavior, turn flow, and win/lose conditions.
- `ai-docs/game-graphics.md` - tile visuals, board rendering, UI layout, and style guidance.

Use these two docs as the current source of truth.

# Tile Path Game — Design Document

## Overview

A single-HTML, pass-and-play, turn-based board game for 2–4 players. Players place path tiles on a shared grid to force opponents off the board, into dead zones, or onto an opponent's start position. Think of it as a competitive Tsuro-like game with jump mechanics.

---

## 1. Board

| Setting | Small Board | Large Board |
|---------|-------------|-------------|
| Grid    | 7 × 7       | 13 × 13     |
| Jump tiles per player | 3 | 4 |

- Each cell on the grid is either **blank**, **occupied by a tile**, a **start base**, or a **dead zone (X)**.
- Each of the four corners has a **start base** (one per player). Base size depends on board size:
  - **13×13:** 2×2 base (4 cells per player)
  - **7×7:** 1×1 base (1 cell per player)
- Base cells are permanently occupied and cannot have tiles placed on them.
- Certain cells are pre-marked as **dead zones (X)** as part of the board layout. See §3.10 and §3.11 for dead zone maps.
- **Tiles may NOT be placed on start bases or dead zones.**

### 1.1 Coordinate System

- Origin `(0, 0)` at top-left corner.
- `row` increases downward, `col` increases rightward.

### 1.2 Start Bases

Base size differs by board:

**13×13 — 2×2 bases:**

| Player | Corner       | Cells                              |
|--------|--------------|-------------------------------------|
| P1     | Top-left     | (0,0), (0,1), (1,0), (1,1)         |
| P2     | Top-right    | (0,11), (0,12), (1,11), (1,12)     |
| P3     | Bottom-right | (11,11), (11,12), (12,11), (12,12) |
| P4     | Bottom-left  | (11,0), (11,1), (12,0), (12,1)     |

Adjacent cells for first-move placement (13×13):

| Player | Adjacent cells (4 options)              |
|--------|-----------------------------------------|
| P1     | (0,2), (1,2), (2,0), (2,1)             |
| P2     | (0,10), (1,10), (2,11), (2,12)         |
| P3     | (10,11), (10,12), (11,10), (12,10)     |
| P4     | (10,0), (10,1), (11,2), (12,2)         |

**7×7 — 1×1 bases:**

| Player | Corner       | Cell   |
|--------|--------------|--------|
| P1     | Top-left     | (0,0)  |
| P2     | Top-right    | (0,6)  |
| P3     | Bottom-right | (6,6)  |
| P4     | Bottom-left  | (6,0)  |

Adjacent cells for first-move placement (7×7):

| Player | Adjacent cells (2 options)  |
|--------|-----------------------------|
| P1     | (0,1), (1,0)               |
| P2     | (0,5), (1,6)               |
| P3     | (5,6), (6,5)               |
| P4     | (5,0), (6,1)               |

### 1.3 Player Count Setups

**4 players:** All 4 corner bases are active start positions.

**3 players:** Players choose which 3 corners to use. The unused corner's base becomes a **dead zone**.

**2 players:** Players start on **diagonal** corners (e.g., P1 top-left, P2 bottom-right). The two unused diagonal corners become **dead zones**.

### 1.4 Own-Start-Is-Deadly Rule

A player's **own** start base is a **dead zone to that player**. If a player's movement chains them back into their own base, they are **eliminated**. However, an opponent reaching your base means the **opponent wins** (see §6).

---

## 2. Tile Model

Each tile is a square that fits into one grid cell. Every tile has **8 entrances/exits**, 2 per edge:

```
    N0   N1
   ┌──┬──┐
W1 │        │ E0
   │        │
W0 │        │ E1
   └──┴──┘
    S1   S0
```

Entrance labelling (clockwise from top-left):

| Index | Label | Edge   | Position on edge |
|-------|-------|--------|------------------|
| 0     | N0    | North  | Left             |
| 1     | N1    | North  | Right            |
| 2     | E0    | East   | Top              |
| 3     | E1    | East   | Bottom           |
| 4     | S0    | South  | Right            |
| 5     | S1    | South  | Left             |
| 6     | W0    | West   | Bottom           |
| 7     | W1    | West   | Top              |

Adjacency between two tiles sharing an edge: the entrances on one tile connect to the **mirrored** entrances on the neighboring tile.

| Neighbor Direction | This tile's exits | Neighbor's entrances |
|--------------------|-------------------|----------------------|
| North              | N0, N1            | S1, S0               |
| East               | E0, E1            | W1, W0               |
| South              | S0, S1            | N1, N0               |
| West               | W0, W1            | E1, E0               |

### 2.1 Tile Rotation

Every tile can be placed in one of **4 rotations** (0°, 90°, 180°, 270° clockwise). A 90° CW rotation maps entrance index `i` → `(i + 2) % 8`.

---

## 3. Seven Tile Types

There are **7 tile types**. Each defines **4 internal paths** connecting pairs of entrances. Below are the path connections at 0° rotation. **(Confirmed correct by designer.)**

### 3.1 Straightaway

Paths cross straight through the tile (parallel horizontal and vertical lines).

```
Connections: N0↔S1, N1↔S0, E0↔W1, E1↔W0
```

```
  N0  N1
  │    │
  │    │
  │    │
  S1  S0
  (+ E0↔W1, E1↔W0 horizontally)
```

### 3.2 U-Turn

Each path curves back to the same edge it entered from.

```
Connections: N0↔N1, E0↔E1, S0↔S1, W0↔W1
```

```
  N0──N1
  ┌────┐
  │    │
  └────┘
  S1──S0
  (+ E0↔E1, W0↔W1 on sides)
```

### 3.3 Centrifugal

Paths curve toward the adjacent entrance (sweeping outward).

```
Connections: N0↔W1, N1↔E0, E1↔S0, S1↔W0
```

Each path curves 90° toward the next edge clockwise.

### 3.4 Diagonal Straightaway

Paths cross the tile diagonally.

```
Connections: N0↔E1, N1↔W0, S0↔W1, S1↔E0
```

### 3.5 Weave

Paths cross each other in the center of the tile (like an X).

```
Connections: N0↔S0, N1↔S1, E0↔W0, E1↔W1
```

The N-S and E-W paths visually cross/weave over each other.

### 3.6 Jump Tile

A **straightaway with a jump pad**. Uses the same path layout as the straightaway (N0↔S1, N1↔S0, E0↔W1, E1↔W0), but the player **jumps cardinally** — skipping one cell in the exit direction and landing two cells away (see §5.3).

**Jump entrance preservation rule:** When a player enters a jump tile via entrance X, they land on the **same entrance index X** on the destination cell (two cells away). For example, entering via S1 means landing on S1 of the cell beyond the skipped cell.

### 3.7 Diagonal Jump Tile

A **diagonal straightaway with a jump pad**. Uses the same path layout as the diagonal straightaway (N0↔E1, N1↔W0, S0↔W1, S1↔E0), but the player **jumps diagonally to the corner-touching tile** (see §5.3).

The diagonal direction is determined by which corner of the tile the exit entrance is nearest to:

```
Connections & diagonal jump directions (at 0° rotation, corner-touch landing):
```

| Entry Entrance | Exit Entrance | Jump Direction | Land Cell          |
|----------------|---------------|----------------|--------------------|
| N0             | E1            | SE             | (row+1, col+1)    |
| E1             | N0            | NW             | (row-1, col-1)    |
| N1             | W0            | SW             | (row+1, col-1)    |
| W0             | N1            | NE             | (row-1, col+1)    |
| S1             | E0            | NE             | (row-1, col+1)    |
| E0             | S1            | SW             | (row+1, col-1)    |
| S0             | W1            | NW             | (row-1, col-1)    |
| W1             | S0            | SE             | (row+1, col+1)    |

Same entrance preservation rule applies: entering via entrance X → land on entrance X.

### 3.8 Tile Graphics

All tile paths are rendered as **straight lines** (no curves). Visual style per tile:

| Tile                  | Graphic Description                                           |
|-----------------------|---------------------------------------------------------------|
| Straightaway          | 4 straight parallel lines (2 vertical + 2 horizontal)        |
| U-Turn                | Straight lines connecting pairs on the same edge (hairpin)    |
| Centrifugal           | Straight lines connecting adjacent-edge entrances (90° angles)|
| Diagonal Straightaway | 4 straight diagonal lines crossing the tile                  |
| Weave                 | Straight lines crossing in the center (X pattern)            |
| Jump Tile             | Straightaway lines + a **circle** (jump pad symbol) in the center |
| Diagonal Jump Tile    | Diagonal straightaway lines + a **circle** (jump pad symbol) in the center |

The circle distinguishes jump tiles from their non-jump counterparts at a glance.

### 3.9 Tile Supply

| Tile Type             | Per-player limit (7×7) | Per-player limit (13×13) | Unlimited? |
|-----------------------|------------------------|--------------------------|------------|
| Straightaway          | —                      | —                        | Yes        |
| U-Turn                | —                      | —                        | Yes        |
| Centrifugal           | —                      | —                        | Yes        |
| Diagonal Straightaway | —                      | —                        | Yes        |
| Weave                 | —                      | —                        | Yes        |
| Jump                  | 3                      | 4                        | No*        |
| Diagonal Jump         | 3                      | 4                        | No*        |

*Jump pad availability is **configurable** during game setup (see §7.5). Options:
- **Single type only** (cardinal or diagonal): 3 on 7×7, 4 on 13×13 per player.
- **Both types — combined pool**: 3 on 7×7 / 4 on 13×13 total per player, freely split between the two types.
- **Both types — separate pools**: 2 of each type per player.
- **Neither**: No jump tiles available.
Disabled jump types do not appear in the tile selector.

### 3.10 Dead Zone Layout — 13×13 Board (Confirmed)

**13 fixed interior dead zones** form a 4-fold rotationally symmetric diamond pattern centered on (6,6):

| Dead Zone | Symmetry Group |
|-----------|---------------|
| (6, 6)    | Center        |
| (3, 6), (6, 9), (9, 6), (6, 3) | Outer cross (3 cells from center) |
| (4, 4), (4, 8), (8, 8), (8, 4) | Outer diamond |
| (5, 5), (5, 7), (7, 7), (7, 5) | Inner diamond |

**Complete 13×13 board map (4-player mode):**

```
     0  1  2  3  4  5  6  7  8  9 10 11 12
 0:  B  B  .  .  .  .  .  .  .  .  .  B  B
 1:  B  B  .  .  .  .  .  .  .  .  .  B  B
 2:  .  .  .  .  .  .  .  .  .  .  .  .  .
 3:  .  .  .  .  .  .  X  .  .  .  .  .  .
 4:  .  .  .  .  X  .  .  .  X  .  .  .  .
 5:  .  .  .  .  .  X  .  X  .  .  .  .  .
 6:  .  .  .  X  .  .  X  .  .  X  .  .  .
 7:  .  .  .  .  .  X  .  X  .  .  .  .  .
 8:  .  .  .  .  X  .  .  .  X  .  .  .  .
 9:  .  .  .  .  .  .  X  .  .  .  .  .  .
10:  .  .  .  .  .  .  .  .  .  .  .  .  .
11:  B  B  .  .  .  .  .  .  .  .  .  B  B
12:  B  B  .  .  .  .  .  .  .  .  .  B  B

B = Start base (2×2)    X = Dead zone    . = Blank
```

These 13 interior dead zones are **fixed** and present in all games regardless of player count. In 2- or 3-player mode, the unused corner 2×2 bases also become dead zones (adding 4 or 8 more dead-zone cells).

### 3.11 Dead Zone Layout — 7×7 Board (Confirmed)

**5 fixed interior dead zones** form a 4-fold rotationally symmetric diamond pattern centered on (3,3):

| Dead Zone | Symmetry Group |
|-----------|---------------|
| (3, 3)    | Center        |
| (2, 2), (2, 4), (4, 4), (4, 2) | Diamond |

**Complete 7×7 board map (4-player mode):**

```
     0  1  2  3  4  5  6
 0:  B  .  .  .  .  .  B
 1:  .  .  .  .  .  .  .
 2:  .  .  X  .  X  .  .
 3:  .  .  .  X  .  .  .
 4:  .  .  X  .  X  .  .
 5:  .  .  .  .  .  .  .
 6:  B  .  .  .  .  .  B

B = Start base (1×1)    X = Dead zone    . = Blank
```

These 5 interior dead zones are **fixed** and present in all games regardless of player count.

---

## 4. Player State

Each player tracks:

| Field               | Description                                         |
|---------------------|-----------------------------------------------------|
| `id`                | 1–4                                                 |
| `color`             | Red / Blue / Green / Yellow                         |
| `position`          | `{row, col, entranceIndex}` — current cell + which entrance the player is sitting on |
| `alive`             | Boolean — eliminated players are out                |
| `hasPlacedFirstTile`| Boolean — false until their first tile is placed    |
| `jumpTilesRemaining`| Count of jump tiles left to place                   |
| `base`              | Start base: `{cells: [[r,c],...]}` — 2×2 on 13×13, 1×1 on 7×7 |

---

## 5. Turn Flow

### 5.1 Placement Phase

**First turn (special):** The player places a tile on **any blank cell adjacent to their base** (see §1.2 for adjacent cell counts per board size). They then choose one of the entrances on that tile that faces their base, and their token is placed on that entrance. The player **ends on a blank cell** — there is no path to chain onto from the first tile. **(Confirmed.)**

**Subsequent turns:** The player selects a tile type and rotation, then places the tile on the **single blank cell directly in front of the entrance they are currently on**. The entrance determines which adjacent cell is "in front" (e.g., if on entrance N0 or N1 of a cell, the cell to the North is "in front"). **(Confirmed: most restrictive option — strategic planning is essential.)**

**Placement restrictions:**
- Placing a tile is **mandatory** — you must place exactly one tile per turn.
- Tiles may **NOT** be placed on start bases or dead zones. **(Confirmed.)**

### 5.2 Movement Phase (Automatic)

After a tile is placed, **all players whose current entrance now connects to a newly available path** must move immediately. **(Confirmed: all affected players move, not just the current player.)** Movement is resolved **sequentially in player order** (P1 first, then P2, etc.), like chess. **(Confirmed.)**

1. The player follows the path inside the tile they are on, exiting through the connected entrance.
2. The exit entrance leads into the **adjacent cell** on the neighboring tile's corresponding entrance.
3. If that adjacent cell **has a tile**, the player immediately follows that tile's path (chaining continues in the same turn).
4. If the adjacent cell is **blank**, the player stops there, sitting on the entrance of the blank cell.
5. If the next cell is **off the board**, a **dead zone (X)**, or the player's **own start base** — the player is **eliminated at the boundary** (they do not enter the cell). **(Confirmed.)**
6. If the player reaches **an opponent's start base** — the moving player **wins**.
7. If two players end up on the **exact same position** (same cell + same entrance) — it is a **draw** between those two players.
8. **Loop detection:** If during chaining the player visits the same `(row, col, entrance)` position twice, they are stuck in a loop and are **eliminated**. **(Confirmed.)**

### 5.3 Jump Pad Movement

There are two types of jump tiles: **Jump** (cardinal) and **Diagonal Jump** (diagonal). Both preserve entrance index on landing.

**Jump Tile (cardinal):** The player jumps in the cardinal direction (N/S/E/W) determined by the exit entrance. Skip one cell, land two cells away in that direction.

**Diagonal Jump Tile:** The player jumps diagonally (NE/NW/SE/SW) to the **corner-touching diagonal tile** determined by the path's diagonal direction (see §3.7). This is a one-step diagonal landing.

**Common rules for both jump types:**
- The player lands on the **same entrance index** they used to enter the jump tile (e.g., S1 → S1). **(Confirmed.)**
- If the landing cell is blank, the player stops there on that entrance.
- If the landing cell has a tile, the player enters via that entrance and continues following paths (chaining).
- If the landing cell is off the board / dead zone / own base — the player is **eliminated**.

**Cardinal-jump-specific rule:**
- The player **flies over** the skipped cell, ignoring any tile on it. **(Confirmed.)**

### 5.4 Turn Order

Players take turns in order: P1 → P2 → P3 → P4 → P1 → ... (skipping eliminated players).

---

## 6. Win / Lose / Draw Conditions

| Condition | Result |
|-----------|--------|
| Player's path leads off the board | Player is **eliminated at the boundary** (does not leave the board) |
| Player's path leads into a dead zone (X) | Player is **eliminated at the boundary** (does not enter the dead zone cell) |
| Player's path leads into their **own** start base | Player is **eliminated at the boundary** (own base is deadly) |
| Player gets stuck in a **loop** (revisits same position during chaining) | Player is **eliminated** (stuck in loop) |
| Player reaches any cell of an **opponent's** start base | Moving player **wins the game** |
| Two players land on the exact same position | **Draw** between those two |
| Only one player remains alive | That player **wins** |

---

## 7. UI Design

### 7.1 Layout

```
┌─────────────────────────────────────────────┐
│  HEADER: Game title, current player, status │
├────────────────────┬────────────────────────┤
│                    │  SIDEBAR:              │
│   GAME BOARD       │  - Tile selector       │
│   (SVG / Canvas)   │  - Rotation control    │
│                    │  - Jump tiles left     │
│                    │  - Player status panel │
│                    │  - End turn button     │
├────────────────────┴────────────────────────┤
│  FOOTER: Rules summary / help toggle        │
└─────────────────────────────────────────────┘
```

### 7.2 Board Rendering

- Use **SVG** for crisp rendering at any zoom level.
- Each cell is a square (e.g., 60×60 px).
- Paths inside tiles rendered as straight line segments.
- Player tokens rendered as colored circles on their current entrance position.
- Dead zones marked with a red X pattern.
- Start positions marked with player color halos.
- Blank cells shown as light grid squares.
- The board shows only placed tiles; placement guidance is shown via valid-cell highlighting.

### 7.3 Tile Selector

- 7 tile buttons in the sidebar, each showing a small preview of the tile type.
- Tile graphics are visible in the tile menu **at all times** (not only on hover/place).
- Jump tile and diagonal jump tile buttons show remaining count; grayed out when exhausted.
- A rotation slider or 4 rotation buttons (0°, 90°, 180°, 270°).

### 7.4 Interaction Flow

1. Player selects a tile type from the sidebar.
2. Player selects rotation.
3. Player clicks a valid blank cell on the board to place the tile.
4. Movement animates automatically (paths light up, player tokens slide along paths).
5. If no elimination/win occurs, turn passes to next player.

### 7.5 Setup Screen

- Choose board size: 7×7 or 13×13.
- Choose number of players: 2, 3, or 4.
- **Jump pad selection:** Choose which jump pad types to include:
  - Cardinal jump only (3 on 7×7, 4 on 13×13 per player)
  - Diagonal jump only (3 on 7×7, 4 on 13×13 per player)
  - Both → sub-option for pool mode:
    - **Combined pool:** 3 on 7×7 / 4 on 13×13 total per player, shared across both types
    - **Separate pools:** 2 of each type per player (4 total on 7×7, 4 total on 13×13)
  - Neither (no jump pads at all)
- Enter player names (optional).
- "Start Game" button.

### 7.6 Visual Style

- Clean, modern flat design.
- Dark board background with light grid lines.
- Distinct, colorblind-friendly player colors (Red, Blue, Green, Yellow with pattern fallbacks).
- Smooth CSS animations for token movement.

---

## 8. Technical Architecture

### 8.1 Single HTML File

Everything ships in **one `index.html`** file containing:
- Embedded CSS (`<style>`)
- Embedded JS (`<script>`)
- SVG templates for tiles and board

### 8.2 Code Structure (Logical Modules inside the single file)

```
┌─────────────────────────────────────┐
│  GameConfig                         │  Board size, player count, etc.
│  TileDefinitions                    │  Path mappings for all 6 types
│  BoardState                         │  2D grid of cells
│  PlayerState                        │  Position, alive, jump tiles
│  GameLogic                          │  Placement validation, movement,
│                                     │  chain resolution, win/loss checks
│  Renderer (SVG)                     │  Draw board, tiles, players, animations
│  UIController                       │  Event handlers, tile selector, turns
│  AnimationEngine                    │  Token movement along paths
│  SetupScreen                        │  Pre-game configuration UI
└─────────────────────────────────────┘
```

### 8.3 Data Structures

```javascript
Cell = {
  row: number,
  col: number,
  type: 'blank' | 'tile' | 'start' | 'deadzone',
  tile: null | {
    tileType: 'straightaway'|'uturn'|'centrifugal'|'diagonal'|'weave'|'jump'|'diagonalJump',
    rotation: 0|1|2|3
  },
  owner: null | playerId   // for start base cells
}

Player = {
  id: number,
  name: string,
  color: string,
  row: number,
  col: number,
  entrance: number,         // 0–7, which entrance they sit on
  alive: boolean,
  hasPlacedFirstTile: boolean,
  jumpTilesLeft: number,
  base: {                   // 2×2 (13×13) or 1×1 (7×7)
    cells: [[r,c], ...]     // all cells in base
  }
}

TileDef = {
  name: string,
  paths: [[from, to], ...]  // 4 pairs of entrance indices
}

DEAD_ZONES_13x13 = [
  [3,6], [4,4], [4,8], [5,5], [5,7],
  [6,3], [6,6], [6,9],
  [7,5], [7,7], [8,4], [8,8], [9,6]
]

DEAD_ZONES_7x7 = [
  [2,2], [2,4], [3,3], [4,2], [4,4]
]
```

### 8.4 Movement Algorithm (Pseudocode)

```
function resolveTurn(placingPlayer):
    for player in allAlivePlayers sorted by id:
        if playerIsAffected(player, placedTile):
            resolveMovement(player)

function resolveMovement(player):
    visited = Set()   // track (row, col, entrance) for loop detection

    while true:
        tile = board[player.row][player.col].tile
        if tile is null:
            break  // on blank cell, stop

        state = (player.row, player.col, player.entrance)
        if state in visited:
            eliminate(player)   // stuck in a loop
            return
        visited.add(state)

        exitEntrance = tile.getConnectedExit(player.entrance)

        if tile.type == 'jump':
            direction = getCardinalDirection(exitEntrance)  // N, E, S, W
            (skipRow, skipCol) = step(player.row, player.col, direction)
            (nextRow, nextCol) = step(skipRow, skipCol, direction)
            nextEntrance = player.entrance  // S1→S1
        else if tile.type == 'diagonalJump':
            direction = getDiagonalDirection(player.entrance, exitEntrance)  // NE, NW, SE, SW
            (nextRow, nextCol) = stepDiag(player.row, player.col, direction)
            nextEntrance = player.entrance  // S1→S1
        else:
            (nextRow, nextCol, nextEntrance) = getAdjacentCell(
                player.row, player.col, exitEntrance)

        if outOfBounds(nextRow, nextCol):
            eliminate(player)       // eliminated at boundary
            return
        if isDeadZone(nextRow, nextCol):
            eliminate(player)       // eliminated at boundary
            return
        if isOwnBase(nextRow, nextCol, player):
            eliminate(player)       // own base is deadly
            return
        if isOpponentBase(nextRow, nextCol, player):
            declareWinner(player)   // reached opponent's base
            return

        player.row = nextRow
        player.col = nextCol
        player.entrance = nextEntrance
        checkDrawCondition(player)
```

---

## 9. Resolved & Open Questions

### Resolved

| # | Question | Answer |
|---|----------|--------|
| Q1 | Tile path connections | **Confirmed correct** as documented in §3. |
| Q2 | Tile placement — "in front of" | The single blank cell the player's entrance directly faces. |
| Q3 | Dead zone layout (13×13) | **13 interior dead zones confirmed** — see §3.10 for exact coordinates. |
| Q4 | First move | Player places on **any blank cell adjacent to their base** (4 options on 13×13, 2 options on 7×7), then picks an entrance facing the base. |
| Q5 | Who moves after placement | **All affected players** move immediately, not just the current player. |
| Q6 | Movement order | **Sequential by player order** (P1, P2, P3, P4), like chess. |
| Q7 | Jump direction | Follows the exit direction of the tile's path. |
| Q8 | Jump — skipped cell with tile | **Player flies over it**, ignoring the tile's paths. |
| Q9 | Reaching own start | **Player is eliminated.** Own start base is a dead zone to its owner. |
| Q10 | Three-player setup | **Players choose** which corner becomes the dead zone. |
| Q11 | Tiles on start/dead zones | **No.** Tiles may not be placed on start bases or dead zones. |
| Q12 | Jump — landing entrance | Player lands on the **same entrance index** they used to enter the jump tile (S1 → S1). |
| Q13 | Elimination at dead zone | Player is **eliminated at the boundary** — they do not enter the dead zone cell. |
| Q14 | Dead zone layout (7×7) | **5 interior dead zones confirmed** — (2,2), (2,4), (3,3), (4,2), (4,4). See §3.11. |
| Q15 | First move chaining | **No chaining.** Player ends on a blank cell on their first turn. |
| Q16 | Loop detection | Player stuck in a **loop is eliminated**. Detected by revisiting the same (row, col, entrance). |
| Q17 | 7×7 base size | **1×1** (single corner cell), unlike the 13×13 board's 2×2 bases. |
| Q18 | Diagonal jump tile | Added as 7th tile type. Same paths as diagonal straightaway + diagonal jump mechanic with corner-touch landing. See §3.7. |
| Q19 | Tile graphics | All paths are **straight lines**. Jump tiles have a **circle** symbol in the center. See §3.8. |
| Q20 | Jump pad supply | **Configurable** at setup: single type (full pool), both combined (shared pool), both separate (2 each), or neither. See §3.9 and §7.5. |

### All Questions Resolved

No remaining open questions. The design is fully specified and ready for implementation.

---

## 10. Implementation Phases

| Phase | Description | Scope |
|-------|-------------|-------|
| **Phase 1** | Core data model, tile definitions, board setup | Data layer |
| **Phase 2** | Board rendering with SVG, tile rendering | Visual layer |
| **Phase 3** | Tile selection UI, placement logic, rotation | Interaction |
| **Phase 4** | Movement engine with chaining and jump logic | Game logic |
| **Phase 5** | Win/loss/draw detection, multi-player turns | Game flow |
| **Phase 6** | Setup screen, animations, polish | UX |
| **Phase 7** | Testing, edge cases, balance | QA |

---

## 11. File Deliverable

```
index.html    — Single self-contained HTML file with all CSS + JS
```

Hosted via GitHub Pages at the repository root.
