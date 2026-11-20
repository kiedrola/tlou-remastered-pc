#undef NDEBUG
#include "../src/menu_pc_hook.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

enum { SIZE = 0x2000, PAUSE_ITEMS = 0x400, LOBBY_ITEMS = 0x1000, PAUSE_ENTRY = 0x100, LOBBY_ENTRY = 0x118 };
static _Alignas(16) unsigned char data[SIZE];
static _Alignas(16) unsigned char arena[0x1000];

static void item(uint64_t list, unsigned index, uint32_t hash, uint32_t kind) {
    unsigned char *p = data + list + index * GOW3_MENU_ITEM_SIZE;
    memcpy(p + GOW3_MENU_ITEM_HASH, &hash, 4);
    memcpy(p + GOW3_MENU_ITEM_KIND, &kind, 4);
    const uint64_t name = hash ? (uint64_t)(uintptr_t)data : 0;
    memcpy(p, &name, 8);
}
static void entry(uint64_t at, uint64_t count, uint64_t list) {
    const uint64_t pointer = (uint64_t)(uintptr_t)data + list;
    const uint32_t type = GOW3_MENU_LIST_TYPE;
    memcpy(data + at, &count, 8);
    memcpy(data + at + 8, &pointer, 8);
    memcpy(data + at + 20, &type, 4);
}
static uint32_t hash_at(uint64_t list, unsigned index) {
    return gow3_menu_u32(data + list + index * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_HASH);
}

/* The PC settings page (menu_pc_hook.h) on a small copy of menu.bin's layout: the root entry of
 * the page table, two pages (Options, Controls), the Controls items and the Options list. */
enum { PC_SIZE = 0x3000, PC_ROOTS = 0x40, PC_PAGES = 0x80, PC_RECORDS = 0x100, PC_CONTROLS_ENTRY = 0x400,
       PC_CONTROLS = 0x500, PC_OPTIONS_ENTRY = 0x800, PC_OPTIONS = 0x900, PC_GAMEPLAY_ENTRY = 0xc00,
       PC_GAMEPLAY = 0xd00, PC_CHAPTERS_ENTRY = 0xe00, PC_CHAPTERS = 0xe40 };
