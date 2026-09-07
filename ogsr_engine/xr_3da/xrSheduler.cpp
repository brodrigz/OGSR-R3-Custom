#include "stdafx.h"
#include "xrSheduler.h"
#include "xr_object.h"

float psShedulerCurrent = 10.f;
float psShedulerTarget = 10.f;
float psShedulerMax = 10.f;
constexpr float psShedulerReaction = 1.f; // 0.1f;

void CSheduler::Initialize() { m_processing_now = false; }

void CSheduler::Destroy()
{
    internal_Registration();

    ItemsRT.clear();
    Items.clear();
    ItemsProcessed.clear();
    Registration.clear();
}

void CSheduler::internal_Registration()
{
    for (u32 it = 0; it < Registration.size(); it++)
    {
        ItemReg& R = Registration[it];
        if (R.OP)
        {
            // register
            // search for paired "unregister"

            BOOL bFoundAndErased = FALSE;
            for (u32 pair = it + 1; pair < Registration.size(); pair++)
            {
                ItemReg& R_pair = Registration[pair];
                if ((!R_pair.OP) && (R_pair.Object == R.Object))
                {
                    bFoundAndErased = TRUE;
                    Registration.erase(Registration.begin() + pair);
                    break;
                }
            }

            // register if non-paired
            if (!bFoundAndErased)
            {
                //Msg("SCHEDULER: internal register [%s][%x][%s]", *R.Object->shedule_Name(), R.Object, R.RT ? "true" : "false");
                internal_Register(R.Object, R.RT);
            }
            //else
            //    Msg("SCHEDULER: internal register skipped, because unregister found [%s][%x][%s]", "unknown", R.Object, R.RT ? "true" : "false");
        }
        else
        {
            // unregister
            internal_Unregister(R.Object, R.RT);
        }
    }

    Registration.clear();
}

void CSheduler::internal_Register(ISheduled* object, BOOL RT)
{
    VERIFY(!object->shedule.b_locked);

    if (RT)
    {
        // Fill item structure
        auto& TNext = ItemsRT.emplace_back();
        TNext.dwTimeForExecute = Device.dwTimeGlobal;
        TNext.dwTimeOfLastExecute = Device.dwTimeGlobal;
        TNext.Object = object;
        TNext.scheduled_name = object->shedule_Name();
        object->shedule.b_RT = TRUE;
    }
    else
    {
        // Fill item structure && Insert into priority Queue
        auto& TNext = Items.emplace_back();
        TNext.dwTimeForExecute = Device.dwTimeGlobal;
        TNext.dwTimeOfLastExecute = Device.dwTimeGlobal;
        TNext.Object = object;
        TNext.scheduled_name = object->shedule_Name();
        object->shedule.b_RT = FALSE;
    }
}

bool CSheduler::internal_Unregister(const ISheduled* object, BOOL RT)
{
    if (RT)
    {
        for (u32 i = 0; i < ItemsRT.size(); i++)
        {
            if (ItemsRT[i].Object == object)
            {
                ItemsRT.erase(ItemsRT.begin() + i);
                return true;
            }
        }
    }
    else
    {
        for (auto& Item : Items)
        {
            if (Item.Object == object)
            {
                Item.Object = nullptr;
                return true;
            }
        }
        // A later callback can unregister an object already updated this step.
        for (auto& Item : ItemsProcessed)
        {
            if (Item.Object == object)
            {
                Item.Object = nullptr;
                return true;
            }
        }
    }

    if (m_current_step_obj == object)
    {
        m_current_step_obj = nullptr;
        return true;
    }

    return false;
}

