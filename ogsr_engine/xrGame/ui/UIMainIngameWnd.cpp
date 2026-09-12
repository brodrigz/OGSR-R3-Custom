#include "stdafx.h"

#include "UIMainIngameWnd.h"
#include "UIMessagesWindow.h"
#include "../UIZoneMap.h"

#include <dinput.h>
#include "../actor.h"
#include "../GameObject.h"
#include "../HUDManager.h"
#include "../PDA.h"
#include "../character_info.h"
#include "../character_community.h"
#include "../inventory.h"
#include "../UIGameSP.h"
#include "../weaponmagazined.h"
#include "../missile.h"
#include "../Grenade.h"
#include "../torch.h"
#include "../xrServer_objects_ALife.h"
#include "../alife_simulator.h"
#include "../alife_object_registry.h"
#include "../game_cl_base.h"
#include "../level.h"
#include "../seniority_hierarchy_holder.h"

#include "../date_time.h"
#include "../xrServer_Objects_ALife_Monsters.h"
#include "../../xr_3da/LightAnimLibrary.h"

#include "UIInventoryUtilities.h"

#include "UIXmlInit.h"
#include "UITextureMaster.h"
#include "../ui_base.h"
#include "UIPdaMsgListItem.h"
#include "../alife_registry_wrappers.h"
#include "../actorcondition.h"

#include "../string_table.h"
#include "clsid_game.h"
#include "UIArtefactPanel.h"
#include "UIMap.h"
#include "../inventory_item.h"
#include "../eatable_item.h"
#include "../InventoryOwner.h"
#include "../InventoryBox.h"
#include "../entity_alive.h"
#include "../UsableScriptObject.h"
#include "../xr_level_controller.h"
#include "../Include/xrRender/Kinematics.h"

#ifdef DEBUG
#include "../attachable_item.h"
#include "..\..\xr_3da\xr_input.h"
#endif

#include "UIScrollView.h"
#include "map_hint.h"
#include "UIColorAnimatorWrapper.h"
#include "../game_news.h"
#include "../xr_3da/xr_input.h"

using namespace InventoryUtilities;

#define DEFAULT_MAP_SCALE 1.f
#define MAININGAME_XML "maingame.xml"

static CUIMainIngameWnd* GetMainIngameWindow()
{
    if (g_hud)
    {
        CUI* pUI = g_hud->GetUI();
        if (pUI)
            return pUI->UIMainIngameWnd;
    }
    return nullptr;
}

static CUIStatic* warn_icon_list[8]{};

// alpet: для возможности внешнего контроля иконок (используется в NLC6 вместо типичных индикаторов). Никак не влияет на игру для остальных модов.
static bool external_icon_ctrl = false;

// позволяет расцветить иконку или изменить её размер
static bool SetupGameIcon(CUIMainIngameWnd::EWarningIcons icon, u32 cl, float width, float height)
{
    auto window = GetMainIngameWindow();
    if (!window)
    {
        Msg("!![SetupGameIcon] failed due GetMainIngameWindow() returned NULL");
        return false;
    }

    R_ASSERT(icon > 0 && icon < std::size(warn_icon_list), "!!Invalid first arg for setup_game_icon!");

    CUIStatic* sIcon = warn_icon_list[icon];

    if (width > 0 && height > 0)
    {
        sIcon->SetWidth(width);
        sIcon->SetHeight(height);
        sIcon->SetStretchTexture(cl > 0);
    }
    else
        window->SetWarningIconColor(icon, cl);

    external_icon_ctrl = true;
    return true;
}

CUIMainIngameWnd::CUIMainIngameWnd()
{
    m_pActor = NULL;
    m_pWeapon = NULL;
    m_pGrenade = NULL;
    m_pItem = NULL;
    UIZoneMap = xr_new<CUIZoneMap>();
    m_pPickUpItem = NULL;

    warn_icon_list[ewiWeaponJammed] = &UIWeaponJammedIcon;
    warn_icon_list[ewiRadiation] = &UIRadiaitionIcon;
    warn_icon_list[ewiWound] = &UIWoundIcon;
    warn_icon_list[ewiStarvation] = &UIStarvationIcon;
    warn_icon_list[ewiPsyHealth] = &UIPsyHealthIcon;
    warn_icon_list[ewiInvincible] = &UIInvincibleIcon;
    warn_icon_list[ewiThirst] = &UIThirstIcon;
}

#include "UIProgressShape.h"
extern CUIProgressShape* g_MissileForceShape;

CUIMainIngameWnd::~CUIMainIngameWnd()
{
    DestroyFlashingIcons();
    xr_delete(UIZoneMap);
    if (m_artefactPanel)
        xr_delete(m_artefactPanel);
    HUD_SOUND::DestroySound(m_contactSnd);
    xr_delete(g_MissileForceShape);
}

void CUIMainIngameWnd::Init()
{
    CUIXml uiXml;
    uiXml.Init(CONFIG_PATH, UI_PATH, MAININGAME_XML);

    CUIXmlInit xml_init;
    CUIWindow::Init(0, 0, UI_BASE_WIDTH, UI_BASE_HEIGHT);

    Enable(false);

    AttachChild(&UIStaticHealth);
    xml_init.InitStatic(uiXml, "static_health", 0, &UIStaticHealth);
    m_xml_health_pos = UIStaticHealth.GetWndPos();

    AttachChild(&UIStaticArmor);
    xml_init.InitStatic(uiXml, "static_armor", 0, &UIStaticArmor);

    AttachChild(&UIWeaponBack);
    xml_init.InitStatic(uiXml, "static_weapon", 0, &UIWeaponBack);
    m_xml_weapon_pos = UIWeaponBack.GetWndPos();

    UIWeaponBack.AttachChild(&UIWeaponSignAmmo);
    xml_init.InitStatic(uiXml, "static_ammo", 0, &UIWeaponSignAmmo);
    UIWeaponSignAmmo.SetElipsis(CUIStatic::eepEnd, 2);

    UIWeaponBack.AttachChild(&UIWeaponIcon);
    xml_init.InitStatic(uiXml, "static_wpn_icon", 0, &UIWeaponIcon);
    UIWeaponIcon.SetShader(GetEquipmentIconsShader());
    UIWeaponIcon_rect = UIWeaponIcon.GetWndRect();
    //---------------------------------------------------------
    AttachChild(&UIPickUpItemIcon);
    xml_init.InitStatic(uiXml, "pick_up_item", 0, &UIPickUpItemIcon);
    UIPickUpItemIcon.SetShader(GetEquipmentIconsShader());
    //	UIPickUpItemIcon.ClipperOn	();
    UIPickUpItemIcon.Show(false);

    m_iPickUpItemIconWidth = UIPickUpItemIcon.GetWidth();
    m_iPickUpItemIconHeight = UIPickUpItemIcon.GetHeight();
    m_iPickUpItemIconX = UIPickUpItemIcon.GetWndRect().left;
    m_iPickUpItemIconY = UIPickUpItemIcon.GetWndRect().top;
    //---------------------------------------------------------

    UIWeaponIcon.Enable(false);

    //индикаторы
    UIZoneMap->Init();
    UIZoneMap->SetScale(DEFAULT_MAP_SCALE);

    xml_init.InitStatic(uiXml, "static_pda_online", 0, &UIPdaOnline);
    UIZoneMap->Background()->AttachChild(&UIPdaOnline);

    //Полоса прогресса здоровья
    UIStaticHealth.AttachChild(&UIHealthBar);
    //.	xml_init.InitAutoStaticGroup(uiXml,"static_health", &UIStaticHealth);
    xml_init.InitProgressBar(uiXml, "progress_bar_health", 0, &UIHealthBar);

    //Полоса прогресса армора
    UIStaticArmor.AttachChild(&UIArmorBar);
    //.	xml_init.InitAutoStaticGroup(uiXml,"static_armor", &UIStaticArmor);
    xml_init.InitProgressBar(uiXml, "progress_bar_armor", 0, &UIArmorBar);

    // Подсказки, которые возникают при наведении прицела на объект
    AttachChild(&UIStaticQuickHelp);
    xml_init.InitStatic(uiXml, "quick_info", 0, &UIStaticQuickHelp);
    m_quick_help_xml_pos = UIStaticQuickHelp.GetWndPos();
    m_quick_help_xml_size.set(UIStaticQuickHelp.GetWidth(), UIStaticQuickHelp.GetHeight());
    m_quick_help_xml_clr = UIStaticQuickHelp.GetTextColor();
    InitInteractOverlay();

    uiXml.SetLocalRoot(uiXml.GetRoot());

    m_UIIcons = xr_new<CUIScrollView>();
    m_UIIcons->SetAutoDelete(true);
    xml_init.InitScrollView(uiXml, "icons_scroll_view", 0, m_UIIcons);
    AttachChild(m_UIIcons);

    // Загружаем иконки
    xml_init.InitStatic(uiXml, "starvation_static", 0, &UIStarvationIcon);
    UIStarvationIcon.Show(false);

    xml_init.InitStatic(uiXml, "psy_health_static", 0, &UIPsyHealthIcon);
    UIPsyHealthIcon.Show(false);

    xml_init.InitStatic(uiXml, "weapon_jammed_static", 0, &UIWeaponJammedIcon);
    UIWeaponJammedIcon.Show(false);

    xml_init.InitStatic(uiXml, "radiation_static", 0, &UIRadiaitionIcon);
    UIRadiaitionIcon.Show(false);

    xml_init.InitStatic(uiXml, "wound_static", 0, &UIWoundIcon);
    UIWoundIcon.Show(false);

    xml_init.InitStatic(uiXml, "invincible_static", 0, &UIInvincibleIcon);
    UIInvincibleIcon.Show(false);

    if (Core.Features.test(xrCore::Feature::actor_thirst))
    {
        xml_init.InitStatic(uiXml, "thirst_static", 0, &UIThirstIcon);
        UIThirstIcon.Show(false);
    }

    m_bFlashlightIcon = uiXml.NavigateToNode("flashlight_static", 0) != nullptr;
    if (m_bFlashlightIcon)
    {
        AttachChild(&UIFlashlightIcon);
        xml_init.InitStatic(uiXml, "flashlight_static", 0, &UIFlashlightIcon);
        m_xml_flashlight_pos = UIFlashlightIcon.GetWndPos();
        UIFlashlightIcon.Show(false);
    }

    constexpr const char* warningStrings[] = {
        "jammed",     "radiation", "wounds", "starvation",
        "fatigue", // PsyHealth ???
        "invincible", // Not used
        "thirst",
    };

    // Загружаем пороговые значения для индикаторов
    EWarningIcons i = ewiWeaponJammed;
    while (i <= (Core.Features.test(xrCore::Feature::actor_thirst) ? ewiThirst : ewiInvincible))
    {
        // Читаем данные порогов для каждого индикатора
        const char* cfgRecord = pSettings->r_string("main_ingame_indicators_thresholds", warningStrings[static_cast<int>(i) - 1]);
        u32 count = _GetItemCount(cfgRecord);

        char singleThreshold[8];
        float f = 0;
        for (u32 k = 0; k < count; ++k)
        {
            _GetItem(cfgRecord, k, singleThreshold);
            sscanf(singleThreshold, "%f", &f);

            m_Thresholds[i].push_back(f);
        }

        i = static_cast<EWarningIcons>(i + 1);

        if (i == ewiInvincible)
            i = static_cast<EWarningIcons>(i + 1);
    }

    // Flashing icons initialize
    uiXml.SetLocalRoot(uiXml.NavigateToNode("flashing_icons"));
    InitFlashingIcons(&uiXml);

    uiXml.SetLocalRoot(uiXml.GetRoot());

    AttachChild(&UICarPanel);
    xml_init.InitWindow(uiXml, "car_panel", 0, &UICarPanel);

    AttachChild(&UIMotionIcon);
    UIMotionIcon.Init();

    if (uiXml.NavigateToNode("artefact_panel"))
    {
        m_artefactPanel = xr_new<CUIArtefactPanel>();
        m_artefactPanel->InitFromXML(uiXml, "artefact_panel", 0);
        this->AttachChild(m_artefactPanel);
    }

    HUD_SOUND::LoadSound("maingame_ui", "snd_new_contact", m_contactSnd, SOUND_TYPE_IDLE);
}

