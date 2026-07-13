#include <SDL3/SDL.h>

#include "actions.h"
#include "clock.h"
#include "gtest/gtest.h"
#include "input.h"

namespace G {
namespace {

// Drives the devices and action layer the way Game::RunFrame does: device
// InitForFrame (StartFrame), then injection (events), then Actions::Update.
class ActionsTest : public ::testing::Test {
 protected:
  ActionsTest()
      : keyboard_(&allocator_),
        controllers_(&allocator_),
        actions_(&keyboard_, &mouse_, &controllers_, &touch_, &allocator_) {
    keyboard_.SetTestMode(true);
    mouse_.SetTestMode(true);
    controllers_.SetTestMode(true);
    touch_.SetTestMode(true);
  }

  // Advances one frame without changing any input.
  void Frame() {
    keyboard_.InitForFrame();
    mouse_.InitForFrame();
    controllers_.InitForFrame();
    touch_.InitForFrame();
    actions_.Update();
  }

  ErrorOr<void> Bind(std::string_view action,
                     std::initializer_list<std::string_view> bindings) {
    return actions_.Bind(action, Slice<const std::string_view>(
                                     bindings.begin(), bindings.size()));
  }

  SDL_Scancode Key(std::string_view name) {
    return keyboard_.MapKey(name).code;
  }

