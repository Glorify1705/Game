local M = {}

function M:init()
  G.physics.set_gravity(0, 300)
  G.physics.create_ground(true)
end

function M:update(t, dt)
end

function M:draw()
  G.graphics.clear(0.08, 0.08, 0.12, 1)
end

function M:test_inputs()
  -- Test 1: Revolute joint with motor
  print("testjoints: revolute joint with motor")
  local a = G.physics.add_box(100, 200, 140, 240, 0, {}, { body_type = "static" })
  local b = G.physics.add_box(140, 200, 200, 240, 0, {}, {})
  local rj = G.physics.create_revolute_joint(a, b, 140, 220, {
    enable_motor = true,
    motor_speed = 3.0,
    max_motor_torque = 200,
  })
  G.test.assert_true(rj:is_valid(), "revolute joint should be valid")
  G.test.assert_true(rj:get_type() == "revolute", "type should be revolute")
  G.test.wait_frames(15)
  local angle = rj:get_joint_angle()
  G.test.assert_true(math.abs(angle) > 0.01, "motor should have rotated joint, angle=" .. angle)
  rj:set_motor_speed(-3.0)
  G.test.wait_frames(5)
  print("  PASS")

  -- Test 2: Revolute joint limits
  print("testjoints: revolute joint limits")
  local la = G.physics.add_box(250, 200, 290, 240, 0, {}, { body_type = "static" })
  local lb = G.physics.add_box(290, 200, 350, 240, 0, {}, {})
  local lj = G.physics.create_revolute_joint(la, lb, 290, 220, {
    enable_limit = true,
    lower_angle = -0.5,
    upper_angle = 0.5,
  })
  lj:set_limits(-0.2, 0.2)
  lj:enable_limit(true)
  G.test.wait_frames(10)
  print("  PASS")

  -- Test 3: Distance joint with spring
  print("testjoints: distance joint")
  local c = G.physics.add_circle(400, 100, 15, {}, { body_type = "static" })
  local d = G.physics.add_circle(400, 200, 15, {}, {})
  local dj = G.physics.create_distance_joint(c, d, 400, 100, 400, 200, {
    frequency = 4.0,
    damping_ratio = 0.5,
  })
  G.test.assert_true(dj:is_valid(), "distance joint should be valid")
  G.test.assert_true(dj:get_type() == "distance", "type should be distance")
  G.test.wait_frames(30)
  local len = dj:get_current_length()
  G.test.assert_true(len > 0, "current length should be positive, got " .. len)
  print("  PASS")

  -- Test 4: Weld joint
  print("testjoints: weld joint")
  local e = G.physics.add_box(500, 200, 540, 240, 0, {}, {})
  local f = G.physics.add_box(540, 200, 580, 240, 0, {}, {})
  local wj = G.physics.create_weld_joint(e, f, 540, 220)
  G.test.assert_true(wj:is_valid(), "weld joint should be valid")
  G.test.assert_true(wj:get_type() == "weld", "type should be weld")
  G.test.wait_frames(5)
  print("  PASS")

  -- Test 5: Prismatic joint with motor
  print("testjoints: prismatic joint")
  local g = G.physics.add_box(600, 300, 640, 340, 0, {}, { body_type = "static" })
  local h = G.physics.add_box(600, 250, 640, 290, 0, {}, {})
  local pj = G.physics.create_prismatic_joint(g, h, 620, 300, 0, 1, {
    enable_limit = true,
    lower_translation = -60,
    upper_translation = 60,
    enable_motor = true,
    motor_speed = 60,
    max_motor_force = 500,
  })
  G.test.assert_true(pj:is_valid(), "prismatic joint should be valid")
  G.test.assert_true(pj:get_type() == "prismatic", "type should be prismatic")
  G.test.wait_frames(15)
  local trans = pj:get_joint_translation()
  G.test.assert_true(math.abs(trans) > 0, "prismatic should have translated, got " .. trans)
  print("  PASS")

  -- Test 6: Mouse joint
  print("testjoints: mouse joint")
  local mb = G.physics.add_circle(700, 300, 20, {}, {})
  local mj = G.physics.create_mouse_joint(mb, 700, 300, {
    max_force = 1000,
    frequency = 5.0,
    damping_ratio = 0.7,
  })
  G.test.assert_true(mj:is_valid(), "mouse joint should be valid")
  G.test.assert_true(mj:get_type() == "mouse", "type should be mouse")
  mj:set_target(720, 280)
  G.test.wait_frames(10)
  mj:destroy()
  G.test.assert_true(not mj:is_valid(), "destroyed mouse joint should be invalid")
  print("  PASS")

  -- Test 7: Joint invalidation on body destroy
  print("testjoints: joint invalidation on body destroy")
  local ba = G.physics.add_box(100, 400, 140, 440, 0, {}, { body_type = "static" })
  local bb = G.physics.add_box(140, 400, 180, 440, 0, {}, {})
  local rj2 = G.physics.create_revolute_joint(ba, bb, 140, 420)
  G.test.assert_true(rj2:is_valid(), "joint should be valid before body destroy")
  G.physics.destroy_handle(bb)
  G.test.assert_true(not rj2:is_valid(), "joint should be invalid after body destroy")
  print("  PASS")

  -- Test 8: Wheel joint
  print("testjoints: wheel joint")
  local chassis = G.physics.add_box(200, 500, 300, 530, 0, {}, {})
  local wheel = G.physics.add_circle(250, 545, 15, {}, {})
  local wj2 = G.physics.create_wheel_joint(chassis, wheel, 250, 545, 0, 1, {
    enable_motor = true,
    motor_speed = 5.0,
    max_motor_torque = 200,
    frequency = 4.0,
    damping_ratio = 0.7,
  })
  G.test.assert_true(wj2:is_valid(), "wheel joint should be valid")
  G.test.assert_true(wj2:get_type() == "wheel", "type should be wheel")
  G.test.wait_frames(10)
  wj2:enable_motor(false)
  G.test.wait_frames(5)
  print("  PASS")

  print("testjoints: all tests passed!")
  G.system.quit()
end

return M
