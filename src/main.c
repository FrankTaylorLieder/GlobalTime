#include <pebble.h>

/*
 * TODO Don't listen for taps if there are too few TZs
 * TODO BUG: persisting offset is returning status_t 4, even though it looks like it is working. A problem?
 * TODO BUG Elipsis for label truncation does not work in current font
 * TODO Move Pop? to RHS
 * TODO BUG: when the current TZ is the last in the list, CRASH
 * DONE Show charging symbol
 * DONE Reduce size of JS, and include more interesting TZs
 * DONE Show up to 8 TZs on a second screen when watch is shaken
 * DONE Confirm config of less than 8 TZ works with popup
 * DONE Only go into popup mode on a double shake - seems too slow, debugging messing timings?
 * DONE Add battery indicator
 * DONE Add bluetooth indicator
 * DONE Add indicator if send_tz_request has not replied... may indicate remote TZ configuration is not up to date.
 * DONE BUG: Sometimes crashes on de-init (since adding bonus TZ support), only when logging? Or since move to SDK 2.6? Crash was due to logging causing the app to take too long terminating.
 * DONE Limit label size (and TZ size)
 * DONE Pretty up display
 * DONE Select custom fonts
 * DONE BUG unloading fonts is crashing
 * DONE Allow 5 TZ to be configured, last one is optional if local time is not a configured TZ
 * DONE persist current localtime/TZ offsets locally - so that the watch can start if not connected to the phone
 * DONE Sort timezones based on offset from localtime.
 * DONE Add current time if not one of the specified timezones
 * DONE Add current date to current time display
 * DONE Add TZ label to display (for non-local time displays)
 * DONE Support <4 timezones set
 * DONE Config page, initialise to current settings
 * DONE BUG: when switching to GlobalTime from World Watch, the TZs are not correctly updated. Possibly because WW sends a message we don't interpret. Partially fixed by persisting offsets, but problem is JS is not loading fast enough.
 */
  
// Keys for timezone names
#define KEY_TZ1 6601
#define KEY_TZ2 6602
#define KEY_TZ3 6603
#define KEY_TZ4 6604
#define KEY_TZ5 6605
#define KEY_TZ6 6606
#define KEY_TZ7 6607
#define KEY_TZ8 6608

// Keys for labels
#define KEY_LABEL1 6621
#define KEY_LABEL2 6622
#define KEY_LABEL3 6623
#define KEY_LABEL4 6624
#define KEY_LABEL5 6625
#define KEY_LABEL6 6626
#define KEY_LABEL7 6627
#define KEY_LABEL8 6628

// Keys for timezone offsets
#define KEY_OFFSET1 6611
#define KEY_OFFSET2 6612
#define KEY_OFFSET3 6613
#define KEY_OFFSET4 6614
#define KEY_OFFSET5 6615
#define KEY_OFFSET6 6616
#define KEY_OFFSET7 6617
#define KEY_OFFSET8 6618
  
#define CONFIG_SIZE (8)
  
#define DISPLAY_SIZE (5)

// Timezone string size (max)
#define TZ_SIZE (100)
  
// Label string size (max)
#define LABEL_SIZE (50)
  
// Display the local time
#define DISPLAY_LOCAL_TIME (-1)
  
// Don't display this time
#define OFFSET_NO_DISPLAY (-2000)
  
// Popup window time
#define POPUP_TIMEOUT_MS (10000)

// Popup pending time
#define POPUP_PENDING_TIMEOUT_MS (3000)
  
static Window *s_main_window;
static Window *s_popup_window;
static char build_time[100];

static TextLayer *s_tz_label_layer[4];
static TextLayer *s_tz_time_layer[4];
static BitmapLayer *s_status_bt_layer = NULL;
static BitmapLayer *s_status_battery_layer = NULL;
static BitmapLayer *s_status_charge_layer = NULL;
static TextLayer *s_status_text_layer = NULL;
static TextLayer *s_local_time_layer;
static TextLayer *s_local_date_layer;

// Text storage for status layer
static char s_status_label_text[LABEL_SIZE];

// Text storage for TZ label display
static char s_tz_label_text[4][LABEL_SIZE];

// Popup data
static TextLayer *s_popup_label_layer[CONFIG_SIZE];
static TextLayer *s_popup_time_layer[CONFIG_SIZE];
static char s_popup_label_text[CONFIG_SIZE][LABEL_SIZE];

#define LAYER_TZ_LABEL_WIDTH (104)
#define LAYER_TZ_TIME_WIDTH (40)
#define LAYER_TZ_HEIGHT (21)

