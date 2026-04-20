#include "collision_world.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "logging.h"
#include "zone_stats.h"

namespace G {

void SpatialHash::Init(float cell_size, size_t table_size,
                       Allocator* allocator) {
  cell_size_ = cell_size;
  inv_cell_size_ = 1.0f / cell_size;
  table_size_ = table_size;
  allocator_ = allocator;

  bucket_heads_ = allocator_->NewArray<uint32_t>(table_size_);
  entries_ = allocator_->NewArray<Entry>(kMaxEntries);
  Clear();
}

void SpatialHash::Destroy() {
  if (allocator_ == nullptr) return;
  allocator_->DeallocArray(bucket_heads_, table_size_);
  allocator_->DeallocArray(entries_, kMaxEntries);
  bucket_heads_ = nullptr;
  entries_ = nullptr;
}

void SpatialHash::Clear() {
  for (size_t i = 0; i < table_size_; ++i) {
    bucket_heads_[i] = kNone;
  }
  entry_count_ = 0;
}

size_t SpatialHash::Hash(int cx, int cy) const {
  // Large primes for spatial hash mixing, from Teschner et al.
  // "Optimized Spatial Hashing for Collision Detection of Deformable Objects".
  uint32_t h = static_cast<uint32_t>(cx) * 92837111u ^
               static_cast<uint32_t>(cy) * 689287499u;
  return h % table_size_;
}

void SpatialHash::Insert(uint32_t id, CollisionAABB bounds) {
  int min_cx = static_cast<int>(std::floor(bounds.min.x * inv_cell_size_));
  int min_cy = static_cast<int>(std::floor(bounds.min.y * inv_cell_size_));
  int max_cx = static_cast<int>(std::floor(bounds.max.x * inv_cell_size_));
  int max_cy = static_cast<int>(std::floor(bounds.max.y * inv_cell_size_));

  for (int cy = min_cy; cy <= max_cy; ++cy) {
    for (int cx = min_cx; cx <= max_cx; ++cx) {
      if (entry_count_ >= kMaxEntries) return;
      size_t bucket = Hash(cx, cy);
      uint32_t ei = entry_count_++;
      entries_[ei].id = id;
      entries_[ei].next = bucket_heads_[bucket];
      bucket_heads_[bucket] = ei;
    }
  }
}

size_t SpatialHash::Query(CollisionAABB bounds, uint32_t* out,
                          size_t out_capacity) const {
  int min_cx = static_cast<int>(std::floor(bounds.min.x * inv_cell_size_));
  int min_cy = static_cast<int>(std::floor(bounds.min.y * inv_cell_size_));
  int max_cx = static_cast<int>(std::floor(bounds.max.x * inv_cell_size_));
  int max_cy = static_cast<int>(std::floor(bounds.max.y * inv_cell_size_));

  size_t count = 0;
  for (int cy = min_cy; cy <= max_cy; ++cy) {
    for (int cx = min_cx; cx <= max_cx; ++cx) {
      size_t bucket = Hash(cx, cy);
      uint32_t ei = bucket_heads_[bucket];
      while (ei != kNone) {
        if (count < out_capacity) {
          out[count++] = entries_[ei].id;
        }
        ei = entries_[ei].next;
      }
    }
  }
  return count;
}

size_t SpatialHash::QueryRay(FVec2 origin, FVec2 direction, float max_dist,
                             uint32_t* out, size_t out_capacity) const {
  // DDA-style grid traversal.
  float len = std::sqrt(direction.Dot(direction));
  if (len < 1e-8f) return 0;
  FVec2 dir = direction * (1.0f / len);

  int cx = static_cast<int>(std::floor(origin.x * inv_cell_size_));
  int cy = static_cast<int>(std::floor(origin.y * inv_cell_size_));

  int step_x = dir.x >= 0 ? 1 : -1;
  int step_y = dir.y >= 0 ? 1 : -1;

  float t_max_x, t_max_y;
  float t_delta_x, t_delta_y;

  if (std::abs(dir.x) < 1e-8f) {
    t_max_x = 1e30f;
    t_delta_x = 1e30f;
  } else {
    float next_x = (dir.x >= 0 ? (cx + 1) : cx) * cell_size_;
    t_max_x = (next_x - origin.x) / dir.x;
    t_delta_x = cell_size_ / std::abs(dir.x);
  }

  if (std::abs(dir.y) < 1e-8f) {
    t_max_y = 1e30f;
    t_delta_y = 1e30f;
  } else {
    float next_y = (dir.y >= 0 ? (cy + 1) : cy) * cell_size_;
    t_max_y = (next_y - origin.y) / dir.y;
    t_delta_y = cell_size_ / std::abs(dir.y);
  }

  size_t count = 0;
  float t = 0;
  while (t <= max_dist && count < out_capacity) {
    // Collect all entries in current cell.
    size_t bucket = Hash(cx, cy);
    uint32_t ei = bucket_heads_[bucket];
    while (ei != kNone && count < out_capacity) {
      out[count++] = entries_[ei].id;
      ei = entries_[ei].next;
    }

    // Step to next cell.
    if (t_max_x < t_max_y) {
      t = t_max_x;
      t_max_x += t_delta_x;
      cx += step_x;
    } else {
      t = t_max_y;
      t_max_y += t_delta_y;
      cy += step_y;
    }
  }
  return count;
}

CollisionWorld::CollisionWorld(float cell_size, Allocator* allocator)
    : allocator_(allocator) {
  colliders_ = allocator_->NewArray<Collider>(kMaxColliders);
  for (uint32_t i = 0; i < kMaxColliders; ++i) {
    colliders_[i] = Collider{};
  }

  // Build initial free list.
  for (uint32_t i = 0; i < kMaxColliders; ++i) {
    colliders_[i].active = false;
    colliders_[i].generation = 0;
    colliders_[i].next_free = i + 1;
  }
  colliders_[kMaxColliders - 1].next_free = UINT32_MAX;
  first_free_ = 0;

  spatial_hash_.Init(cell_size, /*table_size=*/1024, allocator);

  prev_triggers_.pairs = allocator_->NewArray<TriggerPair>(kMaxTriggerPairs);
  curr_triggers_.pairs = allocator_->NewArray<TriggerPair>(kMaxTriggerPairs);
  new_triggers_.pairs = allocator_->NewArray<TriggerPair>(kMaxTriggerPairs);
  lost_triggers_.pairs = allocator_->NewArray<TriggerPair>(kMaxTriggerPairs);
}

CollisionWorld::~CollisionWorld() {
  if (allocator_ == nullptr) return;
  allocator_->DeallocArray(colliders_, kMaxColliders);
  spatial_hash_.Destroy();
  allocator_->DeallocArray(prev_triggers_.pairs, kMaxTriggerPairs);
  allocator_->DeallocArray(curr_triggers_.pairs, kMaxTriggerPairs);
  allocator_->DeallocArray(new_triggers_.pairs, kMaxTriggerPairs);
  allocator_->DeallocArray(lost_triggers_.pairs, kMaxTriggerPairs);
}

const CollisionWorld::Collider& CollisionWorld::GetCollider(
    ColliderHandle handle) const {
  DCHECK(handle.index < kMaxColliders);
  const Collider& c = colliders_[handle.index];
  DCHECK(c.active);
  DCHECK(c.generation == handle.generation);
  return c;
}

CollisionWorld::Collider& CollisionWorld::GetColliderMut(
    ColliderHandle handle) {
  DCHECK(handle.index < kMaxColliders);
  Collider& c = colliders_[handle.index];
  DCHECK(c.active);
  DCHECK(c.generation == handle.generation);
  return c;
}

ColliderHandle CollisionWorld::Add(CollisionShape shape, FVec2 position,
                                   CollisionFilter filter, bool is_trigger,
                                   uintptr_t userdata) {
  DCHECK(first_free_ != UINT32_MAX, "Collision world is full");
  uint32_t index = first_free_;
  Collider& c = colliders_[index];
  first_free_ = c.next_free;

  c.shape = shape;
  c.position = position;
  c.filter = filter;
  c.is_trigger = is_trigger;
  c.active = true;
  c.userdata = userdata;
  // generation was already set (either 0 for fresh, or incremented on Remove)
  count_++;

  return {index, c.generation};
}

void CollisionWorld::Remove(ColliderHandle handle) {
  DCHECK(IsValid(handle));
  Collider& c = colliders_[handle.index];
  c.active = false;
  c.generation++;  // Invalidate existing handles
  c.next_free = first_free_;
  first_free_ = handle.index;
  count_--;
}

bool CollisionWorld::IsValid(ColliderHandle handle) const {
  if (handle.index >= kMaxColliders) return false;
  const Collider& c = colliders_[handle.index];
  return c.active && c.generation == handle.generation;
}

void CollisionWorld::SetPosition(ColliderHandle handle, FVec2 position) {
  GetColliderMut(handle).position = position;
}

void CollisionWorld::SetShape(ColliderHandle handle, CollisionShape shape) {
  GetColliderMut(handle).shape = shape;
}

void CollisionWorld::SetFilter(ColliderHandle handle, CollisionFilter filter) {
  GetColliderMut(handle).filter = filter;
}

FVec2 CollisionWorld::GetPosition(ColliderHandle handle) const {
  return GetCollider(handle).position;
}

uintptr_t CollisionWorld::GetUserdata(ColliderHandle handle) const {
  return GetCollider(handle).userdata;
}

uint32_t CollisionWorld::Deduplicate(uint32_t* ids, uint32_t count,
                                     uint32_t exclude_index,
                                     uint16_t mask) const {
  // Simple O(n^2) dedup — fine for small candidate sets from broad phase.
  uint32_t out = 0;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t id = ids[i];
    if (id == exclude_index) continue;
    if (id >= kMaxColliders || !colliders_[id].active) continue;
    if (mask != 0xFFFF && (colliders_[id].filter.category & mask) == 0)
      continue;

    // Check for duplicate.
    bool dup = false;
    for (uint32_t j = 0; j < out; ++j) {
      if (ids[j] == id) {
        dup = true;
        break;
      }
    }
    if (!dup) ids[out++] = id;
  }
  return out;
}

