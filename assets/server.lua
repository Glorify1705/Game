-- Dedicated server for the multiplayer arena test game.
-- Run with: server assets/

local pb = require "pb"
local protoc = require "protoc"

-- Load proto schema.
local p = protoc.new()
assert(p:load(io.open("assets/messages.proto"):read("*a")))

local config = {
    speed = 200,
}

_Server = {}

function _Server:init()
    self.players = {}
    self.next_color = 1
    print("Server ready. Waiting for connections...")
end

function _Server:on_connect(peer_id)
    local color = self.next_color
    self.next_color = (self.next_color % 6) + 1
    self.players[peer_id] = {x = 400, y = 300, color = color}
    print("Player " .. peer_id .. " connected (color " .. color .. ")")
end

function _Server:on_receive(peer_id, data, channel)
    local input = pb.decode("PlayerInput", data)
    local p = self.players[peer_id]
    if not p then return end
    -- TODO: use actual dt from tick
    local dt = 1.0 / 20
    if input.left then p.x = p.x - config.speed * dt end
    if input.right then p.x = p.x + config.speed * dt end
    if input.up then p.y = p.y - config.speed * dt end
    if input.down then p.y = p.y + config.speed * dt end
    if input.color and input.color > 0 then p.color = input.color end
end

function _Server:tick(dt)
    -- Build snapshot.
    local players = {}
    for id, p in pairs(self.players) do
        table.insert(players, {
            peer_id = id,
            x = p.x,
            y = p.y,
            color = p.color,
        })
    end
    if #players > 0 then
        local snapshot = pb.encode("GameSnapshot", {
            players = players,
            server_time = 0,
        })
        -- Broadcast to all peers via ENet (exposed as a global by the server binary).
        -- For now, the server binary handles this via the tick callback return.
        -- TODO: expose G.network.broadcast to server Lua.
    end
end

function _Server:on_disconnect(peer_id)
    self.players[peer_id] = nil
    print("Player " .. peer_id .. " disconnected")
end

function _Server:shutdown()
    print("Server shutting down")
end
