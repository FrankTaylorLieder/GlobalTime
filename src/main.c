#include <pebble.h>

/*
 * DONE persist current localtime/TZ offsets locally - so that the watch can start if not connected to the phone
 * TODO BUG: persisting offset is returning status_t 4, even though it looks like it is working. A problem?
 * DONE Sort timezones based on offset from localtime.
 * DONE Add current time if not one of the specified timezones
 * TODO Add current date to current time display
 * TODO Add TZ label to display (for non-local time displays)
 * DONE Support <4 timezones set
 * TODO Config page, initialise to current settings
 * TODO Add battery indicator
 * TODO Add bluetooth indicator
 * TODO Add indicator if send_tz_request has not replied... may indicate remote TZ configuration is not up to date.
 * TODO Pretty up display
 * TODO Reduce size of JS, and include more interesting TZs
 * DONE BUG: when switching to GlobalTime from World Watch, the TZs are not correctly updated. Possibly because WW sends a message we don't interpret. Partially fixed by persisting offsets, but problem is JS is not loading fast enough.
 */
  
// Keys for timezone names
#define KEY_TZ1 6601
#define KEY_TZ2 6602
#define KEY_TZ3 6603
#define KEY_TZ4 6604

// Keys for timezone offsets
#define KEY_OFFSET1 6611
#define KEY_OFFSET2 6612
#define KEY_OFFSET3 6613
#define KEY_OFFSET4 6614

// Timezone string size (max)
#define TZ_SIZE (100)
  
// Display the local time
#define DISPLAY_LOCAL_TIME (-1)
  
// Don't display this time
#define OFFSET_NO_DISPLAY (-2000)
  
static Window *s_main_window;
static TextLayer *s_time_layer;

// Number of configured timezones
static int s_num_times = 4;

// Offsets for configured timezones, DISPLAY_NO_DISPLAY for no display.
static int32_t s_offset[4];

// Configured timezones, NULL for no display.
static char s_tz[4][TZ_SIZE];

// Previous time we displayed.
static time_t s_last_tick = 0;

// Number of displayed timezones
static int s_num_display = 0;

// Indexes into the s_tz/s_offset array,
// DISPLAY_LOCAL_TIME for the current time,
// DISPLAY_NO_DISPLAY for no display
static int s_display[5];

static void update_time();
static void send_tz_request();

// Compare and swap indexes based on the offsets they refer to.
static void compare_swap(int index[], int i) {
  if (s_offset[index[i]] < s_offset[index[i+1]]) {
    int t = index[i];
    index[i] = index[i+1];
    index[i+1] = t;
  }
}
                                    
static void sort_times() {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "sort_times...");
  // Initialise indexes to unsorted offsets.
  int indexes[4];
  for (int i = 0; i < 4; i++) {
    indexes[i] = i;
  }
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "fresh indexes: %d %d %d %d", indexes[0], indexes[1], indexes[2], indexes[3]);
  
  // Unrolled bubblesort offsets via indexes.
  compare_swap(indexes, 0);
  compare_swap(indexes, 1);
  compare_swap(indexes, 2);
  compare_swap(indexes, 0);
  compare_swap(indexes, 1);
  compare_swap(indexes, 2);
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "sorted indexes: %d %d %d %d", indexes[0], indexes[1], indexes[2], indexes[3]);

  // Iterate offsets (via indexes), inserting local time (replacing a TZ if needed).
  bool found_local = false;
  int d = 0;
  for (int i = 0; i < 4; i++) {
    int offset = s_offset[indexes[i]];
    if (OFFSET_NO_DISPLAY == offset) {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "NO DISPLAY");
      break;
    }
    
    if (0 == offset) {
      if (found_local) {
        // Already found a local, so skip this one
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Already found local");
        continue;
      }
      
      // This is the local time...
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Found local");
      s_display[d++] = DISPLAY_LOCAL_TIME;
      found_local = true;
      continue;
    }
    
    if (!found_local && offset < 0) {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Missed local, adding");
      // We have moved past local time without finding it, so add it in.
      s_display[d++] = DISPLAY_LOCAL_TIME;
      found_local = true;
      // Fall through to add the current TZ
    }
    
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Adding %d", indexes[i]);
    s_display[d++] = indexes[i];
  }
  
  if (!found_local) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Missed local altogether, adding");
    // We did not find or insert a local time in the list at all, so add it last.
    s_display[d++] = DISPLAY_LOCAL_TIME;
    found_local = true;
  }
  
  s_num_display = d;

  for (int i = 0; i < s_num_display; i++) {
    int x = s_display[i];
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Ordered list %d: %s (%ld)",
              i,
              (x == DISPLAY_LOCAL_TIME) ? "LOCAL" : s_tz[x],
              (x == DISPLAY_LOCAL_TIME) ? 0 : s_offset[x]);
  }
  APP_LOG(APP_LOG_LEVEL_DEBUG, "...sort_times");
}

