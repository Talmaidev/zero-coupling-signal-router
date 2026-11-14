/* Regression test for game-settings color override persistence.
 * save writes "itemcolor"/"btncolor"; the loader must be able to read those
 * tokens back and apply both colors. */
#include "zcsr/ui_game_settings.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static zcsr_game_settings* make_settings(unsigned char* buf, size_t bytes,
                                         zcsr_game_settings_item* items,
                                         zcsr_game_settings_tab* tabs) {
    zcsr_game_settings_config cfg;
    memset(items, 0, sizeof(*items));
    items[0].label = "test";
    items[0].type = ZCSR_GAME_SETTINGS_TOGGLE;
    items[0].toggle_value = true;
    memset(tabs, 0, sizeof(*tabs));
    tabs[0].name = "tab";
    tabs[0].items = items;
    tabs[0].count = 1;
    memset(&cfg, 0, sizeof(cfg));
    cfg.tabs = tabs;
    cfg.tab_count = 1;
    cfg.x = 10;
    cfg.y = 10;
    cfg.width = 640;
    cfg.height = 480;
    cfg.item_height = 32;
    cfg.text_scale = 1.4f;
    return zcsr_game_settings_create(buf, bytes, &cfg);
}

int main(void) {
    static unsigned char buf1[64 * 1024];
    static unsigned char buf2[64 * 1024];
    zcsr_game_settings_item items1[1], items2[1];
    zcsr_game_settings_tab tabs1[1], tabs2[1];
    zcsr_game_settings* s1 = make_settings(buf1, sizeof buf1, items1, tabs1);
    zcsr_game_settings* s2 = make_settings(buf2, sizeof buf2, items2, tabs2);
    zcsr_game_settings_color in = { 0.25f, 0.5f, 0.75f };
    zcsr_game_settings_color out = { 0.0f, 0.0f, 0.0f };
    const char* path = "game_settings_overrides_roundtrip.tmp";

    assert(s1);
    assert(s2);
    assert(zcsr_game_settings_override_item_color(s1, 0, 0, &in));
    assert(zcsr_game_settings_override_button_color(s1, 1, &in));
    assert(zcsr_game_settings_save_overrides(s1, path));

    assert(zcsr_game_settings_load_overrides(s2, path));
    assert(zcsr_game_settings_get_item_color(s2, 0, 0, &out));
    assert(out.r > 0.24f && out.r < 0.26f);
    assert(out.g > 0.49f && out.g < 0.51f);
    assert(out.b > 0.74f && out.b < 0.76f);

    memset(&out, 0, sizeof(out));
    assert(zcsr_game_settings_get_button_color(s2, 1, &out));
    assert(out.r > 0.24f && out.r < 0.26f);
    assert(out.g > 0.49f && out.g < 0.51f);
    assert(out.b > 0.74f && out.b < 0.76f);

    zcsr_game_settings_destroy(s1);
    zcsr_game_settings_destroy(s2);
    remove(path);
    printf("game_settings_overrides: PASS\n");
    return 0;
}
