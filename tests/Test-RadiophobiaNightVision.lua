-- Run with Lua_JIT.exe and the Game resource root as arg[1].
-- Optional arg[2]/arg[3]: clean R3 and Seb gamedata roots for real equipment configs.
local path = assert(arg[1]) .. '/gamedata/scripts/ogse/ogse_night_vision.script'
local controller_path = arg[1] .. '/../../ogsr_engine/xrGame/xr_level_controller.cpp'
local controller = assert(io.open(controller_path, 'rb'))
local controller_text = controller:read('*a')
controller:close()
assert(not controller_text:match('DEF_ACTION%s*%(%s*"night_vision_rad"'),
    'Radiophobia NV must not duplicate the dynamically registered custom action')
assert(controller_text:match('keyboard_section%s*=%s*"custom_keyboard_action"'),
    'Engine must load Radiophobia custom actions from system.ltx')
assert(controller_text:match('_stricmp%s*%(%s*_name%s*,%s*"night_vision"%s*%)'),
    'Engine must retain the vanilla night_vision profile alias')
assert(controller_text:match('_stricmp%s*%(%s*_name%s*,%s*"torch_rad"%s*%)'),
    'Engine must accept Radiophobia profiles that name the native torch action torch_rad')
local slots, commands = {}, {}
local alive, menu, animate = true, false, false
local timers, active = {}, nil
local movement_only, ladder_allowed = false, true
local function vec()
    return { set = function(self, x, y, z, w)
        self.x, self.y, self.z, self.w = x, y, z, w
        return self
    end }