#ifdef DEBUG
bool CSheduler::Registered(ISheduled* object) const
{
    u32 count = 0;
    typedef xr_vector<Item> ITEMS;

    {
        ITEMS::const_iterator I = ItemsRT.begin();
        ITEMS::const_iterator E = ItemsRT.end();
        for (; I != E; ++I)
            if ((*I).Object == object)
            {
                //				Msg				("0x%8x found in RT",object);
                count = 1;
                break;
            }
    }
    {
        ITEMS::const_iterator I = Items.begin();
        ITEMS::const_iterator E = Items.end();
        for (; I != E; ++I)
            if ((*I).Object == object)
            {
                //				Msg				("0x%8x found in non-RT",object);
                VERIFY(!count);
                count = 1;
                break;
            }
    }

    {
        ITEMS::const_iterator I = ItemsProcessed.begin();
        ITEMS::const_iterator E = ItemsProcessed.end();
        for (; I != E; ++I)
            if ((*I).Object == object)
            {
                //				Msg				("0x%8x found in process items",object);
                VERIFY(!count);
                count = 1;
                break;
            }
    }

    if (m_current_step_obj == object)
    {
        VERIFY(!count);
        count = 1;
    }

    typedef xr_vector<ItemReg> ITEMS_REG;
    ITEMS_REG::const_iterator I = Registration.begin();
    ITEMS_REG::const_iterator E = Registration.end();
    for (; I != E; ++I)
    {
        if ((*I).Object == object)
        {
            if ((*I).OP)
            {
                //				Msg				("0x%8x found in registration on register",object);
                VERIFY(!count);
                ++count;
            }
            else
            {
                //				Msg				("0x%8x found in registration on UNregister",object);
                VERIFY(count == 1);
                --count;
            }
        }
    }

    VERIFY(!count || (count == 1));
    return (count == 1);
}
#endif // DEBUG

void CSheduler::Register(ISheduled* A, BOOL RT)
{
    VERIFY(!Registered(A));

    auto& R = Registration.emplace_back();
    R.OP = TRUE;
    R.RT = RT;
    R.Object = A;
    R.Object->shedule.b_RT = RT;

    //Msg("SCHEDULER: register [%s][%x]", *A->shedule_Name(), A);
}

void CSheduler::Unregister(ISheduled* A, bool force)
{
    VERIFY(Registered(A));

    if (m_processing_now || force)
    {
        if (internal_Unregister(A, A->shedule.b_RT))
            return;
    }

    auto& R = Registration.emplace_back();
    R.OP = FALSE;
    R.RT = A->shedule.b_RT;
    R.Object = A;
}