CollisionWorld::MoveResult CollisionWorld::MoveAndSlide(ColliderHandle handle,
                                                        FVec2 velocity) {
  ZONE("Collision::MoveAndSlide");
  MoveResult result = {};
  Collider& c = GetColliderMut(handle);
  FVec2 remaining = velocity;

  for (uint32_t iter = 0; iter < kMoveIterations; ++iter) {
    if (remaining.Length2() < 1e-8f) break;

    c.position = c.position + remaining;

    // Query broad phase at new position.
    CollisionAABB bounds = ComputeAABB(c.shape, c.position);
    uint32_t candidates[256];
    size_t num_cand = spatial_hash_.Query(bounds, candidates, 256);
    uint32_t num_unique =
        Deduplicate(candidates, static_cast<uint32_t>(num_cand), handle.index,
                    c.filter.mask);

    // Find deepest collision (ignoring triggers).
    float max_depth = 0;
    CollisionResult deepest = {};
    uint32_t deepest_idx = UINT32_MAX;

    for (uint32_t i = 0; i < num_unique; ++i) {
      uint32_t idx = candidates[i];
      const Collider& other = colliders_[idx];
      if (other.is_trigger) continue;
      if (!ShouldCollide(c.filter, other.filter)) continue;

      CollisionResult cr =
          TestShapes(c.shape, c.position, other.shape, other.position);
      if (cr.hit && cr.depth > max_depth) {
        max_depth = cr.depth;
        deepest = cr;
        deepest_idx = idx;
      }
    }

    if (!deepest.hit) break;

    // Push out along collision normal.
    c.position = c.position + deepest.normal * deepest.depth;

    if (result.contact_count < kMaxContacts) {
      Contact& contact = result.contacts[result.contact_count++];
      contact.other = HandleFor(deepest_idx);
      contact.normal = deepest.normal;
      contact.point = deepest.contact;
      contact.depth = max_depth;
    }

    // Remove velocity component along collision normal.
    float d = remaining.Dot(deepest.normal);
    if (d < 0) {
      remaining = remaining - deepest.normal * d;
    } else {
      break;
    }
  }

  result.position = c.position;
  return result;
}

