// UIMainIngameWnd.h:  окошки-информация в игре
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "UIProgressBar.h"
#include "UIGameLog.h"

#include "../alife_space.h"

#include "UICarPanel.h"
#include "UIMotionIcon.h"
#include "../hudsound.h"
#include "../script_export_space.h"

struct GAME_NEWS_DATA;

class CUIPdaMsgListItem;
class CLAItem;
class CUIZoneMap;
class CUIArtefactPanel;
class CUIScrollView;
class CActor;
class CWeapon;
class CMissile;
class CInventoryItem;
class CGameObject;
class CObject;

class CUIMainIngameWnd : public CUIWindow
{
public:
    CUIMainIngameWnd();
    virtual ~CUIMainIngameWnd();

    virtual void Init();
    virtual void Draw();
    virtual void Update();

    bool OnKeyboardPress(int dik);

protected:
    CUIStatic UIStaticHealth;
    CUIStatic UIStaticArmor;
    CUIStatic UIStaticQuickHelp;
    CUIProgressBar UIHealthBar;
    CUIProgressBar UIArmorBar;
    CUICarPanel UICarPanel;
    CUIMotionIcon UIMotionIcon;
    CUIZoneMap* UIZoneMap;

    //иконка, показывающая количество активных PDA
    CUIStatic UIPdaOnline;

    //изображение оружия
    CUIStatic UIWeaponBack;
    CUIStatic UIWeaponSignAmmo;
    CUIStatic UIWeaponIcon;

    Frect UIWeaponIcon_rect;

public:
    CUIStatic* GetPDAOnline() { return &UIPdaOnline; };
    CUIZoneMap* GetUIZoneMap() { return UIZoneMap; }

protected:
    // 5 статиков для отображения иконок:
    // - сломанного оружия
    // - радиации
    // - ранения
    // - голода
    // - усталости
    CUIStatic UIWeaponJammedIcon;
    CUIStatic UIRadiaitionIcon;
    CUIStatic UIWoundIcon;
    CUIStatic UIStarvationIcon;
    CUIStatic UIPsyHealthIcon;
    CUIStatic UIInvincibleIcon;
    CUIStatic UIThirstIcon;
    CUIStatic UIFlashlightIcon;
    bool m_bFlashlightIcon{};
    //	CUIStatic			UISleepIcon;
    //	CUIStatic			UIArtefactIcon;

    CUIScrollView* m_UIIcons{};

public:
    CUIArtefactPanel* m_artefactPanel{};

public:
    // Енумы соответсвующие предупреждающим иконкам
    enum EWarningIcons
    {
        ewiAll = 0,
        ewiWeaponJammed,
        ewiRadiation,
        ewiWound,
        ewiStarvation,
        ewiPsyHealth,
        ewiInvincible,
        ewiThirst,
        //		ewiSleep,
        //		ewiArtefact,
    };

    // Задаем цвет соответствующей иконке
    void SetWarningIconColor(EWarningIcons icon, const u32 cl);
    void TurnOffWarningIcon(EWarningIcons icon);

    // Пороги изменения цвета индикаторов, загружаемые из system.ltx
    typedef xr_map<EWarningIcons, xr_vector<float>> Thresholds;
    typedef Thresholds::iterator Thresholds_it;
    Thresholds m_Thresholds;

    // Енум перечисления возможных мигающих иконок
    enum EFlashingIcons
    {
        efiPdaTask = 0,
        efiMail
    };

    void SetFlashIconState_(EFlashingIcons type, bool enable);

    void AnimateContacts(bool b_snd);
    HUD_SOUND m_contactSnd;

    void ReceiveNews(GAME_NEWS_DATA* news);

protected:
    void SetWarningIconColor(CUIStatic* s, const u32 cl);
    void InitFlashingIcons(CUIXml* node);
    void DestroyFlashingIcons();
    void UpdateFlashingIcons();
    void UpdateActiveItemInfo();
    void UpdateFlashlightIcon();

