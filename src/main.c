#include <pebble.h>

/*
 * TODO persist current localtime/TS offsets locally - so that the watch can start if not connected to the phone
 * TODO Sort timezones based on offset from localtime.
 * TODO Add current time if not one of the specified timezones
 * TODO Add current date to current time display
 * TODO Add TZ name to display (for non-local time displays)
 * TODO Support <4 timezones set
 * TODO Config page, initialise to current settings
 * TODO Add battery indicator
 * TODO Add bluetooth indicator
 * TODO Add indicator if send_tz_request has not replied... may indicate remote TZ configuration is not up to date.
 * TODO Pretty up display
 * TODO Reduce size of JS, and include more interesting TZs
 */
  
// Keys for timezone names
#define KEY_TZ1 1
#define KEY_TZ2 2
#define KEY_TZ3 3
#define KEY_TZ4 4

// Keys for timezone offsets
#define KEY_OFFSET1 11
#define KEY_OFFSET2 12
#define KEY_OFFSET3 13
#define KEY_OFFSET4 14

// Timezone string size (max)
#define TZ_SIZE (100)
  
static Window *s_main_window;
static TextLayer *s_time_layer;

// Offsets from local time, in minutes.
static int s_num_times = 4;
static int32_t s_offsets[4];
static char s_tz[4][TZ_SIZE];
static time_t s_last_tick = 0;

static void update_time();
static void send_tz_request();

static void inbox_received_callback(DictionaryIterator *received, void *context) {
  Tuple *o1_tuple = dict_find(received, KEY_OFFSET1);
  Tuple *o2_tuple = dict_find(received, KEY_OFFSET2);
  Tuple *o3_tuple = dict_find(received, KEY_OFFSET3);
  Tuple *o4_tuple = dict_find(received, KEY_OFFSET4);

  if (o1_tuple) {
    s_offsets[0] = o1_tuple->value->int32;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Offset 1: %ld", s_offsets[0]);
  }
  
  if (o2_tuple) {
    s_offsets[1] = o2_tuple->value->int32;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Offset 2: %ld", s_offsets[1]);
  }
  
  if (o3_tuple) {
    s_offsets[2] = o3_tuple->value->int32;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Offset 3: %ld", s_offsets[2]);
  }
  
  if (o4_tuple) {
    s_offsets[3] = o4_tuple->value->int32;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Offset 4: %ld", s_offsets[3]);
  }
  
  Tuple *tz1_tuple = dict_find(received, KEY_TZ1);
  Tuple *tz2_tuple = dict_find(received, KEY_TZ2);
  Tuple *tz3_tuple = dict_find(received, KEY_TZ3);
  Tuple *tz4_tuple = dict_find(received, KEY_TZ4);
  bool tz_set = 0;
  
  if (tz1_tuple) {
    strncpy(s_tz[0], tz1_tuple->value->cstring, TZ_SIZE);
    int w = persist_write_string(KEY_TZ1, s_tz[0]);
    APP_LOG(APP_LOG_LEVEL_INFO, "Configuration: TZ 1: %s (%d)", s_tz[0], w);
    tz_set = 1;
  }
  
  if (tz2_tuple) {
    strncpy(s_tz[1], tz2_tuple->value->cstring, TZ_SIZE);
    persist_write_string(KEY_TZ2, s_tz[1]);
    APP_LOG(APP_LOG_LEVEL_INFO, "Configuration: TZ 2: %s", s_tz[1]);
    tz_set = 1;
  }
  
  if (tz3_tuple) {
    strncpy(s_tz[2], tz3_tuple->value->cstring, TZ_SIZE);
    persist_write_string(KEY_TZ3, s_tz[2]);
    APP_LOG(APP_LOG_LEVEL_INFO, "Configuration: TZ 3: %s", s_tz[2]);
    tz_set = 1;
  }
   
  if (tz4_tuple) {
    strncpy(s_tz[3], tz4_tuple->value->cstring, TZ_SIZE);
    persist_write_string(KEY_TZ4, s_tz[3]);
    APP_LOG(APP_LOG_LEVEL_INFO, "Configuration: TZ 4: %s", s_tz[3]);
    tz_set = 1;
  }
  
  if (tz_set) {
    send_tz_request();
  }
  
  update_time();
}