CollisionWorld::MoveResult CollisionWorld::MoveAndCollide(ColliderHandle handle,
                                                          FVec2 velocity) {
  MoveResult result = {};
  Collider& c = GetColliderMut(handle);

  FVec2 target = c.position + velocity;
  c.position = target;

  CollisionAABB bounds = ComputeAABB(c.shape, c.position);
  uint32_t candidates[256];
  size_t num_cand = spatial_hash_.Query(bounds, candidates, 256);
  uint32_t num_unique = Deduplicate(candidates, static_cast<uint32_t>(num_cand),
                                    handle.index, c.filter.mask);

  float max_depth = 0;
  CollisionResult deepest = {};
  uint32_t deepest_idx = UINT32_MAX;

  for (uint32_t i = 0; i < num_unique; ++i) {
    uint32_t idx = candidates[i];
    const Collider& other = colliders_[idx];
    if (other.is_trigger) continue;
    if (!ShouldCollide(c.filter, other.filter)) continue;

    CollisionResult cr =
        TestShapes(c.shape, c.position, other.shape, other.position);
    if (cr.hit && cr.depth > max_depth) {
      max_depth = cr.depth;
      deepest = cr;
      deepest_idx = idx;
    }
  }

  if (deepest.hit) {
    c.position = c.position + deepest.normal * deepest.depth;
    Contact& contact = result.contacts[result.contact_count++];
    contact.other = HandleFor(deepest_idx);
    contact.normal = deepest.normal;
    contact.point = deepest.contact;
    contact.depth = max_depth;
  }

  result.position = c.position;
  return result;
}