#define LAYER_LOCAL_WIDTH (144)
#define LAYER_LOCAL_TIME_HEIGHT (36)
#define LAYER_LOCAL_DATE_HEIGHT (32)
  
static GFont s_big_font = NULL;
static GFont s_medium_font = NULL;
static GFont s_small_font = NULL;

static GBitmap *s_bmp_bt = NULL;
static GBitmap *s_bmp_nobt = NULL;
static GBitmap *s_bmp_battery[10];
static GBitmap *s_bmp_charge = NULL;
static GBitmap *s_bmp_nocharge = NULL;

// Offsets for configured timezones, DISPLAY_NO_DISPLAY for no display.
static int32_t s_offset[CONFIG_SIZE];

// Labels for configured timezones.
static char s_label[CONFIG_SIZE][LABEL_SIZE];

// Configured timezones, NULL for no display.
static char s_tz[CONFIG_SIZE][TZ_SIZE];

// Previous time we displayed.
static time_t s_last_tick = 0;

// Number of displayed timezones
static int s_num_display = 0;

// Track whether we've checked the offsets since the last change
static bool s_offsets_up_to_date = false;

// Indexes into the s_tz/s_offset array,
// DISPLAY_LOCAL_TIME for the current time,
// DISPLAY_NO_DISPLAY for no display
static int s_display[DISPLAY_SIZE];

// Indexes into s_tz/s_offset array for popup display.
static int s_p_display[CONFIG_SIZE];

// Remember the last BT connection state.
static bool s_last_bt_connected = true;

// Popup control: 0 - no popup, 1 - popup pending, 2 - popup displayed
static int s_popup_state = false;

// Remember the popup timer handle to allow it to be cancelled
static AppTimer *s_popup_timer_handle = NULL;

static void update_time();
static void send_tz_request();
static void create_layers();
static void create_popup_layers();
static void update_status();

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
  
  // Determine of any of the offsets is local time,
  // if so then we can take all 5 TZs as one will be local time.
  int usable_tz = 4;
  for (int i = 0; i < CONFIG_SIZE; i++) {
    if (0 == s_offset[i]) {
      // Found a local time, so we can use all 5 configured TZs
      usable_tz = 5;
      break;
    }
  }
    
  // Initialise indexes to unsorted offsets.
  int indexes[CONFIG_SIZE];
  for (int i = 0; i < CONFIG_SIZE; i++) {
    indexes[i] = i;
  }
  
  // Bubblesort offsets via indexes.
  for (int i = 0; i < (usable_tz - 1); i++) {
    for (int j = 0; j < (usable_tz - 1 - i); j++) {
      compare_swap(indexes, j);
    }
  }
  
  // Iterate offsets (via indexes), inserting local time (replacing a TZ if needed).
  bool found_local = false;
  int d = 0;
  for (int i = 0; i < usable_tz; i++) {
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
  
  create_layers();

  // ----- Handle popup display ------

  // Initialise indexes to unsorted offsets.
  int pindexes[CONFIG_SIZE];
  for (int i = 0; i < CONFIG_SIZE; i++) {
    pindexes[i] = i;
  }
  
  // Bubblesort offsets via indexes.
  for (int i = 0; i < (CONFIG_SIZE - 1); i++) {
    for (int j = 0; j < (CONFIG_SIZE - 1 - i); j++) {
      compare_swap(pindexes, j);
    }
  }
  
  for (int i = 0; i < CONFIG_SIZE; i++) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Popup: %d", pindexes[i]);
    s_p_display[i] = pindexes[i];
  }
  
  create_popup_layers();
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "...sort_times");
}