static _Alignas(16) unsigned char pc[PC_SIZE];
static _Alignas(16) unsigned char pc_arena[0x2000];
static void pc_put32(uint64_t at, uint32_t v) { memcpy(pc + at, &v, 4); }
static void pc_put64(uint64_t at, uint64_t v) { memcpy(pc + at, &v, 8); }
static void pc_item(uint64_t list, unsigned index, uint32_t hash, uint32_t kind) {
    const uint64_t at = list + index * GOW3_MENU_ITEM_SIZE;
    pc_put32(at + GOW3_MENU_ITEM_HASH, hash);
    pc_put32(at + GOW3_MENU_ITEM_KIND, kind);
    pc_put32(at + 0x60, index == 0 ? 0x1a40000u : 0);
}
static void pc_entry(uint64_t at, uint64_t count, uint64_t list) {
    pc_put64(at, count);
    pc_put64(at + 8, (uint64_t)(uintptr_t)pc + list);
    pc_put32(at + 20, GOW3_MENU_LIST_TYPE);
}
static void test_pc_page(void) {
    const uint64_t base = (uint64_t)(uintptr_t)pc;
    pc_put32(20, 1);
    pc_put64(24, base + PC_ROOTS); /* fixed up by the loader: an address */
    pc_put32(PC_ROOTS, GOW3_MENU_PAGE_TABLE_NAME);
    pc_put32(PC_ROOTS + 4, GOW3_MENU_PAGE_TABLE_TYPE);
    pc_put64(PC_ROOTS + 8, base + PC_PAGES);
    pc_put64(PC_PAGES, 4);
    pc_put64(PC_PAGES + 8, base + PC_RECORDS);
    pc_put32(PC_RECORDS + GOW3_MENU_PAGE_HASH, 0xc7597732u);
    pc_put64(PC_RECORDS + GOW3_MENU_PAGE_LIST, base + PC_OPTIONS_ENTRY);
    const uint64_t controls_page = PC_RECORDS + GOW3_MENU_PAGE_SIZE;
    pc_put32(controls_page + GOW3_MENU_PAGE_HASH, GOW3_MENU_PAGE_CONTROLS);
    pc_put32(controls_page + GOW3_MENU_PAGE_TITLE, 0x98ab8e19u);
    pc_put64(controls_page + GOW3_MENU_PAGE_LIST, base + PC_CONTROLS_ENTRY);
    pc_put32(controls_page + 0x98, 0x99e55e57u);
    const uint64_t gameplay_page = PC_RECORDS + 2 * GOW3_MENU_PAGE_SIZE;
    pc_put32(gameplay_page + GOW3_MENU_PAGE_HASH, GOW3_MENU_PAGE_GAMEPLAY);
    pc_put64(gameplay_page + GOW3_MENU_PAGE_LIST, base + PC_GAMEPLAY_ENTRY);
    pc_item(PC_GAMEPLAY, 0, 0x4b09865au, GOW3_MENU_KIND_COMBO);
    pc_put32(PC_GAMEPLAY + GOW3_MENU_ITEM_BINDING, 3);
    pc_put32(PC_GAMEPLAY + GOW3_MENU_ITEM_PLACE, 0xffffu);
    pc_put32(PC_CONTROLS + 4 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_PLACE, 0xffffffffu);
    pc_put32(PC_CONTROLS + 4 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_KIND + 4, 2);
    pc_entry(PC_GAMEPLAY_ENTRY, 1, PC_GAMEPLAY);
    const uint64_t chapters_page = PC_RECORDS + 3 * GOW3_MENU_PAGE_SIZE;
    pc_put32(chapters_page + GOW3_MENU_PAGE_HASH, GOW3_MENU_PAGE_CHAPTERS_AREA);
    pc_put32(chapters_page + 0x40, 0x12345678u);
    pc_put64(chapters_page + GOW3_MENU_PAGE_LIST, base + PC_CHAPTERS_ENTRY);
    pc_item(PC_CHAPTERS, 0, 0x1e5deb0eu, GOW3_MENU_KIND_LIST);
    pc_put32(PC_CHAPTERS + GOW3_MENU_ITEM_BINDING, 0x11);
    pc_entry(PC_CHAPTERS_ENTRY, 1, PC_CHAPTERS);
    pc_item(PC_CONTROLS, 0, 0x44dbc3b6u, GOW3_MENU_KIND_HEADER);
    pc_item(PC_CONTROLS, 1, 0, 5);
    pc_item(PC_CONTROLS, 2, 0, GOW3_MENU_KIND_SPACER);
    pc_item(PC_CONTROLS, 3, GOW3_MENU_BUTTON_RESET_DEFAULTS, GOW3_MENU_KIND_BUTTON);
    pc_item(PC_CONTROLS, 4, 0, GOW3_MENU_KIND_TOGGLE);
    pc_put32(PC_CONTROLS + 4 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_BINDING, 1);
    pc_put32(PC_CONTROLS + 4 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_OPTION, 0x19d81965u);
    pc_put32(PC_CONTROLS + 1 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_BINDING, 2);
    pc_entry(PC_CONTROLS_ENTRY, 5, PC_CONTROLS);
    pc_item(PC_OPTIONS, 0, 0xc79a65b5u, GOW3_MENU_KIND_BUTTON);
    pc_item(PC_OPTIONS, 1, GOW3_MENU_BUTTON_OPTIONS_CONTROLS, GOW3_MENU_KIND_BUTTON);
    pc_put32(PC_OPTIONS + GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_PAGE, GOW3_MENU_PAGE_CONTROLS);
    pc_item(PC_OPTIONS, 2, GOW3_MENU_BUTTON_OPTIONS_VIDEO, GOW3_MENU_KIND_BUTTON);
    pc_item(PC_OPTIONS, 3, GOW3_MENU_BUTTON_OPTIONS_LANGUAGE, GOW3_MENU_KIND_BUTTON);
    pc_item(PC_OPTIONS, 4, 0x2be77c25u, GOW3_MENU_KIND_BUTTON);
    pc_entry(PC_OPTIONS_ENTRY, 5, PC_OPTIONS);

    static const Gow3PcRow rows[] = {
        {GOW3_PC_ROW_HEADER, 0x53126e93u, 0},
        {GOW3_PC_ROW_BUTTON, GOW3_MENU_PC_ROW_LABEL + 1, 0},
        {GOW3_PC_ROW_SPACER, 0, 0},
        {GOW3_PC_ROW_BUTTON, GOW3_MENU_PC_ROW_LABEL + 3, 0},
        {GOW3_PC_ROW_TOGGLE, GOW3_MENU_PC_ROW_LABEL + 4, GOW3_MENU_PC_OPTION + 4},
        {GOW3_PC_ROW_SLIDER, GOW3_MENU_PC_ROW_LABEL + 5, GOW3_MENU_PC_OPTION + 5},
        {GOW3_PC_ROW_LIST, GOW3_MENU_PC_ROW_LABEL + 6, GOW3_MENU_PC_OPTION + 6},
    };
    size_t used = 0;
    Gow3MenuPcPatched done = gow3_menu_pc_patch(pc, PC_SIZE, pc_arena, sizeof(pc_arena), &used, rows, 7);
    assert(done.page == 1 && done.button == 1 && done.saves == 1 && used);

    /* The page table: the file's records copied, ours appended (a copy of Controls). */
    assert(gow3_menu_u64(pc + PC_PAGES) == 6);
    const unsigned char *records = (const unsigned char *)(uintptr_t)gow3_menu_u64(pc + PC_PAGES + 8);
    assert(records >= pc_arena && (uintptr_t)records % 16 == 0);
    assert(gow3_menu_u32(records + GOW3_MENU_PAGE_SIZE + GOW3_MENU_PAGE_HASH) == GOW3_MENU_PAGE_CONTROLS);
    const unsigned char *page = records + 4 * GOW3_MENU_PAGE_SIZE;
    /* The save list page: Chapters' area page with our title and one list item, our int binding. */
    const unsigned char *save_page = records + 5 * GOW3_MENU_PAGE_SIZE;
    assert(gow3_menu_u32(save_page + GOW3_MENU_PAGE_HASH) == GOW3_MENU_SAVES_PAGE);
    assert(gow3_menu_u32(save_page + GOW3_MENU_PAGE_TITLE) == GOW3_MENU_SAVES_TITLE);
    assert(gow3_menu_u32(save_page + 0x40) == 0x12345678u);
    const unsigned char *save_entry = (const unsigned char *)(uintptr_t)gow3_menu_u64(save_page + GOW3_MENU_PAGE_LIST);
    assert(gow3_menu_u64(save_entry) == 1);
    const unsigned char *save_item = (const unsigned char *)(uintptr_t)gow3_menu_u64(save_entry + 8);
    assert(gow3_menu_u32(save_item + GOW3_MENU_ITEM_KIND) == GOW3_MENU_KIND_LIST);
    assert(gow3_menu_u32(save_item + GOW3_MENU_ITEM_HASH) == GOW3_MENU_SAVES_LIST);
    assert(gow3_menu_u32(save_item + GOW3_MENU_ITEM_BINDING) == GOW3_MENU_BINDING_INT);
    assert(gow3_menu_u32(save_item + GOW3_MENU_ITEM_OPTION) == GOW3_MENU_SAVES_OPTION);
    assert(gow3_menu_u32(page + GOW3_MENU_PAGE_HASH) == GOW3_MENU_PC_PAGE);
    assert(gow3_menu_u32(page + GOW3_MENU_PAGE_TITLE) == GOW3_MENU_PC_TITLE);
    assert(gow3_menu_u32(page + 0x98) == 0x99e55e57u);
    assert(!strcmp((const char *)(uintptr_t)gow3_menu_u64(page), "PAGE_PC_SETTINGS"));
    const unsigned char *entry = (const unsigned char *)(uintptr_t)gow3_menu_u64(page + GOW3_MENU_PAGE_LIST);
    assert(gow3_menu_u64(entry) == 7 && gow3_menu_u32(entry + 20) == GOW3_MENU_LIST_TYPE);
    const unsigned char *items = (const unsigned char *)(uintptr_t)gow3_menu_u64(entry + 8);
    /* Header first (with the first item's layout), then a button, the spacer, a button. */
    assert(gow3_menu_u32(items + GOW3_MENU_ITEM_KIND) == GOW3_MENU_KIND_HEADER);
    assert(gow3_menu_u32(items + GOW3_MENU_ITEM_LABEL) == 0x53126e93u);
    assert(gow3_menu_u32(items + 0x60) == 0x1a40000u);
    const unsigned char *row = items + GOW3_MENU_ITEM_SIZE;
    assert(gow3_menu_u32(row + GOW3_MENU_ITEM_KIND) == GOW3_MENU_KIND_BUTTON && gow3_menu_u32(row + 0x60) == 0);
    assert(gow3_menu_u32(row + GOW3_MENU_ITEM_HASH) == GOW3_MENU_PC_ROW + 1);
    assert(gow3_menu_u32(row + GOW3_MENU_ITEM_LABEL) == GOW3_MENU_PC_ROW_LABEL + 1);
    assert(gow3_menu_u32(items + 2 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_KIND) == GOW3_MENU_KIND_SPACER);
    assert(gow3_menu_u32(items + 3 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_HASH) == GOW3_MENU_PC_ROW + 3);
    /* The toggle and the slider: the models' kinds and binding kinds, our option sids. */
    const unsigned char *toggle = items + 4 * GOW3_MENU_ITEM_SIZE, *slider = items + 5 * GOW3_MENU_ITEM_SIZE;
    assert(gow3_menu_u32(toggle + GOW3_MENU_ITEM_KIND) == GOW3_MENU_KIND_TOGGLE);
    assert(gow3_menu_u32(toggle + GOW3_MENU_ITEM_BINDING) == 1 && gow3_menu_u32(toggle + GOW3_MENU_ITEM_OPTION) == GOW3_MENU_PC_OPTION + 4);
    assert(gow3_menu_u32(toggle + GOW3_MENU_ITEM_LABEL) == GOW3_MENU_PC_ROW_LABEL + 4);
    assert(gow3_menu_u32(slider + GOW3_MENU_ITEM_KIND) == GOW3_MENU_KIND_SLIDER);
    assert(gow3_menu_u32(slider + GOW3_MENU_ITEM_BINDING) == 2 && gow3_menu_u32(slider + GOW3_MENU_ITEM_OPTION) == GOW3_MENU_PC_OPTION + 5);
    /* The list: the combo's widget with an int binding, at a later row's place. */
    const unsigned char *list = items + 6 * GOW3_MENU_ITEM_SIZE;
    assert(gow3_menu_u32(list + GOW3_MENU_ITEM_KIND) == GOW3_MENU_KIND_COMBO);
    assert(gow3_menu_u32(list + GOW3_MENU_ITEM_KIND + 4) == 2);
    assert(gow3_menu_u32(list + GOW3_MENU_ITEM_BINDING) == GOW3_MENU_BINDING_INT);
    assert(gow3_menu_u32(list + GOW3_MENU_ITEM_OPTION) == GOW3_MENU_PC_OPTION + 6);
    assert(gow3_menu_u32(list + GOW3_MENU_ITEM_PLACE) == 0xffffffffu && gow3_menu_u32(list + GOW3_MENU_ITEM_HASH) == GOW3_MENU_PC_ROW + 6);

    /* Options: our button after Language, before Credits, opening our page. */
    assert(gow3_menu_u64(pc + PC_OPTIONS_ENTRY) == 6);
    const unsigned char *options = (const unsigned char *)(uintptr_t)gow3_menu_u64(pc + PC_OPTIONS_ENTRY + 8);
    const unsigned char *button = options + 4 * GOW3_MENU_ITEM_SIZE;
    assert(gow3_menu_u32(options + 3 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_HASH) == GOW3_MENU_BUTTON_OPTIONS_LANGUAGE);
    assert(gow3_menu_u32(button + GOW3_MENU_ITEM_HASH) == GOW3_MENU_PC_BUTTON);
    assert(gow3_menu_u32(button + GOW3_MENU_ITEM_LABEL) == GOW3_MENU_PC_LABEL);
    assert(gow3_menu_u32(button + GOW3_MENU_ITEM_PAGE) == GOW3_MENU_PC_PAGE);
    assert(!strcmp((const char *)(uintptr_t)gow3_menu_u64(button + GOW3_MENU_ITEM_PAGE_NAME), "PAGE_PC_SETTINGS"));
    assert(gow3_menu_u32(options + 5 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_HASH) == 0x2be77c25u);
    /* The file's own arrays are unchanged. */
    assert(gow3_menu_u32(pc + PC_OPTIONS + 4 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_HASH) == 0x2be77c25u);

    /* Patched data is left alone. */
    const size_t before = used;
    done = gow3_menu_pc_patch(pc, PC_SIZE, pc_arena, sizeof(pc_arena), &used, rows, 7);
    assert(!done.page && !done.button && used == before);

    /* Fresh data without room: nothing changes, nothing taken. */
    pc_put64(PC_PAGES, 4);
    pc_put64(PC_PAGES + 8, base + PC_RECORDS);
    pc_entry(PC_OPTIONS_ENTRY, 5, PC_OPTIONS);
    used = sizeof(pc_arena) - 64;
    done = gow3_menu_pc_patch(pc, PC_SIZE, pc_arena, sizeof(pc_arena), &used, rows, 7);
    assert(!done.page && !done.button && used == sizeof(pc_arena) - 64);
    assert(gow3_menu_u64(pc + PC_PAGES) == 4 && gow3_menu_u64(pc + PC_OPTIONS_ENTRY) == 5);

    /* Without the Controls page there is no model: nothing is added. */
    pc_put32(controls_page + GOW3_MENU_PAGE_HASH, 0x12345678u);
    used = 0;
    done = gow3_menu_pc_patch(pc, PC_SIZE, pc_arena, sizeof(pc_arena), &used, rows, 7);
    assert(!done.page && !done.button && !used);
}

