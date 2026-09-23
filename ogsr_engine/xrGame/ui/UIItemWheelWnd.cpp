#include "stdafx.h"
#include "UIItemWheelWnd.h"

#include "xrUIXmlParser.h"
#include "UIXmlInit.h"
#include "../actor.h"
#include "../inventory.h"
#include "../eatable_item.h"
#include "../Medkit.h"
#include "../Antirad.h"
#include "../Grenade.h"
#include "../CustomDetector.h"
#include "../Weapon.h"
#include "../scope.h"
#include "../silencer.h"
#include "../grenadelauncher.h"
#include "../player_hud.h"
#include "../hudmanager.h"
#include "../string_table.h"
#include "../xr_level_controller.h"
#include "../xrMessages.h"
#include "../../xr_3da/xr_input.h"
#include "UIIconParams.h"

#include <dinput.h>

// Quick Action Wheel art by Aoldri / Maid, used with permission from HarukaSai.

u32 g_item_wheel = 1;
char g_item_wheel_pins[2048] = {0};

namespace
{
constexpr u32 kMaxWheelSlices = 12;
constexpr float kIconSize = 42.f;
constexpr float kDeadzoneSq = 24.f * 24.f;

LPCSTR tab_titles[eItemWheelTabCount] = {"st_item_wheel_pins", "st_item_wheel_meds", "st_item_wheel_food", "st_item_wheel_grenades", "st_item_wheel_attachments"};
LPCSTR tab_textures[eItemWheelTabCount] = {"ui_qaw_category_devices", "ui_qaw_category_meds", "ui_qaw_category_food", "ui_qaw_category_grenades", "ui_qaw_category_attachments"};

xr_vector<shared_str> s_pins;

void trim_inplace(char* s)
{
    char* src = s;
    while (*src == ' ' || *src == '\t')
        ++src;
    if (src != s)
        memmove(s, src, xr_strlen(src) + 1);

    size_t n = xr_strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = 0;
}

void ParsePins()
{
    s_pins.clear();
    if (!g_item_wheel_pins[0])
        return;

    const int n = _GetItemCount(g_item_wheel_pins);
    string256 tok{};
    for (int i = 0; i < n; ++i)
    {
        _GetItem(g_item_wheel_pins, i, tok);
        trim_inplace(tok);
        if (!tok[0])
            continue;

        bool dup = false;
        for (const shared_str& p : s_pins)
        {
            if (p == tok)
            {
                dup = true;
                break;
            }
        }
        if (!dup)
            s_pins.push_back(tok);
    }
}

void WritePins()
{
    g_item_wheel_pins[0] = 0;
    for (u32 i = 0; i < s_pins.size(); ++i)
    {
        if (i)
            xr_strcat(g_item_wheel_pins, sizeof(g_item_wheel_pins), ",");
        xr_strcat(g_item_wheel_pins, sizeof(g_item_wheel_pins), s_pins[i].c_str());
    }
}

// R3's usable radio inherits antir_f to receive scripted use callbacks.
// Keep it pinnable, but do not classify the device as medicine or food.
bool IsRadio(CInventoryItem* item) { return item && item->object().cNameSect() == "hand_radio_f"; }

bool IsMed(CInventoryItem* item) { return !IsRadio(item) && (smart_cast<CMedkit*>(item) || smart_cast<CAntirad*>(item)); }

bool IsFood(CInventoryItem* item) { return !IsRadio(item) && smart_cast<CEatableItem*>(item) && !IsMed(item); }

bool IsGrenade(CInventoryItem* item) { return smart_cast<CGrenade*>(item); }

bool IsDetector(CInventoryItem* item) { return smart_cast<CCustomDetector*>(item); }

bool IsAddonItem(CInventoryItem* item)
{
    return smart_cast<CScope*>(item) || smart_cast<CSilencer*>(item) || smart_cast<CGrenadeLauncher*>(item);
}

bool WeaponBusy(CWeapon* wpn)
{
    if (!wpn)
        return true;
    return wpn->GetState() != CHudItem::eIdle;
}

void PlaceCentered(CUIStatic* s, float cx, float cy)
{
    const Fvector2 sz = s->GetWndSize();
    s->SetWndPos(cx - sz.x * 0.5f, cy - sz.y * 0.5f);
}

float WrapTwoPi(float a)
{
    while (a < 0.f)
        a += PI_MUL_2;
    while (a >= PI_MUL_2)
        a -= PI_MUL_2;
    return a;
}

float AngleDelta(float a, float b)
{
    float d = _abs(WrapTwoPi(a) - WrapTwoPi(b));
    if (d > PI)
        d = PI_MUL_2 - d;
    return d;
}
} // namespace

