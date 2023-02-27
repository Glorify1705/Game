Object = require "classic"
Lume = require 'lume'

Status = {
    ACCELERATING = 0,
    FULL_SPEED = 1,
    DECELERATING = 2,
    STOPPED = 3
}

ACCELERATION = 0.30
MAX_SPEED = 10;
MIN_DISTANCE = 2000.0
ANGLE_DELTA = 0.1

local function length2(x, y)
    return x * x + y * y
end

local function distance2(x0, y0, x1, y1)
    return length2(x1 - x0, y1 - y0)
end

Player = Object:extend()

function Player:new(x, y)
    self.x = x or 0
    self.y = y or 0
    self.aim_x = 0
    self.aim_y = 0
    self.angle = 0
    self.speed = 0
    self.status = Status.STOPPED
end

function Player:update(dt)
    local aim_x, aim_y = G.input.mouse_position()
    if distance2(self.x, self.y, aim_x, aim_y) > MIN_DISTANCE then
        self.aim_x = aim_x
        self.aim_y = aim_y
    end

    if G.input.is_key_down('w') then
        if self.status == Status.STOPPED or self.status == Status.DECELERATING then
            self.status = Status.ACCELERATING
        end
    else
        if self.status == Status.ACCELERATING or self.status == Status.FULL_SPEED then
            self.status = Status.DECELERATING
        end
    end

    if G.input.is_key_down('lshift') then
        G.console.log("Is down")
    else
        if G.input.is_key_down('d') then
            self.angle = self.angle + ANGLE_DELTA
        elseif G.input.is_key_down('a') then
            self.angle = self.angle - ANGLE_DELTA
        end
    end

    if self.status == Status.ACCELERATING then
        self.speed = self.speed + ACCELERATION * dt
        if self.speed > MAX_SPEED then
            self.speed = MAX_SPEED
            self.status = Status.FULL_SPEED
        end
    elseif self.status == Status.DECELERATING then
        self.speed = self.speed - ACCELERATION * dt
        if self.speed < 0 then
            self.speed = 0
            self.status = Status.STOPPED
        end
    end

    self.x = self.x + math.sin(math.pi - self.angle) * self.speed
    self.y = self.y + math.cos(math.pi - self.angle) * self.speed
end

function Player:render()
    G.renderer.draw_sprite("playerShip1_green", self.x, self.y, self.angle)
end

function Player:center_camera()
    local vx, vy = G.renderer.viewport()
    G.renderer.translate( -self.x, -self.y)
    local mx, my = G.input.mouse_wheel()
    local factor = 0.4 + my * 0.9;
    G.renderer.scale(factor, factor)
    G.renderer.rotate( -self.angle)
    G.renderer.translate(self.x, self.y)
    G.renderer.translate(vx / 2 - self.x, vy / 2 - self.y)
end

return Player
