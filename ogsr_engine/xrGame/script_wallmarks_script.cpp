#include "stdafx.h"
#include "script_wallmarks_manager.h"

#include "GamePersistent.h"
#include "script_game_object.h"

using namespace luabind;

static ScriptWallmarksManager* GetWallmarksManager() { return &GamePersistent().GetWallmarksManager(); }

#pragma optimize("s", on)
void CScriptWallmarksManager::script_register(lua_State* L)
{
    module(L)
    [
        class_<ScriptWallmarksManager>("ScriptWallmarksManager")
            .def(constructor<>())
            .def("place", static_cast<void (ScriptWallmarksManager::*)(Fvector, Fvector, float, float, LPCSTR,
                              CScriptGameObject*, float)>(&ScriptWallmarksManager::PlaceWallmark))
            .def("place", static_cast<void (ScriptWallmarksManager::*)(Fvector, Fvector, float, float, LPCSTR,
                              CScriptGameObject*, float, bool)>(&ScriptWallmarksManager::PlaceWallmark))
            .def("place_skeleton", &ScriptWallmarksManager::PlaceSkeletonWallmark),
        def("wallmarks_manager", &GetWallmarksManager)
    ];
}
