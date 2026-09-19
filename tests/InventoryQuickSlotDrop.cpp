// Exercise the production inventory drop dispatch with a scripted UI tree.
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

using LPCSTR = const char*;
struct Fvector2 { float x, y; };
struct Frect
{
    float x1{}, y1{}, x2{}, y2{};
    bool in(Fvector2 p) { return p.x >= x1 && p.x <= x2 && p.y >= y1 && p.y <= y2; }
};
struct CUIWindow
{
    const char* name = "";
    bool shown = true, enabled = true;
    CUIWindow* parent = nullptr;
    std::vector<CUIWindow*> children;
    Frect rect;
    CUIWindow* GetParent() { return parent; }
    bool IsShown() { return shown; }
    bool IsEnabled() { return enabled; }
    void GetAbsoluteRect(Frect& r) { r = rect; }
    void Attach(CUIWindow& w) { children.push_back(&w); w.parent = this; }
    CUIWindow* FindChild(LPCSTR n)
    {
        if (!std::strcmp(name, n)) return this;
        for (auto child : children)
            if (auto found = child->FindChild(n)) return found;
        return nullptr;
    }
};
struct Cursor
{
    Fvector2 position{};
    Fvector2 GetCursorPosition() { return position; }
} cursor;
Cursor* GetUICursor() { return &cursor; }
struct DragItem
{
    CUIWindow* target = nullptr;
    CUIWindow* BackList() { return target; }
} drag;
struct CUIDragDropListEx { inline static DragItem* m_drag_item = &drag; };
struct Item
{
    bool quest = false;
    bool IsQuestItem() { return quest; }
};
struct CUICellItem
{
    Item item;
    unsigned children = 0;
    CUIWindow owner;
    CUIWindow* OwnerList() { return &owner; }
    unsigned ChildsCount() { return children; }
};
struct CUIInventoryWnd : CUIWindow
{
    CUICellItem* selected = nullptr;
    int dropped = 0;
    bool droppedStack = false;
    void SetCurrentItem(CUICellItem* item) { selected = item; }
    Item* CurrentIItem() { return selected ? &selected->item : nullptr; }
    CUICellItem* CurrentItem() { return selected; }
    void DropCurrentItem(bool all) { ++dropped; droppedStack = all; }
    bool OnItemDrop(CUICellItem* itm);
};
#include "inventory-drop.inl"

int main()
{
    CUIInventoryWnd inventory;
    CUIWindow overlay, slots, buttons[4];
    inventory.Attach(overlay);
    overlay.Attach(slots);
    const char* names[] = {"slot1", "slot2", "slot3", "slot4"};
    CUICellItem item;
    for (int i = 0; i < 4; ++i)
    {
        buttons[i].name = names[i];
        buttons[i].rect = {float(400 + i * 65), 437, float(433 + i * 65), 478};
        slots.Attach(buttons[i]);
        cursor.position = {float(410 + i * 65), 450};
        for (unsigned stack : {0u, 3u})
        {
            item.children = stack;
            assert(inventory.OnItemDrop(&item));
            assert(inventory.dropped == 0);
        }
    }
    // A real release outside the grids still drops the item/stack.
    cursor.position = {800, 500};
    assert(inventory.OnItemDrop(&item));
    assert(inventory.dropped == 1 && inventory.droppedStack);
    item.children = 0;
    inventory.OnItemDrop(&item);
    assert(inventory.dropped == 2 && !inventory.droppedStack);
    item.item.quest = true;
    inventory.OnItemDrop(&item);
    assert(inventory.dropped == 2);
    item.item.quest = false;
    // Hidden or disabled ancestors must not create invisible protected zones.
    cursor.position = {410, 450};
    overlay.shown = false;
    inventory.OnItemDrop(&item);
    assert(inventory.dropped == 3);
    overlay.shown = true;
    slots.enabled = false;
    inventory.OnItemDrop(&item);
    assert(inventory.dropped == 4);
    slots.enabled = true;
    // A layout without scripted quick slots retains normal outside-drop behavior.
    slots.children.clear();
    inventory.OnItemDrop(&item);
    assert(inventory.dropped == 5);
    // Native grid destinations continue through the original dispatch.
    drag.target = &item.owner;
    assert(!inventory.OnItemDrop(&item));
    assert(inventory.dropped == 5);
    std::cout << "Inventory quick-slot drop regression checks passed\n";
}
