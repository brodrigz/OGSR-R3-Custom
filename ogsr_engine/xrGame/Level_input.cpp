#include "stdafx.h"
#include <dinput.h>
#include "HUDmanager.h"
#include "..\xr_3da\XR_IOConsole.h"
#include "entity_alive.h"
#include "game_sv_single.h"
#include "alife_simulator.h"
#include "alife_simulator_header.h"
#include "level_graph.h"
#include "../xr_3da/fdemorecord.h"
#include "level.h"
#include "xr_level_controller.h"
#include "game_cl_base.h"
#include "stalker_movement_manager.h"
#include "Inventory.h"
#include "xrServer.h"

#include "actor.h"
#include "huditem.h"
#include "ui/UIDialogWnd.h"
#include "clsid_game.h"
#include "../xr_3da/xr_input.h"
#include "saved_game_wrapper.h"

#include "game_object_space.h"
#include "script_callback_ex.h"
#include "GamePersistent.h"
#include "MainMenu.h"

#ifdef DEBUG
#include "ai/monsters/BaseMonster/base_monster.h"
#endif

#ifdef DEBUG
extern void try_change_current_entity();
extern void restore_actor();
#endif

#include "embedded_editor/embedded_editor_main.h"

bool g_bDisableAllInput = false;

extern float g_fTimeFactor;
extern int g_bHudAdjustMode;

#define CURRENT_ENTITY() (game ? CurrentEntity() : nullptr)

namespace
{
constexpr u32 kMaxBindedActions = 16;

u32 fill_key_actions(int key, EGameActions* dst)
{
    u32 n = get_binded_actions(key, dst, kMaxBindedActions);
    if (!n)
    {
        dst[0] = kNOTBINDED;
        n = 1;
    }
    return n;
}

bool action_blocked(const xr_set<EGameActions>& blocked, EGameActions cmd) { return blocked.find(cmd) != blocked.end(); }

bool action_allowed_when_movement_only(EGameActions cmd)
{
    return (cmd < kCAM_1 || cmd == kPAUSE || cmd == kSCREENSHOT || cmd == kQUIT || cmd == kCONSOLE);
}
} // namespace

void CLevel::IR_OnMouseWheel(int direction)
{
    if (CImGuiEditor::Get().Editor_MouseWheel(direction))
        return;

    if (g_bDisableAllInput)
        return;

    if (g_bHudAdjustMode)
    {
        if (CURRENT_ENTITY())
        {
            IInputReceiver* IR = smart_cast<IInputReceiver*>(smart_cast<CGameObject*>(CURRENT_ENTITY()));
            if (IR)
                IR->IR_OnMouseWheel(direction);
        }
        return;
    }

    if (HUD().GetUI()->IR_OnMouseWheel(direction))
        return;
    if (Device.Paused())
        return;

    if (game && Game().IR_OnMouseWheel(direction))
        return;

    if (Actor())
        Actor()->callback(GameObject::eOnMouseWheel)(direction);

    if (HUD().GetUI()->MainInputReceiver())
        return;

    const int dik = (direction > 0) ? MOUSE_WHEEL_UP : MOUSE_WHEEL_DOWN;
    if (get_binded_action(dik) != kNOTBINDED)
    {
        IR_OnKeyboardPress(dik);
        return;
    }

    if (CURRENT_ENTITY())
    {
        IInputReceiver* IR = smart_cast<IInputReceiver*>(smart_cast<CGameObject*>(CURRENT_ENTITY()));
        if (IR)
            IR->IR_OnMouseWheel(direction);
    }
}

static int mouse_button_2_key[] = {MOUSE_1, MOUSE_2, MOUSE_3, MOUSE_4, MOUSE_5, MOUSE_6, MOUSE_7, MOUSE_8};

void CLevel::IR_OnMousePress(int btn) { IR_OnKeyboardPress(mouse_button_2_key[btn]); }

void CLevel::IR_OnMouseRelease(int btn) { IR_OnKeyboardRelease(mouse_button_2_key[btn]); }

void CLevel::IR_OnMouseHold(int btn) { IR_OnKeyboardHold(mouse_button_2_key[btn]); }

void CLevel::IR_OnMouseMove(int dx, int dy)
{
    if (CImGuiEditor::Get().Editor_MouseMove(dx, dy))
        return;

    if (g_bDisableAllInput)
        return;
    if (HUD().GetUI()->IR_OnMouseMove(dx, dy))
        return;
    if (Device.Paused())
        return;

    if (Actor())
        Actor()->callback(GameObject::eOnMouseMove)(dx, dy);

    if (CURRENT_ENTITY())
    {
        IInputReceiver* IR = smart_cast<IInputReceiver*>(smart_cast<CGameObject*>(CURRENT_ENTITY()));
        if (IR)
            IR->IR_OnMouseMove(dx, dy);
    }
}