  SystemAllocator allocator_;
  Keyboard keyboard_;
  Mouse mouse_;
  Controllers controllers_;
  Touch touch_;
  Actions actions_;
};

TEST_F(ActionsTest, BindParsesAllDeviceForms) {
  EXPECT_FALSE(Bind("everything", {"key:space", "mouse:left", "mouse:2",
                                   "gamepad:a", "touch"})
                   .is_error());
  EXPECT_TRUE(actions_.Has("everything"));
}

TEST_F(ActionsTest, BindRejectsInvalidStrings) {
  EXPECT_TRUE(Bind("bad", {"key:nosuchkey"}).is_error());
  EXPECT_TRUE(Bind("bad", {"mouse:banana"}).is_error());
  EXPECT_TRUE(Bind("bad", {"gamepad:zz"}).is_error());
  EXPECT_TRUE(Bind("bad", {"warp:x"}).is_error());
  EXPECT_TRUE(Bind("bad", {"mouse"}).is_error());
  EXPECT_TRUE(Bind("bad", {}).is_error());
  EXPECT_TRUE(Bind("bad", {"key:a", "key:b", "key:c", "key:d", "key:e", "key:f",
                           "key:g", "key:h", "key:i"})
                  .is_error());
  EXPECT_FALSE(actions_.Has("bad"));
}

TEST_F(ActionsTest, KeyBindingPressDownRelease) {
  ASSERT_FALSE(Bind("jump", {"key:space"}).is_error());
  Frame();
  EXPECT_FALSE(actions_.IsDown("jump"));

  keyboard_.InjectKeyDown(Key("space"));
  actions_.Update();
  EXPECT_TRUE(actions_.IsPressed("jump"));
  EXPECT_TRUE(actions_.IsDown("jump"));

  Frame();
  EXPECT_FALSE(actions_.IsPressed("jump")) << "edge clears after a frame";
  EXPECT_TRUE(actions_.IsDown("jump"));

  keyboard_.InjectKeyUp(Key("space"));
  actions_.Update();
  EXPECT_TRUE(actions_.IsReleased("jump"));
  EXPECT_FALSE(actions_.IsDown("jump"));

  Frame();
  EXPECT_FALSE(actions_.IsReleased("jump"));
}

TEST_F(ActionsTest, ActionLevelEdges) {
  // Holding one bound key while tapping another must not produce a second
  // pressed edge: edges are action-level, not per-binding.
  ASSERT_FALSE(Bind("fire", {"key:z", "key:x"}).is_error());
  keyboard_.InjectKeyDown(Key("z"));
  actions_.Update();
  EXPECT_TRUE(actions_.IsPressed("fire"));

  Frame();
  keyboard_.InjectKeyDown(Key("x"));
  actions_.Update();
  EXPECT_FALSE(actions_.IsPressed("fire"))
      << "second binding while already down is not a new press";
  EXPECT_TRUE(actions_.IsDown("fire"));
}

TEST_F(ActionsTest, MultiKeyHoldReleaseOneStaysDown) {
  ASSERT_FALSE(Bind("fire", {"key:z", "key:x"}).is_error());
  keyboard_.InjectKeyDown(Key("z"));
  keyboard_.InjectKeyDown(Key("x"));
  actions_.Update();
  Frame();
  keyboard_.InjectKeyUp(Key("z"));
  actions_.Update();
  EXPECT_TRUE(actions_.IsDown("fire"));
  EXPECT_FALSE(actions_.IsReleased("fire"));
}

TEST_F(ActionsTest, MouseBinding) {
  ASSERT_FALSE(Bind("shoot", {"mouse:left"}).is_error());
  Frame();
  mouse_.InjectButtonDown(0);
  // Mouse::IsDown requires the state to persist across a frame boundary
  // (previous && current), matching its raw-query semantics.
  Frame();
  EXPECT_TRUE(actions_.IsDown("shoot"));
  EXPECT_TRUE(actions_.IsPressed("shoot"));
}

TEST_F(ActionsTest, GamepadBinding) {
  ASSERT_FALSE(Bind("confirm", {"gamepad:a"}).is_error());
  Frame();
  controllers_.InjectButtonDown(controllers_.StrToButton("a"));
  Frame();
  EXPECT_TRUE(actions_.IsDown("confirm"));
}

TEST_F(ActionsTest, TouchAnyFingerBinding) {
  ASSERT_FALSE(Bind("flap", {"touch"}).is_error());
  Frame();
  touch_.InjectFingerDown(1, 10, 10, 1.0f);
  actions_.Update();
  EXPECT_TRUE(actions_.IsPressed("flap"));
  EXPECT_TRUE(actions_.IsDown("flap"));

  touch_.InjectFingerUp(1);
  actions_.Update();
  EXPECT_TRUE(actions_.IsReleased("flap"));
}

TEST_F(ActionsTest, RebindReplacesBindingsAndResetsState) {
  ASSERT_FALSE(Bind("jump", {"key:space"}).is_error());
  keyboard_.InjectKeyDown(Key("space"));
  actions_.Update();
  EXPECT_TRUE(actions_.IsDown("jump"));

  ASSERT_FALSE(Bind("jump", {"key:w"}).is_error());
  actions_.Update();
  EXPECT_FALSE(actions_.IsPressed("jump"))
      << "state resets on rebind; the still-held old key must not count";
  keyboard_.InjectKeyDown(Key("w"));
  actions_.Update();
  EXPECT_TRUE(actions_.IsDown("jump"));
}

TEST_F(ActionsTest, ActionTimeAccumulatesAndResets) {
  ASSERT_FALSE(Bind("charge", {"key:c"}).is_error());
  EXPECT_DOUBLE_EQ(actions_.DownTime("charge"), 0.0);
  keyboard_.InjectKeyDown(Key("c"));
  actions_.Update();
  for (int i = 0; i < 4; ++i) Frame();
  EXPECT_DOUBLE_EQ(actions_.DownTime("charge"), 5.0 * TimeStepInSeconds());
  keyboard_.InjectKeyUp(Key("c"));
  actions_.Update();
  EXPECT_DOUBLE_EQ(actions_.DownTime("charge"), 0.0);
}

TEST_F(ActionsTest, GetBindingsRoundTrips) {
  ASSERT_FALSE(Bind("move", {"key:w", "gamepad:dpadu", "touch"}).is_error());
  std::string_view out[Actions::kMaxBindingsPerAction];
  const size_t count =
      actions_.GetBindings("move", out, Actions::kMaxBindingsPerAction);
  ASSERT_EQ(count, 3u);
  EXPECT_EQ(out[0], "key:w");
  EXPECT_EQ(out[1], "gamepad:dpadu");
  EXPECT_EQ(out[2], "touch");
  EXPECT_EQ(actions_.GetBindings("unbound", out, 8), 0u);
}

TEST_F(ActionsTest, UnboundActionQueriesReturnFalse) {
  EXPECT_FALSE(actions_.IsDown("ghost"));
  EXPECT_FALSE(actions_.IsPressed("ghost"));
  EXPECT_FALSE(actions_.IsReleased("ghost"));
  EXPECT_DOUBLE_EQ(actions_.DownTime("ghost"), 0.0);
  EXPECT_FALSE(actions_.Has("ghost"));
}

}  // namespace
}  // namespace G
