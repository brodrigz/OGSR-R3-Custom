-- Isolated module contract probe, not a substitute for launching the game.
-- Invoke with Lua_JIT.exe and the path to ogsr_hud_animation_callbacks.script.
local path = assert(arg[1], "missing bridge script path")
local input = assert(io.open(path, "rb"))
local source = input:read("*a")
input:close()

-- Match CScriptEngine's FILE_HEADER and the minimum engine API used at load.
system_ini = function() return {} end
-- Clean R3's _g.script already installs the play callback. Preserve a sentinel
-- here to distinguish a redundant namespaced bridge from a missing base hook.
local baseline_play = function(anm_table) return anm_table end
CHudItem__PlayHUDMotion = baseline_play
local name = "ogsr_hud_animation_callbacks"
local wrapped = "local this; module('" .. name .. "', package.seeall, function(m) this=m end);\n" .. source
assert(loadstring(wrapped, "@" .. path))()
local bridge = assert(_G[name])
print("module.attach:", type(bridge.attach))
print("global play callback:", type(rawget(_G, "CHudItem__PlayHUDMotion")))
print("namespaced play callback:", type(rawget(bridge, "CHudItem__PlayHUDMotion")))
print("baseline global callback unchanged:", CHudItem__PlayHUDMotion == baseline_play)
print("global end callback:", type(rawget(_G, "CHudItem__OnAnimationEnd")))
local ok, error_text = pcall(function()
    assert(type(bridge.attach) == "function", "ogse_signals requires module.attach")
    bridge.attach({})
end)
print("isolated registration:", ok, error_text or "ok")
if not ok then os.exit(2) end
