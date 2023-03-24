#include "allocators.h"
#include "logging.h"

namespace G {

BumpAllocator::BumpAllocator(size_t size) {
  ptr_ = reinterpret_cast<uintptr_t>(::new uint8_t[size]);
  pos_ = ptr_;
  end_ = ptr_ + size;
}

void* BumpAllocator::Alloc(size_t size, size_t align) {
  DCHECK(align > 0 && !(align & (align - 1)));
  uintptr_t prev = Align(pos_, align);
  DCHECK(prev + size <= end_, "Out of memory");
  pos_ = prev + size;
  return reinterpret_cast<void*>(prev);
}

void* BumpAllocator::Realloc(void* p, size_t old_size, size_t new_size) {
  if (p == nullptr) return Alloc(new_size, /*align=*/1);
  const uintptr_t ptr = reinterpret_cast<uintptr_t>(p);
  if (ptr + old_size == pos_) {
    pos_ += new_size - old_size;
    return p;
  } else {
    LOG("Reallocation not against TIP - This could be very slow");
    void* new_location = Alloc(new_size, /*align=*/1);
    std::memcpy(new_location, p, old_size);
    return new_location;
  }
}

}  // namespace G