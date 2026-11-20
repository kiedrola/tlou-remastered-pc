/* The save data dialog's list as a page of the game's own menu (src/menu_pc_hook.h: a copy of
 * Chapters' area page, PAGE_PC_SAVES): the rows are the game's list widget, the shown row's save
 * picture is drawn by the overlay where Chapters has its own. The dialog opens on the save
 * system's thread: it only asks, and the menu's next update (its entry hook) opens the page.
 * GOW3_NATIVE_SAVE_LIST=0 keeps the overlay list. */
#ifndef GOW3_RUNTIME_MENU_SAVES_H
#define GOW3_RUNTIME_MENU_SAVES_H
#include <stdint.h>
#include "runtime.h"
#include "runtime_menu_pc.h"

/* At hook install: the page navigation functions, and the menu update hook in place. */
void runtime_menu_saves_set_navigation(void *open, void *back, void *dispatcher);
void runtime_menu_saves_set_update_hooked(void);
/* The page was added to the menu data. */
void runtime_menu_saves_page_added(void);
/* The menu update hook's helper (menu, the update's esi). */
ABI void runtime_menu_saves_update(void *menu, uint32_t unused);
/* A list dialog's first opening (type 1 save, 2 load; delete keeps the overlay list): its rows go
 * to the page (labels UTF-8, pictures PNG paths or NULL, chapters the saves' sub-chapter names);
 * 1 when the page will show them (the dialog is then answered "cancelled"), 0 to use the overlay
 * list. */
int runtime_menu_saves_open(int type, int count, int new_first, const char *const *labels,
                            const char *const *pictures, const char *const *chapters);
/* The row chosen on the page (once; -1 none): the replayed button's dialog answers with it. */
int runtime_menu_saves_take_choice(void);
/* From the dispatcher hook: the button pressed last (the one a dialog comes from, pressed again
 * once a row is chosen). */
void runtime_menu_saves_note_button(uint32_t hash);
/* A press (inside the dispatcher): 1 when it was the list's (the shown row is chosen). */
int runtime_menu_saves_button(void *menu, uint32_t hash);
/* The list's binding (runtime_menu_pc_binding, for GOW3_MENU_SAVES_OPTION). */
void runtime_menu_saves_bind(unsigned char *binding);
/* The page title, from the text hook (NULL for other ids). */
const char *runtime_menu_saves_text(Gow3TextLookup lookup, void *table, uint32_t sid);
#endif
