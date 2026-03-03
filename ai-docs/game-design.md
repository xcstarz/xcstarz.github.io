# Tile Path Game — Design Document

## Overview

A single-HTML, pass-and-play, turn-based board game for 2–4 players. Players place path tiles on a shared grid to force opponents off the board, into dead zones, or onto an opponent's start position. Think of it as a competitive Tsuro-like game with jump mechanics.

---

## 1. Board

| Setting | Small Board | Large Board |
|---------|-------------|-------------|
| Grid    | 7 × 7       | 13 × 13     |
| Jump tiles per player | 3 | 4 |

- Each cell on the grid is either **blank**, **occupied by a tile**, a **start position**, or a **dead zone (X)**.
- The four corner cells are **start positions** (one per player).
- Certain cells are pre-marked as **dead zones (X)**. The exact layout of dead zones (other than unused corners) is an open question — see §9.

### 1.1 Coordinate System

- Origin `(0, 0)` at top-left corner.
- `row` increases downward, `col` increases rightward.
- Corner positions:
  - Top-left `(0, 0)` — Player 1 (e.g., Red)
  - Top-right `(0, W-1)` — Player 2 (e.g., Blue)
  - Bottom-right `(H-1, W-1)` — Player 3 (e.g., Green)
  - Bottom-left `(H-1, 0)` — Player 4 (e.g., Yellow)

### 1.2 Two-Player Setup

When only 2 players are present, they start on **diagonal** corners:
- Player 1 → `(0, 0)`, Player 2 → `(H-1, W-1)` (or the other diagonal)
- The two unused corner cells become **dead zones (X)**.

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

## 3. Six Tile Types

Each tile type defines **4 internal paths** connecting pairs of entrances. Below are the assumed path connections (at 0° rotation). **These need verification against the reference images — see §9 Open Questions.**

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

A special tile that contains a **jump pad**. The jump pad causes the player to skip over the next cell in the direction of travel and land on the cell beyond it (see §5.3).

**Jump tile path layout is an open question** — see §9. Assumed to function like a straightaway but with the jump mechanic on each of its 4 paths.

### 3.7 Tile Supply

| Tile Type             | Per-player limit (7×7) | Per-player limit (13×13) | Unlimited? |
|-----------------------|------------------------|--------------------------|------------|
| Straightaway          | —                      | —                        | Yes        |
| U-Turn                | —                      | —                        | Yes        |
| Centrifugal           | —                      | —                        | Yes        |
| Diagonal Straightaway | —                      | —                        | Yes        |
| Weave                 | —                      | —                        | Yes        |
| Jump                  | 3                      | 4                        | No         |

---

## 4. Player State

Each player tracks:

| Field               | Description                                         |
|---------------------|-----------------------------------------------------|
| `id`                | 1–4                                                 |
| `color`             | Red / Blue / Green / Yellow                         |
| `position`          | `{row, col, entranceIndex}` — current cell + which entrance the player is sitting on |
| `alive`             | Boolean — eliminated players are out                |
| `jumpTilesRemaining`| Count of jump tiles left to place                   |
| `startCorner`       | Which corner is this player's start                 |

---

## 5. Turn Flow

### 5.1 Placement Phase

1. The current player selects a **tile type** and a **rotation** (0°/90°/180°/270°).
2. The player places the tile on **any blank cell adjacent to a cell that already has a tile or a start position** (standard adjacency — N/S/E/W, not diagonal). This means tiles expand outward from the starting area.
3. Placing a tile is **mandatory** — you must place exactly one tile per turn.

> **Open question:** Can a tile be placed on *any* blank cell, or only on the cell directly "in front of" the current player's entrance? The rules say "in front of them" which suggests it must be the cell the player's current entrance faces. See §9.

### 5.2 Movement Phase (Automatic)

After a tile is placed, **all players whose current entrance now connects to a newly available path** must move immediately:

1. The player follows the path inside the tile they are on, exiting through the connected entrance.
2. The exit entrance leads into the **adjacent cell** on the neighboring tile's corresponding entrance.
3. If that adjacent cell **has a tile**, the player immediately follows that tile's path (chaining continues in the same turn).
4. If the adjacent cell is **blank**, the player stops there, sitting on the entrance of the blank cell.
5. If the adjacent cell is **off the board** or a **dead zone (X)** — the player is **eliminated**.
6. If the player reaches **an opponent's start position** — the moving player **wins**.
7. If two players end up on the **exact same position** (same cell + same entrance) — it is a **draw** between those two players.

### 5.3 Jump Pad Movement

When a player is on a tile that is a **jump tile**:
- Instead of moving to the adjacent cell, the player **skips over** the immediately adjacent cell and lands on the cell **two steps away** in the direction of travel.
- If the landing cell is blank, the player sits on the entrance of that blank cell matching the orientation of the entrance used to enter the jump tile.
- If the landing cell has a tile, the player enters via the corresponding entrance and continues following paths (chaining).
- If the skipped cell or landing cell is off the board / dead zone — the player is **eliminated**.

### 5.4 Turn Order

Players take turns in order: P1 → P2 → P3 → P4 → P1 → ... (skipping eliminated players).

---

## 6. Win / Lose / Draw Conditions

| Condition | Result |
|-----------|--------|
| Player moves off the board | That player is **eliminated** |
| Player moves onto a dead zone (X) | That player is **eliminated** |
| Player reaches an opponent's start corner | Moving player **wins the game** |
| Two players land on same position | **Draw** between those two |
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
- Paths inside tiles rendered as colored curves/lines.
- Player tokens rendered as colored circles on their current entrance position.
- Dead zones marked with a red X pattern.
- Start positions marked with player color halos.
- Blank cells shown as light grid squares.
- Hovering over a valid placement cell shows a **ghost preview** of the selected tile.

