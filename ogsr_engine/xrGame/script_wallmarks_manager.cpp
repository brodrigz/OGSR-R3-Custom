#include "stdafx.h"
#include "script_wallmarks_manager.h"

#include "Level.h"
#include "script_game_object.h"
#include "../Include/xrRender/FactoryPtr.h"
#include "../Include/xrRender/RenderVisual.h"
#include "../Include/xrRender/WallMarkArray.h"
#include "../xr_3da/GameMtlLib.h"

struct ScriptWallmarksArray
{
    FactoryPtr<IWallMarkArray> wallmarks;
    shared_str section;

    explicit ScriptWallmarksArray(LPCSTR section_name) : section(section_name)
    {
        R_ASSERT2(pSettings->section_exist(section_name),
            make_string("[ScriptWallmarksManager] Can't find section '%s'", section_name));

        LPCSTR wallmarks_string = READ_IF_EXISTS(pSettings, r_string, section_name, "wallmarks", nullptr);
        R_ASSERT2(wallmarks_string,
            make_string("[ScriptWallmarksManager] Can't find 'wallmarks' in section '%s'", section_name));

        string256 texture;
        const int count = _GetItemCount(wallmarks_string);
        for (int i = 0; i < count; ++i)
            wallmarks->AppendMark(_GetItem(wallmarks_string, i, texture));
    }
};

ScriptWallmarksManager::~ScriptWallmarksManager()
{
    for (auto*& wallmarks : m_script_wallmarks)
        xr_delete(wallmarks);
}

IWallMarkArray* ScriptWallmarksManager::FindSection(LPCSTR section)
{
    for (const auto* wallmarks : m_script_wallmarks)
    {
        if (!xr_strcmp(*wallmarks->section, section))
            return &*wallmarks->wallmarks;
    }

    auto* wallmarks = xr_new<ScriptWallmarksArray>(section);
    m_script_wallmarks.push_back(wallmarks);
    return &*wallmarks->wallmarks;
}

void ScriptWallmarksManager::PlaceWallmark(Fvector dir, Fvector start_pos, float trace_dist, float wallmark_size,
    LPCSTR section, CScriptGameObject* ignore_obj, float ttl)
{
    PlaceWallmark(dir, start_pos, trace_dist, wallmark_size, section, ignore_obj, ttl, true);
}

void ScriptWallmarksManager::PlaceWallmark(Fvector dir, Fvector start_pos, float trace_dist, float wallmark_size,
    LPCSTR section, CScriptGameObject* ignore_obj, float ttl, bool random_rotation)
{
    collide::rq_result result;
    const bool reached_static_world = Level().ObjectSpace.RayPick(start_pos, dir, trace_dist, collide::rqtBoth, result,
                                          ignore_obj ? &ignore_obj->object() : nullptr) &&
        !result.O;
    if (!reached_static_world)
        return;

    CDB::TRI* triangle = Level().ObjectSpace.GetStaticTris() + result.element;
    const SGameMtl* material = GMLib.GetMaterialByIdx(triangle->material);
    if (material->Flags.is(SGameMtl::flSuppressWallmarks))
        return;

    Fvector end_point;
    end_point.mad(start_pos, dir, result.range);
    Render->add_StaticWallmark(FindSection(section), end_point, wallmark_size, triangle,
        Level().ObjectSpace.GetStaticVerts(), random_rotation, ttl);
}

void ScriptWallmarksManager::PlaceSkeletonWallmark(CScriptGameObject* object, LPCSTR section, Fvector start_pos,
    Fvector dir, float wallmark_size, float ttl)
{
    if (!object || !object->object().Visual())
        return;

    IKinematics* kinematics = object->object().Visual()->dcast_PKinematics();
    if (!kinematics)
        return;

    Render->add_SkeletonWallmark(&object->object().XFORM(), kinematics, FindSection(section), start_pos, dir,
        wallmark_size, true, ttl);
}