// Обработка нажатия клавиш
extern bool g_block_pause;
extern bool g_block_all_except_movement;

void CLevel::IR_OnKeyboardPress(int key)
{
    if (CImGuiEditor::Get().Editor_KeyPress(key))
        return;

    if (GamePersistent().OnKeyboardPress(key))
        return;

    EGameActions cmds[kMaxBindedActions];
    const u32 cmd_count = fill_key_actions(key, cmds);
    EGameActions _curr = cmds[0];

    const bool b_ui_exist = (Has_HUD() && HUD().GetUI());

    for (u32 i = 0; i < cmd_count; ++i)
    {
        _curr = cmds[i];
        if (action_blocked(m_blocked_actions, _curr))
            continue;

        switch (_curr)
        {
        case kSCREENSHOT:
            Render->Screenshot();
            return;

        case kCONSOLE:
            Console->Show();
            return;

        case kQUIT: {
            if (b_ui_exist && HUD().GetUI()->MainInputReceiver())
            {
                if (HUD().GetUI()->MainInputReceiver()->IR_OnKeyboardPress(key))
                    return; // special case for mp and main_menu

                if (MainMenu()->IsActive() || !Device.Paused())
                    HUD().GetUI()->StartStopMenu(HUD().GetUI()->MainInputReceiver(), true);
            }
            else
                Console->Execute("main_menu");
            return;
        }

        case kPAUSE:
            if (!g_block_pause)
            {
                Device.Pause(!Device.Paused(), TRUE, TRUE, "li_pause_key");
            }
            return;
        default: break;
        }
    }

    _curr = cmds[0];

    if (g_bDisableAllInput)
        return;

    if (g_block_all_except_movement)
    {
        bool any = false;
        for (u32 i = 0; i < cmd_count; ++i)
        {
            if (!action_blocked(m_blocked_actions, cmds[i]) && action_allowed_when_movement_only(cmds[i]))
            {
                any = true;
                break;
            }
        }
        if (!any)
            return;
    }

    if (!b_ui_exist)
        return;

    if (Actor())
    {
        Actor()->callback(GameObject::eOnKeyPress)(key, _curr);

        if (g_bDisableAllInput)
            return;
    }

    if (b_ui_exist && HUD().GetUI()->IR_OnKeyboardPress(key))
        return;

    if (Device.Paused())
        return;

    if (game && Game().IR_OnKeyboardPress(key))
        return;

    for (u32 i = 0; i < cmd_count; ++i)
    {
        _curr = cmds[i];
        if (action_blocked(m_blocked_actions, _curr))
            continue;
        if (g_block_all_except_movement && !action_allowed_when_movement_only(_curr))
            continue;

        if (_curr == kQUICK_SAVE)
        {
            Console->Execute("save");
            return;
        }
        if (_curr == kQUICK_LOAD)
        {
#ifdef DEBUG
            FS.get_path("$game_config$")->m_Flags.set(FS_Path::flNeedRescan, TRUE);
            FS.get_path("$game_scripts$")->m_Flags.set(FS_Path::flNeedRescan, TRUE);
            FS.rescan_pathes();
#endif // DEBUG
            string_path saved_game, command;
            strconcat(sizeof(saved_game), saved_game, Core.UserName, "_", "quicksave");
            if (!CSavedGameWrapper::valid_saved_game(saved_game))
                return;

            strconcat(sizeof(command), command, "load ", saved_game);
            Console->Execute(command);
            return;
        }
    }

#ifdef DEBUG
    if (key == DIK_RETURN || key == DIK_NUMPADENTER)
    {
        bDebug = !bDebug;
        return;
    }

    if (key == DIK_BACK)
    {
        HW.Caps.SceneMode = (HW.Caps.SceneMode + 1) % 3;
        return;
    }

    if (key == DIK_F4)
    {
        if (pInput->iGetAsyncKeyState(DIK_LALT))
            ;
        else if (pInput->iGetAsyncKeyState(DIK_RALT))
            ;
        else
        {
            bool bOk = false;
            u32 i = 0, j, n = Objects.o_count();
            if (pCurrentEntity)
                for (; i < n; ++i)
                    if (Objects.o_get_by_iterator(i) == pCurrentEntity)
                        break;
            if (i < n)
            {
                j = i;
                bOk = false;
                for (++i; i < n; ++i)
                {
                    CEntityAlive* tpEntityAlive = smart_cast<CEntityAlive*>(Objects.o_get_by_iterator(i));
                    if (tpEntityAlive)
                    {
                        bOk = true;
                        break;
                    }
                }
                if (!bOk)
                    for (i = 0; i < j; ++i)
                    {
                        CEntityAlive* tpEntityAlive = smart_cast<CEntityAlive*>(Objects.o_get_by_iterator(i));
                        if (tpEntityAlive)
                        {
                            bOk = true;
                            break;
                        }
                    }
                if (bOk)
                {
                    CObject* tpObject = CurrentEntity();
                    CObject* __I = Objects.o_get_by_iterator(i);
                    CObject** I = &__I;

                    SetEntity(*I);
                    if (tpObject != *I)
                    {
                        CActor* pActor = smart_cast<CActor*>(tpObject);
                        if (pActor)
                            pActor->inventory().Items_SetCurrentEntityHud(false);
                    }
                    if (tpObject)
                    {
                        Engine.Sheduler.Unregister(tpObject);
                        Engine.Sheduler.Register(tpObject, TRUE);
                    };
                    Engine.Sheduler.Unregister(*I);
                    Engine.Sheduler.Register(*I, TRUE);

                    CActor* pActor = smart_cast<CActor*>(*I);
                    if (pActor)
                    {
                        pActor->inventory().Items_SetCurrentEntityHud(true);

                        CHudItem* pHudItem = smart_cast<CHudItem*>(pActor->inventory().ActiveItem());
                        if (pHudItem)
                        {
                            pHudItem->OnStateSwitch(pHudItem->GetState());
                        }
                    }
                }
            }
            return;
        }
    }
    if (key == MOUSE_1)
    {
        if (pInput->iGetAsyncKeyState(DIK_LALT))
        {
            if (CurrentEntity()->CLS_ID == CLSID_OBJECT_ACTOR)
                try_change_current_entity();
            else
                restore_actor();
            return;
        }
    }

    if (key == DIK_DIVIDE)
    {
        if (OnServer())
        {
            Server->game->SetGameTimeFactor(g_fTimeFactor);
        }
    }
    else if (key == DIK_MULTIPLY)
    {
        if (OnServer())
        {
            float NewTimeFactor = 1000.f;
            Server->game->SetGameTimeFactor(NewTimeFactor);
        }
    }
#endif

    if (bindConsoleCmds.execute(key))
        return;

    if (b_ui_exist && HUD().GetUI()->MainInputReceiver())
        return;

    if (CURRENT_ENTITY())
    {
        IInputReceiver* IR = smart_cast<IInputReceiver*>(smart_cast<CGameObject*>(CURRENT_ENTITY()));
        if (IR)
        {
            CActor* actor = smart_cast<CActor*>(CURRENT_ENTITY());
            for (u32 i = 0; i < cmd_count; ++i)
            {
                _curr = cmds[i];
                if (_curr == kNOTBINDED)
                    continue;
                if (action_blocked(m_blocked_actions, _curr))
                    continue;
                if (g_block_all_except_movement && !action_allowed_when_movement_only(_curr))
                    continue;

                if (actor)
                {
                    if (actor->OnActionPress(_curr))
                        break;
                }
                else
                {
                    IR->IR_OnKeyboardPress(_curr);
                    break;
                }
            }
        }
    }

#ifdef DEBUG
    CObject* obj = Level().Objects.FindObjectByName("monster");
    if (obj)
    {
        CBaseMonster* monster = smart_cast<CBaseMonster*>(obj);
        if (monster)
            monster->debug_on_key(key);
    }
#endif
}

