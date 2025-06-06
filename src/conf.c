#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <pico/stdlib.h>

#include <ff.h>

#include "conf.h"
#include "log.h"
#include "main.h"


#define CONF_FILE_PATH          "conf/WittyPi5.conf"
#define CONF_FILE_MAX_SIZE      CONF_MAX_KEY_LENGTH * CONF_MAX_ITEMS + CONF_MAX_ITEMS + 32

#define SUPPRESS_CONF_FILE_SAVING_US    5000000


conf_obj_t config;
conf_obj_t original_config;

conf_obj_t default_config = {
    .items = {
        {.key = CONF_ADDRESS, .value = I2C_SLAVE_ADDR},

        {.key = CONF_DEFAULT_ON_DELAY, .value = 255},
        {.key = CONF_POWER_CUT_DELAY, .value = 15},

        {.key = CONF_PULSE_INTERVAL, .value = 10},
        {.key = CONF_BLINK_LED, .value = 100},
        {.key = CONF_DUMMY_LOAD, .value = 0},

        {.key = CONF_LOW_VOLTAGE, .value = 0},
        {.key = CONF_RECOVERY_VOLTAGE, .value = 0},
        
        {.key = CONF_PS_PRIORITY, .value = 0},

        {.key = CONF_ADJ_VUSB, .value = 0},
        {.key = CONF_ADJ_VIN, .value = 0},
        {.key = CONF_ADJ_VOUT, .value = 0},
        {.key = CONF_ADJ_IOUT, .value = 0},
        
        {.key = CONF_WATCHDOG, .value = 0},
        
        {.key = CONF_LOG_TO_FILE, .value = 1},
                
        {.key = CONF_BOOTSEL_FTY_RST, .value = 1},

        {.key = CONF_ALARM1_SECOND, .value = 0},
        {.key = CONF_ALARM1_MINUTE, .value = 0},
        {.key = CONF_ALARM1_HOUR, .value = 0},
        {.key = CONF_ALARM1_DAY, .value = 0},

        {.key = CONF_ALARM2_SECOND, .value = 0},
        {.key = CONF_ALARM2_MINUTE, .value = 0},
        {.key = CONF_ALARM2_HOUR, .value = 0},
        {.key = CONF_ALARM2_DAY, .value = 0},

        {.key = CONF_BELOW_TEMP_ACTION, .value = 0},
        {.key = CONF_BELOW_TEMP_POINT, .value = 0},
        {.key = CONF_OVER_TEMP_ACTION, .value = 0},
        {.key = CONF_OVER_TEMP_POINT, .value = 0},
        
        {.key = CONF_DST_OFFSET, .value = 0},
        {.key = CONF_DST_BEGIN_MON, .value = 0},
        {.key = CONF_DST_BEGIN_DAY, .value = 0},
        {.key = CONF_DST_BEGIN_HOUR, .value = 0},
        {.key = CONF_DST_BEGIN_MIN, .value = 0},
        {.key = CONF_DST_END_MON, .value = 0},
        {.key = CONF_DST_END_DAY, .value = 0},
        {.key = CONF_DST_END_HOUR, .value = 0},
        {.key = CONF_DST_END_MIN, .value = 0},
        {.key = CONF_DST_APPLIED, .value = 0},

		{.key = CONF_SYS_CLOCK_MHZ, .value = 48},
    },
    .count = 39
};

static bool dirty = false;

FILINFO disk_file_info;


// Copy configuration from one to another
bool copy_config(conf_obj_t *dest, conf_obj_t *src) {

    if (!dest || !src) {
        return false;
    }

    for (int i = 0; i < src->count; i++) {
        strcpy(dest->items[i].key, src->items[i].key);
        dest->items[i].value = src->items[i].value;
    }

    dest->count = src->count;
    
    if (dest == &config) {
        dirty = true;
    }
    return true;
}


