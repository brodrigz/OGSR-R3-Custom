-- Run with the repository LuaJIT and the Game resource root as arg[1].
-- Optional arg[2]: extracted clean gamedata, to exercise its real fake-lens script.
local root = assert(arg[1], "missing resource root") .. "/gamedata/scripts/"
local passed = 0
local function check(value, message)
    assert(value, message)
    passed = passed + 1
end
local function load_module(name, path)
    local input = assert(io.open(path, "rb"))
    local source = input:read("*a")
    input:close()
    local header = "local this; module('" .. name .. "', package.seeall, function(m) this=m end);\n"
    assert(loadstring(header .. source, "@" .. path))()
    return assert(_G[name])
end

-- Validate script commands against startup exports, not a console accepting anything.
local engine_file = assert(io.open(arg[1] .. "/../../ogsr_engine/xr_3da/x_ray.cpp", "rb"))
local engine_source = engine_file:read("*a")
engine_file:close()
local exports = {}
for name in engine_source:gmatch('shader_exports%.set_custom_params%s*%(%s*"([^"]+)"') do
    exports[name] = true
end
local commands, sounds, camera_effects = {}, 0, 0
get_console = function() return { execute = function(_, c)
    check(exports[c:match("^(%S+)")], "laser command has no built-in shader export: " .. c)
    commands[#commands + 1] = c
end } end
sound_object = setmetatable({ s2d = 1 }, { __call = function()
    return { play = function() sounds = sounds + 1 end }
end })
level = { add_cam_effector = function() camera_effects = camera_effects + 1 end }
local wearing_nv, scope_nv = false, false
ogse_night_vision = {
    is_nv_working = function() return wearing_nv end,
    is_scope_nightvision_visual_active = function() return scope_nv end,
}
local active, alive, zoomed = nil, true, false
db = { actor = {
    alive = function() return alive end,
    active_item = function() return active end,
    zoom_mode = function() return zoomed end,
} }
local function weapon(id, legacy, native, on, alt)
    return {
        id = function() return id end,
        section = function() return "test_weapon" end,
        is_weapon = function() return true end,
        has_shader_laser = function() return legacy end,
        has_native_laser = function() return native end,
        on = on, alt = alt,
        get_laser_on = function(self) return self.on end,
        is_alt_aim = function(self) return self.alt end,
    }
end
local function expect(command)
    check(commands[#commands] == command, "unexpected laser parameters: " .. tostring(commands[#commands]))
end
local signals = {}
local manager = { subscribe = function(_, entry) signals[entry.signal] = entry.fun end }
local laser = load_module("rad_laser_control", root .. "rad_laser_control.script")
laser.attach(manager)
signals.on_spawn()
expect("shader_param_5 0, 0, 0, 0")
active = weapon(1, true, false, false, false)
signals.on_update()
active.on = true
signals.on_update()
expect("shader_param_5 1, 0, 0, 0")
check(sounds == 1 and camera_effects == 1, "one legacy toggle must play one set of effects")
local before = #commands
local format_calls, original_format = 0, string.format
string.format = function(...)
    format_calls = format_calls + 1
    return original_format(...)
end
signals.on_update()
check(#commands == before and sounds == 1, "unchanged frames must not spam commands/effects")
check(format_calls == 0, "unchanged laser state must not format a console command")
zoomed = true
signals.on_actor_weapon_zoom_in()
expect("shader_param_5 1, 0, 1, 0")
check(format_calls == 1, "a changed laser state must format exactly one command")
string.format = original_format
active.alt = true
signals.on_actor_weapon_alt_aim_switch(true)
expect("shader_param_5 1, 0, 0, 0")
wearing_nv = true
signals.on_update()
expect("shader_param_5 1, 1, 0, 0")
wearing_nv, scope_nv = false, true
signals.on_update()
expect("shader_param_5 1, 1, 0, 0")
scope_nv = false
-- A weapon switch must read the destination weapon's saved sight/laser state.
active = weapon(2, true, false, true, false)
signals.on_update()
expect("shader_param_5 1, 0, 1, 0")
check(sounds == 1, "equipping an already-enabled laser must be silent")
active = weapon(1, true, false, true, true)
signals.on_update()
expect("shader_param_5 1, 0, 0, 0")
signals.on_spawn()
signals.on_update()
expect("shader_param_5 1, 0, 0, 0")
check(sounds == 1, "restoring saved laser state must be silent")
active = weapon(3, false, true, true, false)
signals.on_update()
expect("shader_param_5 0, 0, 1, 1")
active.on = false
signals.on_update()
check(sounds == 1, "native laser effects belong to the engine")
active = { is_weapon = function() return false end }
signals.on_update()
expect("shader_param_5 0, 0, 0, 0")
active = weapon(4, true, false, true, false)
zoomed = false
signals.on_update()
active.on = false
signals.on_update()
expect("shader_param_5 0, 0, 0, 0")
check(sounds == 2, "switching off must play its effect")
alive = false
signals.on_death()
expect("shader_param_5 0, 0, 0, 0")
signals.on_before_destroy()
expect("shader_param_5 0, 0, 0, 0")

-- Exercise the dedicated-scope consumer of the restored alternate-aim signal.
alive, zoomed = true, true
active = weapon(5, false, false, false, false)
active.is_weapon_gl = function() return false end
active.is_3dss_enabled = function() return false end
active.has_scope_nightvision = function() return true end
active.get_scope_nightvision = function() return "scope_profile" end
active.get_weapon = function(self) return { UseScopeTexture = function() return not self.alt end } end
local starts, stops, restores = 0, 0, 0
ogse_night_vision.start_scope_nightvision_visual = function() starts = starts + 1; return true end
ogse_night_vision.stop_scope_nightvision_visual = function() stops = stops + 1 end
ogse_night_vision.suspend_wearable_nightvision_visual = function() end
ogse_night_vision.restore_wearable_nightvision_visual = function() restores = restores + 1 end
ogse_wpn_utils = { is_aiming_complete = function() return true end }
local scope = load_module("ogsr_scope_nightvision", root .. "ogsr_scope_nightvision.script")
signals = {}
scope.attach(manager)
wearing_nv = true
signals.on_actor_weapon_zoom_in(active)
signals.on_update()
check(starts == 1, "scope NV should activate after aiming completes")
active.alt = true
signals.on_actor_weapon_alt_aim_switch(true)
check(stops == 1 and restores == 1, "alternate sights should restore wearable NV")
active.alt = false
signals.on_actor_weapon_alt_aim_switch(false)
signals.on_update()
check(starts == 2, "returning to the scope should reactivate scope NV")
signals.on_actor_weapon_zoom_out(active)
check(stops == 2 and restores == 2, "zoom-out should restore wearable NV")

if arg[2] then
    -- Use the inherited script, not a reimplementation of its signal handler.
    ui_data = { load = function() return true end }
    local radius = nil
    cmd = function(c) radius = tonumber(c:match("^fake_scope_radius (.+)")) or radius end
    get_con_string = function() return "on" end
    get_string = function(_, key) return key == "scope_texture" and "test_scope" or "scope" end
    dsh = { timeout = function(_, fn) fn() end }
    scopeRadii = { scopeRadii = { test_scope = 0.4 } }
    ogse_wpn_utils.get_scope_status = function() return 1 end
    local fake = load_module("fakelens", arg[2] .. "/scripts/fakelens.script")
    signals = {}
    fake.attach(manager)
    active.alt = false
    signals.on_actor_weapon_zoom_in()
    check(radius == 0.4, "original fake lens should activate")
    active.alt = true
    signals.on_actor_weapon_alt_aim_switch(true)
    check(radius == 0, "alternate sights should clear the original fake lens")
    active.alt = false
    signals.on_actor_weapon_alt_aim_switch(false)
    check(radius == 0.4, "returning to scope should restore the original fake lens")
end
print("Passed " .. passed .. " laser/sight integration assertions.")