bool ItemWheel_CanPin(CInventoryItem* item)
{
    if (!item)
        return false;
    return smart_cast<CEatableItem*>(item) || smart_cast<CGrenade*>(item) || IsDetector(item);
}

bool ItemWheel_HasPin(const shared_str& section)
{
    ParsePins();
    for (const shared_str& p : s_pins)
    {
        if (p == section)
            return true;
    }
    return false;
}

void ItemWheel_AddPin(const shared_str& section)
{
    if (!section.size())
        return;
    ParsePins();
    for (const shared_str& p : s_pins)
    {
        if (p == section)
            return;
    }
    if (s_pins.size() >= kMaxWheelSlices)
        return;
    s_pins.push_back(section);
    WritePins();
}

void ItemWheel_RemovePin(const shared_str& section)
{
    ParsePins();
    for (auto it = s_pins.begin(); it != s_pins.end(); ++it)
    {
        if (*it == section)
        {
            s_pins.erase(it);
            WritePins();
            return;
        }
    }
}

u32 ItemWheel_PinCount()
{
    ParsePins();
    return (u32)s_pins.size();
}

CUIItemWheelWnd::CUIItemWheelWnd()
{
    CUIXml uiXml;
    const bool xml_result = uiXml.Init(CONFIG_PATH, UI_PATH, "ui_item_wheel.xml");
    R_ASSERT3(xml_result, "file parsing error ", uiXml.m_xml_file_name);

    CUIXmlInit xml_init;
    xml_init.InitWindow(uiXml, "main", 0, this);

    AttachChild(&m_bg);
    xml_init.InitStatic(uiXml, "bg", 0, &m_bg);

    AttachChild(&m_cursor);
    xml_init.InitStatic(uiXml, "cursor", 0, &m_cursor);
    m_cursor.EnableHeading(true);

    AttachChild(&m_items_root);
    m_items_root.Init(0.f, 0.f, GetWidth(), GetHeight());

    AttachChild(&m_category);
    xml_init.InitStatic(uiXml, "tab_category", 0, &m_category);

    AttachChild(&m_title);
    xml_init.InitStatic(uiXml, "title", 0, &m_title);

    CUIStatic hover_tmp, count_tmp;
    xml_init.InitStatic(uiXml, "hover", 0, &hover_tmp);
    xml_init.InitStatic(uiXml, "count", 0, &count_tmp);
    m_hover_sz = hover_tmp.GetWndSize();
    m_count_sz = count_tmp.GetWndSize();

    xml_init.InitStatic(uiXml, "tab_icon", 0, &m_tab_icons[0]);
    const Fvector2 tab_sz = m_tab_icons[0].GetWndSize();
    for (u32 i = 0; i < eItemWheelTabCount; ++i)
    {
        AttachChild(&m_tab_icons[i]);
        if (i)
        {
            m_tab_icons[i].InitTexture("ui_qaw_tab_icon");
            m_tab_icons[i].SetStretchTexture(true);
            m_tab_icons[i].SetWndSize(tab_sz);
        }

        m_tab_icons[i].AttachChild(&m_tab_logos[i]);
        m_tab_logos[i].InitTexture(tab_textures[i]);
        m_tab_logos[i].SetStretchTexture(true);

        m_tab_icons[i].AttachChild(&m_tab_nums[i]);
        string16 num{};
        xr_sprintf(num, "%u", i + 1);
        m_tab_nums[i].SetFont(UI()->Font()->pFontLetterica16Russian);
        m_tab_nums[i].SetText(num);
        m_tab_nums[i].SetTextColor(color_rgba(255, 255, 255, 255));
        m_tab_nums[i].SetTextAlignment(CGameFont::alCenter);
    }
    m_category.Show(false);

    create_ui_snd(m_snd_attach, "interface\\inv_attach_addon");
    create_ui_snd(m_snd_detach, "interface\\inv_detach_addon");

    LayoutChrome();

    SetTab(eItemWheelMeds);
    Hide();
}

