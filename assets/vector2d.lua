local Object = require 'classic'

Vec2 = Object:extend()

function Vec2:new(x, y)
    self.x = x
    self.y = y
end

function Vec2.zero()
    return Vec2(0, 0)
end

function Vec2:__add(v)
    return Vec2(self.x + v.x, self.y + v.y)
end

function Vec2:__sub(v)
    return Vec2(self.x - v.x, self.y - v.y)
end

function Vec2:__mul(v)
    return Vec2(self.x * v, self.y * v)
end

function Vec2:__div(v)
    return Vec2(self.x / v, self.y / v)
end

function Vec2.dot(a, b)
    return a.x * b.x + a.y * b.y
end

function Vec2.distance2(a, b)
    return (b.x - a.x) ^ 2 + (b.y - a.y) ^ 2
end

function Vec2.distance(a, b)
    return math.sqrt(Vec2.distance(a, b))
end

function Vec2:length2()
    return self.x * self.x + self.y * self.y
end

function Vec2:length()
    return math.sqrt(self:length2())
end

function Vec2:normalized()
    return self / self:length()
end

function Vec2:normal()
    return Vec2(-self.y, self.x)
end

function Vec2:__tostring()
    return string.format("{ x = %f, y = %f }", self.x, self.y)
end

return Vec2
