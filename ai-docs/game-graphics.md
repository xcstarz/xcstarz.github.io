# Tile Path Game - Graphics and UI

## Overview

This document defines the visual presentation, rendering behavior, and UI layout for the Tile Path Game.

---

## 1. Tile Graphics

All tile paths are rendered using straight line segments (no curved paths).

| Tile                  | Graphic Description |
|-----------------------|---------------------|
| Straightaway          | 4 straight parallel lines (2 vertical + 2 horizontal) |
| U-Turn                | Semicircles connecting adjacent edges|
| Centrifugal           | Straight lines connecting adjacent edges (90-degree turns) |
| Diagonal Straightaway | 4 straight diagonal lines crossing the tile |
| Weave                 | Straight crossing lines through center (X-like weave) |
| Jump Tile             | Straightaway lines plus center circle (jump-pad symbol) |
| Diagonal Jump Tile    | Diagonal straightaway lines plus center circle |

The center circle is the visual marker that distinguishes jump-capable tiles.

---

## 2. Board Rendering

- Use SVG for crisp scaling at any zoom level.
- Render each board cell as a square (for example, 60x60 px).
- Render tile paths as straight SVG segments.
- Render player tokens as colored circles positioned on entrance anchors.
- Render dead zones with a red X marker.
- Render start bases with color-coded halos.
- Render blank cells as light grid squares.
- Render only placed tiles; valid placement guidance appears as highlighted cells.

### 2.1 Coordinate Anchors

To keep visuals aligned with rules:
- Entrances `N0/N1`, `E0/E1`, `S0/S1`, `W0/W1` map to fixed anchor points on each cell edge.
- Token placement should snap precisely to entrance anchor points.
- Rotation should transform tile path graphics around tile center.

---

## 3. UI Layout

```
+---------------------------------------------+
| HEADER: title, current player, game status  |
+--------------------+------------------------+
|                    | SIDEBAR:               |
|   GAME BOARD       | - Tile selector        |
|   (SVG / Canvas)   | - Rotation controls    |
|                    | - Jump tiles remaining |
|                    | - Game log             |
|                    | - Place tile button    |
+--------------------+------------------------+
| FOOTER: rules/help toggle                   |
+---------------------------------------------+
```

---

## 4. Tile Selector UI

- Show all 7 tile types as always-visible preview buttons.
- Keep tile graphics visible in menu at all times.
- Show remaining counts on jump tile buttons.
- Gray out jump tile options when exhausted.
- Provide rotation controls as:
  - a slider, or
  - four explicit buttons (0, 90, 180, 270 degrees).

---

## 5. Interaction and Animation

1. Player selects tile type.
2. Player selects rotation.
3. Player clicks highlighted valid cell.
4. Movement animation plays automatically:
   - active paths light up;
   - tokens slide along routes;
   - jump moves animate as clear leaps.
5. UI updates elimination/win/draw state and advances turn when appropriate.

Animation goals:
- Keep motion smooth and readable.
- Prioritize clarity over speed.
- Make jump behavior visually distinct from normal movement.

---

## 6. Setup Screen UI

- Board size selector: 7x7 or 13x13.
- Player count selector: 2, 3, or 4.
- Jump pad mode selector:
  - Cardinal only
  - Diagonal only
  - Both (combined pool or separate pools)
  - Neither
- Optional player name inputs.
- Primary action button: **Start Game**.

---

## 7. Visual Style

- Clean, modern flat style.
- Dark board background with light grid lines.
- Colorblind-friendly palette for players:
  - Red
  - Blue
  - Green
  - Yellow
- Include pattern/shape fallbacks for color differentiation.
- Use subtle CSS transitions for hover, selection, and token movement.

---

## 8. Rendering Notes for Implementation

- Keep rendering and game state separate (renderer should reflect state, not own it).
- Use reusable SVG templates/components for each tile type.
- Base all position calculations on a shared cell-size constant.
- Keep z-order clear:
  1. Grid/background
  2. Base/dead-zone marks
  3. Tile art
  4. Token layer
  5. Overlay effects/highlights

