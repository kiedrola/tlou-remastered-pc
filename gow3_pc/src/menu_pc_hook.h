/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GOW3_MENU_PC_HOOK_H
#define GOW3_MENU_PC_HOOK_H
#include "menu_exit_hook.h"

/* "PC SETTINGS" in the game's own Options page (dc1/menu.bin, update 1.11).
 *
 * The data's root entry PAGES (type PAGE_TABLE) points at {u64 count, u64 records}: one
 * 0xa0-byte record per page {+0 name, +8 hash, +0x10 title text id, +0x18 title sid,
 * +0x48 list entry (the 24-byte {count, items, 0, LIST_TYPE} of its buttons)}. A button item
 * with a page (+0x50 name, +0x58 hash) opens that page when pressed.
 *
 * The patch adds a page of our own, copied from the Controls page (its title, header, toggle,
 * slider, button and spacer items as models), and a button to it in the Options list. The arrays
 * are copied into the arena; the file's own stay as they are.
 *
 * Toggles (item kind 3) and sliders (kind 5) name their value at +0x30: {binding kind, option
 * sid}. The menu turns that into a binding object through one factory (update 1.11: 0x3187d0,
 * factory(menu, binding kind, ?, sid, ?, ?, [stack] ?)), whose sid switches are inlined. Kind 1
 * gives an int binding {vtable, +0x10 int *value, +0x18 min, +0x1c max, +0x20 label sids[2]} (the
 * widgets step from min to max and ask vtable slot 11 for the label of a value); kind 2 a float
 * one {vtable, +0x10 float *value, +0x18 steps, +0x1c min, +0x20 middle, +0x24 max}: the left half
 * of the bar goes from min to middle, the right half from middle to max. Our sids
 * (PC_OPTION + n) go to a helper instead (the factory's two calls are redirected to a stub): it
 * builds the game's own binding for a sid of the same kind and points it at our value. The
 * bindings' vtables have 53 slots (update 1.11; the menu calls slot 42): a copy takes 64. */
#define GOW3_MENU_PAGE_TABLE_NAME 0x74ffceebu
#define GOW3_MENU_PAGE_TABLE_TYPE 0xa990a93bu
#define GOW3_MENU_PAGE_CONTROLS 0x5c717aadu
#define GOW3_MENU_PAGE_GAMEPLAY 0x8917d4adu
#define GOW3_MENU_PAGE_CHAPTERS_AREA 0xf48d0867u
#define GOW3_MENU_BUTTON_OPTIONS_CONTROLS 0x12fccbb5u
#define GOW3_MENU_BUTTON_OPTIONS_VIDEO 0x5a0f9cefu
#define GOW3_MENU_BUTTON_OPTIONS_LANGUAGE 0x74ba56e4u
#define GOW3_MENU_BUTTON_RESET_DEFAULTS 0xfbccebeau
/* Our ids, next to the exit button's (0x3c9e51a7-0x3c9e51aa): the page, the Options button and
 * its label, the page title, then a row's button hash (PC_ROW + i) and label (PC_ROW_LABEL + i). */
#define GOW3_MENU_PC_PAGE 0x3c9e51b0u
#define GOW3_MENU_PC_BUTTON 0x3c9e51b1u
#define GOW3_MENU_PC_LABEL 0x3c9e51b2u
#define GOW3_MENU_PC_TITLE 0x3c9e51b3u
#define GOW3_MENU_PC_ROW 0x3c9e51c0u
#define GOW3_MENU_PC_ROW_LABEL 0x3c9e51e0u
/* The save list page (the save data dialog's list, src/runtime_menu_saves.c): a copy of Chapters'
 * area page, its list item (the list widget, kind 7) with our int binding; its title is ours. */