extern u32 g_minimap_pos;

void CUIMainIngameWnd::UpdateHudClusterLayout()
{
    const int pos = static_cast<int>(g_minimap_pos);
    const bool wide10 = ui_core::is_16x10();
    if (pos == m_applied_hud_cluster_pos && wide10 == m_applied_hud_16x10)
        return;
    m_applied_hud_cluster_pos = pos;
    m_applied_hud_16x10 = wide10;

    float dx = 0.f;
    if (pos != 0)
    {
        constexpr float kLeftPad = 12.f;
        dx = kLeftPad - m_xml_health_pos.x;
    }
    if (ui_core::is_16x10())
        dx += ui_core::hud_16x10_shift;

    UIStaticHealth.SetWndPos(m_xml_health_pos.x + dx, m_xml_health_pos.y);
    UIWeaponBack.SetWndPos(m_xml_weapon_pos.x + dx, m_xml_weapon_pos.y);
    if (m_bFlashlightIcon)
    {
        float fx = m_xml_flashlight_pos.x + dx;
        float fy = m_xml_flashlight_pos.y;
        // Left-cluster used to clamp X onto the health bar. Keep it under the bar instead.
        if (fx < 4.f)
            fx = m_xml_health_pos.x + dx;
        UIFlashlightIcon.SetWndPos(fx, fy);
    }
    UIMotionIcon.ApplyClusterShift(dx);
}

float UIStaticDiskIO_start_time = 0.0f;

void CUIMainIngameWnd::Draw()
{
    if (!m_pActor)
        return;

    UIMotionIcon.SetNoise((s16)(0xffff & iFloor(m_pActor->m_snd_noise * 100.0f)));
    CUIWindow::Draw();
    UIZoneMap->Render();
}

void CUIMainIngameWnd::SetAmmoIcon(const shared_str& sect_name)
{
    if (!sect_name.size())
    {
        UIWeaponIcon.Show(false);
        return;
    };

    UIWeaponIcon.Show(true);
    // properties used by inventory menu
    CIconParams icon_params(sect_name);

    icon_params.set_shader(&UIWeaponIcon);

    float iGridWidth = icon_params.grid_width;

    float w = std::clamp(iGridWidth, 1.f, 2.f) * INV_GRID_WIDTH;
    float h = INV_GRID_HEIGHT;
    w *= UI()->get_current_kx();

    float x = UIWeaponIcon_rect.x1;
    if (iGridWidth < 2.f)
        x += w / 2.0f;

    UIWeaponIcon.SetWndPos(x, UIWeaponIcon_rect.y1);

    UIWeaponIcon.SetWidth(w);
    UIWeaponIcon.SetHeight(h);
};

void CUIMainIngameWnd::Update()
{
    m_pActor = smart_cast<CActor*>(Level().CurrentViewEntity());
    if (!m_pActor)
    {
        m_pItem = NULL;
        m_pWeapon = NULL;
        m_pGrenade = NULL;
        HideInteractPrompt();
        HideInteractDots();
        if (m_bFlashlightIcon)
            UIFlashlightIcon.Show(false);
        CUIWindow::Update();
        return;
    }

    if (!(Device.dwFrame % 30))
    {
        string256 text_str;
        CPda* _pda = m_pActor->GetPDA();
        u32 _cn = 0;
        if (_pda && 0 != (_cn = _pda->ActiveContactsNum()))
        {
            sprintf_s(text_str, "%d", _cn);
            UIPdaOnline.SetText(text_str);
        }
        else
        {
            UIPdaOnline.SetText("");
        }
    };

    if (!(Device.dwFrame % 5))
    {
        if (!(Device.dwFrame % 30))
        {
            bool b_God = GodMode();
            if (b_God)
                SetWarningIconColor(ewiInvincible, 0xffffffff);
            else if (!external_icon_ctrl)
                TurnOffWarningIcon(ewiInvincible);
        }

        // Armor indicator stuff
        PIItem pItem = m_pActor->inventory().ItemFromSlot(OUTFIT_SLOT);
        if (pItem)
        {
            UIArmorBar.Show(true);
            UIStaticArmor.Show(true);
            UIArmorBar.SetProgressPos(pItem->GetCondition() * 100);
        }
        else
        {
            UIArmorBar.Show(false);
            UIStaticArmor.Show(false);
        }

        UpdateActiveItemInfo();

        EWarningIcons i = ewiWeaponJammed;
        while (!external_icon_ctrl && i <= (Core.Features.test(xrCore::Feature::actor_thirst) ? ewiThirst : ewiInvincible))
        {
            float value{};
            switch (i)
            {
                // radiation
            case ewiRadiation: value = m_pActor->conditions().GetRadiation(); break;
            case ewiWound: value = m_pActor->conditions().BleedingSpeed(); break;
            case ewiWeaponJammed:
                if (m_pWeapon)
                    value = 1 - m_pWeapon->GetConditionToShow();
                break;
            case ewiStarvation: value = 1 - m_pActor->conditions().GetSatiety(); break;
            case ewiThirst: value = 1 - m_pActor->conditions().GetThirst(); break;
            case ewiPsyHealth: value = 1 - m_pActor->conditions().GetPsyHealth(); break;
            default: R_ASSERT(!"Unknown type of warning icon");
            }

            // Сначала проверяем на точное соответсвие
            auto rit = std::find(m_Thresholds[i].rbegin(), m_Thresholds[i].rend(), value);

            // Если его нет, то берем последнее меньшее значение ()
            if (rit == m_Thresholds[i].rend())
                rit = std::find_if(m_Thresholds[i].rbegin(), m_Thresholds[i].rend(), std::bind(std::less<float>(), std::placeholders::_1, value));

            if (rit != m_Thresholds[i].rend())
            {
                // Минимальное и максимальное значения границы
                const float min = m_Thresholds[i].front();
                const float max = m_Thresholds[i].back();

                const float v = *rit;
                const float t = std::clamp((v - min) / (max - min), 0.f, 1.f);

                static const auto min_clr{READ_IF_EXISTS(pSettings, r_fvector4, "warning_icon_color", "min", (Fvector4{255.f, 0.f, 255.f, 0.f}))};
                static const auto max_clr{READ_IF_EXISTS(pSettings, r_fvector4, "warning_icon_color", "max", (Fvector4{255.f, 255.f, 0.f, 0.f}))};

                auto to_u = [](const float x) { return static_cast<u32>(std::lround(std::clamp(x, 0.f, 255.f))); };

                SetWarningIconColor(i,
                                    color_argb(to_u(std::lerp(min_clr.x, max_clr.x, t)), to_u(std::lerp(min_clr.y, max_clr.y, t)), to_u(std::lerp(min_clr.z, max_clr.z, t)),
                                               to_u(std::lerp(min_clr.w, max_clr.w, t))));
            }
            else
                TurnOffWarningIcon(i);

            i = (EWarningIcons)(i + 1);

            if (i == ewiInvincible)
                i = (EWarningIcons)(i + 1);
        }
    }

    // health&armor
    UIHealthBar.SetProgressPos(m_pActor->GetfHealth() * 100.0f);
    UIMotionIcon.SetPower(m_pActor->conditions().GetPower() * 100.0f);

    UIZoneMap->UpdateHudLayout();
    UpdateHudClusterLayout();
    UIZoneMap->UpdateRadar(Device.vCameraPosition);
    float h, p;
    Device.vCameraDirection.getHP(h, p);
    UIZoneMap->SetHeading(-h);

    UpdatePickUpItem();
    RenderQuickInfos();
    UpdateFlashlightIcon();
    CUIWindow::Update();
}

