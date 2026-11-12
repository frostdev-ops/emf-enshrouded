-- What a player looks at: the camera ray from PlayerInput.fromClient.camera on the host's server world (the same
-- rule as the C++ SDK's emf/aim.hpp): the candidate within `range` of the player, in front of the camera, at most
-- `max_ray` metres from the ray, the closest to it (near ties: the nearer one along the ray).
local M = {}
local FIX = 4294967296.0  -- 1 m in the game's fixed-point positions
local BODY = 0.5          -- a creature's point: its position plus half a metre

function M.ray(world, player)
	local input = world:get("PlayerInput", player)
	if not input then return nil end
	local cam = input.fromClient.camera
	local q = cam.orientation
	local fx = 2 * (q.x * q.z + q.w * q.y)  -- the camera's +z axis
	local fy = 2 * (q.y * q.z - q.w * q.x)
	local fz = 1 - 2 * (q.x * q.x + q.y * q.y)
	local len = math.sqrt(fx * fx + fy * fy + fz * fz)
	if len < 1e-9 then return nil end
	return { ox = cam.position.x / FIX, oy = cam.position.y / FIX, oz = cam.position.z / FIX,
		dx = fx / len, dy = fy / len, dz = fz / len }
end

-- The entity under the player's crosshair for which filter(id) is true, or nil.
function M.pick(world, player, range, filter, max_ray)
	local ray = M.ray(world, player)
	if not ray then return nil end
	local px, py, pz = world:position(player)
	if not px then return nil end
	max_ray = max_ray or 1.5
	local best, best_perp, best_t
	for _, id in ipairs(world:entities()) do
		if id ~= player then
			local x, y, z = world:position(id)
			if x then
				y = y + BODY
				if math.sqrt((x - px) ^ 2 + (y - py) ^ 2 + (z - pz) ^ 2) <= range then
					local vx, vy, vz = x - ray.ox, y - ray.oy, z - ray.oz
					local t = vx * ray.dx + vy * ray.dy + vz * ray.dz
					if t > 0 then
						local ex, ey, ez = vx - ray.dx * t, vy - ray.dy * t, vz - ray.dz * t
						local perp = math.sqrt(ex * ex + ey * ey + ez * ez)
						if perp <= max_ray and filter(id) then
							if not best or perp < best_perp - 0.25 or (math.abs(perp - best_perp) <= 0.25 and t < best_t) then
								best, best_perp, best_t = id, perp, t
							end
						end
					end
				end
			end
		end
	end
	return best
end

-- The nearest entity (to the player) within `range` of them for which filter(id) is true, seen from the camera at most
-- `half_deg` degrees off where it looks; nil when there is none.
function M.cone(world, player, range, half_deg, filter)
	local ray = M.ray(world, player)
	if not ray then return nil end
	local px, py, pz = world:position(player)
	if not px then return nil end
	local min_cos = math.cos(math.rad(half_deg))
	local best, best_d
	for _, id in ipairs(world:entities()) do
		if id ~= player then
			local x, y, z = world:position(id)
			if x then
				local d = math.sqrt((x - px) ^ 2 + (y - py) ^ 2 + (z - pz) ^ 2)
				if d <= range and (not best_d or d < best_d) then
					local vx, vy, vz = x - ray.ox, y + BODY - ray.oy, z - ray.oz
					local len = math.sqrt(vx * vx + vy * vy + vz * vz)
					if len > 1e-6 and (vx * ray.dx + vy * ray.dy + vz * ray.dz) / len >= min_cos and filter(id) then
						best, best_d = id, d
					end
				end
			end
		end
	end
	return best
end

return M