#define GOW3_MENU_SAVES_PAGE 0x3c9e51b4u
#define GOW3_MENU_SAVES_TITLE 0x3c9e51b5u
#define GOW3_MENU_SAVES_LIST 0x3c9e51b6u
/* Option sids (+0x34 of a toggle or slider), not texts; the save list's is the last one. */
#define GOW3_MENU_PC_OPTION 0x3c9e5200u
#define GOW3_MENU_SAVES_OPTION (GOW3_MENU_PC_OPTION + GOW3_MENU_PC_MAX_OPTIONS - 1)
/* Game options whose binding ours copy: Lock at 30 FPS (int, labels Off/On), Sensitivity (float). */
#define GOW3_MENU_OPTION_LOCK_30 0x9a7ea6e5u
#define GOW3_MENU_OPTION_SENSITIVITY 0x1abac2d6u
enum {
    GOW3_MENU_PAGE_SIZE = 0xa0, GOW3_MENU_PAGE_HASH = 8, GOW3_MENU_PAGE_TITLE_TEXT = 0x10,
    GOW3_MENU_PAGE_TITLE = 0x18, GOW3_MENU_PAGE_LIST = 0x48, GOW3_MENU_MAX_PAGES = 256,
    GOW3_MENU_ITEM_TEXT = 0x20, GOW3_MENU_ITEM_PAGE_NAME = 0x50, GOW3_MENU_ITEM_PAGE = 0x58,
    GOW3_MENU_KIND_HEADER = 0, GOW3_MENU_KIND_SPACER = 1, GOW3_MENU_KIND_TOGGLE = 3, GOW3_MENU_KIND_SLIDER = 5,
    GOW3_MENU_KIND_COMBO = 6, GOW3_MENU_KIND_LIST = 7, GOW3_MENU_BINDING_INT = 1, GOW3_MENU_ITEM_PLACE = 0x6c,
    GOW3_MENU_ITEM_BINDING = 0x30, GOW3_MENU_ITEM_OPTION = 0x34,
    GOW3_MENU_PC_MAX_ROWS = 32, GOW3_MENU_PC_MAX_OPTIONS = 32,
    GOW3_BINDING_VALUE = 0x10, GOW3_BINDING_MIN = 0x18, GOW3_BINDING_MAX = 0x1c, GOW3_BINDING_LABELS = 0x20,
    GOW3_BINDING_STEPS = 0x18, GOW3_BINDING_FLOAT_MIN = 0x1c, GOW3_BINDING_FLOAT_MID = 0x20,
    GOW3_BINDING_FLOAT_MAX = 0x24,
    GOW3_BINDING_MIN_SLOT = 9, GOW3_BINDING_MAX_SLOT = 10, GOW3_BINDING_LABEL_SLOT = 11, GOW3_BINDING_VTABLE_SLOTS = 64
};
_Static_assert(GOW3_MENU_PC_LABEL >= GOW3_MENU_TEXT_FIRST && GOW3_MENU_PC_TITLE <= GOW3_MENU_TEXT_LAST &&
               GOW3_MENU_PC_ROW_LABEL + GOW3_MENU_PC_MAX_ROWS - 1 <= GOW3_MENU_TEXT_LAST,
               "the text hook answers every label of ours");

/* A row of the page: a header (label: a text id, the game's or ours), a spacer, a button, a
 * toggle (on/off), a list (several values: the Difficulty combo's widget, whose text follows the
 * value; the toggle's only redraws between its two ends) or a slider; toggles, lists and sliders
 * show option `option`. */
typedef enum { GOW3_PC_ROW_HEADER, GOW3_PC_ROW_SPACER, GOW3_PC_ROW_BUTTON, GOW3_PC_ROW_TOGGLE,
               GOW3_PC_ROW_SLIDER, GOW3_PC_ROW_LIST } Gow3PcRowKind;
typedef struct { Gow3PcRowKind kind; uint32_t label, option; } Gow3PcRow;
typedef struct { unsigned button, page, saves; } Gow3MenuPcPatched;

