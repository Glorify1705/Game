-- Stress-test benchmark for profiling. Spawns many physics bodies and
-- collision objects to generate load across all subsystems.
-- Controls:
--   1-5: Set object count (100, 300, 500, 1000, 2000)
--   F11: Toggle profiler recording
--   R: Reset simulation
--   Esc: Quit

local Game = {}

local TARGET_COUNT = 500
local GRAVITY_Y = 300

function Game:init()
	G.window.set_dimensions(1280, 800)
	G.window.set_title("Benchmark - Profiler Stress Test")

	self.w, self.h = G.window.dimensions()
	self.world = G.collision.new_world(64)
	self.objects = {}
	self.particles = {}
	self.frame = 0
	self.spawn_timer = 0

	self:setup_walls()
	self:spawn_batch(TARGET_COUNT)
end

function Game:setup_walls()
	local thick = 20
	local w, h = self.w, self.h

	-- Floor, ceiling, left wall, right wall.
	self.wall_shapes = {
		{ G.collision.aabb(w, thick), w / 2, h - thick / 2 },
		{ G.collision.aabb(w, thick), w / 2, thick / 2 },
		{ G.collision.aabb(thick, h), thick / 2, h / 2 },
		{ G.collision.aabb(thick, h), w - thick / 2, h / 2 },
	}
	self.walls = {}
	for _, wall in ipairs(self.wall_shapes) do
		local handle = self.world:add(wall[1], wall[2], wall[3])
		table.insert(self.walls, handle)
	end
end

function Game:spawn_batch(count)
	local w, h = self.w, self.h
	for i = 1, count do
		self:spawn_object(40 + math.random() * (w - 80), 40 + math.random() * (h / 2))
	end
end

function Game:spawn_object(x, y)
	local kind = math.random(3)
	local obj = {
		x = x,
		y = y,
		vx = (math.random() - 0.5) * 200,
		vy = (math.random() - 0.5) * 100,
		kind = kind,
		age = 0,
	}

	if kind == 1 then
		local r = 4 + math.random() * 8
		obj.radius = r
		obj.shape = G.collision.circle(r)
		obj.r, obj.g, obj.b = 80 + math.random(175), 60, 60
	elseif kind == 2 then
		local s = 6 + math.random() * 12
		obj.half = s
		obj.shape = G.collision.aabb(s * 2, s * 2)
		obj.r, obj.g, obj.b = 60, 80 + math.random(175), 60
	else
		local r = 5 + math.random() * 7
		obj.radius = r
		obj.shape = G.collision.circle(r)
		obj.r, obj.g, obj.b = 60, 60, 80 + math.random(175)
	end

	obj.handle = self.world:add(obj.shape, x, y)
	table.insert(self.objects, obj)
end

function Game:spawn_particles(x, y, count)
	for i = 1, count do
		local angle = math.random() * math.pi * 2
		local speed = 30 + math.random() * 80
		table.insert(self.particles, {
			x = x,
			y = y,
			vx = math.cos(angle) * speed,
			vy = math.sin(angle) * speed,
			life = 0.3 + math.random() * 0.5,
			r = 200 + math.random(55),
			g = 150 + math.random(105),
			b = 50 + math.random(100),
		})
	end
end