bool CUIMainIngameWnd::OnKeyboardPress(int dik)
{
    const bool shift = pInput->iGetAsyncKeyState(DIK_LSHIFT) || pInput->iGetAsyncKeyState(DIK_RSHIFT);
    const auto bind = get_binded_action(dik);

    if (bind == kHIDEHUD)
    {
        if (shift)
        {
            UIZoneMap->ZoomOut();
        }
        else
        {
            HUD().GetUI()->HideGameIndicators();
            HUD().GetUI()->hud_disabled_by_user = true;
        }
        return true;
    }
    else if (bind == kSHOWHUD)
    {
        if (shift)
        {
            UIZoneMap->ZoomIn();
        }
        else
        {
            HUD().GetUI()->ShowGameIndicators();
            HUD().GetUI()->hud_disabled_by_user = false;
        }
        return true;
    }

    return false;
}

namespace
{
struct HudInteractCfg
{
    bool floating_prompt;
    bool hide_pickup_icon;
    bool show_item_name;
    bool quiet_action;
    bool nearby_dots;
    u32 dot_limit;
    float offset_x;
    float offset_y;
    float world_y;
    float name_offset_y;
    float action_offset_y;
    float dot_radius;
};

const HudInteractCfg& GetHudInteractCfg()
{
    static const HudInteractCfg cfg{
        !!READ_IF_EXISTS(pSettings, r_bool, "hud_interact", "floating_prompt", TRUE),
        !!READ_IF_EXISTS(pSettings, r_bool, "hud_interact", "hide_pickup_icon", TRUE),
        !!READ_IF_EXISTS(pSettings, r_bool, "hud_interact", "show_item_name", TRUE),
        !!READ_IF_EXISTS(pSettings, r_bool, "hud_interact", "quiet_action", TRUE),
        !!READ_IF_EXISTS(pSettings, r_bool, "hud_interact", "nearby_dots", TRUE),
        READ_IF_EXISTS(pSettings, r_u32, "hud_interact", "dot_limit", 12u),
        READ_IF_EXISTS(pSettings, r_float, "hud_interact", "offset_x", -13.f),
        READ_IF_EXISTS(pSettings, r_float, "hud_interact", "offset_y", 24.f),
        READ_IF_EXISTS(pSettings, r_float, "hud_interact", "world_y", 0.2f),
        READ_IF_EXISTS(pSettings, r_float, "hud_interact", "name_offset_y", 0.f),
        READ_IF_EXISTS(pSettings, r_float, "hud_interact", "action_offset_y", 0.f),
        READ_IF_EXISTS(pSettings, r_float, "hud_interact", "dot_radius", 4.f),
    };
    return cfg;
}

LPCSTR InteractObjectName(CGameObject* obj)
{
    if (!obj)
        return nullptr;

    if (auto* item = smart_cast<CInventoryItem*>(obj))
    {
        LPCSTR name = item->NameShort();
        if (!name || !name[0])
            name = item->Name();
        if (name && name[0])
            return name;
    }

    if (auto* owner = smart_cast<CInventoryOwner*>(obj))
    {
        LPCSTR name = owner->Name();
        if (name && name[0])
            return name;
    }

    return nullptr;
}

bool InteractCommunityId(CGameObject* obj, CHARACTER_COMMUNITY_ID& id)
{
    if (!obj || smart_cast<CInventoryItem*>(obj) || obj->cast_base_monster())
        return false;

    auto* owner = smart_cast<CInventoryOwner*>(obj);
    if (!owner)
        return false;

    const CHARACTER_COMMUNITY_INDEX idx = owner->CharacterInfo().Community().index();
    if (idx < 0)
        return false;

    id = CHARACTER_COMMUNITY::IndexToId(idx, nullptr, true);
    return !id.empty();
}

LPCSTR InteractFactionCaption(CGameObject* obj)
{
    CHARACTER_COMMUNITY_ID id;
    if (!InteractCommunityId(obj, id))
        return nullptr;

    const shared_str translated = CStringTable().translate(id);
    if (!translated || !translated.size())
        return nullptr;
    return *translated;
}

LPCSTR InteractFactionPatch(CGameObject* obj)
{
    CHARACTER_COMMUNITY_ID id;
    if (!InteractCommunityId(obj, id))
        return nullptr;

    LPCSTR slug = *id;
    if (!xr_strcmp(slug, "military") || !xr_strcmp(slug, "stalker_army"))
        slug = "army";
    else if (!xr_strcmp(slug, "freedom_fake") || !xr_strcmp(slug, "actor_freedom"))
        slug = "freedom";
    else if (!xr_strcmp(slug, "actor_dolg"))
        slug = "dolg";
    else if (!xr_strcmp(slug, "stalker_stalker"))
        slug = "stalker";
    else if (!xr_strcmp(slug, "stalker_bandit") || !xr_strcmp(slug, "actor_prebandit"))
        slug = "bandit";
    else if (!xr_strcmp(slug, "stalker_killer"))
        slug = "killer";

    static string64 buf;
    strconcat(sizeof(buf), buf, "ui_mm_faction_", slug);
    if (!CUITextureMaster::ItemExists(buf))
        return nullptr;
    return buf;
}

LPCSTR ShortInteractVerb(LPCSTR action_id)
{
    LPCSTR id = "st_hud_interact_use";
    LPCSTR fallback = "Use";

    if (action_id)
    {
        if (!xr_strcmp(action_id, "inventory_item_use") || !xr_strcmp(action_id, "inventory_item_use_or_drag"))
        {
            id = "st_hud_interact_take";
            fallback = "Take";
        }
        else if (!xr_strcmp(action_id, "character_use"))
        {
            id = "st_hud_interact_talk";
            fallback = "Talk";
        }
        else if (!xr_strcmp(action_id, "dead_character_use") || !xr_strcmp(action_id, "dead_character_use_or_drag"))
        {
            id = "st_hud_interact_search";
            fallback = "Search";
        }
        else if (!xr_strcmp(action_id, "dead_monster_use"))
        {
            id = "st_hud_interact_cut";
            fallback = "Cut trophy";
        }
        else if (!xr_strcmp(action_id, "dead_monster_used") || !xr_strcmp(action_id, "dead_monster_need_knife") ||
                 !xr_strcmp(action_id, "game_object_drag"))
        {
            id = "st_hud_interact_drag";
            fallback = "Drag";
        }
        else if (!xr_strcmp(action_id, "car_character_use"))
        {
            id = "st_hud_interact_enter";
            fallback = "Enter";
        }
        else if (!xr_strcmp(action_id, "unload_weapon"))
        {
            id = "st_hud_interact_unload";
            fallback = "Unload";
        }
        else
            return nullptr;
    }

    const shared_str translated = CStringTable().translate(id);
    if (!translated || !xr_strcmp(*translated, id))
        return fallback;
    return *translated;
}

LPCSTR InteractHudString(LPCSTR id, LPCSTR fallback)
{
    const shared_str translated = CStringTable().translate(id);
    if (!translated || !translated.size() || !xr_strcmp(*translated, id))
        return fallback;
    return *translated;
}

LPCSTR InteractEmptyCaption() { return InteractHudString("st_hud_interact_empty", "(Empty)"); }

LPCSTR InteractTipId(CGameObject* obj)
{
    auto* usable = smart_cast<CUsableScriptObject*>(obj);
    LPCSTR tip = usable ? usable->tip_text() : nullptr;
    return (tip && tip[0]) ? tip : nullptr;
}

LPCSTR CanonicalInteractAction(CGameObject* obj, LPCSTR actor_action)
{
    LPCSTR tip = InteractTipId(obj);
    if (tip && ShortInteractVerb(tip))
        return tip;
    if (actor_action && ShortInteractVerb(actor_action))
        return actor_action;
    return actor_action;
}

LPCSTR InteractNameOverride(LPCSTR tip)
{
    if (!tip)
        return nullptr;
    if (!xr_strcmp(tip, "dead_monster_used"))
        return InteractHudString("st_hud_interact_damaged_carcass", "Damaged carcass");
    if (!xr_strcmp(tip, "dead_monster_need_knife"))
        return InteractHudString("st_hud_interact_need_knife", "Need knife to cut trophy");
    return nullptr;
}

bool InteractLootEmpty(CGameObject* obj)
{
    if (!obj)
        return false;

    if (auto* box = smart_cast<IInventoryBox*>(obj))
        return box->IsEmpty();

    auto* alive = smart_cast<CEntityAlive*>(obj);
    auto* owner = smart_cast<CInventoryOwner*>(obj);
    if (!alive || alive->g_Alive() || !owner)
        return false;

    TIItemContainer items;
    owner->inventory().AddAvailableItems(items, false);
    return items.empty();
}

constexpr float kInteractKeyH = 20.f;
constexpr float kInteractKeySingleW = 15.f;
constexpr float kInteractKeySlice = 5.3125f;
constexpr float kInteractKeyGap = 5.f;
constexpr float kInteractDropPadX = 8.f;
constexpr float kInteractDropPadY = 4.f;
constexpr float kInteractDotSize = 6.6f;
constexpr float kInteractDotFocused = 8.8f;
constexpr float kInteractPatchW = 18.f;
constexpr float kInteractPatchH = 23.f;
constexpr float kInteractPatchGap = 4.f;

void ApplyLetterica(CUIStatic& s)
{
    CFontManager& fonts = HUD().Font();
    if (fonts.pFontLetterica16Russian)
        s.SetFont(fonts.pFontLetterica16Russian);
    s.SetTextAlignment(CGameFont::alLeft);
    s.SetTextComplexMode(false);
}

void InitHudTex(CUIStatic& s, LPCSTR id, float w, float h)
{
    s.Init(0.f, 0.f, w, h);
    s.SetAlignment(waNone);
    s.SetStretchTexture(true);
    s.InitTexture(id);
    s.TextureOn();
    s.Show(false);
}

void FitText(CUIStatic& s, LPCSTR text, u32 color)
{
    s.SetText(text ? text : "");
    s.SetTextColor(color);
    if (s.GetFont() && text && text[0])
    {
        s.AdjustWidthToText();
        s.AdjustHeightToText();
    }
}

void PlaceAt(CUIStatic& s, float x, float y)
{
    Fvector2 pos;
    pos.set(x, y);
    s.SetWndPos(pos);
    s.Show(true);
}

bool IsDragInteractAction(LPCSTR action_id)
{
    return action_id && (!xr_strcmp(action_id, "game_object_drag") || !xr_strcmp(action_id, "dead_monster_used") ||
                         !xr_strcmp(action_id, "dead_monster_need_knife"));
}

bool IsUseOrDragAction(LPCSTR action_id)
{
    return action_id && (!xr_strcmp(action_id, "dead_character_use_or_drag") || !xr_strcmp(action_id, "inventory_item_use_or_drag") ||
                         !xr_strcmp(action_id, "dead_monster_use"));
}

bool IsEatablePickup(CGameObject* obj)
{
    auto* item = smart_cast<CInventoryItem*>(obj);
    auto* eat = item ? item->cast_eatable_item() : nullptr;
    return eat && eat->Useful();
}

bool WorldWeaponHasUnloadableAmmo(CGameObject* obj)
{
    auto* mag = smart_cast<CWeaponMagazined*>(obj);
    if (!mag || mag->unlimited_ammo())
        return false;
    return mag->GetAmmoElapsed() > 0 || mag->GetAmmoElapsed2() > 0;
}

void InteractPromptWorldPos(CGameObject* obj, Fvector& world_pos)
{
    const HudInteractCfg& cfg = GetHudInteractCfg();
    auto* alive = smart_cast<CEntityAlive*>(obj);
    if (alive && alive->g_Alive() && !alive->cast_base_monster() && alive->Visual())
    {
        if (auto* k = smart_cast<IKinematics*>(alive->Visual()))
        {
            u16 bone = k->LL_BoneID("bip01_spine1");
            if (bone == BI_NONE)
                bone = k->LL_BoneID("bip01_spine2");
            if (bone == BI_NONE)
                bone = k->LL_BoneID("bip01_spine");
            if (bone != BI_NONE)
            {
                k->CalculateBones();
                Fmatrix matrix;
                matrix.mul(alive->XFORM(), k->LL_GetBoneInstance(bone).mTransform);
                world_pos = matrix.c;
                return;
            }
        }
    }

    obj->Center(world_pos);
    world_pos.y += cfg.world_y;
}

void ActionKeyLabel(LPCSTR action, char* buf, u32 sz, bool with_shift)
{
    string64 key{};
    GetActionAllBinding(action, key, sizeof(key));
    if (char* sep = strstr(key, " , "))
        *sep = 0;
    if (with_shift)
        sprintf_s(buf, sz, "Shift+%s", key);
    else
        sprintf_s(buf, sz, "%s", key);
}

void PrimaryUseKey(char* buf, u32 sz, bool with_shift) { ActionKeyLabel("use", buf, sz, with_shift); }

float LayoutKeyCap(CUIStatic& single, CUIStatic& left, CUIStatic& center, CUIStatic& right, CUIStatic& bind, float x, float y, LPCSTR key, u32 key_clr, u32 tex_clr)
{
    FitText(bind, key, key_clr);
    const bool one_char = key && xr_strlen(key) == 1;
    const float bind_w = bind.GetWidth();
    const float bind_h = bind.GetHeight();
    const float key_w = one_char ? kInteractKeySingleW : (kInteractKeySlice + _max(bind_w, 9.f) + kInteractKeySlice);

    single.SetColor(tex_clr);
    left.SetColor(tex_clr);
    center.SetColor(tex_clr);
    right.SetColor(tex_clr);

    if (one_char)
    {
        Fvector2 key_size;
        key_size.set(key_w, kInteractKeyH);
        single.SetWndSize(key_size);
        PlaceAt(single, x, y);
        left.Show(false);
        center.Show(false);
        right.Show(false);
    }
    else
    {
        single.Show(false);
        const float center_w = key_w - kInteractKeySlice * 2.f;
        Fvector2 slice_size;
        slice_size.set(kInteractKeySlice, kInteractKeyH);
        Fvector2 center_size;
        center_size.set(center_w, kInteractKeyH);
        left.SetWndSize(slice_size);
        center.SetWndSize(center_size);
        right.SetWndSize(slice_size);
        PlaceAt(left, x, y);
        PlaceAt(center, x + kInteractKeySlice, y);
        PlaceAt(right, x + kInteractKeySlice + center_w, y);
    }

    PlaceAt(bind, x + (key_w - bind_w) * 0.5f, y + (kInteractKeyH - bind_h) * 0.5f);
    return key_w;
}

void HideKeyCap(CUIStatic& single, CUIStatic& left, CUIStatic& center, CUIStatic& right, CUIStatic& bind)
{
    single.Show(false);
    left.Show(false);
    center.Show(false);
    right.Show(false);
    bind.Show(false);
}

u8 InteractFadeAlpha(float dist, float radius, float keep)
{
    if (radius <= EPS_L)
        return 255;

    const float t = _min(1.f, _max(0.f, dist / radius));
    return u8(255.f * (keep + (1.f - keep) * (1.f - t)));
}

bool IsNearbyPickupItem(CObject* obj)
{
    auto* item = smart_cast<CInventoryItem*>(obj);
    if (!item || !obj->getVisible() || obj->getDestroy())
        return false;
    if (item->object().H_Parent())
        return false;
    if (!item->CanTake())
        return false;
    if (obj->CLS_ID == CLSID_OBJECT_G_RPG7 || obj->CLS_ID == CLSID_OBJECT_G_FAKE)
        return false;
    if (auto* grenade = smart_cast<CGrenade*>(obj))
    {
        if (!grenade->Useful())
            return false;
    }
    if (auto* missile = smart_cast<CMissile*>(obj))
    {
        if (!missile->Useful())
            return false;
    }
    return true;
}

void InitTextClone(CUIStatic& dst, CUIStatic& src)
{
    dst.Init(0.f, 0.f, src.GetWidth(), src.GetHeight());
    dst.SetAlignment(waNone);
    dst.TextureOff();
    dst.SetFont(src.GetFont());
    dst.SetTextAlignment(CGameFont::alLeft);
    dst.SetTextComplexMode(false);
    dst.Show(false);
}

bool ProjectWorldToUI(const Fvector& position, Fvector2& out)
{
    Fmatrix world;
    world.identity();
    world.c.set(position);

    Fmatrix projected;
    projected.mul(Device.mFullTransform, world);

    const float clip_w = projected._44;
    if (_abs(clip_w) <= EPS_S)
        return false;

    const float x = projected._41 / clip_w;
    const float y = projected._42 / clip_w;
    const float z = projected._43 / clip_w;

    if (z < 0.f || clip_w < 0.f || _abs(x) > 1.f || _abs(y) > 1.f)
        return false;

    out.set((1.f + x) * 0.5f * UI_BASE_WIDTH, (1.f - y) * 0.5f * UI_BASE_HEIGHT);
    return true;
}
} // namespace

