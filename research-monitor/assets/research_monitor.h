#ifndef RESEARCH_MONITOR_H
#define RESEARCH_MONITOR_H

/* Compact PC telemetry panel for LVGL 8.3+ and LVGL 9.
 * All screen elements are native LVGL objects; no background image is required.
 * Call rm_create() after display/theme initialization, then rm_set_data() when
 * a new telemetry sample is available. Functions must be called from LVGL's
 * UI thread (or protected with the port's LVGL lock).
 */
#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RM_THEME_ICE_LIME = 0,
    RM_THEME_VIOLET   = 1,
    RM_THEME_AMBER    = 2
} rm_theme_t;

typedef struct {
    uint8_t cpu_percent;       /* 0..100 */
    int16_t cpu_temp_c;        /* degrees C */
    uint16_t cpu_mhz;          /* e.g. 4720 */
    uint16_t ram_used_mb;
    uint16_t ram_total_mb;
    uint16_t net_rx_mbps_x10;  /* 842 = 84.2 Mbps */
    uint16_t net_tx_mbps_x10;
    uint8_t disk_active_percent;
    uint16_t disk_read_mbps;   /* e.g. 1843 */
    uint16_t disk_write_mbps;
    int16_t disk_temp_c;
} rm_data_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *cpu_arc;
    lv_obj_t *cpu_value;
    lv_obj_t *cpu_temp;
    lv_obj_t *cpu_clock;
    lv_obj_t *ram_value;
    lv_obj_t *ram_bar;
    lv_obj_t *ram_percent;
    lv_obj_t *net_rx;
    lv_obj_t *net_tx;
    lv_obj_t *net_chart;
    lv_obj_t *disk_bar;
    lv_obj_t *disk_read;
    lv_obj_t *disk_write;
    lv_obj_t *disk_temp;
    lv_obj_t *theme_button[3];
    lv_obj_t *theme_label[3];
    rm_data_t data;
    rm_theme_t theme;
    uint8_t history_index;
    uint16_t history[2][32];
} rm_ui_t;

/* parent may be the active screen or a container. Returns 0 on success. */
int rm_create(rm_ui_t *ui, lv_obj_t *parent, rm_theme_t initial_theme);
void rm_set_data(rm_ui_t *ui, const rm_data_t *data);
void rm_set_theme(rm_ui_t *ui, rm_theme_t theme);
void rm_chart_push(rm_ui_t *ui, uint16_t rx_mbps, uint16_t tx_mbps);

#ifdef __cplusplus
}
#endif
#endif