static void inbox_received_callback(DictionaryIterator *received, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Received message");
  Tuple *o1_tuple = dict_find(received, KEY_OFFSET1);
  Tuple *o2_tuple = dict_find(received, KEY_OFFSET2);
  Tuple *o3_tuple = dict_find(received, KEY_OFFSET3);
  Tuple *o4_tuple = dict_find(received, KEY_OFFSET4);
  Tuple *o5_tuple = dict_find(received, KEY_OFFSET5);
  Tuple *o6_tuple = dict_find(received, KEY_OFFSET6);
  Tuple *o7_tuple = dict_find(received, KEY_OFFSET7);
  Tuple *o8_tuple = dict_find(received, KEY_OFFSET8);

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
  
  if (o5_tuple) {
    s_offset[4] = o5_tuple->value->int32;
    status_t s = persist_write_int(KEY_OFFSET5, s_offset[4]);
    if (s != S_SUCCESS) {
      APP_LOG(APP_LOG_LEVEL_WARNING, "Failed to remember TZ offset: %ld", s);
    }
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Offset 5: %ld", s_offset[4]);
  }
  
  if (o6_tuple) {
    s_offset[5] = o6_tuple->value->int32;
    status_t s = persist_write_int(KEY_OFFSET6, s_offset[5]);
    if (s != S_SUCCESS) {
      APP_LOG(APP_LOG_LEVEL_WARNING, "Failed to remember TZ offset: %ld", s);
    }
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Offset 6: %ld", s_offset[5]);
  }
  
  if (o7_tuple) {
    s_offset[6] = o7_tuple->value->int32;
    status_t s = persist_write_int(KEY_OFFSET7, s_offset[6]);
    if (s != S_SUCCESS) {
      APP_LOG(APP_LOG_LEVEL_WARNING, "Failed to remember TZ offset: %ld", s);
    }
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Offset 7: %ld", s_offset[6]);
  }
  
  if (o8_tuple) {
    s_offset[7] = o8_tuple->value->int32;
    status_t s = persist_write_int(KEY_OFFSET8, s_offset[7]);
    if (s != S_SUCCESS) {
      APP_LOG(APP_LOG_LEVEL_WARNING, "Failed to remember TZ offset: %ld", s);
    }
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Offset 8: %ld", s_offset[7]);
  }
  
  
  Tuple *tz1_tuple = dict_find(received, KEY_TZ1);
  Tuple *tz2_tuple = dict_find(received, KEY_TZ2);
  Tuple *tz3_tuple = dict_find(received, KEY_TZ3);
  Tuple *tz4_tuple = dict_find(received, KEY_TZ4);
  Tuple *tz5_tuple = dict_find(received, KEY_TZ5);
  Tuple *tz6_tuple = dict_find(received, KEY_TZ6);
  Tuple *tz7_tuple = dict_find(received, KEY_TZ7);
  Tuple *tz8_tuple = dict_find(received, KEY_TZ8);
  bool tz_set = false;
  
  if (tz1_tuple) {
    strncpy(s_tz[0], tz1_tuple->value->cstring, TZ_SIZE);
    persist_write_string(KEY_TZ1, s_tz[0]);
    APP_LOG(APP_LOG_LEVEL_INFO, "Configuration: TZ 1: %s", s_tz[0]);
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
  
  if (tz5_tuple) {
    strncpy(s_tz[4], tz5_tuple->value->cstring, TZ_SIZE);
    persist_write_string(KEY_TZ5, s_tz[4]);
    APP_LOG(APP_LOG_LEVEL_INFO, "Configuration: TZ 5: %s", s_tz[4]);
    tz_set = true;
  }
  
  if (tz6_tuple) {
    strncpy(s_tz[5], tz6_tuple->value->cstring, TZ_SIZE);
    persist_write_string(KEY_TZ6, s_tz[5]);
    APP_LOG(APP_LOG_LEVEL_INFO, "Configuration: TZ 6: %s", s_tz[5]);
    tz_set = true;
  }
  
  if (tz7_tuple) {
    strncpy(s_tz[6], tz7_tuple->value->cstring, TZ_SIZE);
    persist_write_string(KEY_TZ7, s_tz[6]);
    APP_LOG(APP_LOG_LEVEL_INFO, "Configuration: TZ 7: %s", s_tz[6]);
    tz_set = true;
  }
  
  if (tz8_tuple) {
    strncpy(s_tz[7], tz8_tuple->value->cstring, TZ_SIZE);
    persist_write_string(KEY_TZ8, s_tz[7]);
    APP_LOG(APP_LOG_LEVEL_INFO, "Configuration: TZ 8: %s", s_tz[7]);
    tz_set = true;
  }
  
  Tuple *l1_tuple = dict_find(received, KEY_LABEL1);
  Tuple *l2_tuple = dict_find(received, KEY_LABEL2);
  Tuple *l3_tuple = dict_find(received, KEY_LABEL3);
  Tuple *l4_tuple = dict_find(received, KEY_LABEL4);
  Tuple *l5_tuple = dict_find(received, KEY_LABEL5);
  Tuple *l6_tuple = dict_find(received, KEY_LABEL6);
  Tuple *l7_tuple = dict_find(received, KEY_LABEL7);
  Tuple *l8_tuple = dict_find(received, KEY_LABEL8);
  
  if (l1_tuple) {
    strncpy(s_label[0], l1_tuple->value->cstring, LABEL_SIZE);
    persist_write_string(KEY_LABEL1, s_label[0]);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Configuration: LABEL 1: %s", s_label[0]);
  }

  if (l2_tuple) {
    strncpy(s_label[1], l2_tuple->value->cstring, LABEL_SIZE);
    persist_write_string(KEY_LABEL2, s_label[1]);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Configuration: LABEL 2: %s", s_label[1]);
  }

  if (l3_tuple) {
    strncpy(s_label[2], l3_tuple->value->cstring, LABEL_SIZE);
    persist_write_string(KEY_LABEL3, s_label[2]);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Configuration: LABEL 3: %s", s_label[2]);
  }

  if (l4_tuple) {
    strncpy(s_label[3], l4_tuple->value->cstring, LABEL_SIZE);
    persist_write_string(KEY_LABEL4, s_label[3]);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Configuration: LABEL 4: %s", s_label[3]);
  }
  
  if (l5_tuple) {
    strncpy(s_label[4], l5_tuple->value->cstring, LABEL_SIZE);
    persist_write_string(KEY_LABEL5, s_label[4]);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Configuration: LABEL 5: %s", s_label[4]);
  }
  
  if (l6_tuple) {
    strncpy(s_label[5], l6_tuple->value->cstring, LABEL_SIZE);
    persist_write_string(KEY_LABEL6, s_label[5]);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Configuration: LABEL 6: %s", s_label[5]);
  }
  
  if (l7_tuple) {
    strncpy(s_label[6], l7_tuple->value->cstring, LABEL_SIZE);
    persist_write_string(KEY_LABEL7, s_label[6]);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Configuration: LABEL 7: %s", s_label[6]);
  }
  
  if (l8_tuple) {
    strncpy(s_label[7], l8_tuple->value->cstring, LABEL_SIZE);
    persist_write_string(KEY_LABEL8, s_label[7]);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Configuration: LABEL 8: %s", s_label[7]);
  }

  if (tz_set) {
    send_tz_request();
  } else {
    sort_times();
    s_offsets_up_to_date = true;
  }
  
  update_time();
}

// static void add_line(char *buffer, char *additional_line, bool first_line) {
//   if (!first_line) {
//     strcat(buffer, "\n");
//   }
//   strcat(buffer, additional_line);
// }

static void set_status_text(char *msg) {
  strncpy(s_status_label_text, msg, sizeof(s_status_label_text));
}

static void update_time() {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "UpdateTime...");
  
  // Create long lived buffers
  static char s_local_time[20];
  static char s_local_date[20];
  static char s_tz_time[4][20];
  char tt[20];

  // Get a tm structure
  time_t now;
  time(&now);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Localtime time: %ld", now);
    
  int32_t difference = now - s_last_tick;
  if (difference > 360 || difference < -360 || !s_offsets_up_to_date) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Difference (%ld) is more than 6 minutes, or offsets out of date (%s), requesting TZ information again...",
            difference, s_offsets_up_to_date ? "true" : "false");
    s_offsets_up_to_date = false;
    send_tz_request();
  }
  s_last_tick = now;

  int d = 0;
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
    
    if (0 == offset) {
      strncpy(s_local_time, tt, sizeof(s_local_time));
      text_layer_set_text(s_local_time_layer, s_local_time);
      
      strftime(s_local_date, sizeof(s_local_date), "%a, %d %b", tick_time);
      text_layer_set_text(s_local_date_layer, s_local_date);
    } else {
      s_tz_label_text[d][0] = '\0';
      if (!s_offsets_up_to_date) {
        strncat(s_tz_label_text[d], "?", 1);
      }
      strncat(s_tz_label_text[d], s_label[display], LABEL_SIZE - 1);
      text_layer_set_text(s_tz_label_layer[d], s_tz_label_text[d]);
      
      strncpy(s_tz_time[d], tt, sizeof(s_tz_time[d]));
      text_layer_set_text(s_tz_time_layer[d], s_tz_time[d]);
              
      d++;
    }    
  }
  
  update_status();
}

