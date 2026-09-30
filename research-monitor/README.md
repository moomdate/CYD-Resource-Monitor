# SYSTEM RESEARCH · PC Monitor UI

320 × 240 landscape PC telemetry dashboard. The browser page is a simulator with adjustable demo values and ICE/LIME, VIOLET, and AMBER color profiles.

## Preview

Open `index.html` in a modern browser. Use the sliders or IDLE / GAMING / TRANSFER buttons to simulate load. Values are demo data and are not connected to a host PC.

## LVGL files

- `assets/research_monitor.h`
- `assets/research_monitor.c`
- `assets/research_monitor_concept.png` (static visual reference only)

The C implementation draws the entire UI with native LVGL widgets. The image is not required at runtime. APIs target LVGL 8.3 and LVGL 9; compile the C file with your LVGL include path and the fonts enabled in `lv_conf.h`.

### Minimal integration

```c
#include "research_monitor.h"

static rm_ui_t monitor;

void app_ui_init(void)
{
    rm_create(&monitor, lv_screen_active(), RM_THEME_ICE_LIME); // LVGL 9
    // LVGL 8: rm_create(&monitor, lv_scr_act(), RM_THEME_ICE_LIME);
}

void update_monitor(void)
{
    rm_data_t d = {
        .cpu_percent = 68,
        .cpu_temp_c = 64,
        .cpu_mhz = 4720,
        .ram_used_mb = 12400,
        .ram_total_mb = 32000,
        .net_rx_mbps_x10 = 842,  // 84.2 Mbps
        .net_tx_mbps_x10 = 128,  // 12.8 Mbps
        .disk_active_percent = 72,
        .disk_read_mbps = 1843,
        .disk_write_mbps = 420,
        .disk_temp_c = 49
    };
    rm_set_data(&monitor, &d);
    rm_chart_push(&monitor, 84, 13); // graph samples in Mbps, capped to 100
}

void set_saved_theme(unsigned theme_from_nvs)
{
    rm_set_theme(&monitor, (rm_theme_t)theme_from_nvs);
}
```

The chart values passed to `rm_chart_push` are scaled 0–100 Mbps for the compact sparkline. Call the update functions from the LVGL/UI task or while holding the LVGL port lock. For LVGL 8 use `lv_scr_act()`; for LVGL 9 use `lv_screen_active()`.

### Display configuration

The view is authored at exactly 320 × 240. It fits an RGB565 TFT with LVGL's default software rendering and does not require a full-screen image asset. Keep `LV_COLOR_DEPTH` matched to your display driver. The header uses standard Montserrat 10/14/28 fonts when enabled, otherwise `LV_FONT_DEFAULT` is used. Enable those fonts in `lv_conf.h` for the intended hierarchy.

## Layout map

| Area | Position | Data |
|---|---:|---|
| Header | x9 y8 | SYSTEM RESEARCH, node/status |
| CPU | x9 y38 | load arc, temperature, clock |
| RAM | x151 y38 | used/total and capacity bar |
| Network | x151 y95 | RX/TX rates and scrolling chart |
| NVMe | x9 y155 | activity, read/write rates, temperature |

All positions are in physical display pixels; edit the constants in `research_monitor.c` to adapt it to another resolution.
