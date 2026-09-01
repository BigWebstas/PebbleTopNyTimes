#include <pebble.h>

#define MAX_ARTICLES 15
#define TITLE_LEN 96
#define ABSTRACT_LEN 320
#define SECTION_LEN 24
#define BYLINE_LEN 56

typedef struct {
  char title[TITLE_LEN];
  char abstract[ABSTRACT_LEN];
  char section[SECTION_LEN];
  char byline[BYLINE_LEN];
} Article;

static Article s_articles[MAX_ARTICLES];
static int s_article_count = 0;
static bool s_loading = true;
static char s_status[64] = "Loading top stories…";

// Backlight override: 0 (system default) until settings arrive, so an
// unconfigured watch never touches the backlight API. Negative
// (BACKLIGHT_MODE_ALWAYS_ON) forces it on while the app is open; positive is
// a relight-and-hold duration in seconds. Same scheme as PebbleSuperProductivity.
#define BACKLIGHT_MODE_ALWAYS_ON -1
static int32_t s_backlight_mode = 0;
static AppTimer *s_backlight_timer = NULL;

// Layout constants. Every row is one fixed height; the title is always a
// single line. A title too wide for the row marquee-scrolls while its row is
// selected. Emery (Pebble Time 2) renders third-party apps at the "Large"
// content size, so its fonts and heights step up one notch. Same marquee +
// emery approach as PebbleSuperProductivity.
#if defined(PBL_PLATFORM_EMERY)
#define TITLE_FONT_KEY FONT_KEY_GOTHIC_24_BOLD
#define SECTION_FONT_KEY FONT_KEY_GOTHIC_18
#define BANNER_FONT_KEY FONT_KEY_GOTHIC_18_BOLD
#define BANNER_HEIGHT 26
#define ROW_H_PAD 10
#define ROW_HEIGHT 62
#define ROW_SECTION_HEIGHT 20
#define ROW_TITLE_LINE_H 30
#define DETAIL_FONT_KEY FONT_KEY_GOTHIC_28
#define SCROLL_INTERVAL_MS 100
#define SCROLL_STEP_PX 2
#else
#define TITLE_FONT_KEY FONT_KEY_GOTHIC_18_BOLD
#define SECTION_FONT_KEY FONT_KEY_GOTHIC_14
#define BANNER_FONT_KEY FONT_KEY_GOTHIC_14
#define BANNER_HEIGHT 18
#define ROW_H_PAD 6
#define ROW_HEIGHT 44
#define ROW_SECTION_HEIGHT 16
#define ROW_TITLE_LINE_H 22
#define DETAIL_FONT_KEY FONT_KEY_GOTHIC_24
#define SCROLL_INTERVAL_MS 300
#define SCROLL_STEP_PX 6
#endif
#define SCROLL_GAP_PX 24

static Window *s_list_window;
static TextLayer *s_banner_layer;
static MenuLayer *s_menu_layer;
static TextLayer *s_status_layer;

// Marquee state for the selected row's title.
static AppTimer *s_scroll_timer = NULL;
static int s_scroll_offset_px = 0;

static Window *s_detail_window;
static ScrollLayer *s_detail_scroll;
static TextLayer *s_detail_text;
static char s_detail_buf[TITLE_LEN + ABSTRACT_LEN + SECTION_LEN + BYLINE_LEN + 16];

static void refresh_menu(void);

// ---------- Backlight ----------

static void backlight_timer_callback(void *data) {
  s_backlight_timer = NULL;
  // Hands control back to automatic backlight behavior, not "force off".
  light_enable(false);
}