// Add item into configuration
bool conf_add(conf_obj_t *obj, const char *key, uint8_t value) {
    if (obj->count >= CONF_MAX_ITEMS) return false;

    conf_item_t *item = &obj->items[obj->count];
    strncpy(item->key, key, CONF_MAX_KEY_LENGTH - 1);
    item->key[CONF_MAX_KEY_LENGTH - 1] = '\0';
    item->value = value;
    obj->count++;
    
    if (obj == &config) {
        dirty = true;
    }
    return true;
}


// Remove configuration item by key
bool conf_remove(conf_obj_t *obj, const char *key) {
    if (obj == NULL || key == NULL) {
        return false;
    }
    for (uint8_t i = 0; i < obj->count; i++) {
        if (strcmp(obj->items[i].key, key) == 0) {
            for (uint8_t j = i; j < obj->count - 1; j++) {
                strcpy(obj->items[j].key, obj->items[j+1].key);
                obj->items[j].value = obj->items[j+1].value;
            }
            obj->count--;
            if (obj == &config) {
                dirty = true;
            }
            return true;
        }
    }
    return false;
}


// Check if configuration contains item with given key
bool conf_contains(conf_obj_t *obj, const char *key) {
    for (int i = 0; i < obj->count; i++) {
        if (0 == strcmp(obj->items[i].key, key)) {
            return true;
        }
    }
    return false;
}


/**
 * Serialize configuration as string
 * 
 * @param obj The pointer to configuration object
 * @param buffer The buffer for string
 * @param buf_size The size of buffer
 * @return The actual length of generated string
 */
int conf_serialize(const conf_obj_t *obj, char *buffer, uint16_t buf_size) {
    if (!obj || !buffer || buf_size < 3) return -1;

    int pos = 0;
    buffer[pos++] = '{';
    buffer[pos++] = '\n';

    for (uint8_t i = 0; i < obj->count; i++) {
        const conf_item_t *item = &obj->items[i];

        if (i > 0) {
            if (pos + 1 >= buf_size) return -1;
            buffer[pos++] = ',';
            buffer[pos++] = '\n';
        }

        if (pos + strlen(item->key) + 3 >= buf_size) return -1;
        buffer[pos++] = '"';
        strcpy(&buffer[pos], item->key);
        pos += strlen(item->key);
        buffer[pos++] = '"';
        buffer[pos++] = ':';

        if (pos + 12 >= buf_size) return -1;
        pos += sprintf(&buffer[pos], "%u", item->value);
    }

    if (pos + 1 >= buf_size) return -1;
    buffer[pos++] = '\n';
    buffer[pos++] = '}';
    buffer[pos] = '\0';

    return pos;
}


// Parse value as unit8_t
static bool parse_uint8(const char **str, uint8_t *value) {
    const char *p = *str;
    char num_str[8] = {0};
    size_t len = 0;

    while (isdigit(*p) && len < sizeof(num_str) - 1) {
        num_str[len++] = *p++;
    }
    num_str[len] = '\0';

    long val = strtol(num_str, NULL, 10);

    if (val < 0 || val > 255) return false;

    *value = (uint8_t)val;
    *str = p;
    return true;
}


/**
 * Parse given string as configuration
 * 
 * @param str The string to be parsed
 * @param obj The pointer to configuration object
 * @return true if parse succesfully, false otherwise
 */
bool conf_parse(const char *str, conf_obj_t *obj) {
    if (!str || !obj) {
        return false;
    }

    const char *p = str;
    obj->count = 0;

    while (isspace(*p)) p++;

    if (*p++ != '{') {
        return false;
    }

    while (*p) {
        while (isspace(*p) || *p == ',') p++;

        if (*p == '}') break;

        if (obj->count >= CONF_MAX_ITEMS) {
            return false;
        }
        conf_item_t *item = &obj->items[obj->count];

        if (*p++ != '"') {
            return false;
        }
        size_t key_len = 0;
        while (*p && *p != '"' && key_len < CONF_MAX_KEY_LENGTH - 1) {
            item->key[key_len++] = *p++;
        }
        item->key[key_len] = '\0';
        if (*p++ != '"') {
            return false;
        }

        while (isspace(*p)) p++;
        if (*p++ != ':') {
            return false;
        }
        while (isspace(*p)) p++;

        if (!parse_uint8(&p, &item->value)) {
            return false;
        }
        obj->count++;
    }

    return *p == '}';
}


