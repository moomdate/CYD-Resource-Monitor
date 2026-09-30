#include "research_monitor.h"
#include <stdio.h>
#include <string.h>

/* LVGL 8 uses lv_obj_clear_flag where LVGL 9 uses lv_obj_remove_flag. */
#if LVGL_VERSION_MAJOR >= 9
#define RM_CLEAR_FLAG(obj, flag) lv_obj_remove_flag((obj), (flag))
#else
#define RM_CLEAR_FLAG(obj, flag) lv_obj_clear_flag((obj), (flag))
#endif

typedef struct { lv_color_t accent, bright, lime, warn, red, bg, panel, line, dim, white; } rm_colors_t;
static rm_colors_t C;
static const rm_colors_t palettes[] = {
    {LV_COLOR_MAKE(0x21,0xdd,0xf3),LV_COLOR_MAKE(0x73,0xf5,0xff),LV_COLOR_MAKE(0xa8,0xf4,0x37),LV_COLOR_MAKE(0xff,0xac,0x37),LV_COLOR_MAKE(0xff,0x46,0x59),LV_COLOR_MAKE(0x06,0x0a,0x0d),LV_COLOR_MAKE(0x09,0x11,0x16),LV_COLOR_MAKE(0x25,0x41,0x4b),LV_COLOR_MAKE(0x80,0x93,0x9c),LV_COLOR_MAKE(0xea,0xff,0xff)},
    {LV_COLOR_MAKE(0xbb,0x79,0xff),LV_COLOR_MAKE(0xdf,0xba,0xff),LV_COLOR_MAKE(0x56,0xe0,0xcc),LV_COLOR_MAKE(0xff,0xac,0x37),LV_COLOR_MAKE(0xff,0x46,0x59),LV_COLOR_MAKE(0x06,0x0a,0x0d),LV_COLOR_MAKE(0x09,0x11,0x16),LV_COLOR_MAKE(0x25,0x41,0x4b),LV_COLOR_MAKE(0x80,0x93,0x9c),LV_COLOR_MAKE(0xea,0xff,0xff)},
    {LV_COLOR_MAKE(0xff,0xab,0x37),LV_COLOR_MAKE(0xff,0xd0,0x78),LV_COLOR_MAKE(0xb3,0xf2,0x50),LV_COLOR_MAKE(0xff,0xac,0x37),LV_COLOR_MAKE(0xff,0x46,0x59),LV_COLOR_MAKE(0x06,0x0a,0x0d),LV_COLOR_MAKE(0x09,0x11,0x16),LV_COLOR_MAKE(0x25,0x41,0x4b),LV_COLOR_MAKE(0x80,0x93,0x9c),LV_COLOR_MAKE(0xea,0xff,0xff)}
};

