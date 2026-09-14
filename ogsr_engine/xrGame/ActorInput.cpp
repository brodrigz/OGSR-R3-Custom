#include "stdafx.h"
#include <dinput.h>
#include "Actor.h"
#include "Torch.h"
#include "trade.h"
#include "../xr_3da/camerabase.h"
#ifdef DEBUG
#include "PHDebug.h"
#endif
#include "hit.h"
#include "PHDestroyable.h"
#include "Car.h"
#include "HudManager.h"
#include "UIGameSP.h"
#include "inventory.h"
#include "level.h"
#include "game_cl_base.h"
#include "xr_level_controller.h"
#include "UsableScriptObject.h"
#include "clsid_game.h"
#include "actorcondition.h"
#include "actor_input_handler.h"
#include "string_table.h"
#include "UI/UIStatic.h"
#include "CharacterPhysicsSupport.h"
#include "InventoryBox.h"
#include "player_hud.h"
#include "HudItem.h"
#include "WeaponMagazined.h"
#include "../xr_3da/xr_input.h"
#include "CustomDetector.h"
#include "WeaponKnife.h"
#include "Missile.h"
#include "PDA.h"
#include "ui/UIPDAWnd.h"
#include "ui/UIMainIngameWnd.h"
#include "eatable_item.h"
#include "WeaponMagazinedWGrenade.h"
#include "ai_space.h"
#include "script_engine.h"
#include "script_game_object.h"

bool g_bAutoClearCrouch = true;
extern int g_bHudAdjustMode;

