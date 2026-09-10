#pragma once

#include "soundrender_cache.h"

// refs
struct OggVorbis_File;
struct SoundSourcePrefill;

class CSoundRender_Source : public CSound_source
{
public:
    shared_str pname;
    shared_str fname;
    cache_cat CAT;

    float fTimeTotal{};
    u32 dwBytesTotal{};

    WAVEFORMATEX m_wformat{}; //= SoundRender->wfm;

    float m_fBaseVolume;
    float m_fMinDist;
    float m_fMaxDist;
    float m_fMaxAIDist;
    u32 m_uGameType;

private:
    SoundSourcePrefill* m_prefill{};
    bool m_startup_prefilled{};
    bool m_prefill_queued{};

    void i_decompress(OggVorbis_File* ovf, char* dest, u32 size) const;
    void i_decompress(OggVorbis_File* ovf, float* dest, u32 size) const; // this overload clamps denormalized sounds

    void LoadWave(LPCSTR name);
    void EnsureOpenForPrefill();
    void ClosePrefill();

public:
    CSoundRender_Source();
    ~CSoundRender_Source();

    void load(LPCSTR name);
    void unload();
    void decompress(u32 line, OggVorbis_File* ovf);
    void PrefillCache();
    bool needs_startup_prefill() const { return !m_startup_prefilled && !m_prefill_queued; }
    void mark_prefill_queued() { m_prefill_queued = true; }

    virtual float length_sec() const { return fTimeTotal; }
    virtual u32 game_type() const { return m_uGameType; }
    virtual LPCSTR file_name() const { return *fname; }
    virtual float base_volume() const { return m_fBaseVolume; }
    virtual u16 channels_num() const { return m_wformat.nChannels; }
    virtual u32 bytes_total() const { return dwBytesTotal; }
};
