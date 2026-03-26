#pragma once
#ifndef _GAME_COLLISION_WORLD_H
#define _GAME_COLLISION_WORLD_H

#include <cstdint>
#include <cstring>

#include "allocators.h"
#include "collision.h"
#include "vec.h"

namespace G {

// Spatial hash grid for broad-phase collision detection.
// Persistent allocation — Clear() and rebuild each frame.
class SpatialHash {
 public:
  SpatialHash() = default;
  void Init(float cell_size, size_t table_size, Allocator* allocator);
  void Destroy();

  void Clear();
  void Insert(uint32_t id, CollisionAABB bounds);

  // Query: fills out with IDs whose cells overlap the query AABB.
  // May contain duplicates. Returns count written.
  size_t Query(CollisionAABB bounds, uint32_t* out, size_t out_capacity) const;

  // Ray query using DDA grid traversal.
  size_t QueryRay(FVec2 origin, FVec2 direction, float max_dist, uint32_t* out,
                  size_t out_capacity) const;

 private:
  size_t Hash(int cx, int cy) const;

  float cell_size_ = 64.0f;
  float inv_cell_size_ = 1.0f / 64.0f;
  size_t table_size_ = 0;
  Allocator* allocator_ = nullptr;

  static constexpr uint32_t kMaxEntries = 16384;
  static constexpr uint32_t kNone = UINT32_MAX;

  struct Entry {
    uint32_t id;
    uint32_t next;  // index into entries_ or kNone
  };

  uint32_t* bucket_heads_ = nullptr;  // table_size_ entries
  Entry* entries_ = nullptr;          // kMaxEntries capacity
  uint32_t entry_count_ = 0;
};

// Handle to a collider in a CollisionWorld.
struct ColliderHandle {
  uint32_t index = UINT32_MAX;
  uint32_t generation = 0;

  bool operator==(const ColliderHandle& o) const {
    return index == o.index && generation == o.generation;
  }
  bool operator!=(const ColliderHandle& o) const { return !(*this == o); }
};

class CollisionWorld {
 public:
  static constexpr uint32_t kMaxColliders = 4096;
  static constexpr uint32_t kMaxContacts = 8;
  static constexpr uint32_t kMaxQueryResults = 64;
  static constexpr uint32_t kMoveIterations = 4;
  static constexpr uint32_t kMaxTriggerPairs = 1024;

  struct Collider {
    CollisionShape shape;
    FVec2 position;
    CollisionFilter filter;
    bool is_trigger;
    bool active;
    uintptr_t userdata;
    uint32_t generation;
    uint32_t next_free;
  };

  struct Contact {
    ColliderHandle other;
    FVec2 normal;
    FVec2 point;
    float depth;
  };

  struct MoveResult {
    FVec2 position;
    Contact contacts[kMaxContacts];
    uint32_t contact_count = 0;
  };

  struct OverlapResult {
    ColliderHandle handle;
    FVec2 normal;
    float depth;
  };

  struct RaycastHit {
    ColliderHandle handle;
    FVec2 point;
    FVec2 normal;
    float t;
  };

  CollisionWorld(float cell_size, Allocator* allocator);
  ~CollisionWorld();

  // Collider management
  ColliderHandle Add(CollisionShape shape, FVec2 position,
                     CollisionFilter filter, bool is_trigger,
                     uintptr_t userdata);
  void Remove(ColliderHandle handle);
  bool IsValid(ColliderHandle handle) const;

  void SetPosition(ColliderHandle handle, FVec2 position);
  void SetShape(ColliderHandle handle, CollisionShape shape);
  void SetFilter(ColliderHandle handle, CollisionFilter filter);
  FVec2 GetPosition(ColliderHandle handle) const;
  uintptr_t GetUserdata(ColliderHandle handle) const;

  // Movement with collision resolution
  MoveResult MoveAndSlide(ColliderHandle handle, FVec2 velocity);
  MoveResult MoveAndCollide(ColliderHandle handle, FVec2 velocity);

  // Overlap queries
  uint32_t GetOverlaps(ColliderHandle handle, OverlapResult* out,
                       uint32_t capacity);

  // Spatial queries
  bool Raycast(FVec2 origin, FVec2 direction, float max_dist, uint16_t mask,
               RaycastHit* out);
  uint32_t RaycastAll(FVec2 origin, FVec2 direction, float max_dist,
                      uint16_t mask, RaycastHit* out, uint32_t capacity);
  uint32_t QueryPoint(FVec2 point, uint16_t mask, ColliderHandle* out,
                      uint32_t capacity);
  uint32_t QueryRect(FVec2 min, FVec2 max, uint16_t mask, ColliderHandle* out,
                     uint32_t capacity);
  uint32_t QueryCircle(FVec2 center, float radius, uint16_t mask,
                       ColliderHandle* out, uint32_t capacity);

  // Must be called each frame to rebuild broad phase and detect triggers.
  void Update();

  // Trigger callback Lua registry refs (managed by lua_collision.cc).
  // LUA_NOREF is -2; -1 is LUA_REFNIL which resolves to nil.
  static constexpr int kNoRef = -2;  // == LUA_NOREF
  int trigger_enter_ref = kNoRef;
  int trigger_exit_ref = kNoRef;

  // For __gc cleanup
  uint32_t collider_capacity() const { return kMaxColliders; }
  bool IsActiveSlot(uint32_t index) const { return colliders_[index].active; }
  uintptr_t GetSlotUserdata(uint32_t index) const {
    return colliders_[index].userdata;
  }
  uint32_t active_count() const { return count_; }

  // Trigger pair tracking for Lua callbacks
  struct TriggerPair {
    uint32_t a, b;  // slot indices, a < b
    bool operator==(const TriggerPair& o) const { return a == o.a && b == o.b; }
    bool operator<(const TriggerPair& o) const {
      return a < o.a || (a == o.a && b < o.b);
    }
  };

  const TriggerPair* new_trigger_pairs() const { return new_triggers_.pairs; }
  uint32_t new_trigger_count() const { return new_triggers_.count; }
  const TriggerPair* lost_trigger_pairs() const { return lost_triggers_.pairs; }
  uint32_t lost_trigger_count() const { return lost_triggers_.count; }

  ColliderHandle HandleFor(uint32_t index) const {
    return {index, colliders_[index].generation};
  }

 private:
  const Collider& GetCollider(ColliderHandle handle) const;
  Collider& GetColliderMut(ColliderHandle handle);

  // Deduplicate broad-phase query results and apply filters.
  uint32_t Deduplicate(uint32_t* ids, uint32_t count, uint32_t exclude_index,
                       uint16_t mask) const;

  Collider* colliders_ = nullptr;
  uint32_t first_free_ = 0;
  uint32_t count_ = 0;
  Allocator* allocator_ = nullptr;
  SpatialHash spatial_hash_;

  // Trigger pair tracking: each frame, Update() builds the set of currently
  // overlapping trigger pairs (curr) and diffs it against the previous frame
  // (prev) to produce new (entered this frame) and lost (exited this frame)
  // lists. Lua callbacks fire for new/lost pairs.
  struct TriggerPairList {
    TriggerPair* pairs = nullptr;
    uint32_t count = 0;
  };

  TriggerPairList prev_triggers_;
  TriggerPairList curr_triggers_;
  TriggerPairList new_triggers_;
  TriggerPairList lost_triggers_;
};

}  // namespace G

#endif  // _GAME_COLLISION_WORLD_H
