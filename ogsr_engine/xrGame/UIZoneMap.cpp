#include "stdafx.h"
#include "uizonemap.h"

#include "hudmanager.h"

#include "InfoPortion.h"
#include "Pda.h"

#include "Grenade.h"
#include "level.h"
#include "game_cl_base.h"

#include "actor.h"
#include "ai_space.h"
#include "game_graph.h"

#include "ui/UIMap.h"
#include "ui/UIXmlInit.h"
#include "ui/UIInventoryUtilities.h"
#include "ui_base.h"

extern float g_minimap_scale;
extern float g_minimap_x;
extern float g_minimap_y;
extern u32 g_minimap_pos;

namespace
{
void LayoutHudWnd(CUIWindow* w, const Fvector2& orig_pos, const Fvector2& orig_size, const Fvector2& pivot, float s, float ox, float oy)
{
    Fvector2 np;
    np.x = pivot.x + (orig_pos.x - pivot.x) * s + ox;
    np.y = pivot.y + (orig_pos.y - pivot.y) * s + oy;
    w->SetWndPos(np);
    w->SetWndSize(orig_size.x * s, orig_size.y * s);
}
} // namespace

CUIZoneMap::CUIZoneMap()
{
    m_background = xr_new<CUIStatic>();
    m_pointerDistanceText = xr_new<CUIStatic>();
    m_clock_wnd = xr_new<CUIStatic>();
    m_clipFrame = xr_new<CUIStatic>();
    m_center = xr_new<CUIStatic>();
    m_compass = xr_new<CUIStatic>();
    m_activeMap = xr_new<CUIMiniMap>();
}

CUIZoneMap::~CUIZoneMap()
{
    xr_delete(m_center);
    xr_delete(m_clipFrame);
    xr_delete(m_background);
    xr_delete(m_compass);
}

void CUIZoneMap::Init()
{
    CUIXml uiXml;
    const bool xml_result = uiXml.Init(CONFIG_PATH, UI_PATH, "zone_map.xml");
    R_ASSERT(xml_result, "xml file not found", "zone_map.xml");

    // load map background
    CUIXmlInit xml_init;

    xml_init.InitStatic(uiXml, "minimap:background", 0, m_background);
    // m_background->SetAutoDelete(true);

    xml_init.InitStatic(uiXml, "minimap:background:dist_text", 0, m_pointerDistanceText);
    m_background->AttachChild(m_pointerDistanceText);
    m_pointerDistanceText->SetAutoDelete(true);

    if (uiXml.NavigateToNode("minimap:clock_wnd", 0))
    {
        xml_init.InitStatic(uiXml, "minimap:clock_wnd", 0, m_clock_wnd);
        m_background->AttachChild(m_clock_wnd);
        m_clock_wnd->SetAutoDelete(true);
    }

    xml_init.InitStatic(uiXml, "minimap:level_frame", 0, m_clipFrame);
    // m_clipFrame->SetAutoDelete(true);

    xml_init.InitStatic(uiXml, "minimap:center", 0, m_center);

    m_rounded = uiXml.ReadAttribInt("minimap", 0, "rounded", 0) == 1;
    m_alpha = uiXml.ReadAttribInt("minimap", 0, "alpha", 127);

    m_clipFrame->AttachChild(m_activeMap);

    m_activeMap->SetAutoDelete(true);
    m_activeMap->EnableHeading(true);
    m_activeMap->SetWindowName("minimap");
    m_activeMap->SetRounded(m_rounded);

    xml_init.InitStatic(uiXml, "minimap:compass", 0, m_compass);
    // m_compass->SetAutoDelete(true);

    m_activeMap->SetTextureColor(color_argb(m_alpha, 255, 255, 255));

    //	m_background.AttachChild(&m_compass);

    m_clipFrame->AttachChild(m_center);
    m_center->SetWndPos(m_clipFrame->GetWidth() / 2, m_clipFrame->GetHeight() / 2);
    // m_center->SetAutoDelete(true);
    m_fScale = 1.f;

    m_xml_bg_pos = m_background->GetWndPos();
    m_xml_bg_size = m_background->GetWndSize();
    m_xml_clip_pos = m_clipFrame->GetWndPos();
    m_xml_clip_size = m_clipFrame->GetWndSize();
    m_xml_compass_pos = m_compass->GetWndPos();
    m_xml_compass_size = m_compass->GetWndSize();
    m_applied_hud_scale = -1.f;
    UpdateHudLayout();
}

void CUIZoneMap::Render()
{
    if (static_cast<int>(g_minimap_pos) == 4)
        return;

    m_clipFrame->Draw();
    m_background->Draw();
    m_compass->Draw();
}

void CUIZoneMap::SetHeading(float angle) const
{
    m_activeMap->SetHeading(angle);
    m_compass->SetHeading(angle);
};

void CUIZoneMap::UpdateRadar(Fvector pos) const
{
    m_clipFrame->Update();
    m_background->Update();
    m_activeMap->SetActivePoint(pos);

    if (m_activeMap->GetPointerDistance() > 0.5f)
    {
        string64 str;
        sprintf_s(str, "%.1f m.", m_activeMap->GetPointerDistance());
        m_pointerDistanceText->SetText(str);
    }
    else
    {
        m_pointerDistanceText->SetText("");
    }

    if (m_clock_wnd)
    {
        m_clock_wnd->SetText(InventoryUtilities::GetGameTimeAsString(InventoryUtilities::etpTimeToMinutes).c_str());
    }
}