bool HudInteractEnabled() { return psHUD_Flags.test(HUD_INTERACT) && GetHudInteractCfg().floating_prompt; }

bool HudInteractSuppressVanillaItemLabels() { return HudInteractEnabled(); }

void CUIMainIngameWnd::InitInteractOverlay()
{
    InitTextClone(UIStaticQuickHelpSh, UIStaticQuickHelp);
    InitTextClone(UIStaticInteractName, UIStaticQuickHelp);
    InitTextClone(UIStaticInteractNameSh, UIStaticQuickHelp);
    InitTextClone(UIStaticInteractFaction, UIStaticQuickHelp);
    InitTextClone(UIStaticInteractFactionSh, UIStaticQuickHelp);
    InitTextClone(UIInteractKeyBind, UIStaticQuickHelp);
    InitTextClone(UIStaticQuickHelp2, UIStaticQuickHelp);
    InitTextClone(UIStaticQuickHelp2Sh, UIStaticQuickHelp);
    InitTextClone(UIInteractKeyBind2, UIStaticQuickHelp);
    ApplyLetterica(UIStaticInteractName);
    ApplyLetterica(UIStaticInteractNameSh);
    ApplyLetterica(UIStaticInteractFaction);
    ApplyLetterica(UIStaticInteractFactionSh);
    ApplyLetterica(UIStaticQuickHelp);
    ApplyLetterica(UIStaticQuickHelpSh);
    ApplyLetterica(UIInteractKeyBind);
    ApplyLetterica(UIStaticQuickHelp2);
    ApplyLetterica(UIStaticQuickHelp2Sh);
    ApplyLetterica(UIInteractKeyBind2);

    InitHudTex(UIInteractDrop, "ui_dotmarks_main_drop", 80.f, 28.f);
    InitHudTex(UIInteractKey, "ui_catsy_keybind_bg_single_v4", kInteractKeySingleW, kInteractKeyH);
    InitHudTex(UIInteractKeyL, "ui_catsy_keybind_bg_left_v4", kInteractKeySlice, kInteractKeyH);
    InitHudTex(UIInteractKeyC, "ui_catsy_keybind_bg_center_v4", 9.f, kInteractKeyH);
    InitHudTex(UIInteractKeyR, "ui_catsy_keybind_bg_right_v4", kInteractKeySlice, kInteractKeyH);
    InitHudTex(UIInteractKey2, "ui_catsy_keybind_bg_single_v4", kInteractKeySingleW, kInteractKeyH);
    InitHudTex(UIInteractKey2L, "ui_catsy_keybind_bg_left_v4", kInteractKeySlice, kInteractKeyH);
    InitHudTex(UIInteractKey2C, "ui_catsy_keybind_bg_center_v4", 9.f, kInteractKeyH);
    InitHudTex(UIInteractKey2R, "ui_catsy_keybind_bg_right_v4", kInteractKeySlice, kInteractKeyH);

    UIInteractFactionPatch.Init(0.f, 0.f, kInteractPatchW, kInteractPatchH);
    UIInteractFactionPatch.SetAlignment(waNone);
    UIInteractFactionPatch.SetStretchTexture(true);
    UIInteractFactionPatch.Show(false);

    DetachChild(&UIStaticQuickHelp);
    for (auto& dot : m_interact_dots)
    {
        dot.Init(0.f, 0.f, kInteractDotSize, kInteractDotSize);
        dot.SetAlignment(waCenter);
        dot.SetStretchTexture(true);
        dot.InitTexture("ui_catsy_marker_intdot");
        dot.TextureOn();
        dot.SetColor(color_rgba(255, 255, 255, 220));
        AttachChild(&dot);
        dot.Show(false);
    }
    AttachChild(&UIInteractDrop);
    AttachChild(&UIInteractFactionPatch);
    AttachChild(&UIInteractKeyL);
    AttachChild(&UIInteractKeyC);
    AttachChild(&UIInteractKeyR);
    AttachChild(&UIInteractKey);
    AttachChild(&UIInteractKey2L);
    AttachChild(&UIInteractKey2C);
    AttachChild(&UIInteractKey2R);
    AttachChild(&UIInteractKey2);
    AttachChild(&UIStaticQuickHelpSh);
    AttachChild(&UIStaticQuickHelp2Sh);
    AttachChild(&UIStaticInteractNameSh);
    AttachChild(&UIStaticInteractFactionSh);
    AttachChild(&UIInteractKeyBind);
    AttachChild(&UIInteractKeyBind2);
    AttachChild(&UIStaticInteractName);
    AttachChild(&UIStaticInteractFaction);
    AttachChild(&UIStaticQuickHelp);
    AttachChild(&UIStaticQuickHelp2);
}

