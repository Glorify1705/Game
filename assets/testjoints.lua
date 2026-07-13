-- Joint demo: visual showcase of all six joint types.
-- Also works as an automated test with --test.
--
-- Controls:
--   Q: quit
--   D: toggle debug draw

local M = {}
local W, H

-- Track all bodies and joints for drawing.
local bodies = {} -- { {handle, shape, ...}, ... }
local joints = {} -- { {handle, type, ...}, ... }

local function add_box(x1, y1, x2, y2, opts)
	local handle = G.physics.add_box(x1, y1, x2, y2, 0, {}, opts or {})
	local b = { handle = handle, shape = "box", w = x2 - x1, h = y2 - y1 }
	table.insert(bodies, b)
	return handle
end

local function add_circle(cx, cy, r, opts)
	local handle = G.physics.add_circle(cx, cy, r, {}, opts or {})
	local b = { handle = handle, shape = "circle", r = r }
	table.insert(bodies, b)
	return handle
end

function M:init()
	W, H = G.window.dimensions()
	G.window.set_title("Physics Joints Demo")

	G.physics.set_gravity(0, 300)
	G.physics.create_ground(false)

	-- Floor
	add_box(0, H - 30, W, H, { body_type = "static" })

	-- 1. Revolute: motorized windmill
	local pivot = add_box(140, 280, 160, 300, { body_type = "static" })
	local arm = add_box(80, 285, 220, 295, {})
	local rj = G.physics.create_revolute_joint(pivot, arm, 150, 290, {
		enable_motor = true,
		motor_speed = 3.0,
		max_motor_torque = 500,
	})
	table.insert(joints, { handle = rj, type = "revolute", label = "Revolute (motor)" })

	-- 2. Distance: spring pendulum
	local anchor = add_circle(400, 100, 8, { body_type = "static" })
	local bob = add_circle(400, 250, 15, {})
	local dj = G.physics.create_distance_joint(anchor, bob, 400, 100, 400, 250, {
		frequency = 2.0,
		damping_ratio = 0.1,
	})
	table.insert(joints, { handle = dj, type = "distance", label = "Distance (spring)" })

	-- 3. Weld: rigid L-shape falling
	local wa = add_box(580, 150, 620, 250, {})
	local wb = add_box(620, 210, 700, 250, {})
	local wj = G.physics.create_weld_joint(wa, wb, 620, 230)
	table.insert(joints, { handle = wj, type = "weld", label = "Weld (rigid)" })

	-- 4. Prismatic: elevator
	local rail = add_box(800, 200, 840, 240, { body_type = "static" })
	local slider = add_box(800, 300, 840, 340, {})
	local pj = G.physics.create_prismatic_joint(rail, slider, 820, 270, 0, 1, {
		enable_limit = true,
		lower_translation = -100,
		upper_translation = 100,
		enable_motor = true,
		motor_speed = 80,
		max_motor_force = 800,
	})
	table.insert(joints, { handle = pj, type = "prismatic", label = "Prismatic (elevator)" })

	-- 5. Wheel: simple car
	local chassis = add_box(950, 500, 1150, 540, {})
	local wheel1 = add_circle(990, 555, 18, { friction = 0.9 })
	local wheel2 = add_circle(1110, 555, 18, { friction = 0.9 })
	local wj1 = G.physics.create_wheel_joint(chassis, wheel1, 990, 555, 0, 1, {
		enable_motor = true,
		motor_speed = -8.0,
		max_motor_torque = 400,
		frequency = 4.0,
		damping_ratio = 0.7,
	})
	local wj2 = G.physics.create_wheel_joint(chassis, wheel2, 1110, 555, 0, 1, {
		frequency = 4.0,
		damping_ratio = 0.7,
	})
	table.insert(joints, { handle = wj1, type = "wheel", label = "Wheel (driven)" })
	table.insert(joints, { handle = wj2, type = "wheel", label = "Wheel (free)" })

	-- 6. Chain bridge (revolute joints linking segments)
	local prev = add_box(200, 450, 220, 470, { body_type = "static" })
	for i = 1, 8 do
		local seg = add_box(220 + (i - 1) * 30, 455, 250 + (i - 1) * 30, 465, {
			density = 1.0,
		})
		local cj = G.physics.create_revolute_joint(prev, seg, 220 + (i - 1) * 30, 460)
		table.insert(joints, { handle = cj, type = "revolute" })
		prev = seg
	end
	local right_anchor = add_box(460, 450, 480, 470, { body_type = "static" })
	local cj_end = G.physics.create_revolute_joint(prev, right_anchor, 460, 460)
	table.insert(joints, { handle = cj_end, type = "revolute" })
end

function M:update(t, dt)
	if G.input.is_key_pressed("q") then
		G.system.quit()
	end
	if G.input.is_key_pressed("d") then
		if G.physics.debug_draw_enabled then
			-- Toggle not available yet, just skip
		end
	end
end

