#pragma once

#include "UIDialogWnd.h"
#include "UIStatic.h"

class CInventoryItem;

enum EItemWheelTab : u32
{
    eItemWheelPins = 0,
    eItemWheelMeds,
    eItemWheelFood,
    eItemWheelGrenades,
    eItemWheelTabCount
};

extern u32 g_item_wheel;
extern char g_item_wheel_pins[2048];

bool ItemWheel_CanPin(CInventoryItem* item);
bool ItemWheel_HasPin(const shared_str& section);
void ItemWheel_AddPin(const shared_str& section);
void ItemWheel_RemovePin(const shared_str& section);
u32 ItemWheel_PinCount();

class CUIItemWheelWnd : public CUIDialogWnd
{
    typedef CUIDialogWnd inherited;

    struct Slice
    {
        CUIStatic* hover{};
        CUIStatic* icon{};
        CUIStatic* count{};
        shared_str section;
        u16 object_id{u16(-1)};
        bool grenade{};
        float angle{};
    };

    CUIStatic m_bg;
    CUIStatic m_cursor;
    CUIStatic m_title;
    CUIStatic m_category;
    CUIStatic m_tab_icons[eItemWheelTabCount];
    CUIStatic m_items_root;

    xr_vector<Slice> m_slices;
    EItemWheelTab m_tab{eItemWheelMeds};
    int m_hovered{-1};
    u32 m_inv_frame{u32(-1)};
    u32 m_open_time{};
    Fvector2 m_center{};
    Fvector2 m_hover_sz{};
    Fvector2 m_count_sz{};
    float m_radius{145.f};

    void Rebuild();
    void ClearSlices();
    void UpdateHover();
    void UpdateCursor();
    void SetTab(EItemWheelTab tab);
    void ActivateHovered();
    void UnpinHovered();
    bool HoldKeyDown() const;
    void CloseWheel();

public:
    CUIItemWheelWnd();
    virtual ~CUIItemWheelWnd();

    virtual void Show();
    virtual void Hide();
    virtual void Update();
    virtual void Draw();
    virtual bool OnKeyboard(int dik, EUIMessages keyboard_action);
    virtual bool OnMouse(float x, float y, EUIMessages mouse_action);

    virtual bool StopAnyMove() { return false; }
    virtual bool NeedCursor() const { return true; }
};
