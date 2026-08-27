//////////////////////////////////////////////////////////////////////
// HudSound.cpp:	структура для работы со звуками применяемыми в
//					HUD-объектах (обычные звуки, но с доп. параметрами)
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "HudSound.h"
#include "../xr_3da/x_ray.h"

void HUD_SOUND::LoadSound(LPCSTR section, LPCSTR line, HUD_SOUND& hud_snd, int type)
{
    hud_snd.m_activeSnd = NULL;
    hud_snd.sounds.clear();

    string256 sound_line;
    strcpy_s(sound_line, line);
    int k = 0;
    while (pSettings->line_exist(section, sound_line))
    {
        SSnd& s = hud_snd.sounds.emplace_back();

        LoadSound(section, sound_line, s.snd, type, &s.volume, &s.delay, &s.freq);
        sprintf_s(sound_line, "%s%d", line, ++k);
    } // while

    ASSERT_FMT(!hud_snd.sounds.empty(), "there is no sounds [%s] for [%s]", line, section);
}

void HUD_SOUND::LoadSound(LPCSTR section, LPCSTR line, ref_sound& snd, int type, float* volume, float* delay, float* freq)
{
    LPCSTR str = pSettings->r_string(section, line);
    string256 buf_str;

    int count = _GetItemCount(str);
    R_ASSERT(count);

    _GetItem(str, 0, buf_str);
    snd.create(buf_str, st_Effect, type);

    if (volume != NULL)
    {
        *volume = 1.f;
        if (count > 1)
        {
            _GetItem(str, 1, buf_str);
            if (xr_strlen(buf_str) > 0)
                *volume = (float)atof(buf_str);
        }
    }

    if (delay != NULL)
    {
        *delay = 0;
        if (count > 2)
        {
            _GetItem(str, 2, buf_str);
            if (xr_strlen(buf_str) > 0)
                *delay = (float)atof(buf_str);
        }
    }

    if (freq != NULL)
    {
        *freq = 1.f;
        if (count > 3)
        {
            _GetItem(str, 3, buf_str);
            if (xr_strlen(buf_str) > 0)
                *freq = (float)atof(buf_str);
        }
    }
}

void HUD_SOUND::DestroySound(HUD_SOUND& hud_snd)
{
    xr_vector<SSnd>::iterator it = hud_snd.sounds.begin();
    for (; it != hud_snd.sounds.end(); ++it)
        (*it).snd.destroy();
    hud_snd.sounds.clear();

    hud_snd.m_activeSnd = NULL;
}

void HUD_SOUND::PlaySound(HUD_SOUND& hud_snd, const Fvector& position, const CObject* parent, bool b_hud_mode, bool looped, bool overlap)
{
    if (hud_snd.sounds.empty())
        return;

    if (!overlap)
        StopSound(hud_snd);

    u32 flags = b_hud_mode ? sm_2D : 0;
    if (looped)
        flags |= sm_Looped;

    hud_snd.m_activeSnd = &hud_snd.sounds[Random.randI(hud_snd.sounds.size())];
    float freq = hud_snd.m_activeSnd->freq;
    Fvector pos = (flags & sm_2D) ? Fvector{} : position;

    static const float hud_vol = READ_IF_EXISTS(pSettings, r_float, "hud_sound", "hud_sound_vol_k", 1.0f);
    float vol = hud_snd.m_activeSnd->volume * (b_hud_mode ? hud_vol : 1.0f);

    if (overlap)
    {
        hud_snd.m_activeSnd->snd.play_no_feedback(const_cast<CObject*>(parent), flags, hud_snd.m_activeSnd->delay, &pos, &vol , &freq);
    }
    else
    {
        hud_snd.m_activeSnd->snd.play_at_pos(const_cast<CObject*>(parent), pos, flags, hud_snd.m_activeSnd->delay);
        hud_snd.m_activeSnd->snd.set_volume(vol);
        hud_snd.m_activeSnd->snd.set_frequency(freq);
    }
}

