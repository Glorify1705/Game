-- Network test: multiplayer arena with colored squares.
-- Run the server first: server assets/
-- Then run this client: game run assets/ (select testnetwork from the menu)

local Game = {}

local colors = {
	{ 255, 80, 80 }, -- red
	{ 80, 255, 80 }, -- green
	{ 80, 80, 255 }, -- blue
	{ 255, 255, 80 }, -- yellow
	{ 255, 80, 255 }, -- magenta
	{ 80, 255, 255 }, -- cyan
}

function Game:init()
	G.window.set_title("Network Test - Connecting...")
	self.players = {}
	self.connected = false
	self.server_id = nil

	-- Proto schemas are compiled at pack time and loaded automatically.
	-- Just connect to the server.
	G.network.create_client()
	G.network.connect("127.0.0.1", 7777)
end

function Game:on_connect(peer_id)
	self.connected = true
	self.server_id = peer_id
	G.window.set_title("Network Test - Connected")
end

function Game:on_receive(peer_id, data, channel)
	if channel == 1 then
		-- Unreliable: game snapshot
		local snapshot = G.data.decode("GameSnapshot", data)
		self.players = {}
		for _, p in ipairs(snapshot.players) do
			self.players[p.peer_id] = p
		end
	end
end

function Game:on_disconnect(peer_id)
	self.connected = false
	self.server_id = nil
	self.players = {}
	G.window.set_title("Network Test - Disconnected")
end

function Game:update(t, dt)
	if G.input.is_key_pressed("escape") or G.input.is_key_pressed("q") then
		G.network.disconnect()
		G.system.quit()
		return
	end

	if not self.connected then
		return
	end

	-- Send input to server.
	local input = {
		left = G.input.is_key_down("left"),
		right = G.input.is_key_down("right"),
		up = G.input.is_key_down("up"),
		down = G.input.is_key_down("down"),
	}

	-- Press 1-6 to change color.
	for i = 1, 6 do
		if G.input.is_key_pressed(tostring(i)) then
			input.color = i
		end
	end

	local bytes = G.data.encode("PlayerInput", input)
	G.network.send(self.server_id, bytes, { channel = 0, reliable = true })
end

function Game:draw()
	G.graphics.clear(25, 25, 35, 255)

	-- Draw all players.
	for id, p in pairs(self.players) do
		local c = colors[p.color] or { 255, 255, 255 }
		G.graphics.set_color(c[1], c[2], c[3], 255)
		G.graphics.draw_rect(p.x - 16, p.y - 16, p.x + 16, p.y + 16)
	end

	-- Status text.
	G.graphics.set_color(255, 255, 255, 255)
	if self.connected then
		local count = 0
		for _ in pairs(self.players) do
			count = count + 1
		end
		G.graphics.print(
			"Connected - " .. count .. " player(s). Press 1-6 to change color.",
			10,
			10
		)
	else
		G.graphics.print("Connecting to 127.0.0.1:7777...", 10, 10)
	end
end

return Game
