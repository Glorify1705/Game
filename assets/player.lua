Object = require "classic"
Vec2 = require 'vector2d'
Physics = require 'physics'

Status = {
    ACCELERATING = 0,
    FULL_SPEED = 1,
    DECELERATING = 2,
    STOPPED = 3
}

FORCE = 50.000
ANGLE_DELTA = 20

Player = Object:extend()

function Player:new(x, y)
    self.image = "playerShip1_green"
    self.angle = 0
    local info = G.assets.subtexture_info(self.image)
    self.physics = Physics(x, y, x + info.width, y + info.height, self.angle)
end

function Player:update(dt)
    if G.input.is_key_down('w') then
        self.physics:apply_force(0, -FORCE)
    elseif G.input.is_key_down('s') then
        self.physics:apply_force(0, FORCE)
    end

    local a = math.pi - self.physics:angle()

    if G.input.is_key_down('d') then
        self.physics:apply_torque(ANGLE_DELTA)
    elseif G.input.is_key_down('a') then
        self.physics:apply_torque(-ANGLE_DELTA)
    end

    G.console.watch("Player Position", self.physics:position())
    G.console.watch("Player Angle", self.physics:angle())
    G.console.watch("Player Force", self.force)
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
