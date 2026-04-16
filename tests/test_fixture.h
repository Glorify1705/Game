#pragma once

#include "allocators.h"
#include "gtest/gtest.h"

namespace G {

// Base fixture providing a system allocator for tests that need one.
class AllocTest : public ::testing::Test {
 protected:
  Allocator* alloc = SystemAllocator::Instance();
};

}  // namespace G
