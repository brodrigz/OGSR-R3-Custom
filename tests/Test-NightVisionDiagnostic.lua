-- Run after New-NightVisionDiagnostic.ps1, with the Game resource root as arg[1].
local diagnostic = 'release/nvg-diagnostic-2/gamedata/'
local messages = {}
local original_open = io.open

-- Match Radiophobia's logging behavior: printf goes through a disabled log.
log = function() end
printf = function(format, ...) log(string.format(format, ...)) end
log1 = function(message) messages[#messages + 1] = message end
io.open = function(name, ...)
    if name:match('/gamedata/scripts/ogse/ogse_night_vision.script$') then
        name = diagnostic .. 'scripts/ogse/ogse_night_vision.script'
    end
    return original_open(name, ...)
end
dofile('tests/Test-RadiophobiaNightVision.lua')
io.open = original_open
assert(messages[1]:find('[NVDBG D2] diagnostic script attached', 1, true))
local profile = false
for _, message in ipairs(messages) do
    if message:find('[NVDBG D2] profile=', 1, true) then profile = true end
end
assert(profile, 'Profile diagnostics must survive the disabled gameplay log')

-- Shader bindings execute in a separate VM with a native log function.
local binding_message, shader_name
log = function(message) binding_message = message end
local shader = setmetatable({}, { __index = function(_, method)
    return function(self, ...)
        if method == 'begin' then shader_name = select(2, ...) end
        return self
    end
end })
dofile(diagnostic .. 'shaders/r3/ogsr_nightvision.s')
element_0(shader)
assert(binding_message:find('[NVDBG D2] renderer binding loaded', 1, true))
assert(shader_name == 'ogsr_nightvision_diagnostic_d2')
local file = assert(io.open(diagnostic .. 'shaders/r3/' .. shader_name .. '.ps', 'rb'))
file:close()
print('Diagnostic logging survives disabled printf; shader binding selects the unique diagnostic shader.')
