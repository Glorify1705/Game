Entity = require 'entity'

FORCE = 50.000
ANGLE_DELTA = 20

Player = Entity:extend()

function Player:new(x, y)
    Player.super.new(self, x, y, 0, "playerShip1_green", "player")
    self.health = 100
    self.cooldown = 0
end

function Player:update(dt)
    self.cooldown = self.cooldown - dt
    if self.cooldown < 0 then self.cooldown = 0 end

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
end

function Player:on_collision(other)
    if self.cooldown == 0 then
        self.health = self.health - 10
        self.cooldown = 3
    end
end

function Player:is_player()
    return true
end

function Player:get_health()
    return self.health
end

function Player:center_camera()
    local vx, vy = G.window.dimensions()
    local x, y = G.physics.position(self.physics)
    local angle = G.physics.angle(self.physics)
    G.graphics.translate(-x, -y)
    local mx, my = G.input.mouse_wheel()
    local factor = 0.4 + my * 0.9;
    G.graphics.scale(factor, factor)
    G.graphics.rotate(-angle)
    G.graphics.translate(x, y)
    G.graphics.translate(vx / 2 - x, vy / 2 - y)
end

return Player
