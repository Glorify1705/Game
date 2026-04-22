-- Dedicated server for the multiplayer arena test game.
-- Run with: server assets/
--
-- Proto schemas are compiled by the server binary at startup from
-- messages.proto. G.data.encode/decode and G.network.send/broadcast
-- are provided by the server runtime.

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
    local input = G.data.decode("PlayerInput", data)
    local player = self.players[peer_id]
    if not player then return end
    local dt = 1.0 / 20
    if input.left then player.x = player.x - config.speed * dt end
    if input.right then player.x = player.x + config.speed * dt end
    if input.up then player.y = player.y - config.speed * dt end
    if input.down then player.y = player.y + config.speed * dt end
    if input.color and input.color > 0 then player.color = input.color end
end

function _Server:tick(dt)
    local players = {}
    for id, player in pairs(self.players) do
        table.insert(players, {
            peer_id = id,
            x = player.x,
            y = player.y,
            color = player.color,
        })
    end
    if #players > 0 then
        local bytes = G.data.encode("GameSnapshot", {
            players = players,
            server_time = 0,
        })
        G.network.broadcast(bytes, {channel = 1, reliable = false})
    end
end

function _Server:on_disconnect(peer_id)
    self.players[peer_id] = nil
    print("Player " .. peer_id .. " disconnected")
end

function _Server:shutdown()
    print("Server shutting down")
end