static void inbox_received_callback(DictionaryIterator *received, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Received message");
  Tuple *o1_tuple = dict_find(received, KEY_OFFSET1);
  Tuple *o2_tuple = dict_find(received, KEY_OFFSET2);
  Tuple *o3_tuple = dict_find(received, KEY_OFFSET3);
  Tuple *o4_tuple = dict_find(received, KEY_OFFSET4);

  if (o1_tuple) {
    s_offset[0] = o1_tuple->value->int32;
    status_t s = persist_write_int(KEY_OFFSET1, s_offset[0]);
    if (s != S_SUCCESS) {
      APP_LOG(APP_LOG_LEVEL_WARNING, "Failed to remember TZ offset: %ld", s);
    }
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Offset 1: %ld", s_offset[0]);
  }
  
  if (o2_tuple) {
    s_offset[1] = o2_tuple->value->int32;
    status_t s = persist_write_int(KEY_OFFSET2, s_offset[1]);
    if (s != S_SUCCESS) {
      APP_LOG(APP_LOG_LEVEL_WARNING, "Failed to remember TZ offset: %ld", s);
    }
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Offset 2: %ld", s_offset[1]);
  }
  
  if (o3_tuple) {
    s_offset[2] = o3_tuple->value->int32;
    status_t s = persist_write_int(KEY_OFFSET3, s_offset[2]);
    if (s != S_SUCCESS) {
      APP_LOG(APP_LOG_LEVEL_WARNING, "Failed to remember TZ offset: %ld", s);
    }
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Offset 3: %ld", s_offset[2]);
  }
  
  if (o4_tuple) {
    s_offset[3] = o4_tuple->value->int32;
    status_t s = persist_write_int(KEY_OFFSET4, s_offset[3]);
    if (s != S_SUCCESS) {
      APP_LOG(APP_LOG_LEVEL_WARNING, "Failed to remember TZ offset: %ld", s);
    }
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Offset 4: %ld", s_offset[3]);
  }
  
  Tuple *tz1_tuple = dict_find(received, KEY_TZ1);
  Tuple *tz2_tuple = dict_find(received, KEY_TZ2);
  Tuple *tz3_tuple = dict_find(received, KEY_TZ3);
  Tuple *tz4_tuple = dict_find(received, KEY_TZ4);
  bool tz_set = false;
  
  if (tz1_tuple) {
    strncpy(s_tz[0], tz1_tuple->value->cstring, TZ_SIZE);
    int w = persist_write_string(KEY_TZ1, s_tz[0]);
    APP_LOG(APP_LOG_LEVEL_INFO, "Configuration: TZ 1: %s (%d)", s_tz[0], w);
    tz_set = true;
  }
  
  if (tz2_tuple) {
    strncpy(s_tz[1], tz2_tuple->value->cstring, TZ_SIZE);
    persist_write_string(KEY_TZ2, s_tz[1]);
    APP_LOG(APP_LOG_LEVEL_INFO, "Configuration: TZ 2: %s", s_tz[1]);
    tz_set = true;
  }
  
  if (tz3_tuple) {
    strncpy(s_tz[2], tz3_tuple->value->cstring, TZ_SIZE);
    persist_write_string(KEY_TZ3, s_tz[2]);
    APP_LOG(APP_LOG_LEVEL_INFO, "Configuration: TZ 3: %s", s_tz[2]);
    tz_set = true;
  }
   
  if (tz4_tuple) {
    strncpy(s_tz[3], tz4_tuple->value->cstring, TZ_SIZE);
    persist_write_string(KEY_TZ4, s_tz[3]);
    APP_LOG(APP_LOG_LEVEL_INFO, "Configuration: TZ 4: %s", s_tz[3]);
    tz_set = true;
  }
  
  if (tz_set) {
    send_tz_request();
  } else {
    sort_times();
  }

  update_time();
}

static void add_line(char *buffer, char *additional_line, bool first_line) {
  if (!first_line) {
    strcat(buffer, "\n");
  }
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

  for (int i = 0; i < s_num_display; i++) {
    time_t temp = now;
    
    int display = s_display[i];
    int offset = (DISPLAY_LOCAL_TIME == display) ? 0 : s_offset[display];

    // Apply TZ offset
    temp += offset * 60;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Display %d time: %ld (%d)", i, temp, offset);

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
    if (0 == offset) {
      strcat(tb, " *");
    }
    add_line(buffer, tb, i == 0);
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
  
  s_offset[0] = persist_read_int(KEY_OFFSET1);
  s_offset[1] = persist_read_int(KEY_OFFSET2);
  s_offset[2] = persist_read_int(KEY_OFFSET3);
  s_offset[3] = persist_read_int(KEY_OFFSET4);
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Loaded TZ configuration 1: %s (%ld)", s_tz[0], s_offset[0]);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Loaded TZ configuration 2: %s (%ld)", s_tz[1], s_offset[1]);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Loaded TZ configuration 3: %s (%ld)", s_tz[2], s_offset[2]);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Loaded TZ configuration 4: %s (%ld)", s_tz[3], s_offset[3]);
  
  sort_times();
  
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
