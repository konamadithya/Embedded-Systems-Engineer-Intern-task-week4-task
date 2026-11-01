/*
 * Week 4 - Integrated ESP32 Smart Indoor Environment Monitor
 * Host-based C11 integration model. No physical ESP32 hardware is accessed.
 * Build: gcc -std=c11 -Wall -Wextra -Wpedantic -O2 main.c -o week4
 * Run:   ./week4
 * Commands: HELP, STATUS, MOTION ON|OFF, TEMP value, HUM value,
 *           GAS value, SENSOR FAIL|OK, QUIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#define SENSOR_PERIOD_MS 5000U
#define DISPLAY_PERIOD_MS 2000U
#define TELEMETRY_PERIOD_MS 30000U
#define ALERT_TEMP_C 30.0f
#define ALERT_HUM_PCT 70.0f
#define CLEAR_TEMP_C 28.0f
#define CLEAR_HUM_PCT 65.0f

typedef struct {
    float temperature_c;
    float humidity_pct;
    uint32_t gas_resistance_ohm;
    bool motion;
    bool valid;
} sensor_data_t;

typedef struct {
    bool led_on;
    bool buzzer_on;
    bool alarm_latched;
} output_state_t;

static uint32_t system_ms;
static sensor_data_t sensor = {26.0f, 50.0f, 15000U, false, true};
static output_state_t outputs = {false, false, false};
static uint32_t last_sensor_ms, last_display_ms, last_telemetry_ms;
static bool running = true;

static bool equals_ci(const char *a, const char *b)
{
    if (a == NULL || b == NULL) return false;
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return false;
        ++a; ++b;
    }
    return *a == '\0' && *b == '\0';
}

static bool parse_float_range(const char *s, float min, float max, float *out)
{
    char *end = NULL;
    float value;
    if (s == NULL || out == NULL) return false;
    errno = 0;
    value = strtof(s, &end);
    if (errno != 0 || end == s || *end != '\0' || value < min || value > max)
        return false;
    *out = value;
    return true;
}

static void alert_process(void)
{
    if (!sensor.valid) {
        outputs.led_on = true;       /* Fault indicator */
        outputs.buzzer_on = false;
        return;
    }
    if (sensor.temperature_c >= ALERT_TEMP_C || sensor.humidity_pct >= ALERT_HUM_PCT)
        outputs.alarm_latched = true;
    else if (sensor.temperature_c <= CLEAR_TEMP_C &&
             sensor.humidity_pct <= CLEAR_HUM_PCT)
        outputs.alarm_latched = false;

    outputs.led_on = outputs.alarm_latched || sensor.motion;
    outputs.buzzer_on = outputs.alarm_latched;
}

/* Hardware abstraction boundary: replace this simulation with I2C/GPIO drivers. */
static bool sensor_interface_read(sensor_data_t *data)
{
    if (data == NULL || !sensor.valid) return false;
    *data = sensor;
    return true;
}

/* Hardware abstraction boundary: replace terminal output with OLED/UART/MQTT drivers. */
static void display_interface_update(void)
{
    printf("[DISPLAY %lu ms] T=%.1f C RH=%.1f%% Motion=%s LED=%s\n",
           (unsigned long)system_ms, sensor.temperature_c, sensor.humidity_pct,
           sensor.motion ? "YES" : "NO", outputs.led_on ? "ON" : "OFF");
}

static void telemetry_interface_send(void)
{
    printf("[TELEMETRY %lu ms] {\"temperature_c\":%.1f,\"humidity_rh\":%.1f,"
           "\"gas_resistance_ohm\":%lu,\"motion\":%s,\"sensor_valid\":%s}\n",
           (unsigned long)system_ms, sensor.temperature_c, sensor.humidity_pct,
           (unsigned long)sensor.gas_resistance_ohm,
           sensor.motion ? "true" : "false", sensor.valid ? "true" : "false");
}

static void scheduler_run_due_tasks(void)
{
    uint32_t now = system_ms;
    if ((uint32_t)(now - last_sensor_ms) >= SENSOR_PERIOD_MS) {
        sensor_data_t sample;
        if (sensor_interface_read(&sample)) sensor = sample;
        else sensor.valid = false;
        last_sensor_ms = now;
        alert_process();
        printf("[SENSOR %lu ms] T=%.1f C RH=%.1f%% GAS=%lu ohm valid=%s\n",
               (unsigned long)now, sensor.temperature_c, sensor.humidity_pct,
               (unsigned long)sensor.gas_resistance_ohm, sensor.valid ? "YES" : "NO");
    }
    if ((uint32_t)(now - last_display_ms) >= DISPLAY_PERIOD_MS) {
        last_display_ms = now;
        display_interface_update();
    }
    if ((uint32_t)(now - last_telemetry_ms) >= TELEMETRY_PERIOD_MS) {
        last_telemetry_ms = now;
        telemetry_interface_send();
    }
}