bool CUIZoneMap::ZoomIn()
{
    m_fScale = m_fScale + m_fScale * 0.25f;
    clamp(m_fScale, 0.5f, 2.f);
    ApplyZoom();

    return true;
}

bool CUIZoneMap::ZoomOut()
{
    m_fScale = m_fScale - m_fScale * 0.25f;
    clamp(m_fScale, 0.5f, 2.f);
    ApplyZoom();

    return true;
}

void CUIZoneMap::SetupCurrentMap()
{
    CInifile* pLtx = pGameIni;

    R_ASSERT(pLtx->section_exist(Level().name()));

    // dsh: очередной костыль. Если не создавать новый CUIMiniMap, то после
    // перехода с локации, на которой нет текстуры миникарты, на локацию,
    // где эта текстура есть (например, из X-10 на Радар), миникарта
    // перестает показываться.
    m_clipFrame->DetachChild(m_activeMap);
    m_clipFrame->DetachChild(m_center);

    m_activeMap = xr_new<CUIMiniMap>();
    m_clipFrame->AttachChild(m_activeMap);

    m_activeMap->SetAutoDelete(true);
    m_activeMap->EnableHeading(true);
    m_activeMap->SetRounded(m_rounded);

    m_activeMap->Init(Level().name(), *pLtx, "hud\\default");
    m_activeMap->SetWindowName("minimap"); // имя нужно задавать позже чем Init

    m_activeMap->SetTextureColor(color_argb(m_alpha, 255, 255, 255));

    m_clipFrame->AttachChild(m_center);

    m_applied_hud_scale = -1.f;
    UpdateHudLayout();
}

void CUIZoneMap::ApplyZoom() const
{
    Fvector2 wnd_size;
    const float zoom_factor = float(m_clipFrame->GetWndRect().width()) / 100.0f;
    wnd_size.x = m_activeMap->BoundRect().width() * zoom_factor * m_fScale;
    wnd_size.y = m_activeMap->BoundRect().height() * zoom_factor * m_fScale;
    m_activeMap->SetWndSize(wnd_size);
}

void CUIZoneMap::UpdateHudLayout()
{
    float s = g_minimap_scale;
    clamp(s, 0.5f, 2.f);
    float ox = g_minimap_x;
    float oy = g_minimap_y;

    const float margin_l = m_xml_bg_pos.x;
    const float margin_b = UI_BASE_HEIGHT - (m_xml_bg_pos.y + m_xml_bg_size.y);
    const float margin_r = margin_l;
    const float margin_t = margin_b;
    // XML bottom margin mirrored to the top lands near mid-screen on Radiophobia's
    // zone_map. Pull top corners up so they sit in the actual top band.
    constexpr float kMinimapTopLift = 170.f;
    constexpr float kMinimapRightInset = 120.f;
    float top_y = margin_t - kMinimapTopLift;
    if (top_y < 8.f)
        top_y = 8.f;
    const int pos = static_cast<int>(g_minimap_pos);

    switch (pos)
    {
    case 1: // bottom-right
        ox += UI_BASE_WIDTH - margin_r - m_xml_bg_size.x - m_xml_bg_pos.x - kMinimapRightInset;
        break;
    case 2: // top-left
        oy += top_y - m_xml_bg_pos.y;
        break;
    case 3: // top-right
        ox += UI_BASE_WIDTH - margin_r - m_xml_bg_size.x - m_xml_bg_pos.x - kMinimapRightInset;
        oy += top_y - m_xml_bg_pos.y;
        break;
    default: // bottom-left
        break;
    }

    const bool changed = !fsimilar(s, m_applied_hud_scale) || !fsimilar(ox, m_applied_hud_x) || !fsimilar(oy, m_applied_hud_y) || pos != m_applied_hud_pos;
    if (!changed && m_applied_hud_scale > 0.f)
        return;

    m_applied_hud_scale = s;
    m_applied_hud_x = ox;
    m_applied_hud_y = oy;
    m_applied_hud_pos = pos;

    Fvector2 pivot;
    pivot.x = m_xml_clip_pos.x + m_xml_clip_size.x * 0.5f;
    pivot.y = m_xml_clip_pos.y + m_xml_clip_size.y * 0.5f;

    LayoutHudWnd(m_background, m_xml_bg_pos, m_xml_bg_size, pivot, s, ox, oy);
    LayoutHudWnd(m_clipFrame, m_xml_clip_pos, m_xml_clip_size, pivot, s, ox, oy);
    LayoutHudWnd(m_compass, m_xml_compass_pos, m_xml_compass_size, pivot, s, ox, oy);

    m_center->SetWndPos(m_clipFrame->GetWidth() / 2.f, m_clipFrame->GetHeight() / 2.f);

    if (m_activeMap)
    {
        Frect r;
        m_clipFrame->GetAbsoluteRect(r);
        m_activeMap->SetClipRect(r);
        m_activeMap->WorkingArea().set(r);
        ApplyZoom();
    }
}