static inline unsigned char *gow3_menu_take(unsigned char *arena, size_t arena_size, size_t *used, size_t size) {
    const size_t at = (*used + 15) & ~(size_t)15;
    if (at > arena_size || size > arena_size - at) return NULL;
    *used = at + size;
    memset(arena + at, 0, size);
    return arena + at;
}
static inline uint64_t gow3_menu_string(unsigned char *arena, size_t arena_size, size_t *used, const char *text) {
    const size_t length = strlen(text) + 1;
    unsigned char *at = gow3_menu_take(arena, arena_size, used, length);
    if (!at) return 0;
    memcpy(at, text, length);
    return (uint64_t)(uintptr_t)at;
}
static inline void gow3_menu_put32(unsigned char *p, uint32_t v) { memcpy(p, &v, 4); }
static inline void gow3_menu_put64(unsigned char *p, uint64_t v) { memcpy(p, &v, 8); }

/* In [first, first + size): a pointer to `length` bytes of the data. */
static inline int gow3_menu_inside(uint64_t first, uint64_t size, uint64_t pointer, uint64_t length) {
    return pointer >= first && pointer % 8 == 0 && length <= size && pointer - first <= size - length;
}

/* The list entry {count, items} at `entry` if it is a valid one, else 0 items. */
static inline uint64_t gow3_menu_list(const unsigned char *data, uint64_t size, uint64_t entry, unsigned char **items) {
    const uint64_t first = (uint64_t)(uintptr_t)data;
    if (!gow3_menu_inside(first, size, entry, GOW3_MENU_ENTRY_SIZE)) return 0;
    const unsigned char *e = (const unsigned char *)(uintptr_t)entry;
    const uint64_t count = gow3_menu_u64(e), list = gow3_menu_u64(e + 8);
    if (gow3_menu_u32(e + 20) != GOW3_MENU_LIST_TYPE || !count || count > GOW3_MENU_MAX_ITEMS ||
        !gow3_menu_inside(first, size, list, count * GOW3_MENU_ITEM_SIZE)) return 0;
    *items = (unsigned char *)(uintptr_t)list;
    return count;
}

static inline int gow3_menu_find_item(unsigned char *items, uint64_t count, uint32_t hash, int kind) {
    for (uint64_t i = 0; i < count; ++i) {
        const unsigned char *item = items + i * GOW3_MENU_ITEM_SIZE;
        if ((hash && gow3_menu_u32(item + GOW3_MENU_ITEM_HASH) == hash) ||
            (!hash && kind >= 0 && gow3_menu_u32(item + GOW3_MENU_ITEM_KIND) == (uint32_t)kind)) return (int)i;
    }
    return -1;
}

/* Adds the page (rows as given, at most GOW3_MENU_PC_MAX_ROWS) and its button in Options. Data
 * already patched (our page or button present) is left alone; so is data without the expected
 * pages and lists. *used grows by what was taken from the arena. */
