// Execute the production attachment method with skeleton doubles. Matrices record
// their composition order, so a hand/anchor/offset mix-up cannot pass unnoticed.
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using u16 = uint16_t;
constexpr u16 BI_NONE = 0xffff;
#define R_ASSERT3(test, message, detail) do { if (!(test)) throw std::runtime_error(message); } while (false)

struct Fmatrix
{
    std::vector<int> factors;
    void set(const Fmatrix& other) { factors = other.factors; }
    void mul(const Fmatrix& a, const Fmatrix& b)
    {
        factors = a.factors;
        factors.insert(factors.end(), b.factors.begin(), b.factors.end());
    }
    void mulB_43(const Fmatrix& b) { factors.insert(factors.end(), b.factors.begin(), b.factors.end()); }
};
struct IKinematics
{
    std::unordered_map<std::string, u16> ids;
    std::unordered_map<u16, Fmatrix> poses;
    int lookups{};
    u16 LL_BoneID(const char* name)
    {
        ++lookups;
        auto it = ids.find(name);
        return it == ids.end() ? BI_NONE : it->second;
    }
    Fmatrix LL_GetTransform(u16 id) { return poses.at(id); }
    IKinematics* dcast_PKinematics() { return this; }
};
struct attachable_hud_item { bool m_has_separated_hands{true}; };
struct player_hud
{
    attachable_hud_item* m_attached_items[2]{};
    bool script_anim_item_model{};
    IKinematics *m_model{}, *m_model_2{};
    std::vector<u16> m_ancors{1, 2};
    Fmatrix m_transform{{10}}, m_transform_2{{20}};
    void calc_transform(u16, const Fmatrix&, Fmatrix&, const char* attach_bone = nullptr);
};

#include "hud-attachment-production.inl"

void check(bool value, const char* reason)
{
    if (!value) throw std::runtime_error(reason);
}

int main()
try
{
    IKinematics right, left, replacement;
    right.ids["r_hand"] = 26;
    right.poses = {{1, {{101}}}, {26, {{126}}}};
    left.ids["l_hand"] = 5;
    left.poses = {{2, {{202}}}, {5, {{205}}}};
    replacement.ids["r_hand"] = 30;
    replacement.poses = {{1, {{301}}}, {30, {{330}}}};
    attachable_hud_item knife, detector;
    player_hud hud;
    hud.m_model = &right; hud.m_model_2 = &left;
    hud.m_attached_items[0] = &knife; hud.m_attached_items[1] = &detector;
    Fmatrix grip{{900}}, result;

    hud.calc_transform(0, grip, result);
    check(result.factors == std::vector<int>{10, 101, 900}, "normal knife attachment changed");
    hud.calc_transform(0, grip, result, "");
    check(result.factors == std::vector<int>{10, 101, 900} && right.lookups == 0, "empty opt-out changed attachment");
    hud.calc_transform(0, grip, result, "r_hand");
    check(result.factors == std::vector<int>{10, 126, 900}, "YAKR did not follow the hand with its grip offset");
    right.poses[26] = {{127}};
    hud.m_transform = {{11}};
    hud.calc_transform(0, grip, result, "r_hand");
    check(result.factors == std::vector<int>{11, 127, 900}, "moving hand or camera was not followed");
    hud.calc_transform(1, grip, result);
    check(result.factors == std::vector<int>{20, 202, 900}, "detector attachment changed");
    hud.calc_transform(1, grip, result, "l_hand");
    check(result.factors == std::vector<int>{20, 205, 900}, "left slot used the right skeleton");

    hud.m_model = &replacement;
    hud.calc_transform(0, grip, result, "r_hand");
    check(result.factors == std::vector<int>{11, 330, 900}, "outfit swap retained an old bone index");
    hud.script_anim_item_model = true;
    hud.calc_transform(0, grip, result);
    check(result.factors == std::vector<int>{11, 301, 900}, "script item inherited the weapon override");
    hud.m_attached_items[0] = nullptr;
    hud.calc_transform(0, grip, result);
    check(result.factors == std::vector<int>{11, 301, 900}, "script item without a weapon changed");
    hud.script_anim_item_model = false;
    knife.m_has_separated_hands = false;
    hud.m_attached_items[0] = &knife;
    hud.calc_transform(0, grip, result, "r_hand");
    check(result.factors == std::vector<int>{11, 900}, "combined weapon/hands model changed");

    knife.m_has_separated_hands = true;
    bool rejected = false;
    try { hud.calc_transform(0, grip, result, "missing_bone"); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "invalid bone was silently accepted");
    std::puts("HUD attachment checks passed: opt-in, default, movement, both slots, outfit swap, script items and invalid bone.");
    return 0;
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