int main(void) {
    item(PAUSE_ITEMS, 0, GOW3_MENU_BUTTON_OPTIONS, 2);
    item(PAUSE_ITEMS, 1, 0, 1);
    item(PAUSE_ITEMS, 2, GOW3_MENU_BUTTON_QUIT, GOW3_MENU_KIND_BUTTON);
    entry(PAUSE_ENTRY, 3, PAUSE_ITEMS);
    item(LOBBY_ITEMS, 0, GOW3_MENU_BUTTON_STORY, 2);
    item(LOBBY_ITEMS, 1, GOW3_MENU_BUTTON_OPTIONS, 2);
    item(LOBBY_ITEMS, 2, 0, GOW3_MENU_KIND_DOWNLOADS);
    item(LOBBY_ITEMS, 3, 0, 1);
    entry(LOBBY_ENTRY, 4, LOBBY_ITEMS);

    size_t used = 0;
    Gow3MenuPatched done = gow3_menu_patch(data, SIZE, arena, sizeof(arena), &used, GOW3_MENU_LABEL_EXIT_DESKTOP);
    assert(done.pause == 1 && done.lobby == 1 && used && used % 16 == 0);

    assert(gow3_menu_u64(data + PAUSE_ENTRY) == 4);
    unsigned char *pause = (unsigned char *)(uintptr_t)gow3_menu_u64(data + PAUSE_ENTRY + 8);
    assert(gow3_menu_u32(pause + 2 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_HASH) == GOW3_MENU_BUTTON_QUIT);
    assert(gow3_menu_u32(pause + 3 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_HASH) == GOW3_MENU_EXIT_PAUSE);
    assert(gow3_menu_u32(pause + 3 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_LABEL) == GOW3_MENU_LABEL_EXIT_DESKTOP);
    assert(!strcmp((const char *)(uintptr_t)gow3_menu_u64(pause + 3 * GOW3_MENU_ITEM_SIZE), "BUTTON_EXIT_DESKTOP"));

    assert(gow3_menu_u64(data + LOBBY_ENTRY) == 5);
    unsigned char *lobby = (unsigned char *)(uintptr_t)gow3_menu_u64(data + LOBBY_ENTRY + 8);
    assert(gow3_menu_u32(lobby + 3 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_HASH) == GOW3_MENU_EXIT_LOBBY);
    assert(gow3_menu_u32(lobby + 4 * GOW3_MENU_ITEM_SIZE + GOW3_MENU_ITEM_KIND) == 1);

    assert(hash_at(PAUSE_ITEMS, 2) == GOW3_MENU_BUTTON_QUIT && hash_at(LOBBY_ITEMS, 3) == 0);

    const size_t before = used;
    done = gow3_menu_patch(data, SIZE, arena, sizeof(arena), &used, GOW3_MENU_LABEL_EXIT_DESKTOP);
    assert(!done.pause && !done.lobby && used == before);

    memset(arena, 0, sizeof(arena));
    entry(PAUSE_ENTRY, 3, PAUSE_ITEMS);
    entry(LOBBY_ENTRY, 4, LOBBY_ITEMS);
    used = sizeof(arena) - 16;
    done = gow3_menu_patch(data, SIZE, arena, sizeof(arena), &used, GOW3_MENU_LABEL_EXIT_DESKTOP);
    assert(!done.pause && !done.lobby);

    /* Stubs: fit the 512 bytes probe.c gives each, step over the red zone, keep rsp aligned. */
    static unsigned char stub[1024];
    static const unsigned char load_original[GOW3_MENU_LOAD_LENGTH] = {0x4c, 0x8b, 0x65, 0xb8, 0x49, 0x63, 0x48, 0x14};
    static const unsigned char click_original[GOW3_MENU_CLICK_LENGTH] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57};
    unsigned n = gow3_menu_build_load_stub(stub, 0x1111, 0x2222, load_original);
    assert(n <= 512);
    assert(!memcmp(stub, "\x48\x8d\x64\x24\x80\x9c", 6));
    assert(!memcmp(stub + n - 31, "\x9d\x48\x8d\xa4\x24\x80\x00\x00\x00", 9));
    assert(!memcmp(stub + n - 22, load_original, GOW3_MENU_LOAD_LENGTH));
    n = gow3_menu_build_click_stub(stub, 0x1111, 0x2222, click_original);
    assert(n <= 512);
    /* entry rsp = 8 mod 16; six pushes and 0x108 leave the call 16-byte aligned. */
    assert(!memcmp(stub + 8, "\x48\x81\xec\x08\x01\x00\x00", 7));
    assert(!memcmp(stub + n - 20, click_original, GOW3_MENU_CLICK_LENGTH));
    /* helper(menu, hash) takes the dispatcher's rdi and esi as they are: the call follows the saves. */
    assert(!memcmp(stub + 15 + 8 * 9, "\x48\xb8", 2));

    /* Text stub: our ids (a range, unsigned compare after the subtraction) jump to the helper,
     * any other runs the replayed entry. */
    static const unsigned char text_original[GOW3_MENU_TEXT_LENGTH] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57};
    unsigned original_at = 0;
    n = gow3_menu_build_text_stub(stub, 0x1111, 0x2222, text_original, &original_at);
    /* 14u: original_at is unsigned (clang does not warn, GCC's -Wsign-compare does). */
    assert(n <= 64 && original_at == 26 && stub[12] == 0x73 && 14u + stub[13] == original_at);
    assert(stub[0] == 0x89 && stub[1] == 0xf0 && stub[2] == 0x2d && stub[7] == 0x3d);
    assert(gow3_menu_u32(stub + 3) == GOW3_MENU_TEXT_FIRST);
    assert(gow3_menu_u32(stub + 8) == GOW3_MENU_TEXT_LAST - GOW3_MENU_TEXT_FIRST + 1);
    assert(!memcmp(stub + original_at, text_original, GOW3_MENU_TEXT_LENGTH));

    test_pc_page();

    /* The binding factory's stub: our option range to the helper, the rest to the factory. */
    n = gow3_menu_build_option_stub(stub, 0x1111, 0x2222);
    assert(n <= GOW3_MENU_OPTION_STUB_SIZE && stub[0] == 0x89 && stub[1] == 0xc8);
    assert(gow3_menu_u32(stub + 3) == GOW3_MENU_PC_OPTION && gow3_menu_u32(stub + 8) == GOW3_MENU_PC_MAX_OPTIONS);
    assert(stub[12] == 0x73 && 14 + stub[13] == 26 && gow3_menu_u64(stub + 16) == 0x1111 && gow3_menu_u64(stub + 28) == 0x2222);
    /* Its calls: e8 rel32 to the target, counted past the maximum. */
    static unsigned char code[64];
    const int32_t to_40 = 40 - (3 + 5), to_40b = 40 - (20 + 5), to_other = 7;
    code[3] = 0xe8; memcpy(code + 4, &to_40, 4);
    code[20] = 0xe8; memcpy(code + 21, &to_40b, 4);
    code[30] = 0xe8; memcpy(code + 31, &to_other, 4);
    uint64_t calls[2];
    assert(gow3_menu_find_calls(code, sizeof(code), 40, calls, 2) == 2 && calls[0] == 3 && calls[1] == 20);
    assert(gow3_menu_find_calls(code, sizeof(code), 40, calls, 1) == 2 && calls[0] == 3);
    return 0;
}
