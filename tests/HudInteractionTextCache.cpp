// Uses the production prompt cache block, with UI/font doubles for observability.
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

using u32 = unsigned;
using LPCSTR = const char*;
using xr_string = std::string;
int xr_strcmp(LPCSTR a, LPCSTR b) { return std::strcmp(a, b); }
struct Fvector2
{
    float x{}, y{};
    void set(float a, float b) { x = a; y = b; }
};
struct CGameFont
{
    enum { alLeft, alRight };
    float width = 1, heightScale = 1, height = 16, step = 1;
    Fvector2 interval{1, 1};
    float GetWidthScale() { return width; }
    float GetHeightScale() { return heightScale; }
    float GetHeight() { return height; }
    float GetfXStep() { return step; }
    Fvector2 GetInterval() { return interval; }
};
struct CFontManager { CGameFont* pFontLetterica16Russian{}; };
struct HUDStub { CFontManager fonts; CFontManager& Font() { return fonts; } } hud;
HUDStub& HUD() { return hud; }
struct UIStub
{
    float x = 1, y = 1;
    void ClientToScreenScaledWidth(float& v) { v *= x; }
    void ClientToScreenScaledHeight(float& v) { v *= y; }
} ui;
UIStub* UI() { return &ui; }
struct CUIStatic
{
    CGameFont* font{};
    std::string text;
    Fvector2 size;
    u32 color{};
    int alignment = CGameFont::alLeft, measures = 0, styles = 0;
    bool complex = false;
    CGameFont* GetFont() { return font; }
    void SetFont(CGameFont* p) { font = p; ++styles; }
    void SetTextAlignment(int a) { alignment = a; }
    int GetTextAlignment() { return alignment; }
    void SetTextComplexMode(bool b) { complex = b; }
    void SetText(LPCSTR s) { text = s; }
    LPCSTR GetText() { return text.c_str(); }
    void SetTextColor(u32 c) { color = c; }
    void SetWndSize(Fvector2 v) { size = v; }
    float GetWidth() { return size.x; }
    float GetHeight() { return size.y; }
    void AdjustWidthToText() { ++measures; size.x = float(text.size()) * font->width * font->interval.x * ui.x; }
    void AdjustHeightToText() { size.y = font->height * font->heightScale * font->interval.y * ui.y; }
};

#include "interaction-style.inl"
struct Prompt
{
    #include "interaction-cache-members.inl"
    CUIStatic UIStaticQuickHelp;
    void fit(u32 index, CUIStatic& primary, CUIStatic* shadow, LPCSTR text, u32 color)
    {
        const u32 shadow_clr = color / 2;
        #include "interaction-cache.inl"
        fit_text(index, primary, shadow, text, color);
    }
};

int main()
{
    CGameFont font, replacement;
    hud.fonts.pFontLetterica16Russian = &font;
    Prompt prompt;
    CUIStatic primary, shadow, second;
    prompt.fit(0, primary, &shadow, "Talk", 200);
    assert(primary.measures == 1 && shadow.measures == 0);
    assert(primary.size.x == shadow.size.x && primary.size.y == shadow.size.y);
    for (int i = 0; i != 100; ++i)
        prompt.fit(0, primary, &shadow, "Talk", 100);
    assert(primary.measures == 1 && primary.styles == 1 && shadow.styles == 1);
    assert(primary.color == 100 && shadow.color == 50);

    // A binding/action change must update text and sizes together.
    prompt.fit(0, primary, &shadow, "Shift+Use", 200);
    assert(primary.measures == 2 && shadow.text == "Shift+Use");
    primary.size = {999, 999};
    prompt.fit(0, primary, &shadow, "Shift+Use", 200);
    assert(primary.measures == 2 && primary.size.x == 9);

    // Runtime font controls and UI scaling invalidate every cached label,
    // including labels that were not visible when those settings changed.
    prompt.fit(1, second, nullptr, "Search", 200);
    for (float* setting : {&font.width, &font.heightScale, &font.height,
                          &font.interval.x, &font.interval.y, &ui.x, &ui.y, &font.step})
    {
        const int before = primary.measures, other = second.measures;
        *setting += 0.25f;
        prompt.fit(0, primary, &shadow, "Shift+Use", 200);
        assert(primary.measures == before + 1);
        prompt.fit(1, second, nullptr, "Search", 200);
        assert(second.measures == other + 1);
        prompt.fit(0, primary, &shadow, "Shift+Use", 200);
        assert(primary.measures == before + 1);
    }
    hud.fonts.pFontLetterica16Russian = &replacement;
    const int before = primary.measures;
    prompt.fit(0, primary, &shadow, "Shift+Use", 200);
    assert(primary.measures == before + 1 && primary.font == &replacement && shadow.font == &replacement);

    // A control reused by the vanilla HUD is restyled when the overlay returns.
    prompt.m_interact_text[0].valid = false;
    primary.complex = true;
    primary.alignment = CGameFont::alRight;
    prompt.fit(0, primary, &shadow, "Shift+Use", 200);
    assert(!primary.complex && primary.alignment == CGameFont::alLeft);
    shadow.text = "external edit";
    const int cached = primary.measures;
    prompt.fit(0, primary, &shadow, "Shift+Use", 200);
    assert(shadow.text == primary.text && primary.measures == cached);
    std::cout << "Prompt cache: unchanged frames, shadows, text, fonts, scale and control reuse passed.\n";
}