uint32_t CollisionWorld::GetOverlaps(ColliderHandle handle, OverlapResult* out,
                                     uint32_t capacity) {
  const Collider& c = GetCollider(handle);
  CollisionAABB bounds = ComputeAABB(c.shape, c.position);

  uint32_t candidates[256];
  size_t num_cand = spatial_hash_.Query(bounds, candidates, 256);
  uint32_t num_unique = Deduplicate(candidates, static_cast<uint32_t>(num_cand),
                                    handle.index, c.filter.mask);

  uint32_t result_count = 0;
  for (uint32_t i = 0; i < num_unique && result_count < capacity; ++i) {
    uint32_t idx = candidates[i];
    const Collider& other = colliders_[idx];
    if (!ShouldCollide(c.filter, other.filter)) continue;

    CollisionResult cr =
        TestShapes(c.shape, c.position, other.shape, other.position);
    if (cr.hit) {
      out[result_count].handle = HandleFor(idx);
      out[result_count].normal = cr.normal;
      out[result_count].depth = cr.depth;
      result_count++;
    }
  }
  return result_count;
}

bool CollisionWorld::Raycast(FVec2 origin, FVec2 direction, float max_dist,
                             uint16_t mask, RaycastHit* out) {
  uint32_t candidates[256];
  size_t num_cand =
      spatial_hash_.QueryRay(origin, direction, max_dist, candidates, 256);
  uint32_t num_unique = Deduplicate(candidates, static_cast<uint32_t>(num_cand),
                                    UINT32_MAX, mask);

  float closest_t = max_dist + 1.0f;
  bool found = false;

  for (uint32_t i = 0; i < num_unique; ++i) {
    uint32_t idx = candidates[i];
    const Collider& c = colliders_[idx];

    RaycastResult r =
        RaycastShape(origin, direction, max_dist, c.shape, c.position);
    if (r.hit && r.t < closest_t) {
      closest_t = r.t;
      out->handle = HandleFor(idx);
      out->point = origin + direction * r.t;
      out->normal = r.normal;
      out->t = r.t;
      found = true;
    }
  }
  return found;
}