end
vector, vector4 = vec, vec
math.clamp = function(v, lo, hi) return math.max(lo, math.min(hi, v)) end
local sound = { play = function() end, play_at_pos = function() end, playing = function() return false end }
xr_sound = { get_safe_sound_object = function() return sound end }
sound_object = setmetatable({ s2d = 1, looped = 2 }, { __call = function() return sound end })
db = { actor = {
    alive = function() return alive end,
    item_in_slot = function(_, slot) return slots[slot] end,
    active_item = function() return active end,
} }
level = {
    main_input_receiver = function() return menu end,
    add_pp_effector = function() end,
    add_cam_effector = function() end,
    only_allow_movekeys = function(value)
        assert(type(value) == 'boolean', 'only_allow_movekeys is a setter, not a getter')
        movement_only = value
    end,
    only_movekeys_allowed = function() return movement_only end,
    set_actor_allow_ladder = function(value) ladder_allowed = value end,
}
key_bindings = { kNIGHT_VISION_RAD = 101, kTORCH = 1 }
game = {
    hud_motion_allowed = function() return animate end,
    get_motion_length = function() return 1600 end,
    play_hud_motion = function() end,
    play_hud_anm = function() end,
    set_hud_anm_time = function() return 1 end,
}
dsh = { timeout = function(delay, fn) timers[#timers + 1] = { delay = delay, fn = fn } end }
get_string = function() return nil end
get_string_wq = function(section, key, default)
    if key == 'nightvision_sect' and section == 'nv_helmet' then return 'nv_profile' end
    return default
end
get_vector = function() return vec():set(0, 1, 0) end
get_vector4 = function(_, _, default) return default end
get_float = function(_, key, default) return key == 'r_pnv_mode' and 1 or default end
cmd = function(command, value) commands[#commands + 1] = command .. (value and ' ' .. value or '') end
local source = assert(io.open(path, 'rb'))
local text = source:read('*a')
source:close()
assert(loadstring("local this; module('nv_test', package.seeall, function(m) this=m end);\n" .. text, '@' .. path))()
local nv = nv_test
local animation_path = arg[1] .. '/gamedata/scripts/ogsr/ogsr_actor_animation.script'
local animation_source = assert(io.open(animation_path, 'rb'))
local animation_text = animation_source:read('*a')
animation_source:close()
assert(loadstring("local this; module('ogsr_actor_animation', package.seeall, function(m) this=m end);\n" .. animation_text, '@' .. animation_path))()
local signals = {}
nv.attach({ subscribe = function(_, entry) signals[entry.signal] = entry.fun end })
local function press(bind) signals.on_key_down(49, bind or key_bindings.kNIGHT_VISION_RAD) end
local function has_command(wanted)
    for _, c in ipairs(commands) do if c == wanted then return true end end
    return false
end
local function item(section) return { section = function() return section end } end

press()
assert(not nv.is_nv_working(), 'No NV equipment must remain off')
slots[6], slots[10] = item('plain_outfit'), item('nv_helmet')
press(key_bindings.kTORCH)
assert(not nv.is_nv_working(), 'Unrelated actions must not toggle NV')
press()
assert(nv.is_nv_working() and has_command('r_pnv_mode 1'), 'Helmet NV must reach the renderer')
press()
assert(not nv.is_nv_working() and commands[#commands] == 'r_pnv_mode 0', 'Second press must disable NV')
menu = true
press()
assert(not nv.is_nv_working(), 'Menus must block NV input')
menu, alive = false, false
press()
assert(not nv.is_nv_working(), 'Dead actors must not activate NV')
alive, animate = true, true
press()
assert(#timers == 1 and not nv.is_nv_working(), 'Animated activation must wait for its callback')
assert(movement_only and not ladder_allowed, 'Animation must acquire movement restrictions')
table.remove(timers, 1).fn()
assert(nv.is_nv_working(), 'Animation callback must activate helmet NV')
table.remove(timers, 1).fn()
assert(not movement_only and ladder_allowed, 'Animation must release movement restrictions')
press()
assert(#timers == 1 and nv.is_nv_working(), 'Animated deactivation must wait for its callback')
table.remove(timers, 1).fn()
assert(not nv.is_nv_working(), 'Animation callback must deactivate helmet NV')
table.remove(timers, 1).fn()
active = { section = function() return 'test_weapon' end, get_hud_item_state = function() return 7 end }
assert(not ogsr_actor_animation.allow_animation(), 'Busy weapon must not allow a hand animation')
press()
assert(nv.is_nv_working() and #timers == 0, 'Busy weapon must allow NV without a hand animation')
active.get_hud_item_state = function() return 0 end
assert(ogsr_actor_animation.allow_animation(), 'Idle weapon must allow a hand animation')
active = nil
movement_only = true
assert(not ogsr_actor_animation.allow_animation(), 'Movement restriction must block hand animations')
movement_only = false
nv.item_to_slot()
slots[10] = nil
nv.chek_drop_pnv()
assert(not nv.is_nv_working(), 'Removing the NV helmet must disable NV')
print('Wearable NV input, helmet selection, renderer commands and removal checks passed.')

local function read_profiles(config)
    local file = assert(io.open(config, 'rb'))
    local profiles, section = {}, nil
    for line in file:lines() do
        section = line:match('^%s*%[([^%]]+)%]') or section
        local profile = line:match('^%s*nightvision_sect%s*=%s*([^;%s]+)')
        if section and profile then profiles[section] = profile end
    end
    file:close()
    return profiles
end

if arg[2] and arg[3] then
    local system = assert(io.open(arg[2] .. '/config/system.ltx', 'rb'))
    local system_text = system:read('*a')
    system:close()
    assert(system_text:match('[\r\n]%s*night_vision_rad%s*=%s*kNIGHT_VISION_RAD%s*[\r\n]'),
        'Base R3 system.ltx must register the scripted NV action')
    local base = read_profiles(arg[2] .. '/config/misc/outfit.ltx')
    local seb = read_profiles(arg[3] .. '/config/misc/outfit.ltx')
    local devices = read_profiles(arg[3] .. '/config/misc/items.ltx')
    for section, profile in pairs(devices) do seb[section] = profile end
    local config = base
    get_string_wq = function(section, key, default)
        return key == 'nightvision_sect' and config[section] or default
    end
    animate = false
    slots[6], slots[9], slots[10] = nil, nil, item('helm_hardhat')
    assert(nv.get_nightvision_section_for_nv_fx() == 'effector_nightvision_bad', 'Base R3 Steel Helmet must supply NV')
    press()
    assert(nv.is_nv_working(), 'Base R3 helmet NV must activate')
    press()
    config = seb
    press()
    assert(not nv.is_nv_working(), 'Seb Steel Helmet alone must not gain built-in NV')
    slots[9] = item('device_torch')
    press()
    assert(not nv.is_nv_working(), 'Ordinary flashlight must not supply NV')
    for _, grade in ipairs({ 'bad', 'good', 'elite' }) do
        slots[9] = item('device_nvg_' .. grade)
        assert(nv.get_nightvision_section_for_nv_fx() == 'effector_nightvision_' .. grade, 'Wrong Seb device profile')
        nv.item_to_slot()
        press()
        assert(nv.is_nv_working(), 'Seb NVG device must activate')
        slots[9] = item('device_torch')
        nv.chek_drop_pnv()
        assert(not nv.is_nv_working(), 'Replacing Seb NVG device must stop NV')
    end
    config = base
    slots[10], slots[6] = nil, item('scientific_outfit')
    assert(base.scientific_outfit, 'Reference outfit must have built-in NV')
    press()
    assert(nv.is_nv_working(), 'Base R3 outfit NV must activate')
    press()
    print('Real R3 helmet/outfit and Seb separate-device configuration checks passed.')
end