static inline Gow3MenuPcPatched gow3_menu_pc_patch(unsigned char *data, uint64_t size, unsigned char *arena,
                                                   size_t arena_size, size_t *used, const Gow3PcRow *rows,
                                                   unsigned row_count) {
    Gow3MenuPcPatched done = {0, 0, 0};
    const uint64_t first = (uint64_t)(uintptr_t)data;
    if (size < 32 || !row_count || row_count > GOW3_MENU_PC_MAX_ROWS || rows[0].kind != GOW3_PC_ROW_HEADER) return done;
    /* The root entries: {u32 name, u32 type, u64 pointer}, count at +20, table at +24. +24 is a
     * pointer the loader fixes up (an address by the time the hook sees the data); an offset in
     * the file as stored. */
    const uint64_t roots = gow3_menu_u32(data + 20), table_field = gow3_menu_u64(data + 24);
    const uint64_t table = table_field >= first ? table_field - first : table_field;
    if (roots > 4096 || table > size || roots * 16 > size - table) return done;
    unsigned char *pages_entry = NULL;
    for (uint64_t i = 0; i < roots; ++i) {
        const unsigned char *root = data + table + i * 16;
        if (gow3_menu_u32(root) == GOW3_MENU_PAGE_TABLE_NAME && gow3_menu_u32(root + 4) == GOW3_MENU_PAGE_TABLE_TYPE &&
            gow3_menu_inside(first, size, gow3_menu_u64(root + 8), 16))
            pages_entry = (unsigned char *)(uintptr_t)gow3_menu_u64(root + 8);
    }
    if (!pages_entry) return done;
    const uint64_t page_count = gow3_menu_u64(pages_entry), records = gow3_menu_u64(pages_entry + 8);
    if (!page_count || page_count >= GOW3_MENU_MAX_PAGES) return done;
    /* The page table may already be ours (a copy in the arena): records outside the data then. */
    const int pages_in_data = gow3_menu_inside(first, size, records, page_count * GOW3_MENU_PAGE_SIZE);
    if (!pages_in_data &&
        !gow3_menu_inside((uint64_t)(uintptr_t)arena, arena_size, records, page_count * GOW3_MENU_PAGE_SIZE)) return done;
    const unsigned char *model_page = NULL, *gameplay_page = NULL, *chapters_page = NULL;
    int have_page = 0;
    for (uint64_t i = 0; i < page_count; ++i) {
        const unsigned char *record = (const unsigned char *)(uintptr_t)records + i * GOW3_MENU_PAGE_SIZE;
        const uint32_t hash = gow3_menu_u32(record + GOW3_MENU_PAGE_HASH);
        if (hash == GOW3_MENU_PC_PAGE) have_page = 1;
        if (hash == GOW3_MENU_PAGE_CONTROLS) model_page = record;
        if (hash == GOW3_MENU_PAGE_GAMEPLAY) gameplay_page = record;
        if (hash == GOW3_MENU_PAGE_CHAPTERS_AREA) chapters_page = record;
    }
    if (!model_page) return done;
    unsigned char *controls = NULL;
    const uint64_t control_count = gow3_menu_list(data, size, gow3_menu_u64(model_page + GOW3_MENU_PAGE_LIST), &controls);
    if (!control_count) return done;
    const int header = gow3_menu_find_item(controls, control_count, 0, GOW3_MENU_KIND_HEADER);
    const int spacer = gow3_menu_find_item(controls, control_count, 0, GOW3_MENU_KIND_SPACER);
    const int button = gow3_menu_find_item(controls, control_count, GOW3_MENU_BUTTON_RESET_DEFAULTS, -1);
    const int toggle = gow3_menu_find_item(controls, control_count, 0, GOW3_MENU_KIND_TOGGLE);
    const int slider = gow3_menu_find_item(controls, control_count, 0, GOW3_MENU_KIND_SLIDER);
    if (header != 0 || spacer < 0 || button < 0 || toggle < 0 || slider < 0) return done;
    /* The list model: Gameplay's Difficulty combo (else the toggle). */
    unsigned char *gameplay = NULL;
    const uint64_t gameplay_count =
        gameplay_page ? gow3_menu_list(data, size, gow3_menu_u64(gameplay_page + GOW3_MENU_PAGE_LIST), &gameplay) : 0;
    const int combo = gameplay_count ? gow3_menu_find_item(gameplay, gameplay_count, 0, GOW3_MENU_KIND_COMBO) : -1;
    const unsigned char *list_model = combo >= 0 ? gameplay + (size_t)combo * GOW3_MENU_ITEM_SIZE
                                                 : controls + (size_t)toggle * GOW3_MENU_ITEM_SIZE;
    /* The save list's model: Chapters' area list (one list item), when there is one. */
    unsigned char *chapters = NULL;
    const uint64_t chapters_count =
        chapters_page ? gow3_menu_list(data, size, gow3_menu_u64(chapters_page + GOW3_MENU_PAGE_LIST), &chapters) : 0;
    const int chapters_list = chapters_count ? gow3_menu_find_item(chapters, chapters_count, 0, GOW3_MENU_KIND_LIST) : -1;
    const int saves = chapters_list >= 0;

    if (!have_page) {
        const size_t start = *used;
        unsigned char *items = gow3_menu_take(arena, arena_size, used, (size_t)row_count * GOW3_MENU_ITEM_SIZE);
        unsigned char *entry = gow3_menu_take(arena, arena_size, used, GOW3_MENU_ENTRY_SIZE);
        unsigned char *copy = gow3_menu_take(arena, arena_size, used, (size_t)(page_count + 1 + saves) * GOW3_MENU_PAGE_SIZE);
        unsigned char *save_item = saves ? gow3_menu_take(arena, arena_size, used, GOW3_MENU_ITEM_SIZE) : NULL;
        unsigned char *save_entry = saves ? gow3_menu_take(arena, arena_size, used, GOW3_MENU_ENTRY_SIZE) : NULL;
        const uint64_t save_name = saves ? gow3_menu_string(arena, arena_size, used, "PAGE_PC_SAVES") : 0;
        const uint64_t save_title = saves ? gow3_menu_string(arena, arena_size, used, "MENU_TITLE_PC_SAVES") : 0;
        const uint64_t save_list_name = saves ? gow3_menu_string(arena, arena_size, used, "LIST_PC_SAVES") : 0;
        const uint64_t name = gow3_menu_string(arena, arena_size, used, "PAGE_PC_SETTINGS");
        const uint64_t title = gow3_menu_string(arena, arena_size, used, "MENU_TITLE_PC_SETTINGS");
        const uint64_t row_name = gow3_menu_string(arena, arena_size, used, "BUTTON_PC_SETTING");
        if (!items || !entry || !copy || !name || !title || !row_name ||
            (saves && (!save_item || !save_entry || !save_name || !save_title || !save_list_name))) {
            *used = start;
            return done;
        }
        for (unsigned i = 0; i < row_count; ++i) {
            unsigned char *item = items + (size_t)i * GOW3_MENU_ITEM_SIZE;
            /* The first row is a header, at the Controls header's place on the page (the item's
             * +0x60 and +0x68, which only a list's first item sets). */
            const int model = rows[i].kind == GOW3_PC_ROW_SPACER ? spacer
                            : rows[i].kind == GOW3_PC_ROW_HEADER ? header
                            : rows[i].kind == GOW3_PC_ROW_TOGGLE ? toggle
                            : rows[i].kind == GOW3_PC_ROW_SLIDER ? slider : button;
            memcpy(item, controls + (size_t)model * GOW3_MENU_ITEM_SIZE, GOW3_MENU_ITEM_SIZE);
            if (rows[i].kind == GOW3_PC_ROW_LIST) {
                /* The combo is its list's first item, on a page without the value column: our
                 * row keeps the toggle's flags (+0x14: Controls and Display items have 2, Gameplay
                 * ones 0, and the value then sits further left) and its place fields. */
                memcpy(item, list_model, GOW3_MENU_ITEM_SIZE);
                memcpy(item + GOW3_MENU_ITEM_KIND + 4, controls + (size_t)toggle * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_KIND + 4, 4);
                memcpy(item + 0x60, controls + (size_t)toggle * GOW3_MENU_ITEM_SIZE + 0x60, 8);
                memcpy(item + GOW3_MENU_ITEM_PLACE, controls + (size_t)toggle * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_PLACE, 4);
                gow3_menu_put32(item + GOW3_MENU_ITEM_BINDING, GOW3_MENU_BINDING_INT);
            }
            if (rows[i].kind == GOW3_PC_ROW_SPACER) continue;
            const uint32_t hash = rows[i].kind == GOW3_PC_ROW_HEADER ? rows[i].label : GOW3_MENU_PC_ROW + i;
            gow3_menu_put64(item, row_name);
            gow3_menu_put32(item + GOW3_MENU_ITEM_HASH, hash);
            gow3_menu_put64(item + GOW3_MENU_ITEM_TEXT, row_name);
            gow3_menu_put32(item + GOW3_MENU_ITEM_LABEL, rows[i].label);
            /* The models' binding kind (int for the toggle, float for the slider), our option. */
            if (rows[i].kind == GOW3_PC_ROW_TOGGLE || rows[i].kind == GOW3_PC_ROW_SLIDER || rows[i].kind == GOW3_PC_ROW_LIST)
                gow3_menu_put32(item + GOW3_MENU_ITEM_OPTION, rows[i].option);
        }
        memcpy(entry, (const unsigned char *)(uintptr_t)gow3_menu_u64(model_page + GOW3_MENU_PAGE_LIST), GOW3_MENU_ENTRY_SIZE);
        gow3_menu_put64(entry, row_count);
        gow3_menu_put64(entry + 8, (uint64_t)(uintptr_t)items);
        memcpy(copy, (const unsigned char *)(uintptr_t)records, (size_t)page_count * GOW3_MENU_PAGE_SIZE);
        unsigned char *page = copy + (size_t)page_count * GOW3_MENU_PAGE_SIZE;
        memcpy(page, model_page, GOW3_MENU_PAGE_SIZE);
        gow3_menu_put64(page, name);
        gow3_menu_put32(page + GOW3_MENU_PAGE_HASH, GOW3_MENU_PC_PAGE);
        gow3_menu_put64(page + GOW3_MENU_PAGE_TITLE_TEXT, title);
        gow3_menu_put32(page + GOW3_MENU_PAGE_TITLE, GOW3_MENU_PC_TITLE);
        gow3_menu_put64(page + GOW3_MENU_PAGE_LIST, (uint64_t)(uintptr_t)entry);
        if (saves) {
            /* The list widget with our int binding: the value is the chosen row; a press sends
             * the item's hash to the dispatcher, as Chapters' list does. */
            memcpy(save_item, chapters + (size_t)chapters_list * GOW3_MENU_ITEM_SIZE, GOW3_MENU_ITEM_SIZE);
            gow3_menu_put64(save_item, save_list_name);
            gow3_menu_put32(save_item + GOW3_MENU_ITEM_HASH, GOW3_MENU_SAVES_LIST);
            gow3_menu_put32(save_item + GOW3_MENU_ITEM_BINDING, GOW3_MENU_BINDING_INT);
            gow3_menu_put32(save_item + GOW3_MENU_ITEM_OPTION, GOW3_MENU_SAVES_OPTION);
            memcpy(save_entry, (const unsigned char *)(uintptr_t)gow3_menu_u64(chapters_page + GOW3_MENU_PAGE_LIST),
                   GOW3_MENU_ENTRY_SIZE);
            gow3_menu_put64(save_entry, 1);
            gow3_menu_put64(save_entry + 8, (uint64_t)(uintptr_t)save_item);
            unsigned char *save_page = page + GOW3_MENU_PAGE_SIZE;
            memcpy(save_page, chapters_page, GOW3_MENU_PAGE_SIZE);
            gow3_menu_put64(save_page, save_name);
            gow3_menu_put32(save_page + GOW3_MENU_PAGE_HASH, GOW3_MENU_SAVES_PAGE);
            gow3_menu_put64(save_page + GOW3_MENU_PAGE_TITLE_TEXT, save_title);
            gow3_menu_put32(save_page + GOW3_MENU_PAGE_TITLE, GOW3_MENU_SAVES_TITLE);
            gow3_menu_put64(save_page + GOW3_MENU_PAGE_LIST, (uint64_t)(uintptr_t)save_entry);
            done.saves = 1;
        }
        gow3_menu_put64(pages_entry, page_count + 1 + saves);
        gow3_menu_put64(pages_entry + 8, (uint64_t)(uintptr_t)copy);
        done.page = 1;
    }

    /* The Options list: the one with the Controls and Display buttons. The new button goes after
     * Language (else after Display), copied from Controls (a button that opens a page). */
    for (uint64_t at = 0; at + GOW3_MENU_ENTRY_SIZE <= size; at += 8) {
        unsigned char *items = NULL;
        const uint64_t count = gow3_menu_list(data, size, first + at, &items);
        if (!count || count >= GOW3_MENU_MAX_ITEMS) continue;
        const int model = gow3_menu_find_item(items, count, GOW3_MENU_BUTTON_OPTIONS_CONTROLS, -1);
        const int video = gow3_menu_find_item(items, count, GOW3_MENU_BUTTON_OPTIONS_VIDEO, -1);
        const int language = gow3_menu_find_item(items, count, GOW3_MENU_BUTTON_OPTIONS_LANGUAGE, -1);
        if (model < 0 || video < 0 || gow3_menu_find_item(items, count, GOW3_MENU_PC_BUTTON, -1) >= 0) continue;
        const uint64_t insert = (uint64_t)(language >= 0 ? language : video) + 1;
        const size_t start = *used;
        unsigned char *copy = gow3_menu_take(arena, arena_size, used, (size_t)(count + 1) * GOW3_MENU_ITEM_SIZE);
        const uint64_t name = gow3_menu_string(arena, arena_size, used, "BUTTON_OPTIONS_PC");
        const uint64_t text = gow3_menu_string(arena, arena_size, used, "MENU_PC_SETTINGS");
        const uint64_t page_name = gow3_menu_string(arena, arena_size, used, "PAGE_PC_SETTINGS");
        if (!copy || !name || !text || !page_name) {
            *used = start;
            break;
        }
        memcpy(copy, items, insert * GOW3_MENU_ITEM_SIZE);
        memcpy(copy + (insert + 1) * GOW3_MENU_ITEM_SIZE, items + insert * GOW3_MENU_ITEM_SIZE,
               (count - insert) * GOW3_MENU_ITEM_SIZE);
        unsigned char *item = copy + insert * GOW3_MENU_ITEM_SIZE;
        memcpy(item, items + (size_t)model * GOW3_MENU_ITEM_SIZE, GOW3_MENU_ITEM_SIZE);
        gow3_menu_put64(item, name);
        gow3_menu_put32(item + GOW3_MENU_ITEM_HASH, GOW3_MENU_PC_BUTTON);
        gow3_menu_put64(item + GOW3_MENU_ITEM_TEXT, text);
        gow3_menu_put32(item + GOW3_MENU_ITEM_LABEL, GOW3_MENU_PC_LABEL);
        gow3_menu_put64(item + GOW3_MENU_ITEM_PAGE_NAME, page_name);
        gow3_menu_put32(item + GOW3_MENU_ITEM_PAGE, GOW3_MENU_PC_PAGE);
        unsigned char *entry = data + at;
        gow3_menu_put64(entry, count + 1);
        gow3_menu_put64(entry + 8, (uint64_t)(uintptr_t)copy);
        ++done.button;
    }
    return done;
}