// Load configuration from file
bool load_from_file(const char *path, conf_obj_t *obj) {
    if (!path || !obj) {
        return false;
    }
    FIL fp;
    UINT data_length = 0;
    FRESULT res = f_open(&fp, path, FA_READ);
    if (res == FR_OK) {
        uint8_t data[CONF_FILE_MAX_SIZE];
        res = f_read(&fp, data, CONF_FILE_MAX_SIZE, &data_length);
        if (res == FR_OK) {
            data[data_length] = 0;
            if (conf_parse(data, obj)) {
                f_close(&fp);
                return true;
            } else {
                debug_log("Configuration parsing failed: %s\n", path);
            }
        } else {
            debug_log("Read file %s failed: %d\n", path, res);
        }
        f_close(&fp);
    } else {
        debug_log("Can't open file %s for reading: %d\n", path, res);
    }
    return false;
}


// Save configuration to file
bool save_to_file(const char *path, const conf_obj_t *obj) {
    if (!path || !obj) {
        return false;
    }
    uint8_t data[CONF_FILE_MAX_SIZE];
    int length = conf_serialize(obj, data, CONF_FILE_MAX_SIZE);
    if (length <= 0) {
        return false;
    }
    FIL fp;
    FRESULT res = f_open(&fp, path, FA_WRITE | FA_CREATE_ALWAYS);
    if (res == FR_OK) {
        UINT bw;
        res = f_write(&fp, data, length, &bw);
        if (res == FR_OK && bw > 0) {
            f_sync(&fp);
            f_close(&fp);
            return true;
        } else {
            debug_log("Write file %s failed: %d\n", path, res);
        }
        f_close(&fp);
    } else {
        debug_log("Can't open file %s for writing: %d\n", path, res);
    }
    return false;
}


/**
 * Initialize configuration
 */
void conf_init(void) {

    if (!load_from_file(CONF_FILE_PATH, &config) || config.count == 0) {
        // No usable configuration loaded
        debug_log("Restore to default configuration.\n");
        copy_config(&config, &default_config);
        
    } else {
        conf_obj_t tmp_config;
        
        // Remove items that do not exist in default configuration
        copy_config(&tmp_config, &config);
        for (int i = 0; i < tmp_config.count; i++) {
            if (!conf_contains(&default_config, tmp_config.items[i].key)) {
                debug_log("Remove configuration item: %s\n", tmp_config.items[i].key);
                conf_remove(&config, tmp_config.items[i].key);
                dirty = true;
            }
        }
        
        // Add new items that exist in default configuration
        copy_config(&tmp_config, &config);
        for (int i = 0; i < default_config.count; i++) {
            if (!conf_contains(&tmp_config, default_config.items[i].key)) {
                debug_log("Add configuration item: %s\n", default_config.items[i].key);
                conf_add(&config, default_config.items[i].key, default_config.items[i].value);
                dirty = true;
            }
        }
    }
    
    // Make an original copy
    copy_config(&original_config, &config);
    
    // Backup disk file info
    f_stat(CONF_FILE_PATH, &disk_file_info);
}


// Get item from configuration object
uint8_t conf_get_from(const conf_obj_t *obj, const char *key) {
    if (obj && key) {
        for (int i = 0; i < obj->count; i++) {
            if (0 == strcmp(obj->items[i].key, key)) {
                return obj->items[i].value;
            }
        }
        debug_log("Failed to get configuration with key=%s\n", key);
    }
    return 0;
}


/**
 * Get configuration item
 * 
 * @param key The item key
 * @return The value of configuration item
 */
