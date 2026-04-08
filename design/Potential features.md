---
status: parking-lot
tags: [parking-lot, ideas]
---

# Potential Features

A parking lot for features that have been discussed but aren't clearly worth
building yet. The bar for entries here is "interesting enough to remember,
not obviously the right call today." Each entry should record:

- **The idea** — one or two sentences.
- **Why it might matter** — the case for doing it.
- **Why we're not doing it now** — what would have to change.
- **What would unblock it** — the concrete signal to revisit.

When something here gets a real motivating use case, promote it to a full
design doc and remove it from this list. When something stops being
interesting, delete it.

---

## SOA buffers and batched draw API

**The idea.** A `G.soa` namespace for declaring typed columnar buffers
(`G.soa.new({{"x","f32"},{"y","f32"},...})`), and bulk-draw functions that
consume them: `G.graphics.draw_circles(soa)`, `draw_rects(soa)`,
`draw_sprites(soa)`. The savings come from amortizing the per-call Lua→C
overhead and letting the C-side inner loop run without touching `lua_State`.

**Why it might matter.** When something is drawn in bulk — particles,
projectiles, debug viz, instanced sprites — N individual `draw_circle` calls
do N rounds of `lua_tonumber`/arg validation/state setup, even with the
function cached as a local. A `draw_circles(soa)` call collapses that to one
round of overhead plus a tight C loop. Likely 5–10× on the right workload.
SOA also future-proofs for SIMD passes and instanced rendering, and would
be the natural storage for the eventual particle system.

**Why we're not doing it now.** No game has hit a real performance wall
where per-call lookup or arg-parsing overhead shows up in a profile. The
canonical Lua optimization (`local draw = G.graphics.draw_circle`) hasn't
been exhausted yet. Building SOA storage, type validation, capacity
policies, and color-packing helpers is a fair amount of surface area to
add speculatively, and the right shape is much easier to design after
feeling the pain than before.

**What would unblock it.** A real game (or stress test) showing
`luaV_gettable` / `luaH_getstr` / per-call C overhead in `game-samply`
output as a meaningful fraction of a frame. Or starting work on the
particle system, since particles are the most natural first consumer and
would justify the storage type on their own.

**Related notes.** If/when this lands, the first targets in payoff order
are: `draw_circles`, `draw_rects`/`draw_lines`, `draw_sprites` (instanced),
particle system, `physics.raycast_batch`. AOS vs SOA storage is a real
choice — AOS is fine for `draw_circles` specifically (one cache line per
row), but SOA wins for selective consumers and SIMD. Lua 5.1's lack of
bitwise ops means a `G.color.pack(r,g,b,a) -> u32` helper is a prerequisite.