static void update_status() {
  BatteryChargeState bcs = battery_state_service_peek();
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Battery state: %u%%%s%s", bcs.charge_percent,
          bcs.is_charging ? " charging" : "", bcs.is_plugged ? " plugged" : "");
  
  int i = bcs.charge_percent / 10;
  if (i > 9) i = 9;
  if (i < 0) i = 0;
  bitmap_layer_set_bitmap(s_status_battery_layer, s_bmp_battery[i]);

  bitmap_layer_set_bitmap(s_status_charge_layer, bcs.is_plugged ? s_bmp_charge : s_bmp_nocharge);
  
  bool bt_connected = bluetooth_connection_service_peek();
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Bluetooth %s", bt_connected ? "connected" : "disconnected");
  
  bitmap_layer_set_bitmap(s_status_bt_layer, bt_connected ? s_bmp_bt : s_bmp_nobt);
  
  if (s_last_bt_connected != bt_connected) {
    vibes_double_pulse();
  }
  s_last_bt_connected = bt_connected;

  text_layer_set_text(s_status_text_layer, s_status_label_text);
}

static void update_popup_time() {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "update_popup_time...");
  
  // Create long lived buffers
  static char s_popup_time[CONFIG_SIZE][20];
  char tt[20];

  // Get a tm structure
  time_t now;
  time(&now);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Localtime time: %ld", now);
    
  int32_t difference = now - s_last_tick;
  if (difference > 360 || difference < -360 || !s_offsets_up_to_date) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Difference (%ld) is more than 6 minutes, or offsets out of date (%s), requesting TZ information again...",
            difference, s_offsets_up_to_date ? "true" : "false");
    s_offsets_up_to_date = false;
    send_tz_request();
  }
  s_last_tick = now;

  for (int i = 0; i < CONFIG_SIZE; i++) {
    time_t temp = now;
    
    int display = s_p_display[i];
    int offset = s_offset[display];

    // Apply TZ offset
    temp += offset * 60;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Display %d time: %ld (%d)", i, temp, offset);

    struct tm *tick_time = localtime(&temp);
    
    s_popup_label_text[i][0] = '\0';
    if (OFFSET_NO_DISPLAY == offset) {
      s_popup_time[i][0] = '\0';
    } else {
      // Write the current hours and minutes into the buffer
      if (clock_is_24h_style() == true) {
        // Use 24 hour format
        strftime(tt, sizeof("00:00"), "%H:%M", tick_time);
      } else {
        // Use 12 hour format
        strftime(tt, sizeof("00:00"), "%I:%M", tick_time);
      }

      if (!s_offsets_up_to_date) {
        strncat(s_popup_label_text[i], "?", 1);
      }
      strncat(s_popup_label_text[i], s_label[display], LABEL_SIZE - 1);
      strncpy(s_popup_time[i], tt, sizeof(s_popup_time[i]));
    }

    text_layer_set_text(s_popup_label_layer[i], s_popup_label_text[i]);
    text_layer_set_text(s_popup_time_layer[i], s_popup_time[i]);
  }
}

