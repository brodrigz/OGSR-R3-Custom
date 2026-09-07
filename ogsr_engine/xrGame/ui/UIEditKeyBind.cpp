#include "stdafx.h"

#include "UIEditKeyBind.h"

#include <dinput.h>

#include "UIColorAnimatorWrapper.h"
#include "UIMessages.h"
#include "../xr_level_controller.h"
#include "../object_broker.h"

CUIEditKeyBind::CUIEditKeyBind(bool bPrim)
{
    m_bPrimary = bPrim;
    m_bEditMode = false;

    m_pAnimation = xr_new<CUIColorAnimatorWrapper>("ui_map_area_anim");
    m_pAnimation->Cyclic(true);
    m_bChanged = false;
    m_lines.SetTextComplexMode(false);
    m_keyboard = NULL;
    m_action = NULL;
}
CUIEditKeyBind::~CUIEditKeyBind() { delete_data(m_pAnimation); }

u32 cut_string_by_length(CGameFont* pFont, LPCSTR src, LPSTR dst, u32 dst_size, float length)
{
    if (pFont->IsMultibyte())
    {
        u16 nPos = pFont->GetCutLengthPos(length, src);
        VERIFY(nPos < dst_size);
        strncpy(dst, src, nPos);
        dst[nPos] = '\0';
        return nPos;
    }
    else
    {
        float text_len = pFont->SizeOf_(src);
        UI()->ClientToScreenScaledWidth(text_len);
        VERIFY(xr_strlen(src) <= dst_size);
        strcpy(dst, src);

        while (text_len > length)
        {
            dst[xr_strlen(dst) - 1] = 0;
            VERIFY(xr_strlen(dst));
            text_len = pFont->SizeOf_(dst);
            UI()->ClientToScreenScaledWidth(text_len);
        }

        return xr_strlen(dst);
    }
}

void CUIEditKeyBind::SetText(const char* text)
{
    if (!text || 0 == xr_strlen(text))
        CUILabel::SetText("---");
    else
    {
        string256 buff;

        cut_string_by_length(CUILinesOwner::GetFont(), text, buff, sizeof(buff), GetWidth());

        CUILabel::SetText(buff);
    }
}

void CUIEditKeyBind::Init(float x, float y, float width, float height)
{
    CUILabel::Init(x, y, width, height);
    InitTexture("ui_options_string");
}

void CUIEditKeyBind::InitTexture(LPCSTR texture, bool bHorizontal) { CUILabel::InitTexture(texture, bHorizontal); }

void CUIEditKeyBind::OnFocusLost()
{
    CUILabel::OnFocusLost();
    m_bEditMode = false;
    m_lines.SetTextColor((subst_alpha(m_lines.GetTextColor(), color_get_A(0xffffffff))));
}

bool CUIEditKeyBind::OnMouse(float x, float y, EUIMessages mouse_action)
{
    if (m_bEditMode)
    {
        if (mouse_action == WINDOW_MOUSE_WHEEL_UP)
            return AssignKey(MOUSE_WHEEL_UP);
        if (mouse_action == WINDOW_MOUSE_WHEEL_DOWN)
            return AssignKey(MOUSE_WHEEL_DOWN);
    }
    return CUIWindow::OnMouse(x, y, mouse_action);
}

bool CUIEditKeyBind::AssignKey(int dik)
{
    if (!m_bEditMode)
        return false;

    if (dik == DIK_ESCAPE)
    {
        if (m_keyboard)
        {
            SetText("---");
            m_keyboard = nullptr;
            OnFocusLost();
            m_bChanged = true;
        }
        return true;
    }

    m_keyboard = dik_to_ptr(dik, true);
    if (!m_keyboard)
        return true;

    SetText(m_keyboard->key_local_name.c_str());
    OnFocusLost();
    m_bChanged = true;

    string256 buff;
    sprintf_s(buff, "%s=%s=%d", m_action->action_name, m_keyboard->key_name, m_bPrimary ? 0 : 1);
    SendMessage2Group("key_binding", buff);
    return true;
}

bool CUIEditKeyBind::OnMouseDown(int mouse_btn)
{
    if (m_bEditMode)
        return AssignKey(mouse_btn);

    if (mouse_btn == MOUSE_1)
        m_bEditMode = m_bCursorOverWindow;

    return CUILabel::OnMouseDown(mouse_btn);
}

bool CUIEditKeyBind::OnKeyboard(int dik, EUIMessages keyboard_action)
{
    if (dik == MOUSE_1 || dik == MOUSE_2 || dik == MOUSE_3)
        return false;
    if (CUILabel::OnKeyboard(dik, keyboard_action))
        return true;

    if (m_bEditMode)
    {
        if (keyboard_action == WINDOW_KEY_PRESSED)
            return AssignKey(dik);
        return true;
    }
    return false;
}

void CUIEditKeyBind::Update()
{
    CUILabel::Update();

    m_bTextureAvailable = m_bCursorOverWindow;
    if (m_bEditMode)
    {
        m_pAnimation->Update();
        m_lines.SetTextColor((subst_alpha(m_lines.GetTextColor(), color_get_A(m_pAnimation->GetColor()))));
    }
}

void CUIEditKeyBind::Register(const char* entry, const char* group)
{
    CUIOptionsItem::Register(entry, group);
    m_action = action_name_to_ptr(entry);
    ASSERT_FMT(m_action, "Action [%s] not found. Group: [%s]", entry, group);
}

void CUIEditKeyBind::SetCurrentValue()
{
    _binding* pbinding = &g_key_bindings.at(m_action->id);

    int idx = (m_bPrimary) ? 0 : 1;
    m_keyboard = pbinding->m_keyboard[idx];

    if (m_keyboard)
        SetText(m_keyboard->key_local_name.c_str());
    else
        SetText(NULL);
}

void CUIEditKeyBind::SaveValue()
{
    CUIOptionsItem::SaveValue();

    BindAction2Key();
    m_bChanged = false;
}

#include "..\..\xr_3da\xr_ioconsole.h"
void CUIEditKeyBind::BindAction2Key()
{
    xr_string comm_unbind = (m_bPrimary) ? "unbind " : "unbind_sec ";
    comm_unbind += m_action->action_name;
    Console->Execute(comm_unbind.c_str());

    if (m_keyboard)
    {
        xr_string comm_bind = (m_bPrimary) ? "bind " : "bind_sec ";
        comm_bind += m_action->action_name;
        comm_bind += " ";
        comm_bind += m_keyboard->key_name;
        Console->Execute(comm_bind.c_str());
    }
}

bool CUIEditKeyBind::IsChanged() { return m_bChanged; }

void CUIEditKeyBind::OnMessage(const char* message)
{
    // message = "command=key=slot"  (slot 0 = primary, 1 = alternative)
    if (!m_keyboard || !message)
        return;

    string256 command;
    string256 key;
    int slot = -1;
    if (sscanf(message, "%255[^=]=%255[^=]=%d", command, key, &slot) != 3)
        return;

    if (slot != (m_bPrimary ? 0 : 1))
        return;
    if (0 != xr_strcmp(m_keyboard->key_name, key))
        return;
    if (0 == xr_strcmp(m_action->action_name, command))
        return;
    if (!actions_share_bind_group(m_action->action_name, command))
        return;

    SetText("---");
    m_keyboard = NULL;
    m_bChanged = true;
}