// Called on every user interaction (select, or the menu selection moving) -
// NOT on a settings change (that's apply_backlight_mode()). Mode 0 is a
// no-op: the app never touches the backlight API unless a mode is set.
static void backlight_touch(void) {
  if (s_backlight_timer) {
    app_timer_cancel(s_backlight_timer);
    s_backlight_timer = NULL;
  }
  if (s_backlight_mode == 0) {
    return;
  }
  light_enable(true);
  if (s_backlight_mode == BACKLIGHT_MODE_ALWAYS_ON) {
    return; // Stays on until the mode itself changes - see apply_backlight_mode().
  }
  s_backlight_timer = app_timer_register(s_backlight_mode * 1000, backlight_timer_callback, NULL);
}

// Reacts to s_backlight_mode changing (a settings save), not to user
// interaction. Leaving always-on needs an explicit light_enable(false) here -
// backlight_touch() only turns it on, and the timeout timer never runs in
// always-on mode.
static void apply_backlight_mode(void) {
  if (s_backlight_mode == 0) {
    if (s_backlight_timer) {
      app_timer_cancel(s_backlight_timer);
      s_backlight_timer = NULL;
    }
    light_enable(false);
    return;
  }
  backlight_touch();
}

// ---------- Detail window ----------

static void detail_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_detail_scroll = scroll_layer_create(bounds);
  scroll_layer_set_click_config_onto_window(s_detail_scroll, window);

  s_detail_text = text_layer_create(GRect(4, 0, bounds.size.w - 8, 2000));
  text_layer_set_font(s_detail_text, fonts_get_system_font(DETAIL_FONT_KEY));
  text_layer_set_text(s_detail_text, s_detail_buf);
  text_layer_set_overflow_mode(s_detail_text, GTextOverflowModeWordWrap);

  GSize used = text_layer_get_content_size(s_detail_text);
  text_layer_set_size(s_detail_text, GSize(bounds.size.w - 8, used.h + 12));
  scroll_layer_set_content_size(s_detail_scroll, GSize(bounds.size.w, used.h + 16));

  scroll_layer_add_child(s_detail_scroll, text_layer_get_layer(s_detail_text));
  layer_add_child(root, scroll_layer_get_layer(s_detail_scroll));
}

static void detail_window_unload(Window *window) {
  text_layer_destroy(s_detail_text);
  scroll_layer_destroy(s_detail_scroll);
  window_destroy(s_detail_window);
  s_detail_window = NULL;
}

static void show_detail(int index) {
  if (index < 0 || index >= s_article_count) {
    return;
  }
  Article *a = &s_articles[index];
  snprintf(s_detail_buf, sizeof(s_detail_buf), "%s\n\n%s\n\n%s",
           a->title,
           a->abstract[0] ? a->abstract : "(no summary)",
           a->byline[0] ? a->byline : "");

  s_detail_window = window_create();
  window_set_window_handlers(s_detail_window, (WindowHandlers) {
    .load = detail_window_load,
    .unload = detail_window_unload,
  });
  window_stack_push(s_detail_window, true);
}

// ---------- List window ----------

static bool status_visible(void) {
  return s_loading || s_article_count == 0;
}

static uint16_t menu_get_num_rows(MenuLayer *menu, uint16_t section_index, void *context) {
  return s_article_count;
}

// Width one unwrapped line of the title needs. Drives the "does it need to
// marquee-scroll" decision and the marquee geometry.
static int16_t title_natural_width(const char *title) {
  GSize size = graphics_text_layout_get_content_size(
      title, fonts_get_system_font(TITLE_FONT_KEY), GRect(0, 0, 2000, 100),
      GTextOverflowModeFill, GTextAlignmentLeft);
  return size.w;
}

#if defined(PBL_ROUND)
// Integer square root (Newton's method) - avoids pulling in libm just for this.
static int32_t isqrt(int32_t x) {
  if (x <= 0) {
    return 0;
  }
  int32_t r = x, next = (r + 1) / 2;
  while (next < r) {
    r = next;
    next = (r + x / r) / 2;
  }
  return r;
}

