#include <SDL3/SDL.h>

#include "gtest/gtest.h"
#include "input.h"

namespace G {
namespace {

SDL_Event FingerEvent(Uint32 type, int64_t id, float x, float y,
                      float pressure = 1.0f) {
  SDL_Event event = {};
  event.type = type;
  event.tfinger.fingerID = id;
  event.tfinger.x = x;
  event.tfinger.y = y;
  event.tfinger.pressure = pressure;
  return event;
}

TEST(MapWindowToViewport, IdentityWhenSizesMatch) {
  const FVec2 pos =
      MapWindowToViewport(FVec(10, 20), FVec(100, 100), FVec(100, 100));
  EXPECT_FLOAT_EQ(pos.x, 10);
  EXPECT_FLOAT_EQ(pos.y, 20);
}

TEST(MapWindowToViewport, PillarboxedWiderWindow) {
  // 200x100 window, 100x100 viewport: scale 1, bars 50px on each side.
  const FVec2 pos =
      MapWindowToViewport(FVec(150, 50), FVec(200, 100), FVec(100, 100));
  EXPECT_FLOAT_EQ(pos.x, 100);
  EXPECT_FLOAT_EQ(pos.y, 50);
}

TEST(MapWindowToViewport, LetterboxedTallerWindow) {
  // 100x200 window, 100x100 viewport: scale 1, bars 50px top and bottom.
  const FVec2 pos =
      MapWindowToViewport(FVec(50, 150), FVec(100, 200), FVec(100, 100));
  EXPECT_FLOAT_EQ(pos.x, 50);
  EXPECT_FLOAT_EQ(pos.y, 100);
}

TEST(MapWindowToViewport, ScaledWindow) {
  // 200x200 window, 100x100 viewport: scale 2, no bars.
  const FVec2 pos =
      MapWindowToViewport(FVec(100, 100), FVec(200, 200), FVec(100, 100));
  EXPECT_FLOAT_EQ(pos.x, 50);
  EXPECT_FLOAT_EQ(pos.y, 50);
}

TEST(TouchTest, FingerLifecycle) {
  Touch touch;
  EXPECT_FALSE(touch.AnyDown());

  touch.PushEvent(FingerEvent(SDL_EVENT_FINGER_DOWN, 1, 0.5f, 0.5f));
  EXPECT_TRUE(touch.AnyDown());
  EXPECT_TRUE(touch.AnyPressed());
  EXPECT_FALSE(touch.AnyReleased());
  EXPECT_EQ(touch.DownCount(), 1u);

  touch.InitForFrame();
  EXPECT_TRUE(touch.AnyDown());
  EXPECT_FALSE(touch.AnyPressed()) << "edge must clear after a frame";

  touch.PushEvent(FingerEvent(SDL_EVENT_FINGER_UP, 1, 0.5f, 0.5f));
  EXPECT_FALSE(touch.AnyDown());
  EXPECT_TRUE(touch.AnyReleased());

  touch.InitForFrame();
  EXPECT_FALSE(touch.AnyReleased()) << "release edge lives exactly one frame";
}

TEST(TouchTest, CanceledCountsAsRelease) {
  Touch touch;
  touch.PushEvent(FingerEvent(SDL_EVENT_FINGER_DOWN, 7, 0.1f, 0.1f));
  touch.InitForFrame();
  touch.PushEvent(FingerEvent(SDL_EVENT_FINGER_CANCELED, 7, 0.1f, 0.1f));
  EXPECT_TRUE(touch.AnyReleased());
  EXPECT_FALSE(touch.AnyDown());
}

TEST(TouchTest, MultiFingerCountAndPositions) {
  Touch touch;
  touch.SetWindowAndViewport(FVec(200, 100), FVec(100, 100));
  touch.PushEvent(FingerEvent(SDL_EVENT_FINGER_DOWN, 1, 0.5f, 0.5f));
  touch.PushEvent(FingerEvent(SDL_EVENT_FINGER_DOWN, 2, 0.75f, 0.5f));
  EXPECT_EQ(touch.DownCount(), 2u);

  Touch::Finger fingers[Touch::kMaxFingers];
  const size_t count = touch.GetFingers(fingers, Touch::kMaxFingers);
  ASSERT_EQ(count, 2u);
  // Normalized 0.5 in a 200px-wide pillarboxed window maps to viewport
  // center; 0.75 maps to the right edge.
  EXPECT_FLOAT_EQ(fingers[0].position.x, 50);
  EXPECT_FLOAT_EQ(fingers[1].position.x, 100);
}

TEST(TouchTest, SlotReuseAfterRelease) {
  Touch touch;
  touch.PushEvent(FingerEvent(SDL_EVENT_FINGER_DOWN, 1, 0.5f, 0.5f));
  touch.InitForFrame();
  touch.PushEvent(FingerEvent(SDL_EVENT_FINGER_UP, 1, 0.5f, 0.5f));
  touch.InitForFrame();  // Release edge observable this frame.
  touch.InitForFrame();  // Slot freed.
  touch.PushEvent(FingerEvent(SDL_EVENT_FINGER_DOWN, 2, 0.5f, 0.5f));
  EXPECT_EQ(touch.DownCount(), 1u);
  EXPECT_TRUE(touch.AnyPressed());
}

TEST(TouchTest, EleventhFingerDropped) {
  Touch touch;
  for (int64_t id = 1; id <= 11; ++id) {
    touch.PushEvent(FingerEvent(SDL_EVENT_FINGER_DOWN, id, 0.5f, 0.5f));
  }
  EXPECT_EQ(touch.DownCount(), Touch::kMaxFingers);
}

TEST(TouchTest, MotionUpdatesPosition) {
  Touch touch;
  touch.SetWindowAndViewport(FVec(100, 100), FVec(100, 100));
  touch.PushEvent(FingerEvent(SDL_EVENT_FINGER_DOWN, 1, 0.1f, 0.1f));
  touch.PushEvent(FingerEvent(SDL_EVENT_FINGER_MOTION, 1, 0.9f, 0.8f));
  Touch::Finger finger;
  ASSERT_EQ(touch.GetFingers(&finger, 1), 1u);
  EXPECT_FLOAT_EQ(finger.position.x, 90);
  EXPECT_FLOAT_EQ(finger.position.y, 80);
}

TEST(TouchTest, TestModeUsesViewportCoordinates) {
  Touch touch;
  touch.SetTestMode(true);
  touch.SetWindowAndViewport(FVec(200, 100), FVec(100, 100));
  touch.InjectFingerDown(1, 42, 24, 1.0f);
  Touch::Finger finger;
  ASSERT_EQ(touch.GetFingers(&finger, 1), 1u);
  // Injected coordinates bypass the window mapping entirely.
  EXPECT_FLOAT_EQ(finger.position.x, 42);
  EXPECT_FLOAT_EQ(finger.position.y, 24);
  touch.InjectFingerUp(1);
  EXPECT_FALSE(touch.AnyDown());
}

}  // namespace
}  // namespace G