void CUIMainIngameWnd::HideInteractPrompt()
{
    UIStaticQuickHelp.Show(false);
    UIStaticQuickHelpSh.Show(false);
    UIStaticQuickHelp2.Show(false);
    UIStaticQuickHelp2Sh.Show(false);
    UIStaticInteractName.Show(false);
    UIStaticInteractNameSh.Show(false);
    UIStaticInteractFaction.Show(false);
    UIStaticInteractFactionSh.Show(false);
    UIInteractFactionPatch.Show(false);
    UIInteractDrop.Show(false);
    HideKeyCap(UIInteractKey, UIInteractKeyL, UIInteractKeyC, UIInteractKeyR, UIInteractKeyBind);
    HideKeyCap(UIInteractKey2, UIInteractKey2L, UIInteractKey2C, UIInteractKey2R, UIInteractKeyBind2);
}

void CUIMainIngameWnd::HideInteractDots()
{
    for (auto& dot : m_interact_dots)
        dot.Show(false);
}

void CUIMainIngameWnd::ClearInteractCycle()
{
    m_interact_cycle_count = 0;
    m_interact_sticky = nullptr;
    m_interact_last_look = nullptr;
    m_interact_cycle_lock = false;
}

CGameObject* CUIMainIngameWnd::InteractFocusObject(CGameObject* look_at, LPCSTR look_action) const
{
    const bool look_is_pickup = look_at && IsNearbyPickupItem(look_at);
    if (look_at && look_action && look_action[0] && !look_is_pickup)
        return look_at;
    if (m_interact_sticky)
        return m_interact_sticky;
    return look_at;
}

CInventoryItem* CUIMainIngameWnd::InteractPickupItem()
{
    if (!HudInteractEnabled() || !m_pActor)
        return nullptr;

    CGameObject* look_at = m_pActor->ObjectWeLookingAt();
    CGameObject* focus = InteractFocusObject(look_at, m_pActor->GetDefaultActionForObject());
    if (!focus || !IsNearbyPickupItem(focus))
        return nullptr;
    return smart_cast<CInventoryItem*>(focus);
}

void CUIMainIngameWnd::CycleNearbyInteract()
{
    if (!HudInteractEnabled() || !m_pActor)
        return;

    UpdateNearbyInteractDots(m_pActor->ObjectWeLookingAt());
    if (m_interact_cycle_count == 0)
        return;

    u32 idx = 0;
    bool found = false;
    if (m_interact_sticky)
    {
        for (u32 i = 0; i < m_interact_cycle_count; ++i)
        {
            if (m_interact_cycle[i] == m_interact_sticky)
            {
                idx = i;
                found = true;
                break;
            }
        }
    }
    if (!found)
    {
        CGameObject* look_at = m_pActor->ObjectWeLookingAt();
        for (u32 i = 0; i < m_interact_cycle_count; ++i)
        {
            if (m_interact_cycle[i] == look_at)
            {
                idx = i;
                break;
            }
        }
    }

    m_interact_sticky = m_interact_cycle[(idx + 1) % m_interact_cycle_count];
    m_interact_cycle_lock = true;
}

void CUIMainIngameWnd::UpdateNearbyInteractDots(CGameObject* look_at)
{
    const HudInteractCfg& cfg = GetHudInteractCfg();
    if (!HudInteractEnabled() || !cfg.nearby_dots || !m_pActor)
    {
        HideInteractDots();
        ClearInteractCycle();
        return;
    }

    float radius = cfg.dot_radius;
    if (radius <= EPS_L)
        radius = m_pActor->inventory().GetTakeDist() * 2.f;

    const u32 limit = _min(cfg.dot_limit, u32(kMaxInteractDots));
    if (limit == 0 || radius <= EPS_L)
    {
        HideInteractDots();
        ClearInteractCycle();
        return;
    }

    Level().ObjectSpace.GetNearest(m_interact_nearest, m_pActor->Position(), radius, m_pActor);

    struct Candidate
    {
        CGameObject* obj;
        float dist_sq;
        Fvector2 ui;
    };
    Candidate found[kMaxInteractDots];
    u32 found_count = 0;

    const float radius_sq = radius * radius;
    for (CObject* obj : m_interact_nearest)
    {
        if (!IsNearbyPickupItem(obj))
            continue;

        auto* go = smart_cast<CGameObject*>(obj);
        if (!go)
            continue;

        Fvector world_pos;
        go->Center(world_pos);
        world_pos.y += cfg.world_y;

        const float dist_sq = m_pActor->Position().distance_to_sqr(world_pos);
        if (dist_sq > radius_sq)
            continue;

        Fvector2 ui_pos;
        if (!ProjectWorldToUI(world_pos, ui_pos))
            continue;

        u32 slot = found_count;
        if (found_count < limit)
            ++found_count;
        else
        {
            slot = 0;
            for (u32 i = 1; i < limit; ++i)
            {
                if (found[i].dist_sq > found[slot].dist_sq)
                    slot = i;
            }
            if (dist_sq >= found[slot].dist_sq)
                continue;
        }

        found[slot] = {go, dist_sq, ui_pos};
    }

    for (u32 i = 0; i + 1 < found_count; ++i)
    {
        u32 best = i;
        for (u32 j = i + 1; j < found_count; ++j)
        {
            if (found[j].dist_sq < found[best].dist_sq)
                best = j;
        }
        if (best != i)
        {
            Candidate tmp = found[i];
            found[i] = found[best];
            found[best] = tmp;
        }
    }

    m_interact_cycle_count = found_count;
    bool sticky_alive = false;
    for (u32 i = 0; i < found_count; ++i)
    {
        m_interact_cycle[i] = found[i].obj;
        if (found[i].obj == m_interact_sticky)
            sticky_alive = true;
    }
    if (!sticky_alive)
        m_interact_sticky = nullptr;

    // Empty world must not drop the cycle lock. Aim jitter onto another
    // pickup, or a real look-at change to a different loot item, may snap.
    if (look_at && IsNearbyPickupItem(look_at) && look_at != m_interact_sticky)
    {
        if (!m_interact_cycle_lock)
            m_interact_sticky = look_at;
        else if (look_at != m_interact_last_look)
        {
            m_interact_cycle_lock = false;
            m_interact_sticky = look_at;
        }
    }
    if (look_at)
        m_interact_last_look = look_at;

    CGameObject* selected = m_interact_sticky;
    u32 used = 0;
    for (u32 i = 0; i < found_count; ++i)
    {
        const bool focused = found[i].obj == selected || (!selected && found[i].obj == look_at);
        const u8 alpha = InteractFadeAlpha(_sqrt(found[i].dist_sq), radius, focused ? 0.85f : 0.35f);
        auto& dot = m_interact_dots[used++];
        const float size = focused ? kInteractDotFocused : kInteractDotSize;
        dot.SetWndSize(Fvector2().set(size, size));
        dot.SetColor(color_rgba(255, 255, 255, alpha));
        Fvector2 pos = found[i].ui;
        clamp(pos.x, 0.f, UI_BASE_WIDTH);
        clamp(pos.y, 0.f, UI_BASE_HEIGHT);
        dot.SetWndPos(pos);
        dot.Show(true);
    }
    for (; used < kMaxInteractDots; ++used)
        m_interact_dots[used].Show(false);
}