void CLevel::IR_OnKeyboardRelease(int key)
{
    if (CImGuiEditor::Get().Editor_KeyRelease(key))
        return;

    if (g_bDisableAllInput)
        return;

    EGameActions cmds[kMaxBindedActions];
    const u32 cmd_count = fill_key_actions(key, cmds);
    EGameActions _curr = cmds[0];

    if (action_blocked(m_blocked_actions, _curr) && cmd_count == 1)
        return;

    if (g_block_all_except_movement)
    {
        bool any = false;
        for (u32 i = 0; i < cmd_count; ++i)
        {
            if (!action_blocked(m_blocked_actions, cmds[i]) && action_allowed_when_movement_only(cmds[i]))
            {
                any = true;
                break;
            }
        }
        if (!any)
            return;
    }

    const bool b_ui_exist = (Has_HUD() && HUD().GetUI());

    if (b_ui_exist && HUD().GetUI()->IR_OnKeyboardRelease(key))
        return;

    if (Device.Paused())
    {
        // Hold-to-look must still see the key-up when deactivate pauses the game first.
        if (CURRENT_ENTITY())
        {
            IInputReceiver* IR = smart_cast<IInputReceiver*>(smart_cast<CGameObject*>(CURRENT_ENTITY()));
            if (IR)
            {
                for (u32 i = 0; i < cmd_count; ++i)
                {
                    if (cmds[i] == kFREELOOK && !action_blocked(m_blocked_actions, cmds[i]))
                        IR->IR_OnKeyboardRelease(cmds[i]);
                }
            }
        }
        return;
    }

    if (game && Game().OnKeyboardRelease(_curr))
        return;

    if ((key != DIK_LALT) && (key != DIK_RALT) && (key != DIK_F4) && Actor())
        Actor()->callback(GameObject::eOnKeyRelease)(key, _curr);

    if (b_ui_exist && HUD().GetUI()->MainInputReceiver())
        return;

    if (CURRENT_ENTITY())
    {
        IInputReceiver* IR = smart_cast<IInputReceiver*>(smart_cast<CGameObject*>(CURRENT_ENTITY()));
        if (IR)
        {
            for (u32 i = 0; i < cmd_count; ++i)
            {
                _curr = cmds[i];
                if (_curr == kNOTBINDED)
                    continue;
                if (action_blocked(m_blocked_actions, _curr))
                    continue;
                if (g_block_all_except_movement && !action_allowed_when_movement_only(_curr))
                    continue;
                IR->IR_OnKeyboardRelease(_curr);
            }
        }
    }
}