void HUD_SOUND::StopSound(HUD_SOUND& hud_snd)
{
    for (auto& sound : hud_snd.sounds)
        sound.snd.stop();

    hud_snd.m_activeSnd = nullptr;
}

HUD_SOUND_COLLECTION::~HUD_SOUND_COLLECTION()
{
    StopAllSounds();
    for (auto& sound : m_sound_items)
        HUD_SOUND::DestroySound(sound);
}

HUD_SOUND* HUD_SOUND_COLLECTION::FindSoundItem(LPCSTR alias, const bool assert_if_missing)
{
    const auto it = std::find(m_sound_items.begin(), m_sound_items.end(), alias);
    if (it != m_sound_items.end())
        return &*it;

    R_ASSERT3(!assert_if_missing, "sound item not found in collection", alias);
    return nullptr;
}

void HUD_SOUND_COLLECTION::PlaySound(LPCSTR alias, const Fvector& position, const CObject* parent,
    const bool hud_mode, const bool looped, const u8 index)
{
    for (auto& sound : m_sound_items)
    {
        if (sound.m_b_exclusive)
            HUD_SOUND::StopSound(sound);
    }

    HUD_SOUND* sound = FindSoundItem(alias, true);
    if (!sound || sound->sounds.empty())
        return;

    // OGSR 3.529's HUD_SOUND carries an overlap flag where the original
    // layered API carried a variant index. Variant selection remains random;
    // non-exclusive shot layers are allowed to overlap.
    (void)index;
    HUD_SOUND::PlaySound(*sound, position, parent, hud_mode, looped, !sound->m_b_exclusive);
}

void HUD_SOUND_COLLECTION::StopSound(LPCSTR alias)
{
    if (HUD_SOUND* sound = FindSoundItem(alias, false))
        HUD_SOUND::StopSound(*sound);
}

void HUD_SOUND_COLLECTION::StopAllSounds()
{
    for (auto& sound : m_sound_items)
        HUD_SOUND::StopSound(sound);
}

void HUD_SOUND_COLLECTION::SetPosition(LPCSTR alias, const Fvector& position)
{
    if (HUD_SOUND* sound = FindSoundItem(alias, false); sound && sound->playing())
        sound->set_position(position);
}

static void LoadCollectionSound(const CInifile* ini, LPCSTR section, LPCSTR line, HUD_SOUND& sound, int type)
{
    CInifile* source = const_cast<CInifile*>(ini);
    sound.m_activeSnd = nullptr;
    sound.sounds.clear();

    string256 sound_line;
    xr_strcpy(sound_line, line);
    int suffix = 0;
    while (source->line_exist(section, sound_line))
    {
        HUD_SOUND::SSnd& item = sound.sounds.emplace_back();
        LPCSTR value = source->r_string(section, sound_line);
        string256 token;
        const int count = _GetItemCount(value);
        R_ASSERT(count);

        _GetItem(value, 0, token);
        item.snd.create(token, st_Effect, type);
        item.volume = 1.f;
        item.delay = 0.f;
        item.freq = 1.f;
        if (count > 1 && xr_strlen(_GetItem(value, 1, token)))
            item.volume = static_cast<float>(atof(token));
        if (count > 2 && xr_strlen(_GetItem(value, 2, token)))
            item.delay = static_cast<float>(atof(token));
        if (count > 3 && xr_strlen(_GetItem(value, 3, token)))
            item.freq = static_cast<float>(atof(token));
        xr_sprintf(sound_line, "%s%d", line, ++suffix);
    }

    ASSERT_FMT(!sound.sounds.empty(), "there is no sounds [%s] for [%s]", line, section);
}

void HUD_SOUND_COLLECTION::LoadSound(LPCSTR section, LPCSTR line, LPCSTR alias, const bool exclusive,
    const int type)
{
    if (!pSettings->line_exist(section, line))
        return;

    HUD_SOUND& sound = m_sound_items.emplace_back();
    HUD_SOUND::LoadSound(section, line, sound, type);
    sound.m_alias = alias;
    sound.m_b_exclusive = exclusive;
}

