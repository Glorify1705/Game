Object = require "classic"
Lume = require 'lume'
Vec2 = require 'vector2d'

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

Player = Object:extend()

function Player:new(x, y)
    self.image = "playerShip1_green"
    local info = G.assets.subtexture_info(self.image)
    self.pos = Vec2(x or 0, y or 0)
    self.aim = Vec2(0, 0)
    self.angle = 0
    self.speed = 0
    self.status = Status.STOPPED
end

function Player:update(dt)
    local aim = Vec2(G.input.mouse_position())
    if Vec2.distance2(self.pos, aim) > MIN_DISTANCE then
        self.aim_x = aim
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
        if G.input.is_key_down('d') then
            self.pos.x = self.pos.x + dt / 100
        elseif G.input.is_key_down('a') then
            self.pos.x = self.pos.x - dt / 100
        end
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

    local a = math.pi - self.angle
    self.pos = self.pos + Vec2(math.sin(a), math.cos(a)) * self.speed
end

function Player:render()
    G.renderer.draw_sprite(self.image, self.pos.x, self.pos.y, self.angle)
end

function Player:center_camera()
    local vx, vy = G.renderer.viewport()
    G.renderer.translate( -self.pos.x, -self.pos.y)
    local mx, my = G.input.mouse_wheel()
    local factor = 0.4 + my * 0.9;
    G.renderer.scale(factor, factor)
    G.renderer.rotate( -self.angle)
    G.renderer.translate(self.pos.x, self.pos.y)
    G.renderer.translate(vx / 2 - self.pos.x, vy / 2 - self.pos.y)
end

return Player