// draw_row/get_cell_height draw with graphics_draw_text at a flat x-offset,
// which (unlike menu_cell_basic_draw) doesn't know about the round bezel -
// text near the top/bottom of a round screen can start outside the visible
// circle and get clipped by the display mask. Given a row's on-screen y-span,
// compute how far the circle's chord requires text to start; never below the
// normal padding, and capped so a row caught at the very pole still keeps a
// usable width instead of collapsing to ~0.
static int16_t round_safe_pad_for_range(int16_t y_top, int16_t y_bottom, int16_t cell_width) {
  int16_t cx = PBL_DISPLAY_WIDTH / 2;
  int16_t cy = PBL_DISPLAY_HEIGHT / 2;
  int16_t r = PBL_DISPLAY_WIDTH / 2;
  int16_t dy_top = abs(y_top - cy);
  int16_t dy_bottom = abs(y_bottom - cy);
  int16_t dy = dy_top > dy_bottom ? dy_top : dy_bottom; // farther from center = narrower chord
  if (dy > r) {
    dy = r;
  }
  int32_t half_w = isqrt((int32_t)r * r - (int32_t)dy * dy);
  int16_t pad = cx - (int16_t)half_w;
  int16_t cap = cell_width * 2 / 5; // never eat more than 40% of the row on each side
  if (pad > cap) {
    pad = cap;
  }
  return pad > ROW_H_PAD ? pad : ROW_H_PAD;
}

// For draw_row: the row's real, already-laid-out on-screen position.
static int16_t round_safe_pad(const Layer *cell_layer) {
  GRect bounds = layer_get_bounds(cell_layer);
  GRect on_screen = layer_convert_rect_to_screen(cell_layer, bounds);
  return round_safe_pad_for_range(on_screen.origin.y, on_screen.origin.y + on_screen.size.h,
                                  bounds.size.w);
}

#else
static int16_t round_safe_pad(const Layer *cell_layer) {
  return ROW_H_PAD;
}
#endif

// ---------- Marquee ----------

static void stop_scroll_timer(void) {
  if (s_scroll_timer) {
    app_timer_cancel(s_scroll_timer);
    s_scroll_timer = NULL;
  }
}

static void scroll_timer_callback(void *data) {
  s_scroll_offset_px += SCROLL_STEP_PX;
  layer_mark_dirty(menu_layer_get_layer(s_menu_layer));
  s_scroll_timer = app_timer_register(SCROLL_INTERVAL_MS, scroll_timer_callback, NULL);
}

// Start or stop the marquee to match whether the selected title overflows its
// row. Called on selection change and when the list data changes.
static void refresh_scroll_state(bool reset_offset) {
  if (reset_offset) {
    s_scroll_offset_px = 0;
  }
  bool needs_scroll = false;
  if (s_menu_layer && !status_visible()) {
    MenuIndex sel = menu_layer_get_selected_index(s_menu_layer);
    if (sel.row < s_article_count) {
      int16_t available = layer_get_bounds(menu_layer_get_layer(s_menu_layer)).size.w
                          - 2 * ROW_H_PAD;
      needs_scroll = title_natural_width(s_articles[sel.row].title) > available;
    }
  }
  if (needs_scroll && !s_scroll_timer) {
    s_scroll_timer = app_timer_register(SCROLL_INTERVAL_MS, scroll_timer_callback, NULL);
  } else if (!needs_scroll) {
    stop_scroll_timer();
  }
}

// ---------- Menu rows ----------

