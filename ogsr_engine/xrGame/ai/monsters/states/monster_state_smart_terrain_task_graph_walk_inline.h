#pragma once

#define TEMPLATE_SPECIALIZATION template <typename _Object>

#define CStateMonsterSmartTerrainTaskGraphWalkAbstract CStateMonsterSmartTerrainTaskGraphWalk<_Object>

TEMPLATE_SPECIALIZATION
CALifeSmartTerrainTask* CStateMonsterSmartTerrainTaskGraphWalkAbstract::refresh_task()
{
    m_task = nullptr;
    if (!ai().get_alife())
        return nullptr;

    CSE_ALifeMonsterAbstract* monster = smart_cast<CSE_ALifeMonsterAbstract*>(ai().alife().objects().object(object->ID()));
    if (!monster || monster->m_smart_terrain_id == 0xffff)
        return nullptr;

    m_task = monster->brain().smart_terrain().task(monster);
    return m_task;
}

TEMPLATE_SPECIALIZATION
void CStateMonsterSmartTerrainTaskGraphWalkAbstract::initialize()
{
    inherited::initialize();

    refresh_task();
    VERIFY(m_task);
}

TEMPLATE_SPECIALIZATION
bool CStateMonsterSmartTerrainTaskGraphWalkAbstract::check_start_conditions()
{
    CALifeSmartTerrainTask* task = refresh_task();
    if (!task)
        return false;

    if (object->ai_location().game_vertex_id() == task->game_vertex_id())
        return false;

    return true;
}

TEMPLATE_SPECIALIZATION
bool CStateMonsterSmartTerrainTaskGraphWalkAbstract::check_completion()
{
    CALifeSmartTerrainTask* task = refresh_task();
    if (!task)
        return true;

    // if we get to the graph point - work complete
    if (object->ai_location().game_vertex_id() == task->game_vertex_id())
        return true;
    return false;
}

TEMPLATE_SPECIALIZATION
void CStateMonsterSmartTerrainTaskGraphWalkAbstract::execute()
{
    CALifeSmartTerrainTask* task = refresh_task();
    if (!task)
        return;

    object->set_action(ACT_WALK_FWD);
    object->set_state_sound(MonsterSound::eMonsterSoundIdle);

    object->path().detour_graph_points(task->game_vertex_id());
}

#undef TEMPLATE_SPECIALIZATION
#undef CStateMonsterSmartTerrainTaskGraphWalkAbstract
