local M = {}

local MAX_HIGH_SCORES = 10
local SCORES_WRITE_PATH = "highscores.json"
local SCORES_READ_PATH = "/app/highscores.json"

function M.load()
	if not G.filesystem.exists(SCORES_READ_PATH) then
		return {}
	end
	local buf, rerr = G.filesystem.slurp(SCORES_READ_PATH)
	if rerr then return {} end
	local err, result = G.json.decode(tostring(buf))
	if err or type(result) ~= "table" then return {} end
	return result
end

function M.save(scores)
	local err, json = G.json.encode(scores)
	if err then return end
	G.filesystem.spit(SCORES_WRITE_PATH, json)
end

function M.insert(scores, name, score)
	scores[#scores + 1] = { name = name, score = score }
	-- Sort descending by score.
	for i = #scores - 1, 1, -1 do
		if scores[i + 1].score > scores[i].score then
			scores[i], scores[i + 1] = scores[i + 1], scores[i]
		end
	end
	-- Trim to max.
	while #scores > MAX_HIGH_SCORES do
		scores[#scores] = nil
	end
	return scores
end

function M.qualifies(scores, score)
	if #scores < MAX_HIGH_SCORES then return true end
	return score > scores[#scores].score
end

return M
