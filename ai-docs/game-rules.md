# Tile Path Game - Rules

## Overview

A pass-and-play, turn-based board game for 2-4 players. Players place path tiles on a shared grid to force opponents off the board, into dead zones, or onto an opponent's start position.

---

## 1. Board

| Setting | Small Board | Large Board |
|---------|-------------|-------------|
| Grid    | 7 x 7       | 13 x 13     |
| Jump tiles per player | 3 | 4 |

- Each cell is either blank, occupied by a tile, a start base, or a dead zone (`X`).
- Each corner has a start base (one per player):
  - **13x13:** 2x2 base (4 cells)
  - **7x7:** 1x1 base (1 cell)
- Base cells are permanently occupied and cannot hold tiles.
- Dead zones are fixed by board layout.
- Tiles cannot be placed on start bases or dead zones.

### 1.1 Coordinate System

- Origin `(0, 0)` is top-left.
- `row` increases downward, `col` increases rightward.

### 1.2 Start Bases

**13x13 (2x2 bases):**

| Player | Corner       | Cells                              |
|--------|--------------|-------------------------------------|
| P1     | Top-left     | (0,0), (0,1), (1,0), (1,1)         |
| P2     | Top-right    | (0,11), (0,12), (1,11), (1,12)     |
| P3     | Bottom-right | (11,11), (11,12), (12,11), (12,12) |
| P4     | Bottom-left  | (11,0), (11,1), (12,0), (12,1)     |

Adjacent first-move cells (13x13):

| Player | Adjacent cells |
|--------|----------------|
| P1     | (0,2), (1,2), (2,0), (2,1) |
| P2     | (0,10), (1,10), (2,11), (2,12) |
| P3     | (10,11), (10,12), (11,10), (12,10) |
| P4     | (10,0), (10,1), (11,2), (12,2) |

**7x7 (1x1 bases):**

| Player | Corner       | Cell   |
|--------|--------------|--------|
| P1     | Top-left     | (0,0)  |
| P2     | Top-right    | (0,6)  |
| P3     | Bottom-right | (6,6)  |
| P4     | Bottom-left  | (6,0)  |

Adjacent first-move cells (7x7):

| Player | Adjacent cells |
|--------|----------------|
| P1     | (0,1), (1,0) |
| P2     | (0,5), (1,6) |
| P3     | (5,6), (6,5) |
| P4     | (5,0), (6,1) |

### 1.3 Player Count Setups

- **4 players:** all corner bases active.
- **3 players:** players choose 3 corners; unused corner base becomes a dead zone.
- **2 players:** players use diagonal corners; two unused corners become dead zones.

### 1.4 Own-Start-Is-Deadly Rule

A player's own start base is a dead zone for that player. Re-entering your own base eliminates you. Reaching an opponent's base wins the game.

---

## 2. Tile Model

Each tile has 8 entrances/exits (2 per edge):

```
    N0   N1
   +--+--+
W1 |      | E0
   |      |
W0 |      | E1
   +--+--+
    S1   S0
```

Entrance indices:

| Index | Label | Edge   | Position |
|-------|-------|--------|----------|
| 0     | N0    | North  | Left     |
| 1     | N1    | North  | Right    |
| 2     | E0    | East   | Top      |
| 3     | E1    | East   | Bottom   |
| 4     | S0    | South  | Right    |
| 5     | S1    | South  | Left     |
| 6     | W0    | West   | Bottom   |
| 7     | W1    | West   | Top      |

Adjacent tiles connect through mirrored entrances:

| Neighbor Direction | This tile exits | Neighbor entrances |
|--------------------|-----------------|--------------------|
| North              | N0, N1          | S1, S0             |
| East               | E0, E1          | W1, W0             |
| South              | S0, S1          | N1, N0             |
| West               | W0, W1          | E1, E0             |

### 2.1 Rotation

Tiles support 4 rotations (0, 90, 180, 270 degrees clockwise). A 90 degree CW rotation maps entrance index `i -> (i + 2) % 8`.

---

## 3. Tile Types

Seven tile types define four internal path pairs each (at 0 degree rotation).

### 3.1 Straightaway

`N0<->S1, N1<->S0, E0<->W1, E1<->W0`

### 3.2 U-Turn

`N0<->N1, E0<->E1, S0<->S1, W0<->W1`

### 3.3 Centrifugal

`N0<->W1, N1<->E0, E1<->S0, S1<->W0`

### 3.4 Diagonal Straightaway