static void delete_layer(Layer *layer) {
  if (layer) {
    //APP_LOG(APP_LOG_LEVEL_DEBUG, "Freeing: %p", layer);
    layer_remove_from_parent((Layer *) layer);
    layer_destroy(layer);
  }
}

static void delete_layers() {
  // Null the layers
  for (int i = 0; i < 4; i++) {
    delete_layer((Layer *) s_tz_label_layer[i]);
    delete_layer((Layer *) s_tz_time_layer[i]);
    
    s_tz_label_layer[i] = NULL;
    s_tz_time_layer[i] = NULL;
  }
  
  layer_remove_from_parent((Layer *) s_local_time_layer);
  layer_remove_from_parent((Layer *) s_local_date_layer);

  delete_layer((Layer *) s_local_time_layer);
  delete_layer((Layer *) s_local_date_layer);
  
  s_local_time_layer = NULL;
  s_local_date_layer = NULL;
}

static TextLayer *create_text_layer(Window *window, GRect rect) {
  TextLayer *l = text_layer_create(rect);
  
  //APP_LOG(APP_LOG_LEVEL_DEBUG, "Allocated: %p", l);

  text_layer_set_background_color(l, GColorBlack);
  text_layer_set_text_color(l, GColorClear);
  
  layer_add_child(window_get_root_layer(window), (Layer *) l);

  return l;
}

#define LAYER_STATUS_HEIGHT (16)

static void create_layers() {
  delete_layers();
  
  int d = 0;
  int top = LAYER_STATUS_HEIGHT;
  for (int i = 0; i < s_num_display; i++) {
    int display = s_display[i];
    
    if (DISPLAY_LOCAL_TIME == display) {
      s_local_time_layer = create_text_layer(s_main_window, GRect(0, top, LAYER_LOCAL_WIDTH, LAYER_LOCAL_TIME_HEIGHT));
      text_layer_set_font(s_local_time_layer, s_big_font);
      text_layer_set_text_alignment(s_local_time_layer, GTextAlignmentCenter);
      top += LAYER_LOCAL_TIME_HEIGHT;
      
      s_local_date_layer = create_text_layer(s_main_window, GRect(0, top, LAYER_LOCAL_WIDTH, LAYER_LOCAL_DATE_HEIGHT));
      text_layer_set_font(s_local_date_layer, s_medium_font);
      text_layer_set_text_alignment(s_local_date_layer, GTextAlignmentCenter);
      top += LAYER_LOCAL_DATE_HEIGHT;
    } else {
      s_tz_label_layer[d] = create_text_layer(s_main_window, GRect(0, top, LAYER_TZ_LABEL_WIDTH, LAYER_TZ_HEIGHT));
      text_layer_set_font(s_tz_label_layer[d], s_small_font);
      text_layer_set_text_alignment(s_tz_label_layer[d], GTextAlignmentLeft);
      
      s_tz_time_layer[d] = create_text_layer(s_main_window, GRect(LAYER_TZ_LABEL_WIDTH, top, LAYER_TZ_TIME_WIDTH, LAYER_TZ_HEIGHT));
      text_layer_set_font(s_tz_time_layer[d], s_small_font);
      text_layer_set_text_alignment(s_tz_time_layer[d], GTextAlignmentRight);
      
      top += LAYER_TZ_HEIGHT;
      d++;
    }
  }
}