void CUIMainIngameWnd::LayoutInteractPrompt(const Fvector2& projected, LPCSTR key, LPCSTR action, LPCSTR key2, LPCSTR action2, LPCSTR name, LPCSTR faction, LPCSTR patch, u8 alpha, bool gray_patch)
{
    const HudInteractCfg& cfg = GetHudInteractCfg();
    const u32 name_clr = color_rgba(255, 255, 255, alpha);
    const u32 faction_clr = color_rgba(200, 200, 200, alpha);
    const u32 action_clr = color_rgba(240, 240, 240, alpha);
    const u32 key_clr = color_rgba(0, 0, 0, alpha);
    const u32 shadow_clr = color_argb(_min(u32(alpha), 200u), 0, 0, 0);
    const u32 tex_clr = color_rgba(255, 255, 255, alpha);
    const u32 drop_clr = color_rgba(255, 255, 255, u8(0.7f * float(alpha)));

    ApplyLetterica(UIInteractKeyBind);
    ApplyLetterica(UIStaticQuickHelp);
    ApplyLetterica(UIStaticQuickHelpSh);
    ApplyLetterica(UIInteractKeyBind2);
    ApplyLetterica(UIStaticQuickHelp2);
    ApplyLetterica(UIStaticQuickHelp2Sh);
    ApplyLetterica(UIStaticInteractName);
    ApplyLetterica(UIStaticInteractNameSh);
    ApplyLetterica(UIStaticInteractFaction);
    ApplyLetterica(UIStaticInteractFactionSh);

    const bool has_action = action && action[0] && key && key[0];
    if (has_action)
    {
        FitText(UIStaticQuickHelp, action, action_clr);
        FitText(UIStaticQuickHelpSh, action, shadow_clr);
    }
    else
    {
        HideKeyCap(UIInteractKey, UIInteractKeyL, UIInteractKeyC, UIInteractKeyR, UIInteractKeyBind);
        UIStaticQuickHelp.Show(false);
        UIStaticQuickHelpSh.Show(false);
        UIInteractDrop.Show(false);
    }

    const bool has_second = has_action && key2 && key2[0] && action2 && action2[0];
    if (has_second)
    {
        FitText(UIStaticQuickHelp2, action2, action_clr);
        FitText(UIStaticQuickHelp2Sh, action2, shadow_clr);
    }
    else
    {
        HideKeyCap(UIInteractKey2, UIInteractKey2L, UIInteractKey2C, UIInteractKey2R, UIInteractKeyBind2);
        UIStaticQuickHelp2.Show(false);
        UIStaticQuickHelp2Sh.Show(false);
    }

    const bool has_name = name && name[0];
    if (has_name)
    {
        FitText(UIStaticInteractName, name, name_clr);
        FitText(UIStaticInteractNameSh, name, shadow_clr);
    }
    else
    {
        UIStaticInteractName.Show(false);
        UIStaticInteractNameSh.Show(false);
    }

    const bool has_faction = faction && faction[0];
    if (has_faction)
    {
        FitText(UIStaticInteractFaction, faction, faction_clr);
        FitText(UIStaticInteractFactionSh, faction, shadow_clr);
    }
    else
    {
        UIStaticInteractFaction.Show(false);
        UIStaticInteractFactionSh.Show(false);
    }

    const bool has_patch = patch && patch[0] && CUITextureMaster::ItemExists(patch);
    if (has_patch)
    {
        if (!m_interact_patch_tex.size() || xr_strcmp(*m_interact_patch_tex, patch))
        {
            UIInteractFactionPatch.InitTexture(patch);
            UIInteractFactionPatch.TextureOn();
            m_interact_patch_tex = patch;
        }
        UIInteractFactionPatch.SetColor(gray_patch ? color_rgba(145, 145, 145, alpha) : tex_clr);
        Fvector2 patch_size;
        patch_size.set(kInteractPatchW, kInteractPatchH);
        UIInteractFactionPatch.SetWndSize(patch_size);
    }
    else
    {
        UIInteractFactionPatch.Show(false);
        m_interact_patch_tex = shared_str();
    }

    const float action_w = has_action ? UIStaticQuickHelp.GetWidth() : 0.f;
    const float action_h = has_action ? UIStaticQuickHelp.GetHeight() : 0.f;
    const float action2_w = has_second ? UIStaticQuickHelp2.GetWidth() : 0.f;
    const float action2_h = has_second ? UIStaticQuickHelp2.GetHeight() : 0.f;
    const float name_w = has_name ? UIStaticInteractName.GetWidth() : 0.f;
    const float name_h = has_name ? UIStaticInteractName.GetHeight() : 0.f;
    const float faction_w = has_faction ? UIStaticInteractFaction.GetWidth() : 0.f;
    const float faction_h = has_faction ? UIStaticInteractFaction.GetHeight() : 0.f;

    float key_w = 0.f;
    float key2_w = 0.f;
    if (has_action)
    {
        FitText(UIInteractKeyBind, key, key_clr);
        key_w = (xr_strlen(key) == 1) ? kInteractKeySingleW : (kInteractKeySlice + _max(UIInteractKeyBind.GetWidth(), 9.f) + kInteractKeySlice);
        if (has_second)
        {
            FitText(UIInteractKeyBind2, key2, key_clr);
            key2_w = (xr_strlen(key2) == 1) ? kInteractKeySingleW : (kInteractKeySlice + _max(UIInteractKeyBind2.GetWidth(), 9.f) + kInteractKeySlice);
        }
    }

    const float name_gap = 3.f;
    const float faction_gap = 1.f;
    const float row_gap = 4.f;
    const float pair1_w = has_action ? (key_w + kInteractKeyGap + action_w) : 0.f;
    const float pair2_w = has_second ? (key2_w + kInteractKeyGap + action2_w) : 0.f;
    const float actions_w = has_second ? _max(pair1_w, pair2_w) : pair1_w;
    const float rows_h = has_action ? (has_second ? (kInteractKeyH * 2.f + row_gap) : kInteractKeyH) : 0.f;

    float text_h = 0.f;
    if (has_name)
        text_h += name_h;
    if (has_faction)
        text_h += (text_h > 0.f ? faction_gap : 0.f) + faction_h;

    const float patch_col = has_patch ? kInteractPatchW + kInteractPatchGap : 0.f;
    const float header_text_w = _max(name_w, faction_w);
    const float header_w = patch_col + header_text_w;
    const float header_h = _max(text_h, has_patch ? kInteractPatchH : 0.f);
    const bool has_header = header_h > 0.f;

    float origin_x = projected.x + cfg.offset_x;
    float origin_y = projected.y + cfg.offset_y + cfg.action_offset_y;

    const float cluster_left = has_action ? -kInteractDropPadX : 0.f;
    const float cluster_top = has_action ? (has_header ? -(header_h + name_gap) + cfg.name_offset_y : -kInteractDropPadY) : 0.f;
    const float cluster_right = _max(has_action ? actions_w + kInteractDropPadX : 0.f, has_header ? header_w : 0.f);
    const float cluster_bottom = has_action ? (rows_h + kInteractDropPadY) : (has_header ? header_h : 0.f);

    if (origin_x + cluster_left < 0.f)
        origin_x = -cluster_left;
    if (origin_x + cluster_right > UI_BASE_WIDTH)
        origin_x = UI_BASE_WIDTH - cluster_right;
    if (origin_y + cluster_top < 0.f)
        origin_y = -cluster_top;
    if (origin_y + cluster_bottom > UI_BASE_HEIGHT)
        origin_y = UI_BASE_HEIGHT - cluster_bottom;

    constexpr float shadow = 2.f;
    if (has_action)
    {
        UIInteractDrop.SetColor(drop_clr);
        Fvector2 drop_size;
        drop_size.set(actions_w + kInteractDropPadX * 2.f, rows_h + kInteractDropPadY * 2.f);
        UIInteractDrop.SetWndSize(drop_size);
        PlaceAt(UIInteractDrop, origin_x - kInteractDropPadX, origin_y - kInteractDropPadY);

        LayoutKeyCap(UIInteractKey, UIInteractKeyL, UIInteractKeyC, UIInteractKeyR, UIInteractKeyBind, origin_x, origin_y, key, key_clr, tex_clr);

        const float action_x = origin_x + key_w + kInteractKeyGap;
        const float action_y = origin_y + (kInteractKeyH - action_h) * 0.5f;
        PlaceAt(UIStaticQuickHelpSh, action_x + shadow, action_y + shadow);
        PlaceAt(UIStaticQuickHelp, action_x, action_y);

        if (has_second)
        {
            const float y2 = origin_y + kInteractKeyH + row_gap;
            LayoutKeyCap(UIInteractKey2, UIInteractKey2L, UIInteractKey2C, UIInteractKey2R, UIInteractKeyBind2, origin_x, y2, key2, key_clr, tex_clr);
            const float action2_x = origin_x + key2_w + kInteractKeyGap;
            const float action2_y = y2 + (kInteractKeyH - action2_h) * 0.5f;
            PlaceAt(UIStaticQuickHelp2Sh, action2_x + shadow, action2_y + shadow);
            PlaceAt(UIStaticQuickHelp2, action2_x, action2_y);
        }
    }

    if (has_header)
    {
        const float header_top = has_action ? (origin_y - header_h - name_gap + cfg.name_offset_y) : origin_y;
        const float text_x = origin_x + patch_col;
        float text_y = header_top + (header_h - text_h) * 0.5f;

        if (has_patch)
            PlaceAt(UIInteractFactionPatch, origin_x, header_top + (header_h - kInteractPatchH) * 0.5f);

        if (has_name)
        {
            PlaceAt(UIStaticInteractNameSh, text_x + shadow, text_y + shadow);
            PlaceAt(UIStaticInteractName, text_x, text_y);
            text_y += name_h + (has_faction ? faction_gap : 0.f);
        }
        if (has_faction)
        {
            PlaceAt(UIStaticInteractFactionSh, text_x + shadow, text_y + shadow);
            PlaceAt(UIStaticInteractFaction, text_x, text_y);
        }
    }
}