`N0<->E1, N1<->W0, S0<->W1, S1<->E0`

### 3.5 Weave

`N0<->S0, N1<->S1, E0<->W0, E1<->W1`

### 3.6 Jump Tile (Cardinal)

Same path map as straightaway. On exit, jump two cells in a cardinal direction (skip one cell).

### 3.7 Diagonal Jump Tile

Same path map as diagonal straightaway. On exit, jump to the corner-touching diagonal cell.

Diagonal jump mapping (0 degree):

| Entry | Exit | Direction | Land Cell |
|------|------|-----------|-----------|
| N0 | E1 | SE | (row+1, col+1) |
| E1 | N0 | NW | (row-1, col-1) |
| N1 | W0 | SW | (row+1, col-1) |
| W0 | N1 | NE | (row-1, col+1) |
| S1 | E0 | NE | (row-1, col+1) |
| E0 | S1 | SW | (row+1, col-1) |
| S0 | W1 | NW | (row-1, col-1) |
| W1 | S0 | SE | (row+1, col+1) |

Both jump types preserve entrance index on landing (enter via X -> land on X).

### 3.8 Tile Supply

| Tile Type             | Per-player limit (7x7) | Per-player limit (13x13) | Unlimited? |
|-----------------------|------------------------|--------------------------|------------|
| Straightaway          | -                      | -                        | Yes        |
| U-Turn                | -                      | -                        | Yes        |
| Centrifugal           | -                      | -                        | Yes        |
| Diagonal Straightaway | -                      | -                        | Yes        |
| Weave                 | -                      | -                        | Yes        |
| Jump                  | 3                      | 4                        | No         |
| Diagonal Jump         | 3                      | 4                        | No         |

Jump configuration options:
- Single type only: 3 (7x7) or 4 (13x13) per player.
- Both types, combined pool: 3 (7x7) or 4 (13x13) total per player.
- Both types, separate pools: 2 each type per player.
- Neither: no jump tiles.

### 3.9 Dead Zone Layout - 13x13

Fixed interior dead zones:
`(6,6), (3,6), (6,9), (9,6), (6,3), (4,4), (4,8), (8,8), (8,4), (5,5), (5,7), (7,7), (7,5)`

### 3.10 Dead Zone Layout - 7x7

Fixed interior dead zones:
`(3,3), (2,2), (2,4), (4,4), (4,2)`

---

## 4. Player State

| Field | Description |
|-------|-------------|
| `id` | 1-4 |
| `color` | Red / Blue / Green / Yellow |
| `position` | `{row, col, entranceIndex}` |
| `alive` | elimination status |
| `hasPlacedFirstTile` | whether first tile was placed |
| `jumpTilesRemaining` | jump tiles left |
| `base` | start base cells |

---

## 5. Turn Flow

### 5.1 Placement

- Place exactly one tile per turn.
- First turn: place on any blank cell adjacent to your base; choose an entrance facing your base.
- Subsequent turns: place on the single blank cell directly in front of your current entrance.
- Cannot place on start bases or dead zones.

### 5.2 Automatic Movement

After placement, all affected players move in player order:
1. Follow current tile path to an exit entrance.
2. Move into adjacent cell's mirrored entrance.
3. If next cell has a tile, continue chaining.
4. If blank, stop on that entrance.
5. If off-board, dead zone, or own base, eliminate at boundary.
6. If opponent base reached, moving player wins.
7. If two players end at the same cell + entrance, draw between those players.
8. Repeating `(row, col, entrance)` in one chain is a loop -> elimination.

### 5.3 Jump Movement

- Jump tile: cardinal jump two cells away, skipping one cell.
- Diagonal jump tile: jump one diagonal step to corner-touching cell.
- In both: preserve entrance index on landing.
- Landing on tile continues chaining; landing on blank stops.
- Landing off-board / dead zone / own base eliminates.
- Cardinal jumps ignore the skipped cell's contents.

### 5.4 Turn Order

P1 -> P2 -> P3 -> P4 -> repeat, skipping eliminated players.

---

## 6. Win / Lose / Draw

| Condition | Result |
|-----------|--------|
| Path goes off board | Eliminated at boundary |
| Path goes into dead zone | Eliminated at boundary |
| Path goes into own base | Eliminated at boundary |
| Loop detected | Eliminated |
| Reach opponent base | Win |
| Two players same final position | Draw between those two |
| One player remains alive | Win |

When a player is eliminated, a full-screen overlay displays "Player X was eliminated!" which the player dismisses by clicking OK before play continues.