uint32_t CollisionWorld::RaycastAll(FVec2 origin, FVec2 direction,
                                    float max_dist, uint16_t mask,
                                    RaycastHit* out, uint32_t capacity) {
  ZONE("Collision::RaycastAll");
  uint32_t candidates[256];
  size_t num_cand =
      spatial_hash_.QueryRay(origin, direction, max_dist, candidates, 256);
  uint32_t num_unique = Deduplicate(candidates, static_cast<uint32_t>(num_cand),
                                    UINT32_MAX, mask);

  uint32_t count = 0;
  for (uint32_t i = 0; i < num_unique && count < capacity; ++i) {
    uint32_t idx = candidates[i];
    const Collider& c = colliders_[idx];

    RaycastResult r =
        RaycastShape(origin, direction, max_dist, c.shape, c.position);
    if (r.hit) {
      out[count].handle = HandleFor(idx);
      out[count].point = origin + direction * r.t;
      out[count].normal = r.normal;
      out[count].t = r.t;
      count++;
    }
  }

  // Sort by t.
  std::sort(out, out + count,
            [](const RaycastHit& a, const RaycastHit& b) { return a.t < b.t; });
  return count;
}

uint32_t CollisionWorld::QueryPoint(FVec2 point, uint16_t mask,
                                    ColliderHandle* out, uint32_t capacity) {
  CollisionAABB bounds = {point, point};
  uint32_t candidates[256];
  size_t num_cand = spatial_hash_.Query(bounds, candidates, 256);
  uint32_t num_unique = Deduplicate(candidates, static_cast<uint32_t>(num_cand),
                                    UINT32_MAX, mask);

  uint32_t count = 0;
  for (uint32_t i = 0; i < num_unique && count < capacity; ++i) {
    uint32_t idx = candidates[i];
    const Collider& c = colliders_[idx];
    if (PointInShape(point, c.shape, c.position)) {
      out[count++] = HandleFor(idx);
    }
  }
  return count;
}