void CLevel::IR_OnKeyboardHold(int key)
{
    if (CImGuiEditor::Get().Editor_KeyHold(key))
        return;

    if (g_bDisableAllInput)
        return;

    EGameActions cmds[kMaxBindedActions];
    const u32 cmd_count = fill_key_actions(key, cmds);
    EGameActions _curr = cmds[0];

    if (g_block_all_except_movement)
    {
        bool any = false;
        for (u32 i = 0; i < cmd_count; ++i)
        {
            if (!action_blocked(m_blocked_actions, cmds[i]) && action_allowed_when_movement_only(cmds[i]))
            {
                any = true;
                break;
            }
        }
        if (!any)
            return;
    }

    const bool b_ui_exist = (Has_HUD() && HUD().GetUI());

    if (b_ui_exist && HUD().GetUI()->IR_OnKeyboardHold(key))
        return;
    if (b_ui_exist && HUD().GetUI()->MainInputReceiver())
        return;
    if (Device.Paused())
        return;

    if ((key != DIK_LALT) && (key != DIK_RALT) && (key != DIK_F4) && Actor())
        Actor()->callback(GameObject::eOnKeyHold)(key, _curr);

    if (CURRENT_ENTITY())
    {
        IInputReceiver* IR = smart_cast<IInputReceiver*>(smart_cast<CGameObject*>(CURRENT_ENTITY()));
        if (IR)
        {
            for (u32 i = 0; i < cmd_count; ++i)
            {
                _curr = cmds[i];
                if (_curr == kNOTBINDED)
                    continue;
                if (action_blocked(m_blocked_actions, _curr))
                    continue;
                if (g_block_all_except_movement && !action_allowed_when_movement_only(_curr))
                    continue;
                IR->IR_OnKeyboardHold(_curr);
            }
        }
    }
}

void CLevel::IR_OnMouseStop(int /**axis/**/, int /**value/**/) {}

void CLevel::IR_OnActivate()
{
    if (!pInput)
        return;
    int i;
    for (i = 0; i < CInput::COUNT_KB_BUTTONS; i++)
    {
        if (pInput->iGetAsyncKeyState(i))
        {
            EGameActions action = get_binded_action(i);
            switch (action)
            {
            case kFWD:
            case kBACK:
            case kL_STRAFE:
            case kR_STRAFE:
            case kLEFT:
            case kRIGHT:
            case kUP:
            case kDOWN:
            case kCROUCH:
            case kACCEL:
            case kL_LOOKOUT:
            case kR_LOOKOUT:
            case kWPN_FIRE: {
                IR_OnKeyboardPress(i);
            }
            break;
            };
        };
    }
}

void CLevel::block_action(EGameActions cmd) { m_blocked_actions.insert(cmd); }

void CLevel::unblock_action(EGameActions cmd) { m_blocked_actions.erase(cmd); }