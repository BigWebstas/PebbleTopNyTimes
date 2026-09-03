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
static int s_expected_count = -1; // -1 until AppKeyCount arrives
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
#define DETAIL_TITLE_FONT_KEY FONT_KEY_GOTHIC_28_BOLD
#define DETAIL_BODY_FONT_KEY FONT_KEY_GOTHIC_24
#define DETAIL_BYLINE_FONT_KEY FONT_KEY_GOTHIC_18
#define DETAIL_GAP 10
#else
#define TITLE_FONT_KEY FONT_KEY_GOTHIC_18_BOLD
#define SECTION_FONT_KEY FONT_KEY_GOTHIC_14
#define BANNER_FONT_KEY FONT_KEY_GOTHIC_14
#define BANNER_HEIGHT 18
#define ROW_H_PAD 6
#define ROW_HEIGHT 44
#define ROW_SECTION_HEIGHT 16
#define ROW_TITLE_LINE_H 22
#define DETAIL_TITLE_FONT_KEY FONT_KEY_GOTHIC_24_BOLD
#define DETAIL_BODY_FONT_KEY FONT_KEY_GOTHIC_18
#define DETAIL_BYLINE_FONT_KEY FONT_KEY_GOTHIC_14
#define DETAIL_GAP 6
#endif
#define SCROLL_GAP_PX 24

// Marquee cadence. One pixel every tick is a smooth glide instead of a coarse
// jump; the start pause lets you read the beginning of a title before it moves,
// and the wrap pause gives a beat before it repeats.
#define SCROLL_INTERVAL_MS 50
#define SCROLL_STEP_PX 1
#define SCROLL_START_PAUSE_MS 1500
#define SCROLL_END_PAUSE_MS 700

// How long to keep the loading screen up waiting for the rest of the articles
// after the last one arrived, before revealing a partial list. Re-armed on
// every incoming article, so a slow-but-progressing stream is never cut off.
#define REVEAL_TIMEOUT_MS 12000

static Window *s_list_window;
static TextLayer *s_banner_layer;
static MenuLayer *s_menu_layer;
static TextLayer *s_status_layer;

// Marquee state for the selected row's title.
static AppTimer *s_scroll_timer = NULL;
static int s_scroll_offset_px = 0;
static int s_scroll_period_px = 0; // selected title width + gap; 0 when not scrolling

// Holds the loading screen up until every article has arrived - see
// reveal_timer_callback and inbox_received.
static AppTimer *s_reveal_timer = NULL;

static Window *s_detail_window;
static ScrollLayer *s_detail_scroll;
static TextLayer *s_detail_title_layer;
static TextLayer *s_detail_body_layer;
static TextLayer *s_detail_byline_layer;
static char s_detail_title_buf[TITLE_LEN];
static char s_detail_body_buf[ABSTRACT_LEN];
static char s_detail_byline_buf[BYLINE_LEN];

static void refresh_menu(void);
static void cancel_reveal_timer(void);

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

// Build one word-wrapped text layer, size it to its content, and return the y
// for whatever comes next. Each block is its own layer so the title can be bold
// and larger and the byline small and dimmed - a single TextLayer can't mix
// fonts.
static TextLayer *add_detail_block(int16_t x, int16_t *y, int16_t w, const char *text,
                                   const char *font_key, GColor color) {
  TextLayer *tl = text_layer_create(GRect(x, *y, w, 2000));
  text_layer_set_font(tl, fonts_get_system_font(font_key));
  text_layer_set_overflow_mode(tl, GTextOverflowModeWordWrap);
  text_layer_set_background_color(tl, GColorClear);
  text_layer_set_text_color(tl, color);
  text_layer_set_text(tl, text);
  GSize used = text_layer_get_content_size(tl);
  text_layer_set_size(tl, GSize(w, used.h));
  *y += used.h;
  scroll_layer_add_child(s_detail_scroll, text_layer_get_layer(tl));
  return tl;
}

static void detail_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_detail_byline_layer = NULL;

  s_detail_scroll = scroll_layer_create(bounds);
  scroll_layer_set_click_config_onto_window(s_detail_scroll, window);
  layer_add_child(root, scroll_layer_get_layer(s_detail_scroll));

  int16_t pad = DETAIL_GAP;
  int16_t w = bounds.size.w - 2 * pad;
  int16_t y = pad;

  s_detail_title_layer = add_detail_block(pad, &y, w, s_detail_title_buf,
                                          DETAIL_TITLE_FONT_KEY, GColorBlack);
  y += DETAIL_GAP;
  s_detail_body_layer = add_detail_block(pad, &y, w, s_detail_body_buf,
                                         DETAIL_BODY_FONT_KEY, GColorBlack);
  if (s_detail_byline_buf[0]) {
    y += DETAIL_GAP;
    s_detail_byline_layer = add_detail_block(pad, &y, w, s_detail_byline_buf,
                                             DETAIL_BYLINE_FONT_KEY,
                                             PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
  }

  scroll_layer_set_content_size(s_detail_scroll, GSize(bounds.size.w, y + pad));
}

static void detail_window_unload(Window *window) {
  text_layer_destroy(s_detail_title_layer);
  text_layer_destroy(s_detail_body_layer);
  if (s_detail_byline_layer) {
    text_layer_destroy(s_detail_byline_layer);
    s_detail_byline_layer = NULL;
  }
  scroll_layer_destroy(s_detail_scroll);
  window_destroy(s_detail_window);
  s_detail_window = NULL;
}