    void SetAmmoIcon(const shared_str& seсt_name);

    // first - иконка, second - анимация
    DEF_MAP(FlashingIcons, EFlashingIcons, CUIStatic*);
    FlashingIcons m_FlashingIcons;

    //для текущего активного актера и оружия
    CActor* m_pActor;
    CWeapon* m_pWeapon;
    CMissile* m_pGrenade;
    CInventoryItem* m_pItem;

    // Отображение подсказок при наведении прицела на объект
    void RenderQuickInfos();

public:
    CUICarPanel& CarPanel() { return UICarPanel; };
    CUIMotionIcon& MotionIcon() { return UIMotionIcon; }
    void OnConnected();
    void reset_ui();

protected:
    CInventoryItem* m_pPickUpItem;
    CUIStatic UIPickUpItemIcon;

    float m_iPickUpItemIconX{};
    float m_iPickUpItemIconY{};
    float m_iPickUpItemIconWidth{};
    float m_iPickUpItemIconHeight{};

    Fvector2 m_quick_help_xml_pos{};
    Fvector2 m_quick_help_xml_size{};
    u32 m_quick_help_xml_clr{0xffffffff};

    Fvector2 m_xml_health_pos{};
    Fvector2 m_xml_weapon_pos{};
    int m_applied_hud_cluster_pos{-1};
    bool m_applied_hud_16x10{};

    CUIStatic UIStaticInteractName;
    CUIStatic UIStaticInteractNameSh;
    CUIStatic UIStaticInteractFaction;
    CUIStatic UIStaticInteractFactionSh;
    CUIStatic UIInteractFactionPatch;
    shared_str m_interact_patch_tex;
    CUIStatic UIStaticQuickHelpSh;
    CUIStatic UIInteractDrop;
    CUIStatic UIInteractKey;
    CUIStatic UIInteractKeyL;
    CUIStatic UIInteractKeyC;
    CUIStatic UIInteractKeyR;
    CUIStatic UIInteractKeyBind;
    CUIStatic UIStaticQuickHelp2;
    CUIStatic UIStaticQuickHelp2Sh;
    CUIStatic UIInteractKey2;
    CUIStatic UIInteractKey2L;
    CUIStatic UIInteractKey2C;
    CUIStatic UIInteractKey2R;
    CUIStatic UIInteractKeyBind2;

    enum
    {
        kMaxInteractDots = 16
    };
    CUIStatic m_interact_dots[kMaxInteractDots];
    xr_vector<CObject*> m_interact_nearest;
    CGameObject* m_interact_cycle[kMaxInteractDots]{};
    u32 m_interact_cycle_count{};
    CGameObject* m_interact_sticky{};
    CGameObject* m_interact_last_look{};
    bool m_interact_cycle_lock{};

    void InitInteractOverlay();
    void HideInteractPrompt();
    void HideInteractDots();
    void ClearInteractCycle();
    void UpdateNearbyInteractDots(CGameObject* look_at);
    CGameObject* InteractFocusObject(CGameObject* look_at, LPCSTR look_action) const;
    void LayoutInteractPrompt(const Fvector2& projected, LPCSTR key, LPCSTR action, LPCSTR key2, LPCSTR action2, LPCSTR name, LPCSTR faction, LPCSTR patch, u8 alpha, bool gray_patch);

    void UpdatePickUpItem();
    void UpdateHudClusterLayout();

public:
    void SetPickUpItem(CInventoryItem* PickUpItem);
    void CycleNearbyInteract();
    CInventoryItem* InteractPickupItem();

    DECLARE_SCRIPT_REGISTER_FUNCTION
};

add_to_type_list(CUIMainIngameWnd)
#undef script_type_list
#define script_type_list save_type_list(CUIMainIngameWnd)

bool HudInteractEnabled();
bool HudInteractSuppressVanillaItemLabels();