void CUIItemWheelWnd::LayoutChrome()
{
    m_aspect_kx = 1.f;
    if (UI()->is_widescreen())
    {
        const float kx = UI()->get_current_kx();
        if (kx > 0.05f)
            m_aspect_kx = kx;
    }

    Fvector2 bp = m_bg.GetWndPos();
    Fvector2 bs = m_bg.GetWndSize();
    if (m_aspect_kx < 0.999f)
    {
        const float new_h = bs.y / m_aspect_kx;
        bp.y += (bs.y - new_h) * 0.5f;
        bs.y = new_h;
        m_bg.SetWndPos(bp);
        m_bg.SetWndSize(bs);

        Fvector2 cs = m_cursor.GetWndSize();
        m_cursor.SetWndSize(Fvector2().set(cs.x, cs.y / m_aspect_kx));
    }

    m_center.set(bp.x + bs.x * 0.5f, bp.y + bs.y * 0.5f);
    m_radius_y = m_radius / m_aspect_kx;

    m_cursor.SetWndPos(m_center.x - m_cursor.GetWidth() * 0.5f, m_center.y - m_cursor.GetHeight());
    m_cursor.SetHeadingPivot(Fvector2().set(m_cursor.GetWidth() * 0.5f, m_cursor.GetHeight()), Fvector2().set(0.f, 0.f), false);

    LayoutTabs();
}

void CUIItemWheelWnd::LayoutTabs()
{
    constexpr float kBase = 36.f;
    constexpr float kSpacingDeg = 11.f;
    constexpr float kRadius = 237.f;
    const float start_deg = -((eItemWheelTabCount - 1) * kSpacingDeg) * 0.5f;
    const float rx = kRadius;
    const float ry = kRadius / m_aspect_kx;

    for (u32 i = 0; i < eItemWheelTabCount; ++i)
    {
        const bool sel = (i == u32(m_tab));
        const float scale = sel ? 1.45f : 1.f;
        const float w = kBase * scale;
        const float h = (kBase * scale) / m_aspect_kx;
        const float ang = (start_deg + float(i) * kSpacingDeg) * PI / 180.f;
        const float px = m_center.x + _cos(ang) * rx;
        const float py = m_center.y + _sin(ang) * ry;
        m_tab_icons[i].SetWndSize(Fvector2().set(w, h));
        m_tab_icons[i].SetWndPos(px - w * 0.5f, py - h * 0.5f);

        const float lw = w * 0.65f;
        const float lh = h * 0.65f;
        m_tab_logos[i].SetWndSize(Fvector2().set(lw, lh));
        m_tab_logos[i].SetWndPos((w - lw) * 0.5f, (h - lh) * 0.5f);

        m_tab_nums[i].SetWndSize(Fvector2().set(15.f, 15.f));
        m_tab_nums[i].SetWndPos(w - 14.f, 0.f);

        if (sel)
        {
            m_tab_icons[i].SetColor(color_rgba(255, 255, 255, 255));
            m_tab_logos[i].SetColor(color_rgba(0, 0, 0, 255));
        }
        else
        {
            m_tab_icons[i].SetColor(color_rgba(40, 40, 40, 160));
            m_tab_logos[i].SetColor(color_rgba(255, 255, 255, 150));
        }
    }
}