static void show_detail(int index) {
  if (index < 0 || index >= s_article_count) {
    return;
  }
  Article *a = &s_articles[index];
  strncpy(s_detail_title_buf, a->title, sizeof(s_detail_title_buf) - 1);
  s_detail_title_buf[sizeof(s_detail_title_buf) - 1] = '\0';
  strncpy(s_detail_body_buf, a->abstract[0] ? a->abstract : "(no summary)",
          sizeof(s_detail_body_buf) - 1);
  s_detail_body_buf[sizeof(s_detail_body_buf) - 1] = '\0';
  strncpy(s_detail_byline_buf, a->byline, sizeof(s_detail_byline_buf) - 1);
  s_detail_byline_buf[sizeof(s_detail_byline_buf) - 1] = '\0';

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
  s_scroll_timer = NULL;
  s_scroll_offset_px += SCROLL_STEP_PX;
  uint32_t next_ms = SCROLL_INTERVAL_MS;
  if (s_scroll_period_px > 0 && s_scroll_offset_px >= s_scroll_period_px) {
    s_scroll_offset_px -= s_scroll_period_px; // wrapped a full loop - hold a beat
    next_ms = SCROLL_END_PAUSE_MS;
  }
  layer_mark_dirty(menu_layer_get_layer(s_menu_layer));
  s_scroll_timer = app_timer_register(next_ms, scroll_timer_callback, NULL);
}

// Start or stop the marquee to match whether the selected title overflows its
// row. Called on selection change and when the list data changes.
static void refresh_scroll_state(bool reset_offset) {
  if (reset_offset) {
    s_scroll_offset_px = 0;
  }
  bool needs_scroll = false;
  s_scroll_period_px = 0;
  if (s_menu_layer && !status_visible()) {
    MenuIndex sel = menu_layer_get_selected_index(s_menu_layer);
    if (sel.row < s_article_count) {
      int16_t available = layer_get_bounds(menu_layer_get_layer(s_menu_layer)).size.w
                          - 2 * ROW_H_PAD;
      int16_t natural = title_natural_width(s_articles[sel.row].title);
      needs_scroll = natural > available;
      if (needs_scroll) {
        s_scroll_period_px = natural + SCROLL_GAP_PX;
      }
    }
  }
  if (needs_scroll && !s_scroll_timer) {
    // Start pause: read the beginning of the title before it moves.
    s_scroll_timer = app_timer_register(SCROLL_START_PAUSE_MS, scroll_timer_callback, NULL);
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

  int16_t section_h = a->section[0] ? ROW_SECTION_HEIGHT : 0;
  int16_t block_h = ROW_TITLE_LINE_H + section_h;
  int16_t y = (bounds.size.h - block_h) / 2;
  if (y < 2) {
    y = 2;
  }

  graphics_context_set_text_color(ctx, focused ? GColorWhite : GColorBlack);
  draw_row_title(ctx, a, pad, y, available, focused);

  if (section_h) {
    // Dimmed against the title so the row reads title-first; on B&W the
    // dimmed color collapses back to the focus color, same as before.
    graphics_context_set_text_color(
        ctx, focused ? GColorWhite : PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
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
  text_layer_set_background_color(s_banner_layer, GColorBlack);
  text_layer_set_text_color(s_banner_layer, GColorWhite);
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

  // Centered in the list area, height bounded so it never runs off screen.
  int16_t status_h = 96;
  int16_t status_y = BANNER_HEIGHT + (list_bounds.size.h - status_h) / 2;
  if (status_y < BANNER_HEIGHT) {
    status_y = BANNER_HEIGHT;
  }
  s_status_layer = text_layer_create(GRect(6, status_y, bounds.size.w - 12, status_h));
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
  cancel_reveal_timer();
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

// ---------- Loading reveal ----------

static void cancel_reveal_timer(void) {
  if (s_reveal_timer) {
    app_timer_cancel(s_reveal_timer);
    s_reveal_timer = NULL;
  }
}

// Fires only if the article stream stalls partway. Shows whatever arrived
// rather than leaving the loading screen up forever.
static void reveal_timer_callback(void *data) {
  s_reveal_timer = NULL;
  if (s_loading && s_article_count > 0) {
    s_loading = false;
    refresh_menu();
  }
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
    cancel_reveal_timer();
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
    s_expected_count = count->value->int32;
    // Stay on the loading screen until every row is in - the list then
    // appears fully populated in one paint instead of growing row by row.
    s_loading = (s_expected_count > 0);
    if (s_expected_count == 0) {
      strncpy(s_status, "No stories found", sizeof(s_status) - 1);
    } else {
      strncpy(s_status, "Loading top stories…", sizeof(s_status) - 1);
      s_status[sizeof(s_status) - 1] = '\0';
      cancel_reveal_timer();
      s_reveal_timer = app_timer_register(REVEAL_TIMEOUT_MS, reveal_timer_callback, NULL);
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
      if (s_expected_count >= 0 && s_article_count >= s_expected_count) {
        cancel_reveal_timer();
        s_loading = false;
        refresh_menu();
      } else if (s_loading) {
        // Still waiting on more rows: keep the loading screen, but push the
        // stall timeout back since the stream is making progress.
        cancel_reveal_timer();
        s_reveal_timer = app_timer_register(REVEAL_TIMEOUT_MS, reveal_timer_callback, NULL);
      } else {
        // Loading screen already dismissed (stall fallback fired) - just
        // fold the late row into the visible list.
        refresh_menu();
      }
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
  cancel_reveal_timer();
  light_enable(false);
  window_destroy(s_list_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
