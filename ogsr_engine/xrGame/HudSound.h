//////////////////////////////////////////////////////////////////////
// HudSound.h:		структура для работы со звуками применяемыми в
//					HUD-объектах (обычные звуки, но с доп. параметрами)
//////////////////////////////////////////////////////////////////////

#pragma once

struct HUD_SOUND
{
    HUD_SOUND() : m_activeSnd(NULL), m_b_exclusive(false) {}
    ~HUD_SOUND() { m_activeSnd = NULL; }

    ////////////////////////////////////
    // работа со звуками
    /////////////////////////////////////
    static void LoadSound(LPCSTR section, LPCSTR line, ref_sound& hud_snd, int type = sg_SourceType, float* volume = NULL, float* delay = NULL, float* freq = NULL);

    static void LoadSound(LPCSTR section, LPCSTR line, HUD_SOUND& hud_snd, int type = sg_SourceType);

    static void DestroySound(HUD_SOUND& hud_snd);

    static void PlaySound(HUD_SOUND& snd, const Fvector& position, const CObject* parent, bool hud_mode, bool looped = false, bool overlap = false);

    static void StopSound(HUD_SOUND& snd);

    ICF BOOL playing()
    {
        if (m_activeSnd)
            return m_activeSnd->snd._feedback() ? TRUE : FALSE;
        else
            return FALSE;
    }

    ICF void set_position(const Fvector& pos)
    {
        if (m_activeSnd)
        {
            if (m_activeSnd->snd._feedback() && !m_activeSnd->snd._feedback()->is_2D())
                m_activeSnd->snd.set_position(pos);
            else
                m_activeSnd = NULL;
        }
    }

    struct SSnd
    {
        ref_sound snd;
        float delay; //задержка перед проигрыванием
        float volume; //громкость
        float freq; //коэффициент частоты
    };
    shared_str m_alias;
    SSnd* m_activeSnd;
    bool m_b_exclusive;
    xr_vector<SSnd> sounds;

    bool operator==(LPCSTR alias) const { return xr_strcmp(m_alias.c_str(), alias) == 0; }
};

class CInifile;

class HUD_SOUND_COLLECTION
{
public:
    ~HUD_SOUND_COLLECTION();

    shared_str m_alias;
    xr_vector<HUD_SOUND> m_sound_items;

    HUD_SOUND* FindSoundItem(LPCSTR alias, bool assert_if_missing);
    void PlaySound(LPCSTR alias, const Fvector& position, const CObject* parent, bool hud_mode,
        bool looped = false, u8 index = u8(-1));
    void StopSound(LPCSTR alias);
    void StopAllSounds();
    void SetPosition(LPCSTR alias, const Fvector& position);
    void LoadSound(LPCSTR section, LPCSTR line, LPCSTR alias, bool exclusive = false,
        int type = sg_SourceType);
    void LoadSound(const CInifile* ini, LPCSTR section, LPCSTR line, LPCSTR alias, bool exclusive = false,
        int type = sg_SourceType);
};

class HUD_SOUND_COLLECTION_LAYERED
{
    xr_vector<HUD_SOUND_COLLECTION> m_sound_layered_items;

public:
    ~HUD_SOUND_COLLECTION_LAYERED();

    HUD_SOUND* FindSoundItem(LPCSTR alias, bool assert_if_missing);
    void PlaySound(LPCSTR alias, const Fvector& position, const CObject* parent, bool hud_mode,
        bool looped = false, u8 index = u8(-1));
    void StopSound(LPCSTR alias);
    void StopAllSounds();
    void SetPosition(LPCSTR alias, const Fvector& position);
    void LoadSound(LPCSTR section, LPCSTR line, LPCSTR alias, bool exclusive = false,
        int type = sg_SourceType);
    void LoadSound(const CInifile* ini, LPCSTR section, LPCSTR line, LPCSTR alias, bool exclusive = false,
        int type = sg_SourceType);
};