/*
 * Screen is 144 wide, 168 deep.
 */

#define LAYER_STATUS_LEFT_GAP (51)
#define LAYER_STATUS_BMP_WIDTH (16)
#define LAYER_STATUS_GAP (10)
#define LAYER_STATUS_TEXT_WIDTH (35)

static void main_window_load(Window *window) {
  int left = 0;

  left += LAYER_STATUS_LEFT_GAP;
  s_status_bt_layer = bitmap_layer_create(GRect(left, 0, LAYER_STATUS_BMP_WIDTH, LAYER_STATUS_HEIGHT));
  bitmap_layer_set_alignment(s_status_bt_layer, GAlignRight);
  bitmap_layer_set_compositing_mode(s_status_bt_layer, GCompOpAssignInverted);
  layer_add_child(window_get_root_layer(window), (Layer *) s_status_bt_layer);
  
  left += LAYER_STATUS_BMP_WIDTH + LAYER_STATUS_GAP;
  s_status_battery_layer = bitmap_layer_create(GRect(left, 0,
                                                     LAYER_STATUS_BMP_WIDTH, LAYER_STATUS_HEIGHT));\
  bitmap_layer_set_alignment(s_status_battery_layer, GAlignLeft);
  bitmap_layer_set_compositing_mode(s_status_battery_layer, GCompOpAssignInverted);
  layer_add_child(window_get_root_layer(window), (Layer *) s_status_battery_layer);

  left += LAYER_STATUS_BMP_WIDTH;
  s_status_charge_layer = bitmap_layer_create(GRect(left, 0,
                                                    LAYER_STATUS_BMP_WIDTH, LAYER_STATUS_HEIGHT));\
  bitmap_layer_set_alignment(s_status_charge_layer, GAlignLeft);
  bitmap_layer_set_compositing_mode(s_status_charge_layer, GCompOpAssignInverted);
  layer_add_child(window_get_root_layer(window), (Layer *) s_status_charge_layer);

  left += LAYER_STATUS_BMP_WIDTH;
  s_status_text_layer = create_text_layer(window, GRect(left, 0, LAYER_STATUS_TEXT_WIDTH, LAYER_STATUS_HEIGHT));
  text_layer_set_text_alignment(s_status_text_layer, GAlignRight);
  set_status_text("");

}

static void main_window_unload(Window *window) {
  delete_layers();
  delete_layer((Layer *) s_status_bt_layer);
  delete_layer((Layer *) s_status_battery_layer);
  delete_layer((Layer *) s_status_charge_layer);
  delete_layer((Layer *) s_status_text_layer);
}

static void delete_popup_layers() {
  for (int i = 0; i < CONFIG_SIZE; i++) {
    delete_layer((Layer *) s_popup_label_layer[i]);
    delete_layer((Layer *) s_popup_time_layer[i]);
  }
}

static void create_popup_layers() {
  delete_popup_layers();
  
  int top = 0;
  for (int i = 0; i < CONFIG_SIZE; i++) {
    s_popup_label_layer[i] = create_text_layer(s_popup_window, GRect(0, top, LAYER_TZ_LABEL_WIDTH, LAYER_TZ_HEIGHT));
    text_layer_set_font(s_popup_label_layer[i], s_small_font);
    text_layer_set_text_alignment(s_popup_label_layer[i], GTextAlignmentLeft);
      
    s_popup_time_layer[i] = create_text_layer(s_popup_window, GRect(LAYER_TZ_LABEL_WIDTH, top, LAYER_TZ_TIME_WIDTH, LAYER_TZ_HEIGHT));
    text_layer_set_font(s_popup_time_layer[i], s_small_font);
    text_layer_set_text_alignment(s_popup_time_layer[i], GTextAlignmentRight);
      
    top += LAYER_TZ_HEIGHT;
  }
}

