#include "allocators.h"

#include "logging.h"
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