void CUIMainIngameWnd::RenderQuickInfos()
{
    if (!m_pActor)
    {
        HideInteractPrompt();
        HideInteractDots();
        ClearInteractCycle();
        return;
    }

    static CGameObject* pObject = nullptr;
    LPCSTR actor_action = m_pActor->GetDefaultActionForObject();
    CGameObject* look_at = m_pActor->ObjectWeLookingAt();
    const HudInteractCfg& cfg = GetHudInteractCfg();
    const bool interact = HudInteractEnabled();

    if (interact)
        UpdateNearbyInteractDots(look_at);
    else
    {
        HideInteractDots();
        ClearInteractCycle();
    }

    CGameObject* focus = look_at;
    LPCSTR prompt_action = actor_action;
    if (interact)
    {
        focus = InteractFocusObject(look_at, actor_action);
        if (focus && focus != look_at)
            prompt_action = "inventory_item_use";
        else
            prompt_action = CanonicalInteractAction(focus, actor_action);
    }

    LPCSTR object_name = (interact && cfg.show_item_name) ? InteractObjectName(focus) : nullptr;
    LPCSTR name_override = interact ? InteractNameOverride(InteractTipId(focus)) : nullptr;
    if (name_override)
        object_name = name_override;
    static string256 empty_name;
    if (interact && focus && !name_override && InteractLootEmpty(focus))
    {
        LPCSTR empty = InteractEmptyCaption();
        if (object_name && object_name[0])
            strconcat(sizeof(empty_name), empty_name, object_name, " ", empty);
        else
            strconcat(sizeof(empty_name), empty_name, empty);
        object_name = empty_name;
    }
    const bool has_name = object_name && object_name[0];
    bool has_prompt = prompt_action && prompt_action[0];
    if (has_prompt && focus && !xr_strcmp(prompt_action, "character_use"))
    {
        auto* usable = smart_cast<CUsableScriptObject*>(focus);
        if (usable && !usable->nonscript_usable() && !usable->tip_text())
            has_prompt = false;
    }
    bool show = has_prompt || (interact && has_name && focus);
    Fvector2 ui_pos{};
    if (show && interact)
    {
        if (!focus)
            show = false;
        else
        {
            Fvector world_pos;
            InteractPromptWorldPos(focus, world_pos);
            if (!ProjectWorldToUI(world_pos, ui_pos))
                show = false;
        }
    }
    else if (!interact)
    {
        HideInteractPrompt();
        UIStaticQuickHelp.SetWndPos(m_quick_help_xml_pos);
        UIStaticQuickHelp.SetWndSize(m_quick_help_xml_size);
    }

    if (!show)
    {
        if (interact)
            HideInteractPrompt();
        else
            UIStaticQuickHelp.Show(false);
        pObject = look_at;
        return;
    }

    const bool object_changed = pObject != focus;
    const float dist = focus ? m_pActor->Position().distance_to(focus->Position()) : 0.f;
    const float fade_radius = m_pActor->inventory().GetTakeDist();
    const u8 alpha = InteractFadeAlpha(dist, fade_radius, 0.55f);

    if (interact)
    {
        UIStaticQuickHelp.SetClrLightAnim(nullptr);
        UIStaticQuickHelp.TextureOff();

        string128 key{};
        LPCSTR verb = nullptr;
        if (has_prompt)
        {
            PrimaryUseKey(key, sizeof(key), IsDragInteractAction(prompt_action));
            verb = cfg.quiet_action ? ShortInteractVerb(prompt_action) : nullptr;
            if (!verb || !verb[0])
            {
                if (object_changed || _stricmp(prompt_action, UIStaticQuickHelp.GetText()))
                    UIStaticQuickHelp.SetTextST(prompt_action);
                verb = UIStaticQuickHelp.GetText();
            }
            if (verb && !verb[0])
                verb = nullptr;
        }

        string128 key2{};
        LPCSTR verb2 = nullptr;
        if (verb && WorldWeaponHasUnloadableAmmo(focus))
        {
            ActionKeyLabel("quick_use", key2, sizeof(key2), false);
            verb2 = ShortInteractVerb("unload_weapon");
        }
        else if (verb && IsEatablePickup(focus))
        {
            ActionKeyLabel("quick_use", key2, sizeof(key2), false);
            verb2 = ShortInteractVerb(nullptr);
        }
        else if (verb && IsUseOrDragAction(prompt_action))
        {
            ActionKeyLabel("use", key2, sizeof(key2), true);
            verb2 = ShortInteractVerb("game_object_drag");
        }

        LPCSTR faction = InteractFactionCaption(focus);
        LPCSTR patch = InteractFactionPatch(focus);
        auto* alive = smart_cast<CEntityAlive*>(focus);
        const bool gray_patch = alive && !alive->g_Alive();
        LayoutInteractPrompt(ui_pos, verb ? key : nullptr, verb, verb2 ? key2 : nullptr, verb2, object_name, faction, patch, alpha, gray_patch);
    }
    else
    {
        if (object_changed || _stricmp(actor_action, UIStaticQuickHelp.GetText()))
        {
            UIStaticQuickHelp.SetTextST(actor_action);
            UIStaticQuickHelp.SetTextComplexMode(true);
        }
        UIStaticQuickHelp.Show(true);
        UIStaticQuickHelpSh.Show(false);
        UIStaticInteractName.Show(false);
        UIStaticInteractNameSh.Show(false);
        UIStaticInteractFaction.Show(false);
        UIStaticInteractFactionSh.Show(false);
        UIInteractFactionPatch.Show(false);
        UIInteractDrop.Show(false);
        UIInteractKey.Show(false);
        UIInteractKeyL.Show(false);
        UIInteractKeyC.Show(false);
        UIInteractKeyR.Show(false);
        UIInteractKeyBind.Show(false);
    }

    if (object_changed)
        pObject = focus;
}

void CUIMainIngameWnd::ReceiveNews(GAME_NEWS_DATA* news)
{
    VERIFY(news->texture_name.size());

    HUD().GetUI()->m_pMessagesWnd->AddIconedPdaMessage(*(news->texture_name), news->tex_rect, news->SingleLineText(), news->show_time);
}

void CUIMainIngameWnd::SetWarningIconColor(CUIStatic* s, const u32 cl)
{
    int bOn = (cl >> 24);
    bool bIsShown = s->IsShown();

    if (bOn)
        s->SetColor(cl);

    if (bOn && !bIsShown)
    {
        m_UIIcons->AddWindow(s, false);
        s->Show(true);
    }

    if (!bOn && bIsShown)
    {
        m_UIIcons->RemoveWindow(s);
        s->Show(false);
    }
}

void CUIMainIngameWnd::SetWarningIconColor(EWarningIcons icon, const u32 cl)
{
    bool bMagicFlag = true;

    // Задаем цвет требуемой иконки
    switch (icon)
    {
    case ewiAll: bMagicFlag = false;
    case ewiWeaponJammed:
        SetWarningIconColor(&UIWeaponJammedIcon, cl);
        if (bMagicFlag)
            break;
    case ewiRadiation:
        SetWarningIconColor(&UIRadiaitionIcon, cl);
        if (bMagicFlag)
            break;
    case ewiWound:
        SetWarningIconColor(&UIWoundIcon, cl);
        if (bMagicFlag)
            break;
    case ewiStarvation:
        SetWarningIconColor(&UIStarvationIcon, cl);
        if (bMagicFlag)
            break;
    case ewiThirst:
        SetWarningIconColor(&UIThirstIcon, cl);
        if (bMagicFlag)
            break;
    case ewiPsyHealth:
        SetWarningIconColor(&UIPsyHealthIcon, cl);
        if (bMagicFlag)
            break;
    case ewiInvincible:
        SetWarningIconColor(&UIInvincibleIcon, cl);
        if (bMagicFlag)
            break;
        break;

    default: R_ASSERT(!"Unknown warning icon type");
    }
}

void CUIMainIngameWnd::TurnOffWarningIcon(EWarningIcons icon) { SetWarningIconColor(icon, 0x00ffffff); }

void CUIMainIngameWnd::SetFlashIconState_(EFlashingIcons type, bool enable)
{
    // Включаем анимацию требуемой иконки
    FlashingIcons_it icon = m_FlashingIcons.find(type);
    R_ASSERT2(icon != m_FlashingIcons.end(), "Flashing icon with this type not existed");
    icon->second->Show(enable);
}