static void popup_window_load(Window *window) {
  create_popup_layers();
}

static void popup_window_unload(Window *window) {
  delete_popup_layers();
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
}

static void send_tz_request() {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Requesting TZ offsets: %s, %s, %s, %s, %s, %s, %s, %s, ", s_tz[0], s_tz[1], s_tz[2], s_tz[3], s_tz[4], s_tz[5], s_tz[6], s_tz[7]);

  // Add a key-value pair
  dict_write_cstring(iter, KEY_TZ1, s_tz[0]);
  dict_write_cstring(iter, KEY_TZ2, s_tz[1]);
  dict_write_cstring(iter, KEY_TZ3, s_tz[2]);
  dict_write_cstring(iter, KEY_TZ4, s_tz[3]);
  dict_write_cstring(iter, KEY_TZ5, s_tz[4]);
  dict_write_cstring(iter, KEY_TZ6, s_tz[5]);
  dict_write_cstring(iter, KEY_TZ7, s_tz[6]);
  dict_write_cstring(iter, KEY_TZ8, s_tz[7]);

  // Send the message!
  app_message_outbox_send();
}

static void bluetooth_connection_callback(bool connected) {
  update_status();
}

static void popup_timer_callback(void *data) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Popup timer callback: %d", s_popup_state);
  // State 0: do nothing, probably a race condition
  // State 1: Pending timed out, so return to state 0
  if (2 == s_popup_state) {
    // State 2: Close the window, return to state 0
    window_stack_pop(true);
  }
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Clearing popup state");
  s_popup_state = 0;

  set_status_text("");
  update_status();
}

static void tap_handler(AccelAxisType axis, int32_t direction) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Shake, oh shake the Pebble watch... state=%d", s_popup_state);
  if (2 == s_popup_state) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Popup already open... closing.");
    app_timer_cancel(s_popup_timer_handle);
    popup_timer_callback(NULL);
    return;
  }

  if (1 == s_popup_state) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Popup pending... opening.");
    s_popup_state = 2;
    app_timer_cancel(s_popup_timer_handle);

    update_popup_time();
    window_stack_push(s_popup_window, true);
  
    s_popup_timer_handle = app_timer_register(POPUP_TIMEOUT_MS, popup_timer_callback, NULL);
    return;
  }
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "No popup state... set pending.");
  s_popup_state = 1;
  s_popup_timer_handle = app_timer_register(POPUP_PENDING_TIMEOUT_MS, popup_timer_callback, NULL);

  set_status_text("*");
  update_status();
}

static void battery_state_handler(BatteryChargeState s) {
  update_status();
}


