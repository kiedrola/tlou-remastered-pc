/* "PC SETTINGS" in the game's Options page (src/menu_pc_hook.h): the page's rows (the game's own
 * toggles and sliders), their labels in the game's language, their values. GOW3_PC_MENU=0 leaves
 * it out. */
#ifndef GOW3_RUNTIME_MENU_PC_H
#define GOW3_RUNTIME_MENU_PC_H
#include <stdint.h>
#include "menu_pc_hook.h"
#include "runtime.h"

typedef ABI const char *(*Gow3TextLookup)(void *table, uint32_t sid);

/* The rows to add (NULL when GOW3_PC_MENU=0). */
const Gow3PcRow *runtime_menu_pc_rows(unsigned *count);
/* The text of one of the page's ids (button, title, row names), or NULL; lookup is the game's
 * own, for its language. */
const char *runtime_menu_pc_text(Gow3TextLookup lookup, void *table, uint32_t sid);
/* A button press: 1 when it was one of the page's rows (MORE PC SETTINGS opens the overlay). */
int runtime_menu_pc_button(uint32_t hash);
/* The binding factory (menu_pc_hook.h), and the helper its stub calls for our option sids. */
void runtime_menu_pc_set_factory(void *original);
ABI void *runtime_menu_pc_binding(void *menu, uint32_t kind, uint32_t a, uint32_t sid, uint32_t b, uint32_t c,
                                  uint32_t d);
/* "New save" in the language of the last text table seen. */
const char *runtime_menu_pc_new_save_text(void);
/* A text of the game in the language of the last text table seen (NULL before one or if none). */
const char *runtime_menu_pc_game_text(uint32_t sid);
/* From the pad's sampling: values changed in the page are applied and saved. */
void runtime_menu_pc_tick(void);
#endif