/* The binding factory (update 1.11: 0x3187d0): its entry, up to `cmp esi, 0x34; ja`. */
static const Gow3Signature gow3_menu_binding_factory = {
    "55 48 89 e5 41 57 41 56 41 55 41 54 53 50 45 31 ed 45 89 ce 45 89 c7 41 89 cc 48 89 fb 89 f0 83 fe 34 0f 87", 0};
enum { GOW3_MENU_BINDING_CALLS = 2, GOW3_MENU_OPTION_STUB_SIZE = 40 };
/* Page navigation (update 1.11): open(menu, page hash, 0, 0) at 0x30e8b0 finds the page among the
 * menu's built pages (menu+0x3a0, 150 slots, hash at +0x10) and shows it; back(menu, 0) at
 * 0x30fcc0 returns to the previous one (the Yes/No page's No). The shown pages are a stack:
 * menu+0x878[menu+0xd28] is the top one. */
static const Gow3Signature gow3_menu_page_open = {
    "55 48 89 e5 41 57 41 56 41 55 41 54 53 48 81 ec e8 02 00 00 41 89 cd 41 89 d4 49 89 ff 31 c0 90", 0};
static const Gow3Signature gow3_menu_page_back = {
    "55 48 89 e5 41 57 41 56 53 48 83 ec 58 48 89 fb 48 8b 4b 08 85 c9 74 6b c5 fa 10 83 54 0d 00 00", 0};