function Game:update(t, dt)
	if G.input.is_key_pressed("escape") then
		G.system.quit()
	end
	if G.input.is_key_pressed("r") then
		self:reset()
	end

	local counts = { 100, 300, 500, 1000, 2000 }
	for i, n in ipairs(counts) do
		if G.input.is_key_pressed(tostring(i)) then
			TARGET_COUNT = n
			self:reset()
		end
	end

	self.frame = self.frame + 1

	-- Physics: apply gravity and move objects.
	for _, obj in ipairs(self.objects) do
		obj.vy = obj.vy + GRAVITY_Y * dt
		obj.age = obj.age + dt

		local nx, ny, contacts = self.world:move_and_slide(obj.handle, obj.vx * dt, obj.vy * dt)
		obj.x, obj.y = nx, ny

		-- Bounce off contacts.
		if contacts and #contacts > 0 then
			local max_impact = 0
			for _, c in ipairs(contacts) do
				local dot = obj.vx * c.nx + obj.vy * c.ny
				if math.abs(dot) > max_impact then
					max_impact = math.abs(dot)
				end
				obj.vx = obj.vx - 1.8 * dot * c.nx
				obj.vy = obj.vy - 1.8 * dot * c.ny
			end
			-- Dampen.
			obj.vx = obj.vx * 0.95
			obj.vy = obj.vy * 0.95

			-- Spawn particles on hard impacts.
			if max_impact > 100 then
				self:spawn_particles(obj.x, obj.y, 3)
			end
		end
	end

	-- Spatial queries every few frames to add load.
	if self.frame % 4 == 0 then
		local qx = math.random() * self.w
		local qy = math.random() * self.h
		self.world:query_circle(qx, qy, 80)
		self.world:query_rect(qx - 60, qy - 60, qx + 60, qy + 60)
	end

	-- Raycast every frame.
	local ray_ox = self.w / 2
	local ray_oy = self.h / 2
	local ray_angle = t * 2
	local ray_dx = math.cos(ray_angle)
	local ray_dy = math.sin(ray_angle)
	self.last_ray_hits = self.world:raycast_all(ray_ox, ray_oy, ray_dx, ray_dy, 800)
	self.ray_ox, self.ray_oy = ray_ox, ray_oy
	self.ray_dx, self.ray_dy = ray_dx, ray_dy

	-- Update collision world.
	self.world:update()

	-- Update particles.
	local alive = {}
	for _, p in ipairs(self.particles) do
		p.life = p.life - dt
		if p.life > 0 then
			p.x = p.x + p.vx * dt
			p.y = p.y + p.vy * dt
			p.vy = p.vy + 200 * dt
			table.insert(alive, p)
		end
	end
	self.particles = alive

	-- Continuously spawn/remove to stress allocator.
	self.spawn_timer = self.spawn_timer + dt
	if self.spawn_timer > 0.1 and #self.objects < TARGET_COUNT then
		self.spawn_timer = 0
		self:spawn_batch(math.min(10, TARGET_COUNT - #self.objects))
	end
end

function Game:reset()
	for _, obj in ipairs(self.objects) do
		self.world:remove(obj.handle)
	end
	self.objects = {}
	self.particles = {}
	self.frame = 0
	self:spawn_batch(TARGET_COUNT)
end

function Game:draw()
	G.graphics.clear(0.05, 0.05, 0.08, 1)

	-- Draw walls.
	G.graphics.set_color(40, 40, 50, 255)
	for _, wall in ipairs(self.wall_shapes) do
		local shape = wall[1]
		local wx, wy = wall[2], wall[3]
		G.graphics.draw_rect(wx - self.w / 2, wy - 10, wx + self.w / 2, wy + 10)
	end

	-- Draw objects.
	for _, obj in ipairs(self.objects) do
		G.graphics.set_color(obj.r, obj.g, obj.b, 220)
		if obj.kind == 2 then
			local s = obj.half
			G.graphics.draw_rect(obj.x - s, obj.y - s, obj.x + s, obj.y + s)
		else
			G.graphics.draw_circle(obj.x, obj.y, obj.radius)
		end
	end

	-- Draw particles.
	for _, p in ipairs(self.particles) do
		local a = math.floor(255 * (p.life / 0.8))
		if a > 255 then
			a = 255
		end
		G.graphics.set_color(p.r, p.g, p.b, a)
		G.graphics.draw_circle(p.x, p.y, 2)
	end

	-- Draw raycast.
	if self.last_ray_hits then
		G.graphics.set_color(255, 255, 100, 40)
		local ex = self.ray_ox + self.ray_dx * 800
		local ey = self.ray_oy + self.ray_dy * 800
		G.graphics.draw_line(self.ray_ox, self.ray_oy, ex, ey)
		for _, hit in ipairs(self.last_ray_hits) do
			G.graphics.set_color(255, 50, 50, 180)
			G.graphics.draw_circle(hit.x, hit.y, 4)
		end
	end

	-- HUD.
	G.graphics.set_color(220, 220, 230, 255)
	G.graphics.print(
		string.format("Objects: %d   Particles: %d   Target: %d", #self.objects, #self.particles, TARGET_COUNT),
		16,
		16
	)
	G.graphics.print("1-5: set count (100-2000)   R: reset   F11: profiler   Esc: quit", 16, 38)
end

return Game
