Object = require "classic"
Lume = require 'lume'
Vec2 = require 'vector2d'
Physics = require 'physics'

Status = {
    ACCELERATING = 0,
    FULL_SPEED = 1,
    DECELERATING = 2,
    STOPPED = 3
}

ACCELERATION = 1200.00
MAX_SPEED = 90000;
MIN_DISTANCE = 2000.0
ANGLE_DELTA = 0.3

Player = Object:extend()

function Player:new(x, y)
    self.image = "playerShip1_green"
    local info = G.assets.subtexture_info(self.image)
    self.physics = Physics(100, 100, 199, 175, 0)
    self.speed = 0
    self.status = Status.STOPPED
end

function Player:update(dt)
    if G.input.is_key_down('w') then
        if self.status == Status.STOPPED or self.status == Status.DECELERATING then
            self.status = Status.ACCELERATING
        end
    else
        if self.status == Status.ACCELERATING or self.status == Status.FULL_SPEED then
            self.status = Status.DECELERATING
        end
    end

    if G.input.is_key_down('d') then
        self.physics:rotate(ANGLE_DELTA)
    elseif G.input.is_key_down('a') then
        self.physics:rotate(-ANGLE_DELTA)
    end

    if self.status == Status.ACCELERATING then
        self.speed = self.speed + ACCELERATION * dt / 1000.0
        if self.speed > MAX_SPEED then
            self.speed = MAX_SPEED
            self.status = Status.FULL_SPEED
        end
    elseif self.status == Status.DECELERATING then
        self.speed = self.speed - ACCELERATION * dt / 1000.0
        if self.speed < 0 then
            self.speed = 0
            self.status = Status.STOPPED
        end
    end

    local a = math.pi - self.physics:angle()
    self.physics:apply_force(math.sin(a) * self.speed, math.cos(a) * self.speed)

    G.console.watch("Player Position", self.physics:position())
    G.console.watch("Player Angle", self.physics:angle())
end

function Player:render()
    local v = self.physics:position()
    local angle = self.physics:angle()
    G.renderer.draw_sprite(self.image, v.x, v.y, angle)
end

function Player:center_camera()
    local vx, vy = G.renderer.viewport()
    local x, y = G.physics.position(self.physics)
    local angle = G.physics.angle(self.physics)
    G.renderer.translate(-x, -y)
    local mx, my = G.input.mouse_wheel()
    local factor = 0.4 + my * 0.9;
    G.renderer.scale(factor, factor)
    G.renderer.rotate(-angle)
    G.renderer.translate(x, y)
    G.renderer.translate(vx / 2 - x, vy / 2 - y)
end

return Player