float CUIItemWheelWnd::CursorAngle(float dx, float dy) const
{
    return WrapTwoPi(atan2(dx, -dy * m_aspect_kx));
}

CUIItemWheelWnd::~CUIItemWheelWnd() { ClearSlices(); }

void CUIItemWheelWnd::Show()
{
    m_open_time = Device.dwTimeContinual;
    m_inv_frame = u32(-1);
    inherited::Show();
    Rebuild();
}

void CUIItemWheelWnd::Hide()
{
    ClearSlices();
    inherited::Hide();
}

void CUIItemWheelWnd::ClearSlices()
{
    m_items_root.DetachAll();
    m_slices.clear();
    m_hovered = -1;
}

void CUIItemWheelWnd::SetTab(EItemWheelTab tab)
{
    if (tab >= eItemWheelTabCount)
        tab = eItemWheelMeds;
    m_tab = tab;
    m_title.SetTextST(tab_titles[m_tab]);
    m_category.Show(false);
    LayoutTabs();

    m_inv_frame = u32(-1);
    if (IsShown())
        Rebuild();
}

void CUIItemWheelWnd::Rebuild()
{
    CActor* actor = Actor();
    if (!actor)
    {
        ClearSlices();
        return;
    }

    CInventory& inv = actor->inventory();
    m_inv_frame = inv.ModifyFrame();
    ParsePins();
    ClearSlices();

    struct Group
    {
        shared_str section;
        CInventoryItem* item{};
        u32 count{};
        bool grenade{};
        bool detector{};
        bool addon{};
        bool addon_attached{};
    };
    xr_vector<Group> groups;

    // R3's inventory page filter leaves FIHiddenForInventory set after closing.
    // The wheel uses all owned items and applies its own category/usability checks.
    auto add_item = [&](CInventoryItem* itm, bool as_grenade) {
        if (!itm || !itm->Useful())
            return;
        const shared_str sect = itm->object().cNameSect();
        for (Group& g : groups)
        {
            if (g.section == sect)
            {
                ++g.count;
                return;
            }
        }
        if (groups.size() >= kMaxWheelSlices)
            return;
        groups.push_back({sect, itm, 1, as_grenade, IsDetector(itm), IsAddonItem(itm), false});
    };

    auto add_attached_addon = [&](const shared_str& sect) {
        if (!sect.size() || groups.size() >= kMaxWheelSlices)
            return;
        for (const Group& g : groups)
        {
            if (g.section == sect)
                return;
        }
        groups.push_back({sect, nullptr, 1, false, false, true, true});
    };

    if (m_tab == eItemWheelPins)
    {
        for (const shared_str& sect : s_pins)
        {
            if (groups.size() >= kMaxWheelSlices)
                break;
            CInventoryItem* found = nullptr;
            u32 count = 0;
            bool grenade = false;
            bool detector = false;
            for (CInventoryItem* itm : inv.m_all)
            {
                if (!itm || itm->object().cNameSect() != sect)
                    continue;
                if (!itm->Useful())
                    continue;
                if (!found)
                {
                    found = itm;
                    grenade = IsGrenade(itm);
                    detector = IsDetector(itm);
                }
                ++count;
            }
            if (found)
                groups.push_back({sect, found, count, grenade, detector, IsAddonItem(found), false});
        }
    }
    else if (m_tab == eItemWheelAddons)
    {
        if (CWeapon* wpn = smart_cast<CWeapon*>(actor->inventory().ActiveItem()))
        {
            if (wpn->ScopeAttachable() && wpn->IsScopeAttached())
                add_attached_addon(wpn->GetScopeName());
            if (wpn->SilencerAttachable() && wpn->IsSilencerAttached())
                add_attached_addon(wpn->GetSilencerName());
            if (wpn->GrenadeLauncherAttachable() && wpn->IsGrenadeLauncherAttached())
                add_attached_addon(wpn->GetGrenadeLauncherName());

            for (CInventoryItem* itm : inv.m_all)
            {
                if (itm && itm->Useful() && wpn->CanAttach(itm))
                    add_item(itm, false);
            }
        }
    }
    else
    {
        for (CInventoryItem* itm : inv.m_all)
        {
            bool ok = false;
            bool grenade = false;
            switch (m_tab)
            {
            case eItemWheelMeds: ok = IsMed(itm); break;
            case eItemWheelFood: ok = IsFood(itm); break;
            case eItemWheelGrenades:
                ok = IsGrenade(itm);
                grenade = ok;
                break;
            default: break;
            }
            if (ok)
                add_item(itm, grenade || IsGrenade(itm));
        }
    }

    const u32 n = groups.size();
    const float span = n ? (PI_MUL_2 / float(n)) : PI_MUL_2;

    for (u32 i = 0; i < n; ++i)
    {
        Slice s;
        s.section = groups[i].section;
        s.object_id = groups[i].item ? groups[i].item->object().ID() : u16(-1);
        s.grenade = groups[i].grenade;
        s.detector = groups[i].detector;
        s.addon = groups[i].addon;
        s.addon_attached = groups[i].addon_attached;
        s.angle = span * float(i);

        const float x = m_center.x + _sin(s.angle) * m_radius;
        const float y = m_center.y - _cos(s.angle) * m_radius_y;

        s.hover = xr_new<CUIStatic>();
        s.hover->SetAutoDelete(true);
        s.hover->InitTexture(s.addon_attached ? "ui_qaw_opt_selected" : "ui_qaw_opt_hover");
        s.hover->SetStretchTexture(true);
        s.hover->SetWndSize(m_hover_sz);
        PlaceCentered(s.hover, x, y);
        s.hover->Show(s.addon_attached);
        m_items_root.AttachChild(s.hover);

        s.icon = xr_new<CUIStatic>();
        s.icon->SetAutoDelete(true);
        if (groups[i].item)
            groups[i].item->m_icon_params.set_shader(s.icon);
        else
            CIconParams(groups[i].section).set_shader(s.icon);
        s.icon->SetWndSize(Fvector2().set(kIconSize, kIconSize));
        PlaceCentered(s.icon, x, y);
        m_items_root.AttachChild(s.icon);

        s.count = xr_new<CUIStatic>();
        s.count->SetAutoDelete(true);
        s.count->SetWndSize(m_count_sz);
        PlaceCentered(s.count, x, y + kIconSize * 0.5f + 8.f);
        s.count->SetFont(UI()->Font()->pFontArial14);
        s.count->SetTextAlignment(CGameFont::alCenter);
        s.count->SetVTextAlignment(valCenter);
        s.count->SetTextColor(color_rgba(240, 240, 240, 255));
        string32 buf{};
        xr_sprintf(buf, "%u", groups[i].count);
        s.count->SetText(buf);
        m_items_root.AttachChild(s.count);

        m_slices.push_back(s);
    }

    if (!n)
        m_title.SetTextST("st_item_wheel_empty");
    else
        m_title.SetTextST(tab_titles[m_tab]);
}