enum { GOW3_MENU_PAGE_STACK = 0x878, GOW3_MENU_PAGE_TOP = 0xd28, GOW3_MENU_PAGE_STACK_SIZE = 32,
       GOW3_MENU_PAGE_OBJECT_HASH = 0x10 };
/* The menu's per-frame update (update 1.11: 0x313330, update(menu, ...), called once a frame by
 * the main loop; the dispatcher runs inside it). The save data dialog opens on the save system's
 * own thread: the save list page is opened from here, at the next frame. Its entry is hooked
 * like the dispatcher's (gow3_menu_build_click_stub: helper(menu, esi), then the 6 bytes). */
static const Gow3Signature gow3_menu_update_site = {
    "55 48 89 e5 41 57 41 56 41 55 41 54 53 48 83 ec 28 49 89 fd 48 8d 0d ?? ?? ?? ?? 4c 8d 25 ?? ?? ?? ?? "
    "49 89 d7 48 8d 15 ?? ?? ?? ?? bf eb ce ff 74", 0};
enum { GOW3_MENU_UPDATE_LENGTH = 6 };

/* The calls (e8 rel32) to `target` in code[0, size), at most `max`; returns how many there are. */
static inline unsigned gow3_menu_find_calls(const unsigned char *code, uint64_t size, uint64_t target,
                                            uint64_t *found, unsigned max) {
    unsigned count = 0;
    for (uint64_t at = 0; at + 5 <= size; ++at) {
        if (code[at] != 0xe8) continue;
        int32_t rel;
        memcpy(&rel, code + at + 1, 4);
        if ((int64_t)at + 5 + rel != (int64_t)target) continue;
        if (count < max) found[count] = at;
        ++count;
    }
    return count;
}