static void add_line(char *buffer, char *additional_line) {
  strcat(buffer, "\n");
  strcat(buffer, additional_line);
}

static void update_time() {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "UpdateTime...");
  // Create a long-lived buffer
  static char buffer[128];
  char tb[20];
  char tt[20];
  
  // Reset the buffer for each use
  buffer[0] = 0;

  // Get a tm structure
  time_t now;
  time(&now);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Localtime time: %ld", now);
  
  int32_t difference = now - s_last_tick;
  if (difference > 360 || difference < -360) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Difference is more than 6 minutes, requesting TZ information again...");
    send_tz_request();
  }
  s_last_tick = now;

  for (int i = 0; i < s_num_times; i++) {
    time_t temp = now;
    
    // Apply TZ offset
    temp += s_offsets[i] * 60;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Offset %d time: %ld", i, temp);

    struct tm *tick_time = localtime(&temp);
    
    // Write the current hours and minutes into the buffer
    if (clock_is_24h_style() == true) {
      // Use 24 hour format
      strftime(tt, sizeof("00:00"), "%H:%M", tick_time);
    } else {
      // Use 12 hour format
      strftime(tt, sizeof("00:00"), "%I:%M", tick_time);
    }
    
    tb[0] = 0;
    strcat(tb, tt);
    if (0 == s_offsets[i]) {
      strcat(tb, " *");
    }
    add_line(buffer, tb);
  }
 
  // Display this time on the TextLayer
  text_layer_set_text(s_time_layer, buffer);
}

static void main_window_load(Window *window) {
  // Create time TextLayer
  s_time_layer = text_layer_create(GRect(5, 5, 139, 139));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorBlack);
  
  // Make sure the time is displayed from the start
  update_time();
  
  // Improve the layout to be more like a watchface
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_ROBOTO_CONDENSED_21));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);

  // Add it as a child layer to the Window's root layer
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(s_time_layer));
}

static void main_window_unload(Window *window) {
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
}

static void send_tz_request() {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Requesting TZ offsets: %s, %s, %s, %s", s_tz[0], s_tz[1], s_tz[2], s_tz[3]);

  // Add a key-value pair
  dict_write_cstring(iter, KEY_TZ1, s_tz[0]);
  dict_write_cstring(iter, KEY_TZ2, s_tz[1]);
  dict_write_cstring(iter, KEY_TZ3, s_tz[2]);
  dict_write_cstring(iter, KEY_TZ4, s_tz[3]);

  // Send the message!
  app_message_outbox_send();
}

static void init() {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "GlobalTime initialising...");
  
  // Read current TZ config
  persist_read_string(KEY_TZ1, s_tz[0], 100);
  persist_read_string(KEY_TZ2, s_tz[1], 100);
  persist_read_string(KEY_TZ3, s_tz[2], 100);
  persist_read_string(KEY_TZ4, s_tz[3], 100);
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Loaded TZ configuration: %s, %s, %s, %s", s_tz[0], s_tz[1], s_tz[2], s_tz[3]);
  
  // Register a callback for the UTC offset information
  // TODO register failure callbacks too
  app_message_register_inbox_received(inbox_received_callback);
  
  // Connect to AppMessage stream
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
  
  // Send a request for TZ offsets
  send_tz_request();
  
  // Create main Window element and assign to pointer
  s_main_window = window_create();

  // Set handlers to manage the elements inside the Window
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  // Show the Window on the watch, with animated=true
  window_stack_push(s_main_window, true);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Window pushed");
  
  // Register with TickTimerService
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  // XXX tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
}

static void deinit() {
  // Destroy Window
  window_destroy(s_main_window);

  // Destroy TextLayer
  text_layer_destroy(s_time_layer);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