void CUIItemWheelWnd::UpdateHover()
{
    m_hovered = -1;
    const u32 n = m_slices.size();
    if (!n)
        return;

    const Fvector2 c = GetUICursor()->GetCursorPosition();
    const float dx = c.x - m_center.x;
    const float dy = c.y - m_center.y;
    if (dx * dx + dy * dy < kDeadzoneSq)
    {
        for (Slice& s : m_slices)
            s.hover->Show(s.addon_attached);
        return;
    }

    const float ang = CursorAngle(dx, dy);
    const float span = PI_MUL_2 / float(n);
    float best = span * 0.5f + 0.001f;
    for (u32 i = 0; i < n; ++i)
    {
        const float d = AngleDelta(ang, m_slices[i].angle);
        if (d <= best)
        {
            best = d;
            m_hovered = int(i);
        }
    }

    for (u32 i = 0; i < n; ++i)
        m_slices[i].hover->Show(int(i) == m_hovered || m_slices[i].addon_attached);
}

void CUIItemWheelWnd::UpdateCursor()
{
    const Fvector2 c = GetUICursor()->GetCursorPosition();
    float dx = c.x - m_center.x;
    float dy = c.y - m_center.y;
    if (dx * dx + dy * dy < 1.f)
    {
        dx = 0.f;
        dy = -1.f;
    }
    m_cursor.SetHeading(-CursorAngle(dx, dy));
}