static bool dik_held_os(int dik)
{
    int vk = 0;
    switch (dik)
    {
    case DIK_LMENU: vk = VK_LMENU; break;
    case DIK_RMENU: vk = VK_RMENU; break;
    case DIK_LCONTROL: vk = VK_LCONTROL; break;
    case DIK_RCONTROL: vk = VK_RCONTROL; break;
    case DIK_LSHIFT: vk = VK_LSHIFT; break;
    case DIK_RSHIFT: vk = VK_RSHIFT; break;
    case DIK_LWIN: vk = VK_LWIN; break;
    case DIK_RWIN: vk = VK_RWIN; break;
    case DIK_TAB: vk = VK_TAB; break;
    case DIK_DELETE: vk = VK_DELETE; break;
    case MOUSE_1: vk = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON; break;
    case MOUSE_2: vk = GetSystemMetrics(SM_SWAPBUTTON) ? VK_LBUTTON : VK_RBUTTON; break;
    case MOUSE_3: vk = VK_MBUTTON; break;
    case MOUSE_4: vk = VK_XBUTTON1; break;
    case MOUSE_5: vk = VK_XBUTTON2; break;
    default:
        if (dik >= CInput::COUNT_KB_BUTTONS)
            return pInput && pInput->iGetAsyncKeyState(dik);
        vk = MapVirtualKey(dik, MAPVK_VSC_TO_VK);
        break;
    }
    if (!vk)
        return pInput && pInput->iGetAsyncKeyState(dik);
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool CActor::IsFreelookBindHeld() const
{
    if (!Device.b_is_WindowActive)
        return false;
    if (kFREELOOK >= g_key_bindings.size())
        return false;

    const _binding& bind = g_key_bindings[kFREELOOK];
    for (int i = 0; i < 2; ++i)
    {
        if (bind.m_keyboard[i] && dik_held_os(bind.m_keyboard[i]->dik))
            return true;
    }
    return false;
}

void CActor::IR_OnKeyboardPress(int cmd) { OnActionPress(cmd); }

bool CActor::OnActionPress(int cmd)
{
    if (g_bHudAdjustMode && pInput->iGetAsyncKeyState(DIK_LSHIFT))
    {
        if (pInput->iGetAsyncKeyState(DIK_RETURN) || pInput->iGetAsyncKeyState(DIK_BACKSPACE) || pInput->iGetAsyncKeyState(DIK_DELETE))
            g_player_hud->tune(Ivector{});

        return true;
    }

    if (Remote())
        return true;

    //	if (conditions().IsSleeping())	return;
    if (IsTalking())
        return true;
    if (m_input_external_handler && !m_input_external_handler->authorized(cmd))
        return true;

    //	if (psCallbackFlags.test(CF_KEY_PRESS))
    //		callback(GameObject::eOnKeyPress)(cmd);

    if (cmd == kWPN_FIRE)
    {
        if (inventory().ActiveItem() && inventory().ActiveItem()->StopSprintOnFire())
        {
            mstate_wishful &= ~mcSprint;
        }
    }

    if (cam_freelook != eflDisabled)
    {
        if (cmd == kWPN_FIRE || cmd == kWPN_ZOOM || cmd == kWPN_ZOOM_ALTER)
            return true;
    }

    if (!g_Alive())
        return true;

    if (m_holder && kUSE != cmd)
    {
        m_holder->OnKeyboardPress(cmd);
        if (m_holder->allowWeapon() && inventory().Action(cmd, CMD_START))
            return true;
        return true;
    }
    else if (inventory().Action(cmd, CMD_START))
        return true;

    switch (cmd)
    {
    case kJUMP: {
        mstate_wishful |= mcJump;
        return true;
    }
    case kCROUCH_TOGGLE: {
        g_bAutoClearCrouch = !g_bAutoClearCrouch;
        if (!g_bAutoClearCrouch)
            mstate_wishful |= mcCrouch;
        else
            m_bLowCrouchToggled = false;
        return true;
    }
    case kWALK_TOGGLE: {
        if ((mstate_wishful & mcCrouch) || (mstate_real & mcCrouch))
            return true;
        m_bWalkToggled = !m_bWalkToggled;
        return true;
    }
    case kCROUCH_LOW_TOGGLE: {
        if (!((mstate_wishful & mcCrouch) || (mstate_real & mcCrouch) || !g_bAutoClearCrouch))
            return true;
        m_bLowCrouchToggled = !m_bLowCrouchToggled;
        return true;
    }
    case kSPRINT_TOGGLE: {
        const bool crouched = (mstate_wishful & mcCrouch) || (mstate_real & mcCrouch) || !g_bAutoClearCrouch;
        if (crouched)
        {
            g_bAutoClearCrouch = true;
            mstate_wishful &= ~mcCrouch;
            m_bLowCrouchToggled = false;
        }
        mstate_wishful &= ~mcLookout;
        m_bWalkToggled = false;
        if (psActorFlags.test(AF_SPRINT_HOLD))
            mstate_wishful |= mcSprint;
        else if (mstate_wishful & mcSprint)
            mstate_wishful &= ~mcSprint;
        else
            mstate_wishful |= mcSprint;
        return true;
    }
    case kL_LOOKOUT:
    case kR_LOOKOUT: {
        if ((mstate_wishful & mcSprint) || (mstate_real & mcSprint))
            return true;
        if (psActorFlags.test(AF_LEAN_TOGGLE) && cam_freelook == eflDisabled)
        {
            const u32 lookout = cmd == kL_LOOKOUT ? mcLLookout : mcRLookout;
            if (mstate_wishful & lookout)
                mstate_wishful &= ~mcLookout;
            else
            {
                mstate_wishful &= ~mcLookout;
                mstate_wishful |= lookout;
            }
            return true;
        }
        return false;
    }
    case kCAM_1: cam_Set(eacFirstEye); return true;
    case kCAM_2: cam_Set(eacLookAt); return true;
    case kCAM_3: cam_Set(eacFreeLook); return true;
    case kFREELOOK: {
        if (cam_freelook == eflDisabled && CanUseFreelook() && IsFreelookBindHeld())
            cam_SetFreelook();
        return true;
    }
    case kCYCLE_INTERACT:
        if (HUD().GetUI() && HUD().GetUI()->UIMainIngameWnd)
            HUD().GetUI()->UIMainIngameWnd->CycleNearbyInteract();
        return true;
    case kNIGHT_VISION:
    case kTORCH: {
        auto act_it = inventory().ActiveItem();
        auto active_hud = smart_cast<CHudItem*>(act_it);
        if (active_hud && active_hud->GetState() != CHudItem::eIdle && Core.Features.test(xrCore::Feature::busy_actor_restrictions))
            return true;
        auto pTorch = smart_cast<CTorch*>(inventory().ItemFromSlot(TORCH_SLOT));
        if (pTorch && !smart_cast<CWeaponMagazined*>(act_it) && !smart_cast<CWeaponKnife*>(act_it) && !smart_cast<CMissile*>(act_it))
            cmd == kNIGHT_VISION ? pTorch->SwitchNightVision() : pTorch->Switch();
        return true;
    }
    case kWPN_8: {
        if (auto det = smart_cast<CCustomDetector*>(inventory().ItemFromSlot(DETECTOR_SLOT)))
            det->ToggleDetector(g_player_hud->attached_item(0) != nullptr);
        return true;
    }
    case kUSE: ActorUse(); return true;
    case kQUICK_USE: ActorQuickUse(); return true;
    case kDROP:
        b_DropActivated = TRUE;
        f_DropPower = 0;
        return true;
    case kNEXT_SLOT: {
        OnNextWeaponSlot();
        return true;
    }
    case kPREV_SLOT: {
        OnPrevWeaponSlot();
        return true;
    }

    case kUSE_BANDAGE:
    case kUSE_MEDKIT: {
        auto active_hud = smart_cast<CHudItem*>(inventory().ActiveItem());
        if (active_hud && active_hud->GetState() != CHudItem::eIdle && Core.Features.test(xrCore::Feature::busy_actor_restrictions))
            return true;

        if (!(GetTrade()->IsInTradeState()))
        {
            PIItem itm = inventory().item((cmd == kUSE_BANDAGE) ? CLSID_IITEM_BANDAGE : CLSID_IITEM_MEDKIT);
            if (itm)
            {
                inventory().Eat(itm);
                SDrawStaticStruct* _s = HUD().GetUI()->UIGame()->AddCustomStatic("item_used", true);
                _s->m_endTime = Device.fTimeGlobal + 3.0f; // 3sec
                string1024 str;
                strconcat(sizeof(str), str, *CStringTable().translate("st_item_used"), ": ", itm->Name());
                _s->wnd()->SetText(str);
            }
        }
        return true;
    }
    }
    return false;
}
void CActor::IR_OnMouseWheel(int direction)
{
    if (g_bHudAdjustMode)
    {
        g_player_hud->tune(Ivector{0, 0, direction});
        return;
    }

    // Unbound wheel (no mouse_wheel_up/down binds): keep the old hardcoded chain.
    if (cam_freelook == eflDisabled)
    {
        if (inventory().Action((direction > 0) ? kWPN_ZOOM_DEC : kWPN_ZOOM_INC, CMD_START))
            return;
    }

    if (psActorFlags.test(AF_MOUSE_WHEEL_SWITCH_SLOTS))
    {
        if (direction > 0)
            OnNextWeaponSlot();
        else
            OnPrevWeaponSlot();
    }
    else
    {
        if (direction > 0)
            inventory().Action(kWPN_FIREMODE_NEXT, CMD_START | CMD_OPT);
        else
            inventory().Action(kWPN_FIREMODE_PREV, CMD_START | CMD_OPT);
    }
}
void CActor::IR_OnKeyboardRelease(int cmd)
{
    if (g_bHudAdjustMode && pInput->iGetAsyncKeyState(DIK_LSHIFT))
        return;

    if (Remote())
        return;

    //	if (conditions().IsSleeping())	return;
    if (m_input_external_handler && !m_input_external_handler->authorized(cmd))
        return;

    if (g_Alive())
    {
        //		int dik = get_action_dik((EGameActions)cmd);
        //		if ((dik != DIK_LALT) && (dik != DIK_RALT) && (dik != DIK_F4) /*&& psCallbackFlags.test(CF_KEY_RELEASE)*/)
        //			this->callback(GameObject::eOnKeyRelease)(cmd);
        if (cmd == kUSE)
            PickupModeOff();

        if (m_holder)
        {
            m_holder->OnKeyboardRelease(cmd);

            if (m_holder->allowWeapon() && inventory().Action(cmd, CMD_STOP))
                return;
            return;
        }
        else if (inventory().Action(cmd, CMD_STOP))
            return;

        switch (cmd)
        {
        case kJUMP: mstate_wishful &= ~mcJump; break;
        case kDROP:
            if (GAME_PHASE_INPROGRESS == Game().Phase())
                g_PerformDrop();
            break;
        case kCROUCH: g_bAutoClearCrouch = true; break;
        case kSPRINT_TOGGLE:
            if (psActorFlags.test(AF_SPRINT_HOLD))
                mstate_wishful &= ~mcSprint;
            break;
        case kFREELOOK:
            if (cam_freelook == eflEnabled || cam_freelook == eflEnabling)
                cam_UnsetFreelook();
            break;
        }
    }
}

void CActor::IR_OnKeyboardHold(int cmd)
{
    if (g_bHudAdjustMode && pInput->iGetAsyncKeyState(DIK_LSHIFT))
    {
        if (pInput->iGetAsyncKeyState(DIK_UP))
            g_player_hud->tune(Ivector{0, -1, 0});
        else if (pInput->iGetAsyncKeyState(DIK_DOWN))
            g_player_hud->tune(Ivector{0, 1, 0});
        else if (pInput->iGetAsyncKeyState(DIK_LEFT))
            g_player_hud->tune(Ivector{-1, 0, 0});
        else if (pInput->iGetAsyncKeyState(DIK_RIGHT))
            g_player_hud->tune(Ivector{1, 0, 0});
        else if (pInput->iGetAsyncKeyState(DIK_PGUP))
            g_player_hud->tune(Ivector{0, 0, 1});
        else if (pInput->iGetAsyncKeyState(DIK_PGDN))
            g_player_hud->tune(Ivector{0, 0, -1});
        return;
    }

    if (Remote() || !g_Alive())
        return;
    //	if (conditions().IsSleeping())				return;
    if (m_input_external_handler && !m_input_external_handler->authorized(cmd))
        return;
    if (IsTalking())
        return;

    //	int dik = get_action_dik((EGameActions)cmd);
    //	if ((dik != DIK_LALT) && (dik != DIK_RALT) && (dik != DIK_F4)/* && psCallbackFlags.test(CF_KEY_HOLD)*/)
    //		this->callback(GameObject::eOnKeyHold)(cmd);

    if (m_holder)
    {
        m_holder->OnKeyboardHold(cmd);
        return;
    }

    float LookFactor = GetLookFactor();
    switch (cmd)
    {
    case kUP:
    case kDOWN:
        if (cam_freelook != eflEnabling && cam_freelook != eflDisabling)
            cam_Active()->Move((cmd == kUP) ? kDOWN : kUP, 0, LookFactor);
        break;
    case kSHOWHUD:
    case kHIDEHUD: cam_Active()->Move(cmd); break;
    case kLEFT:
    case kRIGHT:
        if (eacFreeLook != cam_active && cam_freelook != eflEnabling && cam_freelook != eflDisabling)
            cam_Active()->Move(cmd, 0, LookFactor);
        break;

    case kACCEL: mstate_wishful |= mcAccel; break;
    case kL_STRAFE: mstate_wishful |= mcLStrafe; break;
    case kR_STRAFE: mstate_wishful |= mcRStrafe; break;
    case kL_LOOKOUT:
        if (!psActorFlags.test(AF_LEAN_TOGGLE) && cam_freelook == eflDisabled && !(mstate_wishful & mcSprint) && !(mstate_real & mcSprint))
            mstate_wishful |= mcLLookout;
        break;
    case kR_LOOKOUT:
        if (!psActorFlags.test(AF_LEAN_TOGGLE) && cam_freelook == eflDisabled && !(mstate_wishful & mcSprint) && !(mstate_real & mcSprint))
            mstate_wishful |= mcRLookout;
        break;
    case kFWD: mstate_wishful |= mcFwd; break;
    case kBACK: mstate_wishful |= mcBack; break;
    case kCROUCH: mstate_wishful |= mcCrouch; break;
    case kFREELOOK:
        if (!IsFreelookBindHeld())
        {
            if (cam_freelook == eflEnabled || cam_freelook == eflEnabling)
                cam_UnsetFreelook();
            break;
        }
        if (cam_freelook == eflDisabled && CanUseFreelook())
            cam_SetFreelook();
        break;
    }
}

void CActor::IR_OnMouseMove(int dx, int dy)
{
    if (g_bHudAdjustMode)
    {
        g_player_hud->tune(Ivector{dx, dy, 0});
        return;
    }

    PIItem iitem = inventory().ActiveItem();
    if (iitem && iitem->cast_hud_item())
        iitem->cast_hud_item()->ResetSubStateTime();

    if (Remote())
        return;
    //	if (conditions().IsSleeping())	return;

    //	if (psCallbackFlags.test(CF_MOUSE_MOVE))
    //		this->callback(GameObject::eOnMouseMove)(dx, dy);

    if (m_holder)
    {
        m_holder->OnMouseMove(dx, dy);
        return;
    }

    if (cam_freelook == eflEnabling || cam_freelook == eflDisabling)
        return;

    float LookFactor = GetLookFactor();

    CCameraBase* C = cameras[cam_active];
    float scale = (C->f_fov / g_fov) * psMouseSens * psMouseSensScale / 50.f / LookFactor;
    if (dx)
    {
        float d = float(dx) * scale;
        cam_Active()->Move((d < 0) ? kLEFT : kRIGHT, _abs(d));
    }
    if (dy)
    {
        float d = ((psMouseInvert.test(1)) ? -1 : 1) * float(dy) * scale * 3.f / 4.f;
        cam_Active()->Move((d > 0) ? kUP : kDOWN, _abs(d));
    }
}

bool CActor::use_Holder(CHolderCustom* holder)
{
    if (m_holder)
    {
        bool b = false;
        CGameObject* holderGO = smart_cast<CGameObject*>(m_holder);

        if (smart_cast<CCar*>(holderGO))
            b = use_Vehicle(0);
        else if (holderGO->CLS_ID == CLSID_OBJECT_W_MOUNTED || holderGO->CLS_ID == CLSID_OBJECT_W_STATMGUN)
            b = use_MountedWeapon(0);

        if (inventory().ActiveItem())
        {
            CHudItem* hi = smart_cast<CHudItem*>(inventory().ActiveItem());
            if (hi)
                hi->OnAnimationEnd(hi->GetState());
        }

        return b;
    }
    else
    {
        bool b = false;
        CGameObject* holderGO = smart_cast<CGameObject*>(holder);
        if (smart_cast<CCar*>(holder))
            b = use_Vehicle(holder);

        if (holderGO->CLS_ID == CLSID_OBJECT_W_MOUNTED || holderGO->CLS_ID == CLSID_OBJECT_W_STATMGUN)
            b = use_MountedWeapon(holder);

        if (b)
        { // used succesfully
            // switch off torch...
            CAttachableItem* I = CAttachmentOwner::attachedItem(CLSID_DEVICE_TORCH);
            if (I)
            {
                CTorch* torch = smart_cast<CTorch*>(I);
                if (torch)
                    torch->Switch(false);
            }
        }

        if (inventory().ActiveItem())
        {
            CHudItem* hi = smart_cast<CHudItem*>(inventory().ActiveItem());
            if (hi)
                hi->OnAnimationEnd(hi->GetState());
        }

        return b;
    }
}

extern bool g_bDisableAllInput;

void CActor::ActorUse()
{
    auto pGameSP = smart_cast<CUIGameSP*>(HUD().GetUI()->UIGame());
    if (auto Pda = GetPDA(); Pda && Pda->Is3DPDA() && psActorFlags.test(AF_3D_PDA) && pGameSP->PdaMenu->IsShown())
        return;

    auto active_hud = smart_cast<CHudItem*>(inventory().ActiveItem());
    if (active_hud && active_hud->GetState() != CHudItem::eIdle && Core.Features.test(xrCore::Feature::busy_actor_restrictions))
        return;

    if (g_bDisableAllInput || HUD().GetUI()->MainInputReceiver())
        return;

    if (m_holder)
    {
        CGameObject* GO = smart_cast<CGameObject*>(m_holder);
        NET_Packet P;
        CGameObject::u_EventGen(P, GEG_PLAYER_DETACH_HOLDER, ID());
        P.w_u32(GO->ID());
        CGameObject::u_EventSend(P);
        return;
    }

    if (character_physics_support()->movement()->PHCapture())
    {
        character_physics_support()->movement()->PHReleaseObject();
        return;
    }

    if (m_pUsableObject)
    {
        m_pUsableObject->use(this);
        if (g_bDisableAllInput || HUD().GetUI()->MainInputReceiver())
            return;
    }

    if (m_pInvBoxWeLookingAt && m_pInvBoxWeLookingAt->object().nonscript_usable() && m_pInvBoxWeLookingAt->IsOpened())
    {
        // если контейнер открыт
        CUIGameSP* pGameSP = smart_cast<CUIGameSP*>(HUD().GetUI()->UIGame());
        if (pGameSP)
            pGameSP->StartCarBody(this, m_pInvBoxWeLookingAt);
        return;
    }

    else if (!m_pUsableObject || m_pUsableObject->nonscript_usable())
    {
        if (m_pPersonWeLookingAt)
        {
            CEntityAlive* pEntityAliveWeLookingAt = smart_cast<CEntityAlive*>(m_pPersonWeLookingAt);
            VERIFY(pEntityAliveWeLookingAt);
            if (pEntityAliveWeLookingAt->g_Alive())
            {
                TryToTalk();
                return;
            }
            //обыск трупа
            else if (!pInput->iGetAsyncKeyState(DIK_LSHIFT))
            {
                //только если находимся в режиме single
                CUIGameSP* pGameSP = smart_cast<CUIGameSP*>(HUD().GetUI()->UIGame());
                if (pGameSP)
                    pGameSP->StartCarBody(this, m_pPersonWeLookingAt);
                return;
            }
        }

        collide::rq_result& RQ = HUD().GetCurrentRayQuery();
        CPhysicsShellHolder* object = smart_cast<CPhysicsShellHolder*>(RQ.O);
        if (object && object->getVisible())
        {
            if (pInput->iGetAsyncKeyState(DIK_LSHIFT))
            {
                if (object->ActorCanCapture())
                {
                    // Msg("--[%s] Actor Captured object: [%s]", __FUNCTION__, object->cName().c_str());
                    character_physics_support()->movement()->PHCaptureObject(object, (u16)RQ.element);

                    return;
                }
            }
            else if (smart_cast<CHolderCustom*>(object) && RQ.range < inventory().GetTakeDist())
            {
                NET_Packet P;
                CGameObject::u_EventGen(P, GEG_PLAYER_ATTACH_HOLDER, ID());
                P.w_u32(object->ID());
                CGameObject::u_EventSend(P);

                return;
            }
        }
    }

    if (HudInteractEnabled())
    {
        CInventoryItem* item = nullptr;
        if (HUD().GetUI() && HUD().GetUI()->UIMainIngameWnd)
            item = HUD().GetUI()->UIMainIngameWnd->InteractPickupItem();
        m_pending_quick_use = u16(-1);
        TryTakeInventoryItem(item);
        PickupModeOff();
        return;
    }

    PickupModeOn();

    /*if (g_Alive())
        PickupModeUpdate();*/

    PickupModeUpdate_COD();
}

namespace
{
void GiveMagazineAmmoToActor(CWeaponMagazined* mag, CActor* actor)
{
    xr_map<shared_str, u16> ammo;
    for (const auto& cart : mag->m_magazine)
    {
        auto it = ammo.find(cart.m_ammoSect);
        if (it == ammo.end())
            ammo[cart.m_ammoSect] = 1;
        else
            ++it->second;
    }

    mag->UnloadMagazine(false);

    for (const auto& it : ammo)
        mag->SpawnAmmo(it.second, *it.first, actor->ID());
}

bool CanUnloadWorldWeapon(CInventoryItem* item)
{
    auto* mag = smart_cast<CWeaponMagazined*>(item);
    if (!mag || mag->unlimited_ammo())
        return false;
    return mag->GetAmmoElapsed() > 0 || mag->GetAmmoElapsed2() > 0;
}
} // namespace

bool CActor::UnloadWorldWeapon(CInventoryItem* item)
{
    if (!CanUnloadWorldWeapon(item))
        return false;

    auto* mag = smart_cast<CWeaponMagazined*>(item);
    GiveMagazineAmmoToActor(mag, this);
    if (auto* gl = smart_cast<CWeaponMagazinedWGrenade*>(mag))
    {
        if (gl->IsGrenadeLauncherAttached() && mag->GetAmmoElapsed2() > 0)
        {
            gl->PerformSwitchGL();
            GiveMagazineAmmoToActor(mag, this);
            gl->PerformSwitchGL();
        }
    }
    return true;
}

void CActor::ActorQuickUse()
{
    if (g_bDisableAllInput || HUD().GetUI()->MainInputReceiver())
        return;
    if (m_holder || character_physics_support()->movement()->PHCapture())
        return;
    if (!HudInteractEnabled())
        return;
    if (m_pending_quick_use != u16(-1))
        return;

    CInventoryItem* item = nullptr;
    if (HUD().GetUI() && HUD().GetUI()->UIMainIngameWnd)
        item = HUD().GetUI()->UIMainIngameWnd->InteractPickupItem();

    if (CanUnloadWorldWeapon(item))
    {
        bool delayed = false;
        luabind::functor<bool> func;
        if (ai().script_engine().functor("ogsr_actor_animation.try_play_pickup_for_unload", func))
            delayed = func(item->object().lua_game_object());
        if (!delayed)
            UnloadWorldWeapon(item);
        return;
    }

    CEatableItem* eatable = item ? item->cast_eatable_item() : nullptr;
    if (!eatable || !eatable->Useful())
        return;
    if (!inventory().CanTakeItem(item))
        return;

    m_pending_quick_use = item->object().ID();
    m_pending_quick_use_started = Device.dwTimeGlobal;
    if (TryTakeInventoryItem(item) == EItemPickupResult::Rejected)
        m_pending_quick_use = u16(-1);
}

void CActor::TryPendingQuickUse()
{
    if (m_pending_quick_use == u16(-1))
        return;

    // Script-handled pickups can be delayed or vetoed. Do not retain a stale
    // use request indefinitely if the callback never transfers the item.
    CObject* object = Level().Objects.net_Find(m_pending_quick_use);
    if (!g_Alive() || Device.dwTimeGlobal - m_pending_quick_use_started >= 5000 || !object || object->getDestroy() ||
        (object->H_Parent() && object->H_Parent() != this))
    {
        m_pending_quick_use = u16(-1);
        return;
    }

    PIItem itm = inventory().get_object_by_id(m_pending_quick_use);
    if (!itm)
        return;

    m_pending_quick_use = u16(-1);
    CEatableItem* eatable = itm->cast_eatable_item();
    if (eatable && eatable->Useful())
        inventory().Eat(itm);
}

BOOL CActor::HUDview() const { return IsFocused() && (cam_active == eacFirstEye) && ((!m_holder) || (m_holder && m_holder->allowWeapon() && m_holder->HUDView())); }

// void CActor::IR_OnMousePress(int btn)
constexpr u32 SlotsToCheck[] = {
    KNIFE_SLOT, // 0
    FIRST_WEAPON_SLOT, // 1
    SECOND_WEAPON_SLOT, // 2
    GRENADE_SLOT, // 3
    APPARATUS_SLOT, // 4
    BOLT_SLOT, // 5
    PDA_SLOT,
};

void CActor::OnNextWeaponSlot()
{
    u32 ActiveSlot = inventory().GetActiveSlot();
    if (ActiveSlot == NO_ACTIVE_SLOT)
        ActiveSlot = inventory().GetPrevActiveSlot();

    if (ActiveSlot == NO_ACTIVE_SLOT)
        ActiveSlot = KNIFE_SLOT;

    constexpr u32 NumSlotsToCheck = sizeof(SlotsToCheck) / sizeof(u32);
    u32 CurSlot = 0;

    for (; CurSlot < NumSlotsToCheck; CurSlot++)
        if (SlotsToCheck[CurSlot] == ActiveSlot)
            break;

    if (CurSlot >= NumSlotsToCheck)
        return;

    for (u32 i = CurSlot + 1; i < NumSlotsToCheck; i++)
    {
        if (inventory().ItemFromSlot(SlotsToCheck[i]))
        {
            if (SlotsToCheck[i] == PDA_SLOT)
                IR_OnKeyboardPress(kACTIVE_JOBS);
            else
                IR_OnKeyboardPress(kWPN_1 + i);
            return;
        }
    }
}

void CActor::OnPrevWeaponSlot()
{
    u32 ActiveSlot = inventory().GetActiveSlot();
    if (ActiveSlot == NO_ACTIVE_SLOT)
        ActiveSlot = inventory().GetPrevActiveSlot();

    if (ActiveSlot == NO_ACTIVE_SLOT)
        ActiveSlot = KNIFE_SLOT;

    constexpr u32 NumSlotsToCheck = sizeof(SlotsToCheck) / sizeof(u32);
    u32 CurSlot = 0;

    for (; CurSlot < NumSlotsToCheck; CurSlot++)
        if (SlotsToCheck[CurSlot] == ActiveSlot)
            break;

    if (CurSlot >= NumSlotsToCheck)
        return;

    for (s32 i = s32(CurSlot - 1); i >= 0; i--)
    {
        if (inventory().ItemFromSlot(SlotsToCheck[i]))
        {
            if (SlotsToCheck[i] == PDA_SLOT)
                IR_OnKeyboardPress(kACTIVE_JOBS);
            else
                IR_OnKeyboardPress(kWPN_1 + i);
            return;
        }
    }
}

float CActor::GetLookFactor()
{
    if (m_input_external_handler)
        return m_input_external_handler->mouse_scale_factor();

    float factor = 1.f;

    PIItem pItem = inventory().ActiveItem();

    if (pItem)
        factor *= pItem->GetControlInertionFactor();

    VERIFY(!fis_zero(factor));

    if (cam_freelook != eflDisabled)
        return 1.5f;

    return factor;
}

void CActor::set_input_external_handler(CActorInputHandler* handler)
{
    // clear state
    if (handler)
        mstate_wishful = 0;

    // release fire button
    if (handler)
        IR_OnKeyboardRelease(kWPN_FIRE);

    // set handler
    m_input_external_handler = handler;
}