void CSheduler::ProcessStep()
{
    ZoneScoped;
    ZoneValue(Items.size());

    // Normal priority
    u32 dwTime = Device.dwTimeGlobal;

    const bool prefetch = Device.dwPrecacheFrame > 0;
    ItemsProcessed.clear();
    bool stopped{};
    //size_t cnt{};
    CTimer t_total;
    t_total.Start();

    for (size_t it{}; it < Items.size(); ++it)
    {
        if (!Items[it].Object || Items[it].dwTimeForExecute >= dwTime)
        {
            continue;
        }

        Item curr = Items[it];
        // Logically remove the entry before calling user code. Moving the tail
        // for each due object is quadratic; compact the holes once below.
        Items[it].Object = nullptr;
        m_current_step_obj = curr.Object;
        bool skip{false}, shed_need{true};

        {
            __try
            {
                shed_need = curr.Object->shedule_Needed();
            }
            __except (ExceptStackTrace("[CSheduler::ProcessStep] stack trace:\n"))
            {
                Msg("Scheduler tried to update object %s", *curr.scheduled_name);
                skip = true;
            }
        }

        if (skip || !shed_need || !m_current_step_obj)
        {
            m_current_step_obj = nullptr;
            continue;
        }

        __try
        {
            // Calc next update interval
            const u32 dwMin = std::max(30u, curr.Object->shedule.t_min);
            const u32 dwMax = (1000u + curr.Object->shedule.t_max) / 2;

            const float scale = curr.Object->shedule_Scale();
            if (!m_current_step_obj)
                continue;

            u32 dwUpdate = dwMin + iFloor(float(dwMax - dwMin) * scale);
            clamp(dwUpdate, std::max(dwMin, 20u), dwMax);

            const u32 elapsed = dwTime - curr.dwTimeOfLastExecute;

            // if (!Core.DebugFlags.test(xrCore::dbg_DisableObjectsScheduler))
            curr.Object->shedule_Update(std::clamp(elapsed, 1u, std::max(curr.Object->shedule.t_max, 1000u)));

            if (!m_current_step_obj)
            {
                continue;
            }

            // Publish only after the callbacks complete, so cancellation or an
            // exception cannot leave a partially rescheduled object behind.
            curr.scheduled_name = curr.Object->shedule_Name();
            if (!m_current_step_obj)
                continue;

            curr.dwTimeForExecute = dwTime + dwUpdate;
            curr.dwTimeOfLastExecute = dwTime;
            ItemsProcessed.emplace_back(std::move(curr));
            m_current_step_obj = nullptr;

            //cnt++;
        }
        __except (ExceptStackTrace("[CSheduler::ProcessStep2] stack trace:\n"))
        {
            Msg("Scheduler tried to update object %s", *curr.scheduled_name);
            m_current_step_obj = nullptr;
            curr.Object = nullptr;
            continue;
        }

        if (!prefetch && t_total.GetElapsed_ms() > static_cast<u32>(std::floor(psShedulerCurrent)))
        {
            // we have maxed out the load - increase heap
            psShedulerTarget += (psShedulerReaction * 3);

            // if (Core.DebugFlags.test(xrCore::dbg_TraceScheduler))
            {
                // Msg("Break ProcessStep. Processed: [%u], left in queue: [%u]", ItemsProcessed.size(), Items.size());
                if (ItemsProcessed.size() == 1) // кто то жрет все время на кадре
                    Msg("! Single item [%s] took whole update frame!!!", ItemsProcessed.front().scheduled_name.c_str());
            }

            stopped = true;
            break;
        }
    }

    //if (/*Core.DebugFlags.test(xrCore::dbg_TraceScheduler) &&*/ !prefetch && t_total.GetElapsed_ms() > 20)
    //    Msg("Long ProcessStep !!! duration [%u]ms. updated: [%u] objects!", t_total.GetElapsed_ms(), cnt);

    //if (prefetch)
    //    Msg("Prefetch frame, updated: [%u] objects!", cnt);

    // Stable compaction preserves pending-object order, including when the
    // time budget stops the step early. Rescheduled objects remain at the end.
    {
        ZoneScopedN("Scheduler compact");
        std::erase_if(Items, [](const Item& item) { return !item.Object; });
        std::erase_if(ItemsProcessed, [](const Item& item) { return !item.Object; });
        ZoneValue(ItemsProcessed.size());
        Items.insert(Items.end(), std::make_move_iterator(ItemsProcessed.begin()), std::make_move_iterator(ItemsProcessed.end()));
        ItemsProcessed.clear(); // Release references, retain allocation for the next step.
    }

    if (!stopped)
    {
        // always try to decrease target
        psShedulerTarget -= psShedulerReaction;
    }
}

void CSheduler::Update()
{
    ZoneScoped;

    // Initialize
    Device.Statistic->Sheduler.Begin();

    internal_Registration();

    m_processing_now = true;

    u32 dwTime = Device.dwTimeGlobal;

    {
        ZoneScopedN("ItemsRT");

        // Realtime priority
        for (auto& curr : ItemsRT)
        {
            R_ASSERT(curr.Object);

            if (!curr.Object->shedule_Needed())
            {
                curr.dwTimeOfLastExecute = dwTime;
                continue;
            }

            const u32 elapsed = dwTime - curr.dwTimeOfLastExecute;
            curr.Object->shedule_Update(elapsed);
            curr.dwTimeOfLastExecute = dwTime;
        }
    }

    // Normal (sheduled)
    ProcessStep();

    clamp(psShedulerTarget, 3.f, psShedulerMax);

    psShedulerCurrent = 0.9f * psShedulerCurrent + 0.1f * psShedulerTarget;
    Device.Statistic->fShedulerLoad = psShedulerCurrent;

    m_processing_now = false;

    internal_Registration();

    Device.Statistic->Sheduler.End();
}