static void init() {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "GlobalTime initialising... build: %s", build_time);
  
  // Create main Window element and assign to pointer
  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  
  // Create popup window
  s_popup_window = window_create();
  window_set_background_color(s_popup_window, GColorBlack);
  popup_window_load(s_popup_window);
  
  ResHandle big_handle = resource_get_handle(RESOURCE_ID_FONT_COMFORTAA_BOLD_33);
  s_big_font = fonts_load_custom_font(big_handle);
  
  ResHandle medium_handle = resource_get_handle(RESOURCE_ID_FONT_COMFORTAA_BOLD_23);
  s_medium_font = fonts_load_custom_font(medium_handle);
  
  ResHandle small_handle = resource_get_handle(RESOURCE_ID_FONT_COMFORTAA_REGULAR_15);
  s_small_font = fonts_load_custom_font(small_handle);
  
  s_bmp_bt = gbitmap_create_with_resource(RESOURCE_ID_BMP_BT);
  s_bmp_nobt = gbitmap_create_with_resource(RESOURCE_ID_BMP_NOBT);
  s_bmp_charge = gbitmap_create_with_resource(RESOURCE_ID_BMP_CHARGE);
  s_bmp_nocharge = gbitmap_create_with_resource(RESOURCE_ID_BMP_NOCHARGE);
  s_bmp_battery[0] = gbitmap_create_with_resource(RESOURCE_ID_BMP_00);
  s_bmp_battery[1] = gbitmap_create_with_resource(RESOURCE_ID_BMP_10);
  s_bmp_battery[2] = gbitmap_create_with_resource(RESOURCE_ID_BMP_20);
  s_bmp_battery[3] = gbitmap_create_with_resource(RESOURCE_ID_BMP_30);
  s_bmp_battery[4] = gbitmap_create_with_resource(RESOURCE_ID_BMP_40);
  s_bmp_battery[5] = gbitmap_create_with_resource(RESOURCE_ID_BMP_50);
  s_bmp_battery[6] = gbitmap_create_with_resource(RESOURCE_ID_BMP_60);
  s_bmp_battery[7] = gbitmap_create_with_resource(RESOURCE_ID_BMP_70);
  s_bmp_battery[8] = gbitmap_create_with_resource(RESOURCE_ID_BMP_80);
  s_bmp_battery[9] = gbitmap_create_with_resource(RESOURCE_ID_BMP_90);
  
  // Read current TZ config
  persist_read_string(KEY_TZ1, s_tz[0], TZ_SIZE);
  persist_read_string(KEY_TZ2, s_tz[1], TZ_SIZE);
  persist_read_string(KEY_TZ3, s_tz[2], TZ_SIZE);
  persist_read_string(KEY_TZ4, s_tz[3], TZ_SIZE);
  persist_read_string(KEY_TZ5, s_tz[4], TZ_SIZE);
  persist_read_string(KEY_TZ6, s_tz[5], TZ_SIZE);
  persist_read_string(KEY_TZ7, s_tz[6], TZ_SIZE);
  persist_read_string(KEY_TZ8, s_tz[7], TZ_SIZE);
  
  s_offset[0] = persist_read_int(KEY_OFFSET1);
  s_offset[1] = persist_read_int(KEY_OFFSET2);
  s_offset[2] = persist_read_int(KEY_OFFSET3);
  s_offset[3] = persist_read_int(KEY_OFFSET4);
  s_offset[4] = persist_read_int(KEY_OFFSET5);
  s_offset[5] = persist_read_int(KEY_OFFSET6);
  s_offset[6] = persist_read_int(KEY_OFFSET7);
  s_offset[7] = persist_read_int(KEY_OFFSET8);
  
  persist_read_string(KEY_LABEL1, s_label[0], LABEL_SIZE);
  persist_read_string(KEY_LABEL2, s_label[1], LABEL_SIZE);
  persist_read_string(KEY_LABEL3, s_label[2], LABEL_SIZE);
  persist_read_string(KEY_LABEL4, s_label[3], LABEL_SIZE);
  persist_read_string(KEY_LABEL5, s_label[4], LABEL_SIZE);
  persist_read_string(KEY_LABEL6, s_label[5], LABEL_SIZE);
  persist_read_string(KEY_LABEL7, s_label[6], LABEL_SIZE);
  persist_read_string(KEY_LABEL8, s_label[7], LABEL_SIZE);

  for (int i = 0; i < CONFIG_SIZE; i++) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Loaded TZ configuration 1: %s - %s (%ld)", s_label[i], s_tz[i], s_offset[i]);
  }
  
  sort_times();
  
  // Register a callback for the UTC offset information
  // TODO register failure callbacks too
  app_message_register_inbox_received(inbox_received_callback);
  
  // Connect to AppMessage stream
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
  
  // Send a request for TZ offsets
  send_tz_request();

  // Set handlers to manage the elements inside the Window
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  // Show the Window on the watch, with animated=true
  window_stack_push(s_main_window, true);
  
  // Register with TickTimerService
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  
  // Register for bluetooth status changes
  bluetooth_connection_service_subscribe(bluetooth_connection_callback);

  // Register for battery state changes
  battery_state_service_subscribe(battery_state_handler);
  
  // Register for tap events
  accel_tap_service_subscribe(tap_handler);
}

static void deinit() {
  // Destroy Window
  window_destroy(s_main_window);
  
  accel_tap_service_unsubscribe();
  popup_window_unload(s_popup_window);
  
  if (s_big_font) {
    fonts_unload_custom_font(s_big_font);
  }
  if (s_medium_font) {
    fonts_unload_custom_font(s_medium_font);
  }
  if (s_small_font) {
    fonts_unload_custom_font(s_small_font);
  }
  
  if (s_bmp_bt) gbitmap_destroy(s_bmp_bt);
  if (s_bmp_nobt) gbitmap_destroy(s_bmp_nobt);
  if (s_bmp_charge) gbitmap_destroy(s_bmp_charge);
  if (s_bmp_nocharge) gbitmap_destroy(s_bmp_nocharge);
  for (int i = 0; i < 10; i++) {
    if (s_bmp_battery[i]) gbitmap_destroy(s_bmp_battery[i]);
  }
}

int main(void) {
  build_time[0] = 0;
  int s = sizeof(build_time) - 1;
  strncat(build_time, __DATE__, s);
  strncat(build_time, "/", 1);
  s -= strlen(build_time);
  strncat(build_time, __TIME__, s);
  
  init();
  app_event_loop();
  deinit();
}
