-- Lightweight finite state machine.
-- States are plain tables with optional enter(entity, prev), update(entity, dt),
-- exit(entity, next) callbacks.

local FSM = {}
FSM.__index = FSM

function FSM.new(states, initial)
	local self = setmetatable({}, FSM)
	self.states = states
	self.current_name = initial
	local state = states[initial]
	if state and state.enter then
		state.enter(nil, nil)
	end
	return self
end

function FSM:update(entity, dt)
	local state = self.states[self.current_name]
	if state and state.update then
		state.update(entity, dt)
	end
end

function FSM:transition(entity, next_state)
	if next_state == self.current_name then return end
	if not self.states[next_state] then return end
	local prev = self.current_name
	local old = self.states[prev]
	if old and old.exit then
		old.exit(entity, next_state)
	end
	self.current_name = next_state
	local new = self.states[next_state]
	if new and new.enter then
		new.enter(entity, prev)
	end
end

function FSM:current()
	return self.current_name
end

return FSM
