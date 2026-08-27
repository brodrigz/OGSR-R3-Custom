#pragma once

#include "script_export_space.h"

class CScriptGameObject;
class IWallMarkArray;
struct ScriptWallmarksArray;

class ScriptWallmarksManager
{
    xr_vector<ScriptWallmarksArray*> m_script_wallmarks;

public:
    ScriptWallmarksManager() = default;
    ~ScriptWallmarksManager();

    IWallMarkArray* FindSection(LPCSTR section);
    void PlaceWallmark(Fvector dir, Fvector start_pos, float trace_dist, float wallmark_size, LPCSTR section,
        CScriptGameObject* ignore_obj, float ttl);
    void PlaceWallmark(Fvector dir, Fvector start_pos, float trace_dist, float wallmark_size, LPCSTR section,
        CScriptGameObject* ignore_obj, float ttl, bool random_rotation);
    void PlaceSkeletonWallmark(CScriptGameObject* object, LPCSTR section, Fvector start_pos, Fvector dir,
        float wallmark_size, float ttl);
};

using CScriptWallmarksManager = class_exporter<ScriptWallmarksManager>;
add_to_type_list(CScriptWallmarksManager)
#undef script_type_list
#define script_type_list save_type_list(CScriptWallmarksManager)