static uint32_t next_deadline(void)
{
    uint32_t sensor_due = last_sensor_ms + SENSOR_PERIOD_MS;
    uint32_t display_due = last_display_ms + DISPLAY_PERIOD_MS;
    uint32_t telemetry_due = last_telemetry_ms + TELEMETRY_PERIOD_MS;
    uint32_t next = sensor_due;
    if ((int32_t)(display_due - next) < 0) next = display_due;
    if ((int32_t)(telemetry_due - next) < 0) next = telemetry_due;
    return next;
}

/* Event-driven host time advancement: skip directly to the next task deadline. */
static void simulation_advance_ms(uint32_t duration_ms)
{
    uint32_t end = system_ms + duration_ms;
    while ((int32_t)(end - system_ms) > 0) {
        uint32_t due = next_deadline();
        if ((int32_t)(due - end) > 0) {
            system_ms = end;
            break;
        }
        system_ms = due;
        scheduler_run_due_tasks();
    }
}

static void print_status(void)
{
    printf("\n--- INTEGRATED SYSTEM STATUS ---\n");
    printf("Uptime: %lu ms\n", (unsigned long)system_ms);
    printf("Temperature: %.1f C | Humidity: %.1f %%RH\n",
           sensor.temperature_c, sensor.humidity_pct);
    printf("Gas resistance: %lu ohm (indicative only)\n",
           (unsigned long)sensor.gas_resistance_ohm);
    printf("Motion: %s | Sensor: %s\n", sensor.motion ? "DETECTED" : "CLEAR",
           sensor.valid ? "VALID" : "FAULT");
    printf("LED: %s | Buzzer: %s | Alarm: %s\n\n",
           outputs.led_on ? "ON" : "OFF", outputs.buzzer_on ? "ON" : "OFF",
           outputs.alarm_latched ? "LATCHED" : "CLEAR");
}

static void show_help(void)
{
    puts("Commands: HELP | STATUS | MOTION ON|OFF | TEMP value | HUM value");
    puts("          GAS value | SENSOR FAIL|OK | QUIT");
}

static void process_command(char *line)
{
    char command[24] = {0}, arg[64] = {0};
    int fields = sscanf(line, "%23s %63s", command, arg);
    float value;
    if (fields <= 0) return;
    for (size_t i = 0; command[i]; ++i)
        command[i] = (char)toupper((unsigned char)command[i]);

    if (!strcmp(command, "HELP")) show_help();
    else if (!strcmp(command, "STATUS")) print_status();
    else if (!strcmp(command, "QUIT")) running = false;
    else if (!strcmp(command, "MOTION") && fields == 2) {
        if (equals_ci(arg, "ON")) sensor.motion = true;
        else if (equals_ci(arg, "OFF")) sensor.motion = false;
        else { puts("ERROR: use MOTION ON or MOTION OFF"); return; }
        alert_process();
        printf("PIR motion set to %s\n", sensor.motion ? "ON" : "OFF");
    } else if (!strcmp(command, "TEMP") && fields == 2) {
        if (!parse_float_range(arg, 0.0f, 60.0f, &value)) {
            puts("ERROR: temperature range is 0..60 C"); return;
        }
        sensor.temperature_c = value;
        alert_process();
        puts("Temperature updated");
    } else if (!strcmp(command, "HUM") && fields == 2) {
        if (!parse_float_range(arg, 0.0f, 100.0f, &value)) {
            puts("ERROR: humidity range is 0..100 %RH"); return;
        }
        sensor.humidity_pct = value;
        alert_process();
        puts("Humidity updated");
    } else if (!strcmp(command, "GAS") && fields == 2) {
        char *end = NULL;
        errno = 0;
        unsigned long gas = strtoul(arg, &end, 10);
        if (errno || end == arg || *end || gas < 1UL || gas > 1000000UL) {
            puts("ERROR: gas resistance range is 1..1000000 ohm"); return;
        }
        sensor.gas_resistance_ohm = (uint32_t)gas;
        puts("Gas resistance updated");
    } else if (!strcmp(command, "SENSOR") && fields == 2) {
        if (equals_ci(arg, "FAIL")) sensor.valid = false;
        else if (equals_ci(arg, "OK")) sensor.valid = true;
        else { puts("ERROR: use SENSOR FAIL or SENSOR OK"); return; }
        alert_process();
        printf("Sensor status set to %s\n", sensor.valid ? "VALID" : "FAULT");
    } else puts("ERROR: unknown command; type HELP");
}

int main(void)
{
    char line[128];
    puts("ESP32 Smart Indoor Environment Monitor - Integrated Host Simulation");
    show_help();
    while (running) {
        simulation_advance_ms(1000U);
        alert_process();
        printf("UART> ");
        if (fgets(line, sizeof line, stdin) == NULL) break;
        process_command(line);
    }
    puts("Integrated simulation stopped.");
    return EXIT_SUCCESS;
}