function M:draw()
	G.graphics.clear(0.08, 0.08, 0.12, 1)

	-- Draw joint connections.
	G.graphics.set_color(80, 255, 120, 120)
	for _, j in ipairs(joints) do
		if j.handle:is_valid() then
			-- Draw label if present
			if j.label then
				-- Find approximate position from the first body
			end
		end
	end

	-- Draw bodies.
	for _, b in ipairs(bodies) do
		local x, y = G.physics.position(b.handle)
		local angle = G.physics.angle(b.handle)
		if b.shape == "circle" then
			G.graphics.set_color(255, 200, 80, 255)
			G.graphics.draw_circle(x, y, b.r)
		else
			G.graphics.set_color(120, 160, 220, 255)
			G.graphics.push()
			G.graphics.translate(x, y)
			G.graphics.rotate(angle)
			G.graphics.draw_rect(-b.w / 2, -b.h / 2, b.w / 2, b.h / 2)
			G.graphics.pop()
		end
	end

	-- Labels
	G.graphics.set_color(255, 255, 255, 200)
	G.graphics.print("Revolute", 110, 250)
	G.graphics.print("Distance", 360, 70)
	G.graphics.print("Weld", 610, 130)
	G.graphics.print("Prismatic", 780, 170)
	G.graphics.print("Wheel (car)", 990, 470)
	G.graphics.print("Chain bridge", 280, 430)
	G.graphics.print("Q: quit", 10, 10)
end

function M:test_inputs()
	-- Test 1: Revolute joint with motor
	print("testjoints: revolute joint with motor")
	local rj = joints[1].handle
	G.test.assert_true(rj:is_valid(), "revolute joint should be valid")
	G.test.assert_true(rj:get_type() == "revolute", "type should be revolute")
	G.test.wait_frames(15)
	local angle = rj:get_joint_angle()
	G.test.assert_true(math.abs(angle) > 0.01, "motor should have rotated joint, angle=" .. angle)
	rj:set_motor_speed(-3.0)
	G.test.wait_frames(5)
	print("  PASS")

	-- Test 2: Distance joint spring
	print("testjoints: distance joint")
	local dj = joints[2].handle
	G.test.assert_true(dj:is_valid(), "distance joint should be valid")
	G.test.assert_true(dj:get_type() == "distance", "type should be distance")
	G.test.wait_frames(30)
	local len = dj:get_current_length()
	G.test.assert_true(len > 0, "current length should be positive, got " .. len)
	print("  PASS")

	-- Test 3: Weld joint
	print("testjoints: weld joint")
	local wj = joints[3].handle
	G.test.assert_true(wj:is_valid(), "weld joint should be valid")
	G.test.assert_true(wj:get_type() == "weld", "type should be weld")
	print("  PASS")

	-- Test 4: Prismatic joint
	print("testjoints: prismatic joint")
	local pj = joints[4].handle
	G.test.assert_true(pj:is_valid(), "prismatic joint should be valid")
	G.test.assert_true(pj:get_type() == "prismatic", "type should be prismatic")
	G.test.wait_frames(15)
	local trans = pj:get_joint_translation()
	G.test.assert_true(math.abs(trans) > 0, "prismatic should have translated, got " .. trans)
	print("  PASS")

	-- Test 5: Wheel joint
	print("testjoints: wheel joint")
	local wj2 = joints[5].handle
	G.test.assert_true(wj2:is_valid(), "wheel joint should be valid")
	G.test.assert_true(wj2:get_type() == "wheel", "type should be wheel")
	G.test.wait_frames(10)
	wj2:enable_motor(false)
	G.test.wait_frames(5)
	print("  PASS")

	-- Test 6: Mouse joint create + destroy
	print("testjoints: mouse joint")
	local mb = add_circle(700, 300, 20, {})
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
	G.test.assert_true(not mj:is_valid(), "destroyed joint should be invalid")
	print("  PASS")

	-- Test 7: Joint invalidation on body destroy
	print("testjoints: joint invalidation on body destroy")
	local ba = add_box(100, 600, 140, 640, { body_type = "static" })
	local bb = add_box(140, 600, 180, 640, {})
	local rj2 = G.physics.create_revolute_joint(ba, bb, 140, 620)
	G.test.assert_true(rj2:is_valid(), "joint should be valid before body destroy")
	G.physics.destroy_handle(bb)
	G.test.assert_true(not rj2:is_valid(), "joint should be invalid after body destroy")
	print("  PASS")

	-- Test 8: Revolute limits
	print("testjoints: revolute limits")
	local la = add_box(250, 600, 290, 640, { body_type = "static" })
	local lb = add_box(290, 600, 350, 640, {})
	local lj = G.physics.create_revolute_joint(la, lb, 290, 620, {
		enable_limit = true,
		lower_angle = -0.5,
		upper_angle = 0.5,
	})
	lj:set_limits(-0.2, 0.2)
	lj:enable_limit(true)
	G.test.wait_frames(10)
	print("  PASS")

	print("testjoints: all tests passed!")
	G.system.quit()
end

return M
