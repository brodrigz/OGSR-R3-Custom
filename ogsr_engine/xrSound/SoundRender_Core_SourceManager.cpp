#include "stdafx.h"

#include "SoundRender_Core.h"
#include "SoundRender_Source.h"

CSoundRender_Source* CSoundRender_Core::i_create_source(LPCSTR name)
{
    // Search
    string256 id;
    xr_strcpy(id, name);
    _strlwr(id);
    if (strext(id))
        *strext(id) = 0;
    for (u32 it = 0; it < s_sources.size(); it++)
    {
        if (0 == xr_strcmp(*s_sources[it]->fname, id))
            return s_sources[it];
    }

    // Load a _new one
    CSoundRender_Source* S = xr_new<CSoundRender_Source>();
    S->load(id);
    s_sources.push_back(S);
    return S;
}

void CSoundRender_Core::i_destroy_source(CSoundRender_Source* S)
{
    // No actual destroy at all
}

void CSoundRender_Core::queue_prefill(CSoundRender_Source* S)
{
    if (!S || !S->needs_startup_prefill())
        return;

    std::scoped_lock lock{m_prefill_lock};
    if (!S->needs_startup_prefill())
        return;

    S->mark_prefill_queued();
    s_prefill.push_back(S);
}

void CSoundRender_Core::drain_prefill()
{
    xr_vector<CSoundRender_Source*> pending;
    {
        std::scoped_lock lock{m_prefill_lock};
        pending.swap(s_prefill);
    }

    for (CSoundRender_Source* S : pending)
        S->PrefillCache();
}
