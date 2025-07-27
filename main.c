/*
 * main.c - Environmental Monitor (Console Version)
 *
 * A unified console application that displays both global seismic activity
 * and local lightning proximity warnings simultaneously.
 *
 * Version 5.1: Corrected a typo in a variable name that was causing
 * a compilation error.
 *
 * Dependencies: libcurl, jansson
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h> // For sleep()
#include <curl/curl.h>
#include <jansson.h>

// --- Constants ---
#define UPDATE_INTERVAL_SECONDS 120 // Update every 2 minutes

// Seismic Monitor Constants
#define USGS_URL "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/all_hour.geojson"
#define MAJOR_QUAKE_THRESHOLD 6.0
#define MAX_QUAKES 200
#define MAX_ALERTED_IDS 50

// Lightning Monitor Constants
#define WEATHER_API_URL_FORMAT "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f&current=weather_code&hourly=weather_code&forecast_hours=6"
#define LIGHTNING_ALERT_CODE_1 95 // Thunderstorm: Slight or moderate
#define LIGHTNING_ALERT_CODE_2 96 // Thunderstorm with slight hail
#define LIGHTNING_ALERT_CODE_3 99 // Thunderstorm with heavy hail

// ANSI color codes
#define COLOR_RED     "\x1b[31m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_RESET   "\x1b[0m"

// --- Structs ---
struct MemoryStruct {
    char *memory;
    size_t size;
};

typedef struct {
    double mag;
    char place[256];
    char time_ago[20];
    char id[64];
} Earthquake;

// --- Globals ---
// Seismic data
Earthquake g_quakes[MAX_QUAKES];
int g_quake_count = 0;
char g_alerted_ids[MAX_ALERTED_IDS][64];
int g_alerted_ids_count = 0;

// Lightning data
int g_weather_code = 0;
int g_hourly_weather_codes[6] = {0};
int g_is_storm_active = 0;
float g_latitude = 54.53; // Default: Guisborough, UK
float g_longitude = -1.05;

// --- Function Prototypes ---
static size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp);
void fetch_seismic_data(float min_magnitude, float alert_threshold);
void fetch_lightning_data();
void render_display(float min_magnitude);
void format_time_ago(long long event_time_ms, char* buffer, size_t buffer_size);
int compare_quakes(const void *a, const void *b);
void check_for_quake_alerts(float alert_threshold);

// --- Main Function ---
int main(int argc, char *argv[]) {
    float min_magnitude = 0.0;
    float alert_threshold = MAJOR_QUAKE_THRESHOLD;

    // --- Argument Parsing ---
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0 && i + 1 < argc) {
            min_magnitude = atof(argv[i + 1]);
            if (min_magnitude < 0) min_magnitude = 0.0;
            alert_threshold = min_magnitude;
            i++; // Consume the value
        } else if (strcmp(argv[i], "-l") == 0 && i + 2 < argc) {
            g_latitude = atof(argv[i + 1]);
            g_longitude = atof(argv[i + 2]);
            i += 2; // Consume the two values
        } else if (strcmp(argv[i], "test") == 0) {
            alert_threshold = 0.0;
        } else {
            printf("Unknown argument: %s\n", argv[i]);
        }
    }

    printf("--- Starting Environmental Monitor ---\n");
    printf("Seismic Filter: M%.1f+ (Alerts >= %.1f)\n", min_magnitude, alert_threshold);
    printf("Lightning Location: %.2f, %.2f\n", g_latitude, g_longitude);
    sleep(4);

    curl_global_init(CURL_GLOBAL_ALL);

    while (1) {
        fetch_seismic_data(min_magnitude, alert_threshold);
        fetch_lightning_data();
        render_display(min_magnitude);
        printf("\nWaiting %d seconds for the next update...\n", UPDATE_INTERVAL_SECONDS);
        sleep(UPDATE_INTERVAL_SECONDS);
    }

    curl_global_cleanup();
    return 0;
}


// --- Data Fetching and Processing ---

static size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        printf("Not enough memory (realloc returned NULL)\n");
        return 0;
    }
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

void fetch_seismic_data(float min_magnitude, float alert_threshold) {
    CURL *curl_handle;
    CURLcode res;
    struct MemoryStruct chunk = { .memory = malloc(1), .size = 0 };
    g_quake_count = 0;

    curl_handle = curl_easy_init();
    if (curl_handle) {
        curl_easy_setopt(curl_handle, CURLOPT_URL, USGS_URL);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
        res = curl_easy_perform(curl_handle);

        if (res == CURLE_OK) {
            json_t *root;
            json_error_t error;
            root = json_loads(chunk.memory, 0, &error);
            if (root) {
                json_t *features = json_object_get(root, "features");
                for (size_t i = 0; i < json_array_size(features) && g_quake_count < MAX_QUAKES; i++) {
                    json_t *value = json_array_get(features, i);
                    json_t *properties = json_object_get(value, "properties");
                    double mag = json_real_value(json_object_get(properties, "mag"));

                    if (mag >= min_magnitude) {
                        g_quakes[g_quake_count].mag = mag;
                        const char *place = json_string_value(json_object_get(properties, "place"));
                        if(place) strncpy(g_quakes[g_quake_count].place, place, sizeof(g_quakes[g_quake_count].place) - 1);
                        const char *id = json_string_value(json_object_get(properties, "id"));
                        if(id) strncpy(g_quakes[g_quake_count].id, id, sizeof(g_quakes[g_quake_count].id) - 1);
                        long long time = json_integer_value(json_object_get(properties, "time"));
                        format_time_ago(time, g_quakes[g_quake_count].time_ago, sizeof(g_quakes[g_quake_count].time_ago));
                        g_quake_count++;
                    }
                }
                qsort(g_quakes, g_quake_count, sizeof(Earthquake), compare_quakes);
                check_for_quake_alerts(alert_threshold);
                json_decref(root);
            }
        }
        curl_easy_cleanup(curl_handle);
    }
    free(chunk.memory);
}

void fetch_lightning_data() {
    CURL *curl_handle;
    CURLcode res;
    struct MemoryStruct chunk = { .memory = malloc(1), .size = 0 };
    g_weather_code = 0;
    memset(g_hourly_weather_codes, 0, sizeof(g_hourly_weather_codes));
    
    char url_buffer[256];
    snprintf(url_buffer, sizeof(url_buffer), WEATHER_API_URL_FORMAT, g_latitude, g_longitude);

    curl_handle = curl_easy_init();
    if (curl_handle) {
        curl_easy_setopt(curl_handle, CURLOPT_URL, url_buffer);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
        res = curl_easy_perform(curl_handle);

        if (res == CURLE_OK) {
            json_t *root;
            json_error_t error;
            root = json_loads(chunk.memory, 0, &error);
            if (root) {
                json_t *current = json_object_get(root, "current");
                if (json_is_object(current)) {
                    g_weather_code = json_integer_value(json_object_get(current, "weather_code"));
                }
                json_t *hourly = json_object_get(root, "hourly");
                if (json_is_object(hourly)) {
                    json_t* hourly_codes = json_object_get(hourly, "weather_code");
                    if (json_is_array(hourly_codes)) {
                        for (int i = 0; i < 6 && i < json_array_size(hourly_codes); i++) {
                            g_hourly_weather_codes[i] = json_integer_value(json_array_get(hourly_codes, i));
                        }
                    }
                }
                json_decref(root);
            }
        }
        curl_easy_cleanup(curl_handle);
    }
    free(chunk.memory);
}

// --- Display and Utility Functions ---

void render_display(float min_magnitude) {
    printf("\033[H\033[J"); // Clear console
    time_t now = time(NULL);
    char time_buf[100];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S UTC", gmtime(&now));
    
    printf(COLOR_CYAN "--- GLOBAL SEISMIC MONITOR (Min Mag: %.1f) ---" COLOR_RESET, min_magnitude);
    printf("\nLast Updated: %s\n\n", time_buf);
    for (int i = 0; i < g_quake_count; i++) {
        const char* color = (g_quakes[i].mag >= 6.0) ? COLOR_RED : (g_quakes[i].mag >= 4.0) ? COLOR_YELLOW : COLOR_GREEN;
        printf("%s[  M %.1f  ]%-10s%s %s\n", color, g_quakes[i].mag, g_quakes[i].time_ago, COLOR_RESET, g_quakes[i].place);
    }

    printf(COLOR_CYAN "\n--- LIGHTNING PROXIMITY WARNING ---\n" COLOR_RESET);
    printf("Monitoring Location: %.2f, %.2f\n\n", g_latitude, g_longitude);
    
    int is_warning = (g_weather_code == LIGHTNING_ALERT_CODE_1 || g_weather_code == LIGHTNING_ALERT_CODE_2 || g_weather_code == LIGHTNING_ALERT_CODE_3);
    int is_watch = 0;
    for (int i = 1; i < 6; i++) { // Check next 5 hours (index 1 to 5)
        if (g_hourly_weather_codes[i] == LIGHTNING_ALERT_CODE_1 || g_hourly_weather_codes[i] == LIGHTNING_ALERT_CODE_2 || g_hourly_weather_codes[i] == LIGHTNING_ALERT_CODE_3) {
            is_watch = 1;
            break;
        }
    }

    if (is_warning) {
        printf(COLOR_RED "!!! SEVERE THUNDERSTORM WARNING IN EFFECT !!!\n" COLOR_RESET);
        printf("> Isolate antenna and sensitive equipment immediately.\n");
        if (!g_is_storm_active) {
            printf("\a"); fflush(stdout);
            g_is_storm_active = 1;
        }
    } else if (is_watch) {
        printf(COLOR_YELLOW "--- THUNDERSTORM WATCH ---\n" COLOR_RESET);
        printf("> Thunderstorms possible within the next 6 hours. Monitor conditions.\n");
        g_is_storm_active = 0; // Reset active storm flag if warning is over
    }
    else {
        printf(COLOR_GREEN "STATUS: All clear.\n" COLOR_RESET);
        g_is_storm_active = 0;
    }
}

void format_time_ago(long long event_time_ms, char* buffer, size_t buffer_size) {
    time_t now = time(NULL);
    long long diff_s = (now - (event_time_ms / 1000));
    if (diff_s < 60) snprintf(buffer, buffer_size, "%llds ago", diff_s);
    else snprintf(buffer, buffer_size, "%lldm ago", diff_s / 60);
}

int compare_quakes(const void *a, const void *b) {
    Earthquake *quakeA = (Earthquake *)a;
    Earthquake *quakeB = (Earthquake *)b;
    if (quakeA->mag < quakeB->mag) return 1;
    if (quakeA->mag > quakeB->mag) return -1;
    return 0;
}

void check_for_quake_alerts(float alert_threshold) {
    for (int i = 0; i < g_quake_count; i++) {
        if (g_quakes[i].mag >= alert_threshold) {
            int already_alerted = 0;
            for (int j = 0; j < g_alerted_ids_count; j++) {
                if (strcmp(g_quakes[i].id, g_alerted_ids[j]) == 0) {
                    already_alerted = 1;
                    break;
                }
            }
            if (!already_alerted) {
                printf("\a"); fflush(stdout);
                if (g_alerted_ids_count < MAX_ALERTED_IDS) {
                    strcpy(g_alerted_ids[g_alerted_ids_count++], g_quakes[i].id);
                } else {
                    for(int k=0; k < MAX_ALERTED_IDS - 1; k++) strcpy(g_alerted_ids[k], g_alerted_ids[k+1]);
                    strcpy(g_alerted_ids[MAX_ALERTED_IDS - 1], g_quakes[i].id);
                }
            }
        }
    }
}