uint8_t conf_get(const char *key) {
    return conf_get_from(&config, key);
}


// Set item to configuration object
bool conf_set_to(conf_obj_t *obj, const char *key, uint8_t value) {
    if (obj && key) {
        for (int i = 0; i < obj->count; i++) {
            if (0 == strcmp(obj->items[i].key, key)) {
    			uint8_t old_val = obj->items[i].value;
                obj->items[i].value = value;
    			if (obj->items[i].callback) {
    				obj->items[i].callback(key, old_val, value);
    			}
    			if (obj == &config) {
                    dirty = true;
                }
                return true;
            }
        }
        debug_log("Failed to set configuration with key=%s, value=%d\n", key, value);
    }
    return false;
}


/**
 * Set configuration item
 * 
 * @param key The item key
 * @param value The item value
 * @return true if succeed, false otherwise
 */
bool conf_set(const char *key, uint8_t value) {
    return conf_set_to(&config, key, value);
}


// Save configuration to file without any condition
// This will discard any change made directly on the file in USB-Drive
bool conf_save(void) {
    if (dirty) {
    	save_to_file(CONF_FILE_PATH, &config);
        dirty = false;
        return true;
    }
    return false;
}


/**
 * Reset the configuration to default values
 */
void conf_reset(void) {
    debug_log("Reset configuration.\n");
    copy_config(&config, &default_config);
    copy_config(&original_config, &config);
    dirty = false;
}


/**
 * Synchronize the configuration in RAM with the data in file
 */
void conf_sync(void) {
    FILINFO new_info;
    FRESULT res = f_stat(CONF_FILE_PATH, &new_info);
    if (res == FR_OK && (new_info.fdate != disk_file_info.fdate || new_info.ftime != disk_file_info.ftime)) {
        debug_log("conf file is changed.\n");
        conf_obj_t disk_config;
        if (load_from_file(CONF_FILE_PATH, &disk_config) && disk_config.count != 0) {
            if (dirty) {
                debug_log("RAM conf is changed.\n");
                for (int i = 0; i < config.count; i++) {
                    char * key = config.items[i].key;
                    uint8_t value = config.items[i].value;
                    
                    if (!conf_contains(&disk_config, key)) {
                        conf_add(&disk_config, key, value);
                    } else if (conf_get_from(&original_config, key) != value) {
                        conf_set_to(&disk_config, key, value);
                    }
                }
            }
            copy_config(&config, &disk_config);
        }
    }
    if (conf_save()) {
        f_stat(CONF_FILE_PATH, &disk_file_info);
        debug_log("conf file info updated.\n");
    }
}


/**
 * Save configuration to file when:
 *   the "dirty" flag is set, and
 *   the USB drive is not mounted or ejected
 */
void process_conf_task(void) {
    if (dirty) {
        if (get_absolute_time() >= SUPPRESS_CONF_FILE_SAVING_US && !is_usb_msc_device_mounted()) {
            conf_sync();
        }
    }
}


/**
 * Check if startup alarm (ALARM1) is configured
 * 
 * @return true if configured, false otherwise
 */
bool is_startup_alarm_configured(void) {
    return conf_get(CONF_ALARM1_DAY) != 0;
}


/**
 * Check if shutdown alarm (ALARM2) is configured
 * 
 * @return true if configured, false otherwise
 */
bool is_shutdown_alarm_configured(void) {
    return conf_get(CONF_ALARM2_DAY) != 0;
}


/**
 * Register callback function for item changed
 * 
 * @param key The item key
 * @param callback The pointer for callback function, NULL for unregisteration
 * @return true if succeed, false otherwise
 */
bool register_item_changed_callback(const char *key, item_changed_callback_t callback) {
	for (int i = 0; i < config.count; i++) {
        if (0 == strcmp(config.items[i].key, key)) {
			config.items[i].callback = callback;
			return true;
		}
	}
    return false;
}