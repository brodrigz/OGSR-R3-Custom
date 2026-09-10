#include "stdafx.h"

#include "soundrender_core.h"
#include "soundrender_source.h"

//	SEEK_SET	0	File beginning
//	SEEK_CUR	1	Current file pointer position
//	SEEK_END	2	End-of-file
int ov_seek_func(void* datasource, s64 offset, int whence)
{
    switch (whence)
    {
    case SEEK_SET: ((IReader*)datasource)->seek((int)offset); break;
    case SEEK_CUR: ((IReader*)datasource)->advance((int)offset); break;
    case SEEK_END: ((IReader*)datasource)->seek((int)offset + ((IReader*)datasource)->length()); break;
    }
    return 0;
}
size_t ov_read_func(void* ptr, size_t size, size_t nmemb, void* datasource)
{
    IReader* F = (IReader*)datasource;
    size_t exist_block = _max(0ul, iFloor(F->elapsed() / (float)size));
    size_t read_block = _min(exist_block, nmemb);
    F->r(ptr, (int)(read_block * size));
    return read_block;
}
int ov_close_func(void* datasource) { return 0; }
long ov_tell_func(void* datasource) { return ((IReader*)datasource)->tell(); }

void CSoundRender_Source::decompress(u32 line, OggVorbis_File* ovf)
{
    VERIFY(ovf);
    // decompression of one cache-line
    u32 line_size = SoundRender->cache.get_linesize();
    u32 buf_offs = (line * line_size) / (m_wformat.wBitsPerSample / 8) / m_wformat.nChannels;
    u32 left_file = dwBytesTotal - buf_offs;
    u32 left = (u32)_min(left_file, line_size);

    // seek
    u32 cur_pos = u32(ov_pcm_tell(ovf));
    if (cur_pos != buf_offs)
        ov_pcm_seek(ovf, buf_offs);

     // decompress
    const auto dest = SoundRender->cache.get_dataptr(CAT, line);
    if (m_wformat.wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        i_decompress(ovf, static_cast<float*>(dest), left);
    else
        i_decompress(ovf, static_cast<char*>(dest), left);
}

struct SoundSourcePrefill
{
    IReader* wave{};
    OggVorbis_File ovf{};
};

void CSoundRender_Source::ClosePrefill()
{
    if (!m_prefill)
        return;

    ov_clear(&m_prefill->ovf);
    if (m_prefill->wave)
        FS.r_close(m_prefill->wave);
    xr_delete(m_prefill);
}

void CSoundRender_Source::EnsureOpenForPrefill()
{
    if (m_prefill)
        return;

    m_prefill = xr_new<SoundSourcePrefill>();
    constexpr ov_callbacks ovc = {ov_read_func, ov_seek_func, ov_close_func, ov_tell_func};
    m_prefill->wave = FS.r_open(pname.c_str());
    R_ASSERT3(m_prefill->wave && m_prefill->wave->length(), "Can't open wave file:", pname.c_str());
    ov_open_callbacks(m_prefill->wave, &m_prefill->ovf, NULL, 0, ovc);
}

void CSoundRender_Source::LoadWave(LPCSTR pName)
{
    pname = pName;
    ClosePrefill();

    // Header parse stays on the caller (usually the game thread) so create()
    // can return length/format immediately. The file is closed again before
    // create() returns so level load cannot accumulate mapped OGGs.
    EnsureOpenForPrefill();

    vorbis_info* ovi = ov_info(&m_prefill->ovf, -1);
    // verify
    R_ASSERT3(ovi, "Invalid source info:", pName);

#ifdef DEBUG
    if (ovi->channels == 2)
    {
        Msg("stereo sound source [%s]", pName);
    }
#endif

    ZeroMemory(&m_wformat, sizeof(WAVEFORMATEX));

    m_wformat.nSamplesPerSec = (ovi->rate);
    m_wformat.nChannels = u16(ovi->channels);

    if (SoundRender->supports_float_pcm)
    {
        m_wformat.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        m_wformat.wBitsPerSample = 32;
    }
    else
    {
        m_wformat.wFormatTag = WAVE_FORMAT_PCM;
        m_wformat.wBitsPerSample = 16;
    }

    m_wformat.nBlockAlign = m_wformat.wBitsPerSample / 8 * m_wformat.nChannels;
    m_wformat.nAvgBytesPerSec = m_wformat.nSamplesPerSec * m_wformat.nBlockAlign;

    s64 pcm_total = ov_pcm_total(&m_prefill->ovf, -1);
    dwBytesTotal = u32(pcm_total * m_wformat.nBlockAlign);
    fTimeTotal = s_f_def_source_footer + dwBytesTotal / float(m_wformat.nAvgBytesPerSec);

    vorbis_comment* ovm = ov_comment(&m_prefill->ovf, -1);
    if (ovm->comments)
    {
        IReader F(ovm->user_comments[0], ovm->comment_lengths[0]);

        u32 vers{};
        if (F.elapsed() <= static_cast<int>(sizeof vers))
            Msg("! Invalid ogg-comment, file: [%s]", pName);
        else
            vers = F.r_u32();

        if (vers == 0x0001)
        {
            m_fMinDist = F.r_float();
            m_fMaxDist = F.r_float();
            m_fBaseVolume = 1.f;
            m_uGameType = F.r_u32();
            m_fMaxAIDist = m_fMaxDist;
        }
        else if (vers == 0x0002)
        {
            m_fMinDist = F.r_float();
            m_fMaxDist = F.r_float();
            m_fBaseVolume = F.r_float();
            m_uGameType = F.r_u32();
            m_fMaxAIDist = m_fMaxDist;
        }
        else if (vers == OGG_COMMENT_VERSION)
        {
            m_fMinDist = F.r_float();
            m_fMaxDist = F.r_float();
            m_fBaseVolume = F.r_float();
            m_uGameType = F.r_u32();
            m_fMaxAIDist = F.r_float();
        }
        else
        {
            Msg("! Invalid ogg-comment version, file: [%s]", pName);
        }
    }
    else
    {
        Msg("! Missing ogg-comment, file: [%s]", pName);
    }
    R_ASSERT3((m_fMaxAIDist >= 0.1f) && (m_fMaxDist >= 0.1f), "Invalid max distance.", pName);

    SoundRender->cache.cat_create(CAT, dwBytesTotal);
    ClosePrefill();
}

void CSoundRender_Source::PrefillCache()
{
    ZoneScopedN("SoundPrefill");
    if (pname.c_str())
        ZoneText(pname.c_str(), xr_strlen(pname.c_str()));

    m_prefill_queued = false;
    if (m_startup_prefilled)
        return;
    m_startup_prefilled = true;

    const u32 line_size = SoundRender->cache.get_linesize();
    if (!CAT.size || !m_wformat.nAvgBytesPerSec || !line_size)
    {
        ClosePrefill();
        return;
    }

    {
        ZoneScopedN("SoundPrefill/Open");
        EnsureOpenForPrefill();
    }
    if (!m_prefill)
        return;

    // Short one-shots (shots, footsteps, UI) decode fully. Longer clips only
    // warm the 3 startup OpenAL buffers plus one extra block so the first play
    // does not hitch on cache misses. The decoder is then closed; later stream
    // misses reopen via Target::attach().
    u32 lines = CAT.size;
    const u32 two_sec = m_wformat.nAvgBytesPerSec * 2;
    if (dwBytesTotal > two_sec)
    {
        const u32 startup_ms = sdef_target_size + sdef_target_block;
        const u32 startup_bytes = m_wformat.nAvgBytesPerSec / 1000 * startup_ms;
        lines = (startup_bytes + line_size - 1) / line_size;
        if (lines > CAT.size)
            lines = CAT.size;
    }

    for (u32 line = 0; line < lines; ++line)
    {
        if (SoundRender->cache.request(CAT, line))
        {
            ZoneScopedN("SoundPrefill/VorbisDecode");
            decompress(line, &m_prefill->ovf);
        }
    }

    ClosePrefill();
}

void CSoundRender_Source::load(LPCSTR name)
{
    string_path fn, N;
    xr_strcpy(N, name);
    _strlwr(N);
    if (strext(N))
        *strext(N) = 0;

    fname = N;

    strconcat(sizeof(fn), fn, N, ".ogg");
    if (!FS.exist("$level$", fn))
        FS.update_path(fn, "$game_sounds$", fn);

    ASSERT_FMT_DBG(FS.exist(fn), "! Can't find sound [%s.ogg]", N);
    if (!FS.exist(fn))
        FS.update_path(fn, "$game_sounds$", "$no_sound.ogg");

    LoadWave(fn);
}

void CSoundRender_Source::unload()
{
    ClosePrefill();
    m_startup_prefilled = false;
    m_prefill_queued = false;
    SoundRender->cache.cat_destroy(CAT);
    fTimeTotal = 0.0f;
    dwBytesTotal = 0;
}