static void draw_row_title(GContext *ctx, const Article *a, int16_t x, int16_t y,
                           int16_t available, bool focused) {
  GFont font = fonts_get_system_font(TITLE_FONT_KEY);
  GRect box = GRect(x, y, available, ROW_TITLE_LINE_H + 4);
  int16_t natural = title_natural_width(a->title);

  if (focused && natural > available) {
    int16_t period = natural + SCROLL_GAP_PX;
    int16_t shift = -(s_scroll_offset_px % period);
    graphics_draw_text(ctx, a->title, font, GRect(x + shift, y, natural, box.size.h),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, a->title, font, GRect(x + shift + period, y, natural, box.size.h),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  } else {
    graphics_draw_text(ctx, a->title, font, box,
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

static void menu_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *context) {
  Article *a = &s_articles[cell_index->row];
  GRect bounds = layer_get_bounds(cell_layer);
  int16_t pad = round_safe_pad(cell_layer);
  int16_t available = bounds.size.w - 2 * pad;
  bool focused = menu_cell_layer_is_highlighted(cell_layer);

  graphics_context_set_text_color(ctx, focused ? GColorWhite : GColorBlack);

  int16_t section_h = a->section[0] ? ROW_SECTION_HEIGHT : 0;
  int16_t block_h = ROW_TITLE_LINE_H + section_h;
  int16_t y = (bounds.size.h - block_h) / 2;
  if (y < 2) {
    y = 2;
  }

  draw_row_title(ctx, a, pad, y, available, focused);

  if (section_h) {
    graphics_draw_text(ctx, a->section, fonts_get_system_font(SECTION_FONT_KEY),
                       GRect(pad, y + ROW_TITLE_LINE_H, available, section_h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

static int16_t menu_get_cell_height(MenuLayer *menu, MenuIndex *cell_index, void *context) {
  return ROW_HEIGHT;
}

static void menu_select(MenuLayer *menu, MenuIndex *cell_index, void *context) {
  backlight_touch();
  show_detail(cell_index->row);
}

static void menu_selection_changed(MenuLayer *menu, MenuIndex new_index, MenuIndex old_index,
                                   void *context) {
  refresh_scroll_state(true);
  backlight_touch();
}

static void list_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  // Fixed banner: unlike a MenuLayer section header, this never scrolls.
  s_banner_layer = text_layer_create(GRect(0, 0, bounds.size.w, BANNER_HEIGHT));
  text_layer_set_background_color(s_banner_layer, GColorLightGray);
  text_layer_set_text_color(s_banner_layer, GColorBlack);
  text_layer_set_font(s_banner_layer, fonts_get_system_font(BANNER_FONT_KEY));
  text_layer_set_text_alignment(s_banner_layer, GTextAlignmentCenter);
  text_layer_set_text(s_banner_layer, "NYT TOP STORIES");
  layer_add_child(root, text_layer_get_layer(s_banner_layer));

  GRect list_bounds = GRect(0, BANNER_HEIGHT, bounds.size.w, bounds.size.h - BANNER_HEIGHT);

  s_menu_layer = menu_layer_create(list_bounds);
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_rows = menu_get_num_rows,
    .draw_row = menu_draw_row,
    .get_cell_height = menu_get_cell_height,
    .select_click = menu_select,
    .selection_changed = menu_selection_changed,
  });
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
  // No center_focused: it pads the viewport to vertically center the
  // selection, which left a blank gap above row 0. A plain top-aligned list
  // (PebbleSuperProductivity's approach) has no such gap.
  layer_add_child(root, menu_layer_get_layer(s_menu_layer));

  s_status_layer = text_layer_create(GRect(6, BANNER_HEIGHT + list_bounds.size.h / 3,
                                           bounds.size.w - 12, bounds.size.h));
  text_layer_set_font(s_status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_status_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_status_layer, GTextOverflowModeWordWrap);
  text_layer_set_background_color(s_status_layer, GColorClear);
  text_layer_set_text(s_status_layer, s_status);
  layer_add_child(root, text_layer_get_layer(s_status_layer));

  refresh_menu();
}

static void list_window_unload(Window *window) {
  stop_scroll_timer();
  text_layer_destroy(s_status_layer);
  s_status_layer = NULL;
  menu_layer_destroy(s_menu_layer);
  text_layer_destroy(s_banner_layer);
  s_banner_layer = NULL;
}

static void refresh_menu(void) {
  if (s_menu_layer) {
    menu_layer_reload_data(s_menu_layer);
  }
  if (s_status_layer) {
    text_layer_set_text(s_status_layer, s_status);
    layer_set_hidden(text_layer_get_layer(s_status_layer), !status_visible());
    layer_set_hidden(menu_layer_get_layer(s_menu_layer), status_visible());
  }
  refresh_scroll_state(true);
}

// ---------- AppMessage ----------

static void copy_tuple(Tuple *t, char *dest, size_t len) {
  if (t) {
    strncpy(dest, t->value->cstring, len - 1);
    dest[len - 1] = '\0';
  }
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  // Sent as its own standalone message, independent of the article-fetch
  // protocol below, so it can arrive (and apply) even if the fetch fails.
  // Only re-applied when the value actually changed, matching PSP - avoids
  // re-triggering a fresh timeout on every incidental message.
  Tuple *backlight = dict_find(iter, MESSAGE_KEY_BacklightMode);
  if (backlight && backlight->value->int32 != s_backlight_mode) {
    s_backlight_mode = backlight->value->int32;
    apply_backlight_mode();
  }

  Tuple *err = dict_find(iter, MESSAGE_KEY_AppKeyError);
  if (err) {
    s_loading = false;
    s_article_count = 0;
    strncpy(s_status, err->value->cstring, sizeof(s_status) - 1);
    s_status[sizeof(s_status) - 1] = '\0';
    refresh_menu();
    return;
  }

  Tuple *count = dict_find(iter, MESSAGE_KEY_AppKeyCount);
  if (count) {
    s_article_count = 0;
    s_loading = (count->value->int32 > 0);
    if (count->value->int32 == 0) {
      strncpy(s_status, "No stories found", sizeof(s_status) - 1);
    }
    refresh_menu();
    return;
  }

  Tuple *index = dict_find(iter, MESSAGE_KEY_AppKeyIndex);
  if (index) {
    int i = index->value->int32;
    if (i >= 0 && i < MAX_ARTICLES) {
      copy_tuple(dict_find(iter, MESSAGE_KEY_AppKeyTitle), s_articles[i].title, TITLE_LEN);
      copy_tuple(dict_find(iter, MESSAGE_KEY_AppKeyAbstract), s_articles[i].abstract, ABSTRACT_LEN);
      copy_tuple(dict_find(iter, MESSAGE_KEY_AppKeySection), s_articles[i].section, SECTION_LEN);
      copy_tuple(dict_find(iter, MESSAGE_KEY_AppKeyByline), s_articles[i].byline, BYLINE_LEN);
      if (i + 1 > s_article_count) {
        s_article_count = i + 1;
      }
      s_loading = false;
      refresh_menu();
    }
  }
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "inbox dropped: %d", reason);
}

static void request_fetch(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, MESSAGE_KEY_AppKeyRequest, 1);
    app_message_outbox_send();
  }
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "outbox failed: %d", reason);
}

// ---------- App lifecycle ----------

static void init(void) {
  s_list_window = window_create();
  window_set_window_handlers(s_list_window, (WindowHandlers) {
    .load = list_window_load,
    .unload = list_window_unload,
  });
  window_stack_push(s_list_window, true);

#ifdef _PBL_API_EXISTS_app_touch_navigation_enable
  // Opt into the system touch-navigation bridge on touchscreen hardware (Pebble
  // Time 2 and newer). It maps taps/swipes onto the same select/up/down/back
  // handling the app already has, so no separate touch code path is needed.
  app_touch_navigation_enable(true);
#endif

  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  app_message_open(1024, 128);

  request_fetch();
}

static void deinit(void) {
  // Relinquish the backlight to automatic control before exiting - harmless
  // if the app never touched it.
  if (s_backlight_timer) {
    app_timer_cancel(s_backlight_timer);
  }
  stop_scroll_timer();
  light_enable(false);
  window_destroy(s_list_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