static const lv_font_t *font_small(void) {
#if LV_FONT_MONTSERRAT_10
    return &lv_font_montserrat_10;
#else
    return LV_FONT_DEFAULT;
#endif
}
static const lv_font_t *font_mid(void) {
#if LV_FONT_MONTSERRAT_14
    return &lv_font_montserrat_14;
#else
    return LV_FONT_DEFAULT;
#endif
}
static const lv_font_t *font_big(void) {
#if LV_FONT_MONTSERRAT_28
    return &lv_font_montserrat_28;
#elif LV_FONT_MONTSERRAT_24
    return &lv_font_montserrat_24;
#else
    return LV_FONT_DEFAULT;
#endif
}
static lv_obj_t *label(lv_obj_t *p, const char *s, lv_coord_t x, lv_coord_t y, const lv_font_t *f, lv_color_t col) {
    lv_obj_t *o=lv_label_create(p); lv_label_set_text(o,s); lv_obj_set_pos(o,x,y); lv_obj_set_style_text_font(o,f,0); lv_obj_set_style_text_color(o,col,0); lv_obj_set_style_text_letter_space(o,0,0); return o;
}
static lv_obj_t *panel(lv_obj_t *p, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    lv_obj_t *o=lv_obj_create(p); lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h); lv_obj_set_style_bg_color(o,C.panel,0); lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0); lv_obj_set_style_border_color(o,C.line,0); lv_obj_set_style_border_width(o,1,0); lv_obj_set_style_radius(o,2,0); lv_obj_set_style_pad_all(o,0,0); lv_obj_set_scrollbar_mode(o,LV_SCROLLBAR_MODE_OFF); return o;
}
static lv_obj_t *bar(lv_obj_t *p, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, lv_color_t color) {
    lv_obj_t *o=lv_bar_create(p); lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h); lv_bar_set_range(o,0,100); lv_bar_set_value(o,0,LV_ANIM_OFF); lv_obj_set_style_bg_color(o,lv_color_hex(0x172b31),LV_PART_MAIN); lv_obj_set_style_bg_opa(o,LV_OPA_COVER,LV_PART_MAIN); lv_obj_set_style_border_width(o,0,LV_PART_MAIN); lv_obj_set_style_radius(o,0,LV_PART_MAIN); lv_obj_set_style_bg_color(o,color,LV_PART_INDICATOR); lv_obj_set_style_bg_opa(o,LV_OPA_COVER,LV_PART_INDICATOR); lv_obj_set_style_radius(o,0,LV_PART_INDICATOR); return o;
}
void rm_set_theme(rm_ui_t *ui, rm_theme_t theme) {
    if(!ui || theme>RM_THEME_AMBER) return; ui->theme=theme; C=palettes[theme];
    if(!ui->root) return;
    lv_obj_set_style_bg_color(ui->root,C.bg,0);
    /* Theme-sensitive elements are refreshed in place to keep the API small. */
    lv_obj_set_style_text_color(ui->cpu_value,C.white,0); lv_obj_set_style_text_color(ui->cpu_temp,C.lime,0);
    lv_obj_set_style_text_color(ui->cpu_clock,C.white,0); lv_obj_set_style_text_color(ui->ram_percent,C.lime,0);
    lv_obj_set_style_text_color(ui->net_rx,C.accent,0); lv_obj_set_style_text_color(ui->net_tx,C.lime,0);
    lv_obj_set_style_bg_color(ui->cpu_arc,C.accent,LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ui->ram_bar,C.lime,LV_PART_INDICATOR); lv_obj_set_style_bg_color(ui->disk_bar,C.accent,LV_PART_INDICATOR);
    if(ui->net_chart){lv_chart_set_series_color(ui->net_chart,lv_chart_get_series_next(ui->net_chart,NULL),C.lime);lv_chart_set_series_color(ui->net_chart,lv_chart_get_series_next(ui->net_chart,lv_chart_get_series_next(ui->net_chart,NULL)),C.accent);}
    for(int i=0;i<3;i++) if(ui->theme_button[i]) {lv_obj_set_style_bg_color(ui->theme_button[i],i==(int)theme?C.accent:C.panel,0);lv_obj_set_style_border_color(ui->theme_button[i],i==(int)theme?C.accent:C.line,0);lv_obj_set_style_text_color(ui->theme_label[i],i==(int)theme?C.bg:C.white,0);}
}
int rm_create(rm_ui_t *ui, lv_obj_t *parent, rm_theme_t initial_theme) {
    if(!ui || !parent) return -1; memset(ui,0,sizeof(*ui)); ui->theme=initial_theme; C=palettes[initial_theme<=RM_THEME_AMBER?initial_theme:RM_THEME_ICE_LIME];
    ui->root=lv_obj_create(parent); lv_obj_remove_style_all(ui->root); lv_obj_set_size(ui->root,320,240); lv_obj_set_pos(ui->root,0,0); lv_obj_set_style_bg_color(ui->root,C.bg,0); lv_obj_set_style_bg_opa(ui->root,LV_OPA_COVER,0); RM_CLEAR_FLAG(ui->root,LV_OBJ_FLAG_SCROLLABLE);
    /* Header */
    lv_obj_t *head=panel(ui->root,9,8,302,25); lv_obj_set_style_bg_color(head,lv_color_hex(0x0a1217),0);
    lv_obj_t *mark=lv_obj_create(head); lv_obj_remove_style_all(mark); lv_obj_set_pos(mark,7,5); lv_obj_set_size(mark,3,13); lv_obj_set_style_bg_color(mark,C.accent,0); lv_obj_set_style_bg_opa(mark,LV_OPA_COVER,0);
    label(head,"SYSTEM RESEARCH",16,5,font_mid(),C.white); label(head,"/ NODE 01",112,8,font_small(),C.dim);
    lv_obj_t *online=lv_obj_create(head); lv_obj_remove_style_all(online); lv_obj_set_size(online,5,5); lv_obj_align(online,LV_ALIGN_RIGHT_MID,-65,0); lv_obj_set_style_radius(online,LV_RADIUS_CIRCLE,0); lv_obj_set_style_bg_color(online,C.lime,0); lv_obj_set_style_bg_opa(online,LV_OPA_COVER,0);
    label(head,"ONLINE",244,7,font_small(),C.lime); label(head,"14:27",270,7,font_small(),C.white);
    /* CPU card and load arc */
    lv_obj_t *cpu=panel(ui->root,9,38,137,110); label(cpu,"CPU",8,5,font_mid(),C.accent); label(cpu,"AMD RYZEN 7",36,7,font_small(),C.dim);
    ui->cpu_arc=lv_arc_create(cpu); lv_obj_set_pos(ui->cpu_arc,8,20); lv_obj_set_size(ui->cpu_arc,78,78); lv_arc_set_bg_angles(ui->cpu_arc,135,45); lv_arc_set_angles(ui->cpu_arc,135,320); lv_arc_set_range(ui->cpu_arc,0,100); lv_arc_set_value(ui->cpu_arc,68); lv_obj_set_style_arc_width(ui->cpu_arc,6,LV_PART_MAIN); lv_obj_set_style_arc_color(ui->cpu_arc,lv_color_hex(0x1a2c33),LV_PART_MAIN); lv_obj_set_style_arc_width(ui->cpu_arc,6,LV_PART_INDICATOR); lv_obj_set_style_arc_color(ui->cpu_arc,C.accent,LV_PART_INDICATOR); lv_obj_set_style_bg_opa(ui->cpu_arc,LV_OPA_TRANSP,LV_PART_KNOB); lv_obj_set_style_border_width(ui->cpu_arc,0,LV_PART_KNOB); RM_CLEAR_FLAG(ui->cpu_arc,LV_OBJ_FLAG_CLICKABLE);
    ui->cpu_value=label(cpu,"68",26,40,font_big(),C.white); label(cpu,"% LOAD",31,70,font_small(),C.accent);
    ui->cpu_temp=label(cpu,"64°C",94,35,font_mid(),C.lime); label(cpu,"CORE TEMP",94,50,font_small(),C.dim);
    ui->cpu_clock=label(cpu,"4.72 GHz",94,68,font_small(),C.white); label(cpu,"CLOCK",94,81,font_small(),C.dim);
    /* RAM card */
    lv_obj_t *ram=panel(ui->root,151,38,160,51); label(ram,"SYSTEM MEMORY",7,5,font_mid(),C.white); label(ram,"DDR5",7,20,font_small(),C.dim);
    ui->ram_value=label(ram,"12.4 / 32 GB",73,5,font_small(),C.white); ui->ram_percent=label(ram,"39%",134,20,font_small(),C.lime); ui->ram_bar=bar(ram,7,37,146,7,C.lime);
    /* Network card and small native chart */
    lv_obj_t *net=panel(ui->root,151,95,160,53); label(net,"NETWORK",7,4,font_mid(),C.white); label(net,"/ REAL TIME",52,7,font_small(),C.dim);
    ui->net_rx=label(net,"↓ 84.2 Mbps",7,17,font_small(),C.accent); ui->net_tx=label(net,"↑ 12.8 Mbps",82,17,font_small(),C.lime);
    ui->net_chart=lv_chart_create(net); lv_obj_set_pos(ui->net_chart,7,30); lv_obj_set_size(ui->net_chart,146,18); lv_chart_set_type(ui->net_chart,LV_CHART_TYPE_LINE); lv_chart_set_point_count(ui->net_chart,32); lv_chart_set_range(ui->net_chart,LV_CHART_AXIS_PRIMARY_Y,0,100); lv_chart_set_div_line_count(ui->net_chart,0,3); lv_obj_set_style_bg_opa(ui->net_chart,LV_OPA_TRANSP,0); lv_obj_set_style_border_width(ui->net_chart,0,0); lv_obj_set_style_pad_all(ui->net_chart,0,0); lv_obj_set_style_line_width(ui->net_chart,1,LV_PART_ITEMS); lv_obj_set_style_size(ui->net_chart,0,LV_PART_INDICATOR); lv_obj_set_style_line_color(ui->net_chart,lv_color_hex(0x24404a),LV_PART_MAIN); lv_obj_set_style_line_opa(ui->net_chart,LV_OPA_30,LV_PART_MAIN);
    lv_chart_series_t *rxs=lv_chart_add_series(ui->net_chart,C.accent,LV_CHART_AXIS_PRIMARY_Y); lv_chart_series_t *txs=lv_chart_add_series(ui->net_chart,C.lime,LV_CHART_AXIS_PRIMARY_Y); for(int i=0;i<32;i++){lv_chart_set_next_value(ui->net_chart,rxs,15+(i*17%75));lv_chart_set_next_value(ui->net_chart,txs,5+(i*7%24));}
    /* Disk card */
    lv_obj_t *disk=panel(ui->root,9,155,302,66); label(disk,"NVMe SSD",7,4,font_mid(),C.accent); label(disk,"/ STORAGE TELEMETRY",66,7,font_small(),C.dim); label(disk,"ACTIVE 72%",229,6,font_small(),C.lime);
    ui->disk_bar=bar(disk,7,22,286,6,C.accent);
    ui->disk_read=label(disk,"READ  1.8 GB/s",7,34,font_small(),C.white); ui->disk_write=label(disk,"WRITE 420 MB/s",105,34,font_small(),C.white); ui->disk_temp=label(disk,"49°C",229,34,font_small(),C.lime);
    label(ui->root,"TELEMETRY // 14:27",11,225,font_small(),C.dim); label(ui->root,"PCIe 4.0 · LINK OK",232,225,font_small(),C.dim);
    ui->data=(rm_data_t){68,64,4720,12400,32000,842,128,72,1843,420,49}; rm_set_data(ui,&ui->data); rm_set_theme(ui,initial_theme); return 0;
}