### 7.3 Tile Selector

- 6 tile buttons in the sidebar, each showing a small preview of the tile type.
- Jump tile button shows remaining count; grayed out when exhausted.
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
// Cell on the board
Cell = {
  row: number,
  col: number,
  type: 'blank' | 'tile' | 'start' | 'deadzone',
  tile: null | { tileType: string, rotation: 0|1|2|3 },
  owner: null | playerId   // for start cells
}

// Player
Player = {
  id: number,
  name: string,
  color: string,
  row: number,
  col: number,
  entrance: number,         // 0–7, which entrance they sit on
  alive: boolean,
  jumpTilesLeft: number,
  startRow: number,
  startCol: number
}

// Tile definition (base rotation)
TileDef = {
  name: string,
  paths: [[from, to], ...]  // 4 pairs of entrance indices
}
```

### 8.4 Movement Algorithm (Pseudocode)

```
function resolveMovement(player):
    while true:
        tile = board[player.row][player.col].tile
        if tile is null:
            break  // on blank cell, stop

        exitEntrance = tile.getConnectedExit(player.entrance)
        (nextRow, nextCol, nextEntrance) = getAdjacentCell(player.row, player.col, exitEntrance)

        if tile.type == 'jump':
            // skip one cell, land on the cell beyond
            (skipRow, skipCol) = getAdjacentCell(player.row, player.col, exitEntrance)
            (nextRow, nextCol, nextEntrance) = getAdjacentCell(skipRow, skipCol, exitEntrance)
            // preserve entrance orientation from the jump entrance

        if outOfBounds(nextRow, nextCol) or isDeadZone(nextRow, nextCol):
            eliminate(player)
            return

        if isOpponentStart(nextRow, nextCol, player):
            declareWinner(player)
            return

        player.row = nextRow
        player.col = nextCol
        player.entrance = nextEntrance

        // check for collision with other players
        checkDrawCondition(player)
```

---

## 9. Open Questions

These need clarification before implementation can proceed:

### Q1: Exact Tile Path Connections (CRITICAL)

The PDF contained 6 reference images (one per tile type) showing the actual path layouts, but the images could not be extracted. The path connections in §3 are **best guesses** based on the tile names. Please verify or provide the exact entrance-pair connections for:
- Straightaway
- Centrifugal
- U-Turn
- Diagonal Straightaway
- Weave
- Jump Tile

### Q2: Tile Placement Rules — "In Front Of"

> Rule 4: "the player must choose a tile to place **in front of them**."

Does "in front of" mean:
- **(A)** The single blank cell that the player's current entrance directly faces? (Most restrictive — player must plan carefully)
- **(B)** Any blank cell adjacent (N/S/E/W) to the player's current cell?
- **(C)** Any blank cell on the board?

**Current assumption:** Option (A) — the cell the entrance faces.

### Q3: Dead Zone Layout

Are there dead zones on the board **besides** the unused corners in 2-player mode? If yes:
- Are they fixed or randomly generated?
- Are they symmetric?
- How many on 7×7 vs. 13×13?

**Current assumption:** Only unused corners in 2-player mode are dead zones. No other dead zones on the board.

### Q4: Initial Placement / First Move

> Rule 9: "When you start you place a tile next to your start position, and choose one of the entrances that touches your start tile."

- Does "next to" mean any of the (up to 2) non-corner-edge-adjacent blank cells next to the corner?
- After placing, the player's token moves from the start position to the chosen entrance on the newly placed tile. Does the token then immediately follow the tile's path?

**Current assumption:** Player places a tile on a cell adjacent to their start corner, picks an entrance on that tile that faces the start corner, and the token moves to that entrance. No further movement occurs on the first turn (since the tile is the first placed, there is nothing beyond it to chain into).

### Q5: "All Players Touching Entrances Move Immediately"

> Rule 6: "all players touching the entrances are forced to move immediately"

When a tile is placed, does this mean:
- **(A)** Only the current player moves?
- **(B)** ALL players whose current position now has a newly completed path (because the placed tile connects to their cell) must also move immediately?

**Current assumption:** Option (B) — all affected players move. This is a key strategic element.

### Q6: Movement Order When Multiple Players Move

If multiple players are forced to move simultaneously after a tile placement:
- Do they move in player order (P1 first, then P2, etc.)?
- Or truly simultaneously (resolve all movements, then check collisions)?

**Current assumption:** Simultaneous resolution — all compute their final positions, then check for collisions/draws.

### Q7: Jump Tile — Direction of Jump

Does the jump pad skip in the **direction the player is already traveling** (based on which entrance they used), or does the jump tile have a fixed jump direction regardless of entry?

**Current assumption:** The jump is in the direction of the exit entrance (i.e., the path through the tile determines exit direction, then the player skips one cell in that direction).

### Q8: Jump Tile — What if the Skipped Cell Has a Tile?

If the cell being skipped over (during a jump) already has a tile on it, does:
- **(A)** The player simply flies over it, ignoring its paths?
- **(B)** The jump is blocked?

**Current assumption:** Option (A) — the player flies over, ignoring the skipped cell's tile.

### Q9: Reaching Your Own Start Position

What happens if a player's movement chains them back to **their own** start position?
- Is it a safe stop?
- Does the player pass through?

**Current assumption:** The player stops there; it is safe.

### Q10: Three-Player Setup

With 3 players, which 3 corners are used and which 1 becomes a dead zone?
- Is it player's choice?
- Fixed arrangement?

**Current assumption:** Players choose, or the 4th corner (bottom-left) is the default dead zone.

### Q11: Can You Place a Tile on a Start Position?

Start positions occupy corner cells. Can tiles be placed directly on a start cell?

**Current assumption:** No — start positions are permanent and cannot be overwritten.

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
