---
status: implemented
tags: [demo, gameplay]
---

# Testgame1 Demo Improvements

## Current State

Testgame1 is a space-themed demo showing a player ship (`playerShip1_green`)
flying with physics, two rotating meteors, a health bar, background music, and
a laser sound effect on click. The player takes damage on meteor collision with
a cooldown tween. There is no scoring, no shooting, no enemy AI, no
progression, and the G2 game mode is empty.

The demo currently exercises: sprites, physics (forces, torque, collisions),
audio (streaming music + one-shot effects), input (keyboard + mouse), tweening,
entity system, and hot-reload.

## Goals

Turn testgame1 into a small but complete arcade game that doubles as a
showcase for engine features. Each feature below should be implementable
independently so they can land incrementally.

## Feature List

### 1. Shooting

The player can fire projectiles in the direction the ship faces. Click or hold
Space to shoot. Projectiles are entities with physics bodies that travel
forward and despawn after a timer or on collision.

- Sprite: `laserGreen01` (or `laserGreen11` for a bigger bolt)
- Sound: `laser.wav` (already loaded)
- Cooldown: ~0.2s between shots, shown visually on the ship (flash or alpha)
- Projectiles should be pooled or capped to avoid unbounded allocation

Engine features exercised: entity lifecycle, timer cooldowns, physics body
creation/destruction.

### 2. Meteor Destruction and Splitting

Meteors break apart when hit by projectiles. Large meteors split into 2-3
medium ones, medium into 2 small ones, small ones just explode.

- Sprites: `meteorBrown_big1-4`, `meteorBrown_med1`, `meteorBrown_small1-2`
- Destruction triggers a brief explosion animation and screen shake
- Score awarded per meteor destroyed (100 / 50 / 25 by size)

Engine features exercised: entity removal, spawning, camera shake (if the
engine supports it, otherwise a simple offset tween on the draw origin).

### 3. Score and HUD

- Score counter top-right, drawn with `numeral0`-`numeral9` sprites
- Health bar stays top-left (already exists)
- Wave indicator: "Wave 3" text or numeral display
- Lives display using `playerLife1_green` sprites

Engine features exercised: sprite-based text rendering, HUD layout.

### 4. Wave Spawning

Meteors arrive in waves. Each wave spawns more and faster meteors from random
screen edges. Short pause between waves with a "Wave N" announcement.

- Wave 1: 3 large meteors, slow drift
- Wave 2: 5 large + 2 medium, moderate speed
- Wave N: scale count and speed, mix in grey meteors
  (`meteorGrey_big1-4`) for visual variety
- Spawns offset outside the visible area, given random inward velocity

Engine features exercised: timers, random number generation (already has
`Random`), entity management at scale.

### 5. Scrolling Starfield Background

Parallax star layers scrolling behind the action. Two or three layers at
different speeds using `star1`, `star2`, `star3` sprites (or just small dots).

- Layer 0 (far): tiny dots, slow scroll
- Layer 1 (mid): `star1` sprites, medium scroll
- Layer 2 (near): `star2` sprites, fast scroll
- Wraps seamlessly using modulo positioning

Engine features exercised: draw ordering / layers, parallax math.

### 6. Powerups

Destroyed meteors occasionally drop a powerup that drifts slowly and can be
collected by the player.

| Powerup | Sprite | Effect |
|---------|--------|--------|
| Shield | `powerupBlue_shield` | Absorb next hit |
| Rapid fire | `powerupGreen_bolt` | 2x fire rate for 5s |
| Heal | `powerupRed_star` | Restore 25 health |
| Score bonus | `powerupYellow_star` | 500 bonus points |

- Powerups despawn after 8 seconds if not collected
- Active powerup shown as icon next to health bar
- Collect sound: a short blip (reuse `pong-blip1.ogg` or add a new one)

Engine features exercised: collision filtering, timed entity lifecycle, tween
(blinking before despawn).

### 7. Player Death and Game Over

When health reaches 0:
- Ship plays an explosion sequence (sprites `fire00`-`fire19` or
  `explosion1`-`explosion5`)
- Lose a life; respawn at center with brief invincibility (flashing ship)
- If no lives remain, show "Game Over" screen with final score
- Press Enter to restart

Engine features exercised: animation playback, game state transitions, input
in non-gameplay state.

### 8. Polish

- **Screen wrap**: entities that leave one edge reappear on the opposite side
  (classic Asteroids behavior)
- **Particle sparks** on bullet/meteor collision (small sprites or circles with
  short lifetime and random velocity)
- **Smooth camera** following the player using `Player:center_camera()` (already
  partially implemented but unused)
- **Music switch**: play `weapons_mode.ogg` during intense waves,
  `music.ogg` during calm phases
- **Damage flash** on meteors when hit (tint red briefly, already proven with
  player cooldown tween)

## Implementation Order

Recommended sequence to keep the game playable at each step:

1. Shooting (the game becomes interactive)
2. Meteor destruction + splitting (the game has a goal)
3. Score + HUD (feedback loop)
4. Wave spawning (progression)
5. Player death + game over (complete loop)
6. Starfield background (visual quality)
7. Powerups (depth)
8. Polish pass (juice)

## Sprite Atlas Reference

The demo should use sprites from **sheet.sprites.json** (loaded as `sheet.qoi`)
which contains all the space shooter assets. Key sprite groups:

- Ships: `playerShip1_green`, `playerShip2_green`, `playerShip3_green`
- Enemies: `enemyBlack1-5`, `enemyBlue1-5`, `enemyGreen1-5`, `enemyRed1-5`
- Meteors: `meteorBrown_big1-4`, `meteorBrown_med1`, `meteorBrown_small1-2`,
  `meteorGrey_*` variants
- Lasers: `laserGreen01-16`, `laserRed01-16`, `laserBlue01-16`
- Effects: `fire00-19`
- Powerups: `powerupBlue_shield`, `powerupGreen_bolt`, `powerupRed_star`,
  `powerupYellow_star`
- HUD: `numeral0-9`, `numeralX`, `playerLife1_green`
- UFOs: `ufoBlue`, `ufoGreen`, `ufoRed`, `ufoYellow`