uint32_t CollisionWorld::QueryRect(FVec2 min, FVec2 max, uint16_t mask,
                                   ColliderHandle* out, uint32_t capacity) {
  CollisionAABB query_bounds = {min, max};
  uint32_t candidates[256];
  size_t num_cand = spatial_hash_.Query(query_bounds, candidates, 256);
  uint32_t num_unique = Deduplicate(candidates, static_cast<uint32_t>(num_cand),
                                    UINT32_MAX, mask);

  // Use AABB-shape overlap for precise test.
  FVec2 center = FVec((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  float hw = (max.x - min.x) * 0.5f;
  float hh = (max.y - min.y) * 0.5f;
  CollisionShape query_shape = MakeAABB(hw * 2.0f, hh * 2.0f);

  uint32_t count = 0;
  for (uint32_t i = 0; i < num_unique && count < capacity; ++i) {
    uint32_t idx = candidates[i];
    const Collider& c = colliders_[idx];
    CollisionResult cr = TestShapes(query_shape, center, c.shape, c.position);
    if (cr.hit) {
      out[count++] = HandleFor(idx);
    }
  }
  return count;
}

uint32_t CollisionWorld::QueryCircle(FVec2 center, float radius, uint16_t mask,
                                     ColliderHandle* out, uint32_t capacity) {
  CollisionAABB query_bounds = {FVec(center.x - radius, center.y - radius),
                                FVec(center.x + radius, center.y + radius)};
  uint32_t candidates[256];
  size_t num_cand = spatial_hash_.Query(query_bounds, candidates, 256);
  uint32_t num_unique = Deduplicate(candidates, static_cast<uint32_t>(num_cand),
                                    UINT32_MAX, mask);

  CollisionShape query_shape = MakeCircle(radius);

  uint32_t count = 0;
  for (uint32_t i = 0; i < num_unique && count < capacity; ++i) {
    uint32_t idx = candidates[i];
    const Collider& c = colliders_[idx];
    CollisionResult cr = TestShapes(query_shape, center, c.shape, c.position);
    if (cr.hit) {
      out[count++] = HandleFor(idx);
    }
  }
  return count;
}

void CollisionWorld::Update() {
  ZONE("Collision::Update");
  // Rebuild spatial hash.
  spatial_hash_.Clear();
  for (uint32_t i = 0; i < kMaxColliders; ++i) {
    if (!colliders_[i].active) continue;
    CollisionAABB bounds =
        ComputeAABB(colliders_[i].shape, colliders_[i].position);
    spatial_hash_.Insert(i, bounds);
  }

  // Swap trigger pair buffers.
  std::swap(prev_triggers_, curr_triggers_);
  uint32_t prev_count = prev_triggers_.count;
  curr_triggers_.count = 0;

  // Find all current trigger overlapping pairs.
  for (uint32_t i = 0; i < kMaxColliders; ++i) {
    if (!colliders_[i].active) continue;
    if (!colliders_[i].is_trigger) continue;

    CollisionAABB bounds =
        ComputeAABB(colliders_[i].shape, colliders_[i].position);
    uint32_t candidates[256];
    size_t num_cand = spatial_hash_.Query(bounds, candidates, 256);

    for (size_t ci = 0; ci < num_cand; ++ci) {
      uint32_t j = candidates[ci];
      if (j == i) continue;
      if (j >= kMaxColliders || !colliders_[j].active) continue;
      if (!ShouldCollide(colliders_[i].filter, colliders_[j].filter)) continue;

      // Avoid duplicates: only store pairs where a < b.
      uint32_t a = i < j ? i : j;
      uint32_t b = i < j ? j : i;

      // Check if already in curr_triggers_.
      bool already = false;
      for (uint32_t k = 0; k < curr_triggers_.count; ++k) {
        if (curr_triggers_.pairs[k].a == a && curr_triggers_.pairs[k].b == b) {
          already = true;
          break;
        }
      }
      if (already) continue;

      // Narrow-phase confirm.
      CollisionResult cr =
          TestShapes(colliders_[a].shape, colliders_[a].position,
                     colliders_[b].shape, colliders_[b].position);
      if (cr.hit && curr_triggers_.count < kMaxTriggerPairs) {
        curr_triggers_.pairs[curr_triggers_.count++] = {a, b};
      }
    }
  }

  // Diff: find new pairs (in curr but not prev) and lost pairs (in prev but
  // not curr).
  new_triggers_.count = 0;
  lost_triggers_.count = 0;

  for (uint32_t i = 0; i < curr_triggers_.count; ++i) {
    bool found = false;
    for (uint32_t j = 0; j < prev_count; ++j) {
      if (curr_triggers_.pairs[i] == prev_triggers_.pairs[j]) {
        found = true;
        break;
      }
    }
    if (!found && new_triggers_.count < kMaxTriggerPairs) {
      new_triggers_.pairs[new_triggers_.count++] = curr_triggers_.pairs[i];
    }
  }

  for (uint32_t i = 0; i < prev_count; ++i) {
    bool found = false;
    for (uint32_t j = 0; j < curr_triggers_.count; ++j) {
      if (prev_triggers_.pairs[i] == curr_triggers_.pairs[j]) {
        found = true;
        break;
      }
    }
    if (!found && lost_triggers_.count < kMaxTriggerPairs) {
      lost_triggers_.pairs[lost_triggers_.count++] = prev_triggers_.pairs[i];
    }
  }
}

}  // namespace G
