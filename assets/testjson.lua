-- Test program for G.json and G.filesystem extensions. Run with:
--   game run --test -- testjson

local M = {}

function M:init()
  print("testjson: init")
end

function M:update(t, dt) end
function M:draw() end

local function test_decode_object()
  local err, result = G.json.decode('{"name":"hello","value":42}')
  G.test.assert_true(err == nil, "decode object: unexpected error: " .. tostring(err))
  G.test.assert_true(result.name == "hello", "decode object: name mismatch")
  G.test.assert_true(result.value == 42, "decode object: value mismatch")
  print("  decode object: PASS")
end

local function test_decode_array()
  local err, result = G.json.decode('[1, 2, 3]')
  G.test.assert_true(err == nil, "decode array: unexpected error")
  G.test.assert_true(#result == 3, "decode array: length mismatch")
  G.test.assert_true(result[1] == 1, "decode array: [1] mismatch")
  G.test.assert_true(result[2] == 2, "decode array: [2] mismatch")
  G.test.assert_true(result[3] == 3, "decode array: [3] mismatch")
  print("  decode array: PASS")
end

local function test_decode_nested()
  local err, result = G.json.decode('{"items":[{"id":1},{"id":2}],"count":2}')
  G.test.assert_true(err == nil, "decode nested: unexpected error")
  G.test.assert_true(result.count == 2, "decode nested: count mismatch")
  G.test.assert_true(#result.items == 2, "decode nested: items length")
  G.test.assert_true(result.items[1].id == 1, "decode nested: items[1].id")
  G.test.assert_true(result.items[2].id == 2, "decode nested: items[2].id")
  print("  decode nested: PASS")
end

local function test_decode_scalars()
  local err, result

  err, result = G.json.decode("true")
  G.test.assert_true(err == nil and result == true, "decode true")

  err, result = G.json.decode("false")
  G.test.assert_true(err == nil and result == false, "decode false")

  err, result = G.json.decode("null")
  G.test.assert_true(err == nil and result == nil, "decode null")

  err, result = G.json.decode("3.14")
  G.test.assert_true(err == nil, "decode number: error")
  G.test.assert_true(math.abs(result - 3.14) < 0.001, "decode number: value")

  err, result = G.json.decode('"hello"')
  G.test.assert_true(err == nil and result == "hello", "decode string")

  print("  decode scalars: PASS")
end

local function test_decode_malformed()
  local err, result = G.json.decode("{bad json")
  G.test.assert_true(err ~= nil, "decode malformed: expected error")
  G.test.assert_true(result == nil, "decode malformed: expected nil result")
  print("  decode malformed: PASS")
end

local function test_encode_object()
  local err, result = G.json.encode({name = "hello", value = 42})
  G.test.assert_true(err == nil, "encode object: unexpected error: " .. tostring(err))
  -- Decode it back to verify round-trip.
  local err2, decoded = G.json.decode(result)
  G.test.assert_true(err2 == nil, "encode object: decode failed")
  G.test.assert_true(decoded.name == "hello", "encode object: name mismatch")
  G.test.assert_true(decoded.value == 42, "encode object: value mismatch")
  print("  encode object: PASS")
end

local function test_encode_array()
  local err, result = G.json.encode({10, 20, 30})
  G.test.assert_true(err == nil, "encode array: unexpected error")
  local err2, decoded = G.json.decode(result)
  G.test.assert_true(err2 == nil, "encode array: decode failed")
  G.test.assert_true(#decoded == 3, "encode array: length")
  G.test.assert_true(decoded[1] == 10, "encode array: [1]")
  G.test.assert_true(decoded[3] == 30, "encode array: [3]")
  print("  encode array: PASS")
end

local function test_encode_nested()
  local data = {
    players = {
      {name = "alice", score = 100},
      {name = "bob", score = 200},
    },
    level = 5,
  }
  local err, json = G.json.encode(data)
  G.test.assert_true(err == nil, "encode nested: unexpected error")
  local err2, decoded = G.json.decode(json)
  G.test.assert_true(err2 == nil, "encode nested: decode failed")
  G.test.assert_true(decoded.level == 5, "encode nested: level")
  G.test.assert_true(#decoded.players == 2, "encode nested: players count")
  G.test.assert_true(decoded.players[1].name == "alice", "encode nested: alice")
  G.test.assert_true(decoded.players[2].score == 200, "encode nested: bob score")
  print("  encode nested: PASS")
end

local function test_encode_scalars()
  local err, result

  err, result = G.json.encode(true)
  G.test.assert_true(err == nil and result == "true", "encode true")

  err, result = G.json.encode(false)
  G.test.assert_true(err == nil and result == "false", "encode false")

  err, result = G.json.encode(42)
  G.test.assert_true(err == nil and result == "42", "encode number")

  err, result = G.json.encode("hello")
  G.test.assert_true(err == nil and result == '"hello"', "encode string")

  print("  encode scalars: PASS")
end

local function test_encode_empty()
  -- Empty table: lua_objlen returns 0, no key 1, so encodes as object.
  local err, result = G.json.encode({})
  G.test.assert_true(err == nil, "encode empty: unexpected error")
  G.test.assert_true(result == "{}", "encode empty: expected {}, got: " .. tostring(result))
  print("  encode empty: PASS")
end

-- PhysFS paths: spit/delete use paths relative to the write dir (bare names).
-- slurp/stat/exists use the virtual mount path (/app/ prefix).
-- PhysFS: the write dir is mounted at /app for reading.
-- Writes (spit) and deletes use bare paths relative to write dir.
-- Reads (slurp, stat, exists) use the /app/ mount prefix.
local function wp(name) return name end               -- write path
local function rp(name) return "/app/" .. name end     -- read path

local function test_filesystem_stat()
  local werr = G.filesystem.spit(wp("testjson_tmp.txt"), "hello world")
  G.test.assert_true(werr == nil, "stat: spit failed: " .. tostring(werr))

  local err, info = G.filesystem.stat(rp("testjson_tmp.txt"))
  G.test.assert_true(err == nil, "stat: unexpected error: " .. tostring(err))
  G.test.assert_true(info.type == "file", "stat: expected type file")
  G.test.assert_true(info.size == 11, "stat: expected size 11, got " .. tostring(info.size))
  G.test.assert_true(info.modtime ~= nil, "stat: modtime missing")
  print("  filesystem.stat: PASS")
end

local function test_filesystem_delete()
  local werr = G.filesystem.spit(wp("testjson_del.txt"), "delete me")
  G.test.assert_true(werr == nil, "delete: spit failed")
  G.test.assert_true(G.filesystem.exists(rp("testjson_del.txt")), "delete: file should exist")

  local err = G.filesystem.delete(wp("testjson_del.txt"))
  G.test.assert_true(err == nil, "delete: unexpected error: " .. tostring(err))
  G.test.assert_true(not G.filesystem.exists(rp("testjson_del.txt")), "delete: file should be gone")

  -- Deleting again should be a no-op, not an error.
  local err2 = G.filesystem.delete(wp("testjson_del.txt"))
  G.test.assert_true(err2 == nil, "delete: second delete should succeed")
  print("  filesystem.delete: PASS")
end

local function test_json_file_roundtrip()
  local data = {greeting = "hello", numbers = {1, 2, 3}, flag = true}
  local err, json = G.json.encode(data)
  G.test.assert_true(err == nil, "roundtrip: encode failed")

  local werr = G.filesystem.spit(wp("testjson_rt.json"), json)
  G.test.assert_true(werr == nil, "roundtrip: spit failed")

  local buf, rerr = G.filesystem.slurp(rp("testjson_rt.json"))
  G.test.assert_true(rerr == nil, "roundtrip: slurp failed: " .. tostring(rerr))

  local derr, result = G.json.decode(tostring(buf))
  G.test.assert_true(derr == nil, "roundtrip: decode failed")
  G.test.assert_true(result.greeting == "hello", "roundtrip: greeting")
  G.test.assert_true(#result.numbers == 3, "roundtrip: numbers length")
  G.test.assert_true(result.flag == true, "roundtrip: flag")

  -- Clean up.
  G.filesystem.delete(wp("testjson_rt.json"))
  print("  json file roundtrip: PASS")
end

function M:test_inputs()
  print("testjson: starting")
  G.test.wait_frames(1)

  print("testjson: G.json.decode")
  test_decode_object()
  test_decode_array()
  test_decode_nested()
  test_decode_scalars()
  test_decode_malformed()

  print("testjson: G.json.encode")
  test_encode_object()
  test_encode_array()
  test_encode_nested()
  test_encode_scalars()
  test_encode_empty()

  print("testjson: G.filesystem extensions")
  test_filesystem_stat()
  test_filesystem_delete()

  print("testjson: file roundtrip")
  test_json_file_roundtrip()

  -- Clean up the stat test file.
  G.filesystem.delete(wp("testjson_tmp.txt"))

  print("testjson: ALL PASSED")
end

return M