bool CUIItemWheelWnd::HoldKeyDown() const
{
    if (!pInput || g_key_bindings.size() <= kITEM_WHEEL)
        return false;

    const _binding& b = g_key_bindings[kITEM_WHEEL];
    if (b.m_keyboard[0] && pInput->iGetAsyncKeyState(b.m_keyboard[0]->dik))
        return true;
    if (b.m_keyboard[1] && pInput->iGetAsyncKeyState(b.m_keyboard[1]->dik))
        return true;
    return false;
}

void CUIItemWheelWnd::CloseWheel()
{
    if (IsShown() && GetHolder())
        GetHolder()->StartStopMenu(this, false);
}

void CUIItemWheelWnd::ActivateHovered()
{
    UpdateHover();
    if (m_hovered < 0 || m_hovered >= int(m_slices.size()))
        return;

    CActor* actor = Actor();
    if (!actor)
        return;

    if (m_slices[m_hovered].addon)
    {
        CWeapon* wpn = smart_cast<CWeapon*>(actor->inventory().ActiveItem());
        if (WeaponBusy(wpn))
            return;

        if (m_slices[m_hovered].addon_attached)
        {
            wpn->Detach(m_slices[m_hovered].section.c_str(), true);
            if (m_snd_detach._handle())
                m_snd_detach.play(nullptr, sm_2D);
        }
        else
        {
            CInventoryItem* item = actor->inventory().get_object_by_id(m_slices[m_hovered].object_id);
            if (!item || !item->Useful())
            {
                Rebuild();
                return;
            }

            if (!wpn->CanAttach(item))
            {
                if (smart_cast<CScope*>(item) && wpn->IsScopeAttached())
                    wpn->Detach(wpn->GetScopeName().c_str(), true);
                else if (smart_cast<CSilencer*>(item) && wpn->IsSilencerAttached())
                    wpn->Detach(wpn->GetSilencerName().c_str(), true);
                else if (smart_cast<CGrenadeLauncher*>(item) && wpn->IsGrenadeLauncherAttached())
                    wpn->Detach(wpn->GetGrenadeLauncherName().c_str(), true);
            }

            if (wpn->CanAttach(item) && wpn->Attach(item, true))
            {
                if (m_snd_attach._handle())
                    m_snd_attach.play(nullptr, sm_2D);
            }
        }

        CloseWheel();
        return;
    }

    CInventoryItem* item = actor->inventory().get_object_by_id(m_slices[m_hovered].object_id);
    if (!item || !item->Useful())
    {
        Rebuild();
        return;
    }

    if (m_slices[m_hovered].detector || IsDetector(item))
    {
        CInventory& inv = actor->inventory();
        PIItem equipped = inv.ItemFromSlot(DETECTOR_SLOT);
        auto* det = smart_cast<CCustomDetector*>(item);
        const bool fast = g_player_hud && g_player_hud->attached_item(0) != nullptr;
        if (equipped == item)
        {
            if (det)
                det->ToggleDetector(fast);
        }
        else
        {
            if (equipped)
            {
                if (auto* old_det = smart_cast<CCustomDetector*>(equipped))
                    old_det->HideDetector(true);
                inv.Ruck(equipped);
            }
            item->SetSlot(u8(DETECTOR_SLOT));
            inv.Slot(item, true);
            if (det)
                det->ShowDetector(fast);
        }
    }
    else if (m_slices[m_hovered].grenade || IsGrenade(item))
    {
        CInventory& inv = actor->inventory();
        PIItem equipped = inv.ItemFromSlot(GRENADE_SLOT);
        if (equipped == item)
        {
            inv.Activate(GRENADE_SLOT, eKeyAction, true);
        }
        else
        {
            if (equipped)
                inv.Ruck(equipped);
            item->SetSlot(u8(GRENADE_SLOT));
            inv.Slot(item, false);
        }
    }
    else
    {
        if (item->object().H_Parent())
        {
            NET_Packet P;
            item->object().u_EventGen(P, GEG_PLAYER_ITEM_EAT, item->object().H_Parent()->ID());
            P.w_u16(item->object().ID());
            item->object().u_EventSend(P);
        }
    }

    CloseWheel();
}