void HUD_SOUND_COLLECTION::LoadSound(const CInifile* ini, LPCSTR section, LPCSTR line, LPCSTR alias,
    const bool exclusive, const int type)
{
    CInifile* source = const_cast<CInifile*>(ini);
    if (!source->line_exist(section, line))
        return;

    HUD_SOUND& sound = m_sound_items.emplace_back();
    LoadCollectionSound(ini, section, line, sound, type);
    sound.m_alias = alias;
    sound.m_b_exclusive = exclusive;
}

HUD_SOUND_COLLECTION_LAYERED::~HUD_SOUND_COLLECTION_LAYERED() { StopAllSounds(); }

HUD_SOUND* HUD_SOUND_COLLECTION_LAYERED::FindSoundItem(LPCSTR alias, const bool assert_if_missing)
{
    for (auto& layer : m_sound_layered_items)
    {
        if (layer.m_alias == alias)
            return layer.FindSoundItem(alias, assert_if_missing);
    }
    R_ASSERT3(!assert_if_missing, "layered sound item not found in collection", alias);
    return nullptr;
}

void HUD_SOUND_COLLECTION_LAYERED::PlaySound(LPCSTR alias, const Fvector& position, const CObject* parent,
    const bool hud_mode, const bool looped, const u8 index)
{
    for (auto& layer : m_sound_layered_items)
    {
        if (layer.m_alias == alias)
            layer.PlaySound(alias, position, parent, hud_mode, looped, index);
    }
}

void HUD_SOUND_COLLECTION_LAYERED::StopSound(LPCSTR alias)
{
    for (auto& layer : m_sound_layered_items)
    {
        if (layer.m_alias == alias)
            layer.StopSound(alias);
    }
}

void HUD_SOUND_COLLECTION_LAYERED::StopAllSounds()
{
    for (auto& layer : m_sound_layered_items)
        layer.StopAllSounds();
}

void HUD_SOUND_COLLECTION_LAYERED::SetPosition(LPCSTR alias, const Fvector& position)
{
    for (auto& layer : m_sound_layered_items)
    {
        if (layer.m_alias == alias)
            layer.SetPosition(alias, position);
    }
}

void HUD_SOUND_COLLECTION_LAYERED::LoadSound(LPCSTR section, LPCSTR line, LPCSTR alias,
    const bool exclusive, const int type)
{
    LoadSound(pSettings, section, line, alias, exclusive, type);
}

void HUD_SOUND_COLLECTION_LAYERED::LoadSound(const CInifile* ini, LPCSTR section, LPCSTR line, LPCSTR alias,
    const bool exclusive, const int type)
{
    CInifile* source = const_cast<CInifile*>(ini);
    if (!source->line_exist(section, line))
        return;

    string256 referenced_section;
    _GetItem(source->r_string(section, line), 0, referenced_section);
    if (source->section_exist(referenced_section))
    {
        string64 layer_line;
        int layer_index = 1;
        xr_sprintf(layer_line, "snd_%d_layer", layer_index);
        while (source->line_exist(referenced_section, layer_line))
        {
            HUD_SOUND_COLLECTION& layer = m_sound_layered_items.emplace_back();
            layer.m_alias = alias;
            // Weapon layers are loaded from pSettings.  R3 calls the native
            // collection loader here, which in turn uses HUD_SOUND::LoadSound.
            // Keep the alternate-INI parser exclusively for explosions.
            if (ini == pSettings)
                layer.LoadSound(referenced_section, layer_line, alias, exclusive, type);
            else
                layer.LoadSound(ini, referenced_section, layer_line, alias, exclusive, type);
            xr_sprintf(layer_line, "snd_%d_layer", ++layer_index);
        }
        return;
    }

    HUD_SOUND_COLLECTION& layer = m_sound_layered_items.emplace_back();
    layer.m_alias = alias;
    if (ini == pSettings)
        layer.LoadSound(section, line, alias, exclusive, type);
    else
        layer.LoadSound(ini, section, line, alias, exclusive, type);
}