void rm_set_data(rm_ui_t *ui, const rm_data_t *d) {
    if(!ui||!d||!ui->root) return; ui->data=*d;
    char s[32]; uint8_t cpu=d->cpu_percent>100?100:d->cpu_percent; uint8_t disk=d->disk_active_percent>100?100:d->disk_active_percent;
    lv_arc_set_value(ui->cpu_arc,cpu); snprintf(s,sizeof(s),"%u",cpu); lv_label_set_text(ui->cpu_value,s);
    snprintf(s,sizeof(s),"%d°C",(int)d->cpu_temp_c); lv_label_set_text(ui->cpu_temp,s);
    snprintf(s,sizeof(s),"%.2f GHz",d->cpu_mhz/1000.0f); lv_label_set_text(ui->cpu_clock,s);
    uint16_t total=d->ram_total_mb?d->ram_total_mb:1, used=d->ram_used_mb>total?total:d->ram_used_mb; uint8_t rp=(uint32_t)used*100/total;
    snprintf(s,sizeof(s),"%.1f / %.1f GB",used/1000.0f,total/1000.0f); lv_label_set_text(ui->ram_value,s); snprintf(s,sizeof(s),"%u%%",rp); lv_label_set_text(ui->ram_percent,s); lv_bar_set_value(ui->ram_bar,rp,LV_ANIM_OFF);
    snprintf(s,sizeof(s),"↓ %.1f Mbps",d->net_rx_mbps_x10/10.0f); lv_label_set_text(ui->net_rx,s); snprintf(s,sizeof(s),"↑ %.1f Mbps",d->net_tx_mbps_x10/10.0f); lv_label_set_text(ui->net_tx,s);
    uint16_t read=d->disk_read_mbps,write=d->disk_write_mbps;
    if(read>=1000) snprintf(s,sizeof(s),"READ %.2f GB/s",read/1000.0f); else snprintf(s,sizeof(s),"READ %u MB/s",read); lv_label_set_text(ui->disk_read,s);
    snprintf(s,sizeof(s),"WRITE %u MB/s",write); lv_label_set_text(ui->disk_write,s); snprintf(s,sizeof(s),"%d°C",(int)d->disk_temp_c); lv_label_set_text(ui->disk_temp,s); lv_bar_set_value(ui->disk_bar,disk,LV_ANIM_OFF);
    /* Temperature threshold uses red while keeping other data unchanged. */
    lv_obj_set_style_text_color(ui->cpu_temp,d->cpu_temp_c>=85?C.red:C.lime,0); lv_obj_set_style_text_color(ui->disk_temp,d->disk_temp_c>=70?C.red:C.lime,0);
}

void rm_chart_push(rm_ui_t *ui, uint16_t rx, uint16_t tx) {
    if(!ui||!ui->net_chart) return; uint16_t a=rx>100?100:rx,b=tx>100?100:tx;
    lv_chart_series_t *first=lv_chart_get_series_next(ui->net_chart,NULL); lv_chart_series_t *second=lv_chart_get_series_next(ui->net_chart,first);
    if(first&&second){lv_chart_set_next_value(ui->net_chart,first,a);lv_chart_set_next_value(ui->net_chart,second,b);lv_chart_refresh(ui->net_chart);}
}