void CUIItemWheelWnd::UnpinHovered()
{
    if (m_tab != eItemWheelPins)
        return;
    UpdateHover();
    if (m_hovered < 0 || m_hovered >= int(m_slices.size()))
        return;
    ItemWheel_RemovePin(m_slices[m_hovered].section);
    Rebuild();
}

void CUIItemWheelWnd::Update()
{
    inherited::Update();

    CActor* actor = Actor();
    if (!actor || !actor->g_Alive())
    {
        CloseWheel();
        return;
    }

    if (Device.dwTimeContinual > m_open_time + 80 && !HoldKeyDown())
    {
        CloseWheel();
        return;
    }

    if (actor->inventory().ModifyFrame() != m_inv_frame)
        Rebuild();

    UpdateHover();
    UpdateCursor();
}

void CUIItemWheelWnd::Draw() { inherited::Draw(); }

bool CUIItemWheelWnd::OnKeyboard(int dik, EUIMessages keyboard_action)
{
    if (keyboard_action == WINDOW_KEY_RELEASED)
    {
        if (is_binded(kITEM_WHEEL, dik))
        {
            CloseWheel();
            return true;
        }
        return false;
    }

    if (keyboard_action != WINDOW_KEY_PRESSED)
        return false;

    if (dik == DIK_ESCAPE)
    {
        CloseWheel();
        return true;
    }

    if (dik == DIK_1)
    {
        SetTab(eItemWheelPins);
        return true;
    }
    if (dik == DIK_2)
    {
        SetTab(eItemWheelMeds);
        return true;
    }
    if (dik == DIK_3)
    {
        SetTab(eItemWheelFood);
        return true;
    }
    if (dik == DIK_4)
    {
        SetTab(eItemWheelGrenades);
        return true;
    }
    if (dik == DIK_5)
    {
        SetTab(eItemWheelAddons);
        return true;
    }

    if (is_binded(kITEM_WHEEL, dik))
        return true;

    return false;
}

bool CUIItemWheelWnd::OnMouse(float x, float y, EUIMessages mouse_action)
{
    switch (mouse_action)
    {
    case WINDOW_MOUSE_WHEEL_UP:
        SetTab(EItemWheelTab((m_tab + 1) % eItemWheelTabCount));
        return true;
    case WINDOW_MOUSE_WHEEL_DOWN:
        SetTab(EItemWheelTab((m_tab + eItemWheelTabCount - 1) % eItemWheelTabCount));
        return true;
    case WINDOW_LBUTTON_DOWN:
        ActivateHovered();
        return true;
    case WINDOW_RBUTTON_DOWN:
        UnpinHovered();
        return true;
    default: break;
    }
    return false;
}
