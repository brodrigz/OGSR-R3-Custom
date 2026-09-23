# Shared by the publisher and the release regression checks. No game install needed.
function Assert-RadiophobiaUI {
    param([Parameter(Mandatory)][string]$Root)

    foreach ($relative in @(
        'config/ui/maingame.xml', 'config/ui/maingame_16.xml',
        'config/ui/pda.xml', 'config/ui/pda_16.xml', 'config/ui/talk.xml', 'config/ui/talk_16.xml',
        'config/ui/ui_compass.xml', 'config/ui/ui_item_wheel.xml',
        'scripts/ui/ui_main_menu.script', 'scripts/ui/ui_mm_opt_main.script',
        'scripts/ui/ui_mm_opt_gameplay.script', 'scripts/ui/ui_mm_opt_video.script',
        'scripts/ui/ui_mm_opt_video_adv.script'
    )) {
        if (-not (Test-Path -LiteralPath (Join-Path $Root "gamedata/$relative") -PathType Leaf)) {
            throw "Required Radiophobia UI file is missing: $relative"
        }
    }
    foreach ($name in @('hud_interact_texd.xml', 'ui_item_wheel_descr.xml', 'ui_tactic_compass_descr.xml')) {
        $descriptor = [xml]::new()
        $descriptor.Load((Join-Path $Root "gamedata/config/ui/textures_descr/$name"))
        foreach ($file in $descriptor.SelectNodes('/w/file')) {
            $texture = Join-Path $Root ('gamedata/textures/' + $file.GetAttribute('name') + '.dds')
            if (-not (Test-Path -LiteralPath $texture -PathType Leaf)) { throw "Required UI texture is missing: $texture" }
        }
    }
    $uiPath = Join-Path $Root 'gamedata\config\ui\ui_mm_opt.xml'
    $ui = [xml]::new()
    $ui.Load($uiPath)
    $keys = [xml]::new()
    $keys.Load((Join-Path $Root 'gamedata\config\ui\ui_keybinding.xml'))
    foreach ($command in @('crouch', 'walk_toggle', 'crouch_low_toggle')) {
        if (-not $keys.SelectSingleNode("//*[@exe='$command']")) {
            throw "Radiophobia ui_keybinding.xml is missing $command."
        }
    }
    if ($keys.SelectSingleNode("//*[@exe='crouch_toggle']")) {
        throw 'Controls still expose the obsolete separate crouch-toggle bind.'
    }
    if ($keys.SelectSingleNode("//*[@exe='accel']")) {
        throw 'Controls still expose the obsolete contextual walk/low-crouch bind.'
    }
    $requiredPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($path in @(
        'main_dialog:tab_game_hud', 'main_dialog:tab_controls_mode', 'tab_hud:cap_sec_minimap',
        'tab_gameplay:list_economy', 'tab_gameplay:check_backpack_anim',
        'tab_sound:check_subtitles', 'tab_sound:cap_language', 'tab_sound:list_language',
        'tab_sound:cap_mastervolume', 'tab_sound:cap_musicvolume',
        'tab_sound:track_musicvolume', 'tab_sound:track_mastervolume',
        'tab_sound:cap_snd_device', 'tab_sound:list_snd_device', 'tab_sound:check_eax',
        'tab_controls:cap_mousesens', 'tab_controls:cap_keyboardsetup',
        'tab_controls:track_mousesens', 'tab_controls:check_mouseinvert',
        'tab_controls:key_binding', 'tab_controls:btn_default'
    )) { [void]$requiredPaths.Add($path) }

    # Check every literal XML path used by the shipped options scripts. Strip
    # Lua comments first so disabled controls do not become false requirements.
    $scripts = Get-ChildItem -LiteralPath (Join-Path $Root 'gamedata\scripts\ui') -Filter 'ui_mm_opt_*.script'
    foreach ($script in $scripts) {
        $text = [IO.File]::ReadAllText($script.FullName)
        $text = [regex]::Replace($text, '(?s)--\[\[.*?\]\]', '')
        $text = [regex]::Replace($text, '(?m)--[^\r\n]*', '')
        foreach ($match in [regex]::Matches($text, 'xml:Init\w+\(\s*"([\w:]+)"\s*[,)]')) {
            [void]$requiredPaths.Add($match.Groups[1].Value)
        }
        if ($script.Name -eq 'ui_mm_opt_video_adv.script') {
            foreach ($match in [regex]::Matches($text, '\{\s*"(\w+)"\s*,\s*"(track|list|check)"')) {
                [void]$requiredPaths.Add('video_adv:cap_' + $match.Groups[1].Value)
                [void]$requiredPaths.Add('video_adv:' + $match.Groups[2].Value + '_' + $match.Groups[1].Value)
            }
        }
    }
    foreach ($path in $requiredPaths) {
        if (-not $ui.SelectSingleNode('/window/' + $path.Replace(':', '/'))) {
            throw "Radiophobia options XML is missing script-required node: $path"
        }
    }
    if ($ui.SelectSingleNode('/window/background/texture').InnerText -ne 'ui\menu_video' -or
        $ui.SelectSingleNode('/window/main_dialog/dialog/texture').InnerText -ne 'a_gfx_shadow') {
        throw 'Options must retain the Radiophobia menu background and panel, not the SoC layout.'
    }
    if ($ui.SelectSingleNode('//options_item[@entry="g_alt_aim_remember"]')) {
        throw 'Options contain the removed g_alt_aim_remember command.'
    }
    foreach ($axis in @('x', 'y')) {
        if (-not $ui.SelectSingleNode("/window/tab_hud/track_font_scale_$axis/options_item[@entry='g_font_scale_$axis' and @group='mm_opt_gameplay']")) {
            throw "Missing HUD font control: g_font_scale_$axis"
        }
    }
    foreach ($action in @('aim', 'sprint', 'lean', 'crouch', 'walk', 'low_crouch')) {
        if (-not $ui.SelectSingleNode("/window/tab_input_behavior/list_$action/options_item[@entry='g_${action}_input_mode' and @group='mm_opt_input']")) {
            throw "Missing hold/toggle selector for $action."
        }
    }
    foreach ($oldEntry in @('wpn_aim_toggle', 'lean_toggle', 'g_sprint_hold')) {
        if ($ui.SelectSingleNode("//options_item[@entry='$oldEntry']")) {
            throw "Legacy hold/toggle setting remains outside Input Behavior: $oldEntry"
        }
    }
    foreach ($entry in @('r_aa_dlss_quality', 'r_aa_dlss_preset', 'r_aa_fsr3_quality', 'r_xegtao_bent_normals', 'r_ao_mode', 'r2_ssao')) {
        if (-not $ui.SelectSingleNode("//options_item[@entry='$entry' and @depend='vid']")) {
            throw "Options must restart video resources for $entry."
        }
    }

    foreach ($language in @('eng', 'rus')) {
        $ids = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        # R3 registers the menu/keybinding tables; the engine loads the HUD table.
        # Merely shipping another XML file does not register its translations.
        foreach ($name in @('ui_st_mm.xml', 'ui_st_keybinding.xml', 'ui_st_hud_interact.xml')) {
            $file = Get-Item -LiteralPath (Join-Path $Root "gamedata\config\text\$language\$name")
            # Some inherited string values contain engine-tolerated XML text
            # that System.Xml rejects. IDs have a simple, stable syntax, so
            # scan those directly while still catching cross-file duplicates.
            $bytes = [IO.File]::ReadAllBytes($file.FullName)
            $encoding = [Text.Encoding]::GetEncoding(1251)
            $text = $encoding.GetString($bytes)
            # Catch lost Cyrillic text: UTF-8 replacement markers or undefined CP1251 bytes.
            $replacementMarker = $encoding.GetString([byte[]](0xEF, 0xBF, 0xBD))
            if ($text.Contains($replacementMarker) -or [Array]::IndexOf($bytes, [byte]0x98) -ge 0) {
                throw "Corrupted $language UI translation encoding: $name"
            }
            foreach ($match in [regex]::Matches($text, '<string\s+id="([^"]+)"')) {
                $id = $match.Groups[1].Value
                if (-not $ids.Add($id)) {
                    throw "Duplicate $language UI translation: $id"
                }
            }
        }
        foreach ($id in @('ui_mm_dlss_quality', 'video_settings_name_70', 'video_settings_desc_70',
            'ui_mm_dlss_preset', 'video_settings_name_72', 'video_settings_desc_72',
            'st_xegtao', 'ui_mm_xegtao_bent_normals', 'video_settings_name_73', 'video_settings_desc_73',
            'st_minimap_pos_bl', 'st_minimap_pos_br', 'st_minimap_pos_tl', 'st_minimap_pos_tr', 'st_minimap_pos_off',
            'st_opt_fsr3', 'ui_mm_fsr3_quality', 'video_settings_name_71', 'video_settings_desc_71',
            'ui_mm_backpack_anim', 'ui_mm_hint_backpack_anim', 'ui_mm_sprint_hold', 'ui_mm_sticky_aim',
            'st_input_hold', 'st_input_toggle', 'ui_mm_tab_bindings', 'ui_mm_tab_input_behavior',
            'st_cap_list_economy', 'st_cap_list_difficulty', 'ui_mm_sec_minimap', 'ui_st_take_all_hint',
            'ui_mm_font_scale_x', 'ui_mm_font_scale_y')) {
            if (-not $ids.Contains($id)) { throw "Missing $language UI translation: $id" }
        }
    }
}