/* The factory's stub: sids in [PC_OPTION, PC_OPTION + MAX_OPTIONS) go to helper (same arguments,
 * a tail jump), every other to the factory. eax is free at a call. */
static inline unsigned gow3_menu_build_option_stub(unsigned char *out, uintptr_t helper, uintptr_t factory) {
    unsigned k = 0;
    const uint32_t first = GOW3_MENU_PC_OPTION, count = GOW3_MENU_PC_MAX_OPTIONS;
    out[k++] = 0x89; out[k++] = 0xc8;                                    /* mov eax, ecx */
    out[k++] = 0x2d; memcpy(out + k, &first, 4); k += 4;                 /* sub eax, first */
    out[k++] = 0x3d; memcpy(out + k, &count, 4); k += 4;                 /* cmp eax, count */
    out[k++] = 0x73; out[k++] = 12;                                       /* jae factory */
    out[k++] = 0x48; out[k++] = 0xb8; memcpy(out + k, &helper, 8); k += 8; /* mov rax, helper */
    out[k++] = 0xff; out[k++] = 0xe0;                                     /* jmp rax */
    out[k++] = 0x48; out[k++] = 0xb8; memcpy(out + k, &factory, 8); k += 8; /* mov rax, factory */
    out[k++] = 0xff; out[k++] = 0xe0;                                     /* jmp rax */
    return k;
}
#endif
