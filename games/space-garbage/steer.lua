-- Steering behaviors for 2D AI.
-- Each function returns (fx, fy) -- a force vector suitable for physics:apply_force().

local sqrt = math.sqrt
local cos = math.cos
local sin = math.sin
local atan2 = math.atan2

local steer = {}

-- Wrap a delta for toroidal worlds so it takes the shortest path.
local function wrap_delta(dx, size)
	if dx > size / 2 then return dx - size end
	if dx < -size / 2 then return dx + size end
	return dx
end

-- Normalize (dx, dy) and scale to magnitude. Returns (0, 0) if zero-length.
local function direction(dx, dy, magnitude)
	local len = sqrt(dx * dx + dy * dy)
	if len < 0.001 then return 0, 0 end
	return dx / len * magnitude, dy / len * magnitude
end

-- Steer toward target.
function steer.seek(ax, ay, tx, ty, max_force)
	return direction(tx - ax, ty - ay, max_force)
end

-- Steer away from target.
function steer.flee(ax, ay, tx, ty, max_force)
	return direction(ax - tx, ay - ty, max_force)
end

-- Steer toward target, decelerating within slow_radius.
function steer.arrive(ax, ay, tx, ty, max_force, slow_radius)
	local dx = tx - ax
	local dy = ty - ay
	local dist = sqrt(dx * dx + dy * dy)
	if dist < 0.001 then return 0, 0 end
	local force = max_force
	if dist < slow_radius then
		force = max_force * (dist / slow_radius)
	end
	return dx / dist * force, dy / dist * force
end

-- Predictive pursuit: steer toward where the target will be.
function steer.pursue(ax, ay, tx, ty, tvx, tvy, max_force)
	local dx = tx - ax
	local dy = ty - ay
	local dist = sqrt(dx * dx + dy * dy)
	local prediction = dist / (max_force + 0.001)
	return steer.seek(ax, ay, tx + tvx * prediction, ty + tvy * prediction, max_force)
end

-- Seek with toroidal world wrapping.
function steer.seek_toroidal(ax, ay, tx, ty, max_force, ww, wh)
	local dx = wrap_delta(tx - ax, ww)
	local dy = wrap_delta(ty - ay, wh)
	return direction(dx, dy, max_force)
end

-- Predictive pursuit with toroidal wrapping.
function steer.pursue_toroidal(ax, ay, tx, ty, tvx, tvy, max_force, ww, wh)
	local dx = wrap_delta(tx - ax, ww)
	local dy = wrap_delta(ty - ay, wh)
	local dist = sqrt(dx * dx + dy * dy)
	local prediction = dist / (max_force + 0.001)
	local ptx = ax + dx + tvx * prediction
	local pty = ay + dy + tvy * prediction
	return steer.seek(ax, ay, ptx, pty, max_force)
end

-- Separation: push away from neighbors to prevent stacking.
-- neighbors is a list of {x, y} tables.
function steer.separate_toroidal(ax, ay, neighbors, desired_dist, max_force, ww, wh)
	local fx, fy = 0, 0
	local count = 0
	for i = 1, #neighbors do
		local n = neighbors[i]
		local dx = wrap_delta(ax - n.x, ww)
		local dy = wrap_delta(ay - n.y, wh)
		local dist = sqrt(dx * dx + dy * dy)
		if dist > 0.001 and dist < desired_dist then
			local weight = (desired_dist - dist) / desired_dist
			fx = fx + (dx / dist) * weight
			fy = fy + (dy / dist) * weight
			count = count + 1
		end
	end
	if count > 0 then
		fx = fx / count
		fy = fy / count
		return direction(fx, fy, max_force)
	end
	return 0, 0
end

return steer