void CUIMainIngameWnd::InitFlashingIcons(CUIXml* node)
{
    const char* const flashingIconNodeName = "flashing_icon";
    int staticsCount = node->GetNodesNum("", 0, flashingIconNodeName);

    CUIXmlInit xml_init;
    CUIStatic* pIcon = NULL;
    // Пробегаемся по всем нодам и инициализируем из них статики
    for (int i = 0; i < staticsCount; ++i)
    {
        pIcon = xr_new<CUIStatic>();
        xml_init.InitStatic(*node, flashingIconNodeName, i, pIcon);
        shared_str iconType = node->ReadAttrib(flashingIconNodeName, i, "type", "none");

        // Теперь запоминаем иконку и ее тип
        EFlashingIcons type = efiPdaTask;

        if (iconType == "pda")
            type = efiPdaTask;
        else if (iconType == "mail")
            type = efiMail;
        else
            R_ASSERT(!"Unknown type of mainingame flashing icon");

        R_ASSERT2(m_FlashingIcons.find(type) == m_FlashingIcons.end(), "Flashing icon with this type already exists");

        CUIStatic*& val = m_FlashingIcons[type];
        val = pIcon;

        AttachChild(pIcon);
        pIcon->Show(false);
    }
}

void CUIMainIngameWnd::DestroyFlashingIcons()
{
    for (FlashingIcons_it it = m_FlashingIcons.begin(); it != m_FlashingIcons.end(); ++it)
    {
        DetachChild(it->second);
        xr_delete(it->second);
    }

    m_FlashingIcons.clear();
}

void CUIMainIngameWnd::UpdateFlashingIcons()
{
    for (FlashingIcons_it it = m_FlashingIcons.begin(); it != m_FlashingIcons.end(); ++it)
    {
        it->second->Update();
    }
}

void CUIMainIngameWnd::AnimateContacts(bool b_snd)
{
    UIPdaOnline.ResetClrAnimation();

    if (b_snd)
        HUD_SOUND::PlaySound(m_contactSnd, Fvector().set(0, 0, 0), 0, true);
}

void CUIMainIngameWnd::SetPickUpItem(CInventoryItem* PickUpItem)
{
    //	m_pPickUpItem = PickUpItem;
    if (m_pPickUpItem != PickUpItem)
    {
        m_pPickUpItem = PickUpItem;
        UIPickUpItemIcon.Show(false);
        UIPickUpItemIcon.DetachAll();
    }
};

#include "UICellCustomItems.h"
#include "../game_object_space.h"
#include "../script_callback_ex.h"
#include "../script_game_object.h"
#include "../Actor.h"

typedef CUIWeaponCellItem::eAddonType eAddonType;

CUIStatic* init_addon(CUIWeaponCellItem* cell_item, LPCSTR sect, float scale, float scale_x, eAddonType idx)
{
    CUIStatic* addon = xr_new<CUIStatic>();
    addon->SetAutoDelete(true);

    auto pos = cell_item->get_addon_offset(idx);
    pos.x *= scale * scale_x;
    pos.y *= scale;

    CIconParams params(sect);
    Frect rect = params.original_rect();
    params.set_shader(addon);
    addon->SetWndRect(pos.x, pos.y, rect.width() * scale * scale_x, rect.height() * scale);
    addon->SetColor(color_rgba(255, 255, 255, 192));

    return addon;
}

void CUIMainIngameWnd::UpdatePickUpItem()
{
    if (!m_pPickUpItem || !Level().CurrentViewEntity() || Level().CurrentViewEntity()->CLS_ID != CLSID_OBJECT_ACTOR ||
        (HudInteractEnabled() && GetHudInteractCfg().hide_pickup_icon))
    {
        if (UIPickUpItemIcon.IsShown())
        {
            UIPickUpItemIcon.Show(false);
        }

        return;
    };
    if (UIPickUpItemIcon.IsShown())
        return;

    // properties used by inventory menu
    CIconParams& params = m_pPickUpItem->m_icon_params;
    Frect rect = params.original_rect();

    float scale_x = m_iPickUpItemIconWidth / rect.width();

    float scale_y = m_iPickUpItemIconHeight / rect.height();

    scale_x = (scale_x > 1) ? 1.0f : scale_x;
    scale_y = (scale_y > 1) ? 1.0f : scale_y;

    float scale = scale_x < scale_y ? scale_x : scale_y;

    params.set_shader(&UIPickUpItemIcon);

    UIPickUpItemIcon.SetWidth(rect.width() * scale * UI()->get_current_kx());
    UIPickUpItemIcon.SetHeight(rect.height() * scale);

    UIPickUpItemIcon.SetWndPos(m_iPickUpItemIconX + (m_iPickUpItemIconWidth - UIPickUpItemIcon.GetWidth()) / 2,
                               m_iPickUpItemIconY + (m_iPickUpItemIconHeight - UIPickUpItemIcon.GetHeight()) / 2);

    UIPickUpItemIcon.SetColor(color_rgba(255, 255, 255, 192));
    if (auto wpn = m_pPickUpItem->cast_weapon())
    {
        CUIWeaponCellItem cell_item{wpn};

        if (wpn->SilencerAttachable() && wpn->IsSilencerAttached())
        {
            auto sil = init_addon(&cell_item, *wpn->GetSilencerName(), scale, UI()->get_current_kx(), eAddonType::eSilencer);
            UIPickUpItemIcon.AttachChild(sil);
        }

        if (wpn->ScopeAttachable() && wpn->IsScopeAttached())
        {
            auto scope = init_addon(&cell_item, *wpn->GetScopeName(), scale, UI()->get_current_kx(), eAddonType::eScope);
            UIPickUpItemIcon.AttachChild(scope);
        }

        if (wpn->GrenadeLauncherAttachable() && wpn->IsGrenadeLauncherAttached())
        {
            auto launcher = init_addon(&cell_item, *wpn->GetGrenadeLauncherName(), scale, UI()->get_current_kx(), eAddonType::eLauncher);
            UIPickUpItemIcon.AttachChild(launcher);
        }
    }

    // Real Wolf: Колбек для скриптового добавления своих иконок. 10.08.2014.
    g_actor->callback(GameObject::eUIPickUpItemShowing)(m_pPickUpItem->object().lua_game_object(), &UIPickUpItemIcon);

    UIPickUpItemIcon.Show(true);
};

void CUIMainIngameWnd::UpdateFlashlightIcon()
{
    if (!m_bFlashlightIcon)
        return;

    bool on = false;
    if (CTorch* torch = smart_cast<CTorch*>(m_pActor->inventory().ItemFromSlot(TORCH_SLOT)))
        on = torch->torch_active();

    if (!on)
    {
        if (CWeapon* wpn = smart_cast<CWeapon*>(m_pActor->inventory().ActiveItem()))
            on = wpn->IsFlashlightOn();
    }

    UIFlashlightIcon.Show(on);
}

void CUIMainIngameWnd::UpdateActiveItemInfo()
{
    PIItem item = m_pActor->inventory().ActiveItem();
    if (item && item->NeedBriefInfo())
    {
        xr_string str_name;
        xr_string icon_sect_name;
        xr_string str_count;
        item->GetBriefInfo(str_name, icon_sect_name, str_count);

        UIWeaponSignAmmo.Show(true);
        UIWeaponBack.SetText(str_name.c_str());
        UIWeaponSignAmmo.SetText(str_count.c_str());
        SetAmmoIcon(icon_sect_name.c_str());

        //-------------------
        m_pWeapon = smart_cast<CWeapon*>(item);
    }
    else
    {
        UIWeaponIcon.Show(false);
        UIWeaponSignAmmo.Show(false);
        UIWeaponBack.SetText("");
        m_pWeapon = item ? smart_cast<CWeapon*>(item) : nullptr;
    }
}

void CUIMainIngameWnd::OnConnected() { UIZoneMap->SetupCurrentMap(); }

void CUIMainIngameWnd::reset_ui()
{
    m_pActor = NULL;
    m_pWeapon = NULL;
    m_pGrenade = NULL;
    m_pItem = NULL;
    m_pPickUpItem = NULL;
    UIMotionIcon.ResetVisibility();
    HideInteractPrompt();
    HideInteractDots();
    ClearInteractCycle();
}

using namespace luabind::detail;

template <typename T>
bool test_push_window(lua_State* L, CUIWindow* wnd)
{
    T* derived = smart_cast<T*>(wnd);
    if (derived)
    {
        convert_to_lua<T*>(L, derived);
        return true;
    }
    return false;
}

void GetStaticRaw(CUIMainIngameWnd* wnd, lua_State* L)
{
    // wnd->GetChildWndList();
    shared_str name = lua_tostring(L, 2);
    CUIWindow* child = wnd->FindChild(name, 2);
    if (!child)
    {
        CUIStatic* src = wnd->GetUIZoneMap()->Background();
        child = src->FindChild(name, 5);

        if (!child)
        {
            src = wnd->GetUIZoneMap()->ClipFrame();
            child = src->FindChild(name, 5);
        }
        if (!child)
        {
            src = wnd->GetUIZoneMap()->Compass();
            child = src->FindChild(name, 5);
        }
    }

    if (child)
    {
        // if (test_push_window<CUIMotionIcon>  (L, child)) return;
        if (test_push_window<CUIProgressBar>(L, child))
            return;
        if (test_push_window<CUIStatic>(L, child))
            return;
        if (test_push_window<CUIWindow>(L, child))
            return;
    }
    lua_pushnil(L);
}

using namespace luabind;


void CUIMainIngameWnd::script_register(lua_State* L)
{
    module(L)[(
        class_<CUIMainIngameWnd, CUIWindow>("CUIMainIngameWnd")
            .def("GetStatic", &GetStaticRaw, raw<2>()),

        def("get_main_window", &GetMainIngameWindow), // get_mainingame_window better??
        def("setup_game_icon", &SetupGameIcon)
    )];
}
