// Screen geometry shared by the art (tools/gen_chrome.py) and the renderer - do not edit.
#pragma once

struct Rect { int x, y, w, h; };

static const Rect L_HEADER = { 9, 8, 302, 25 };
static const Rect L_CPU = { 9, 38, 137, 110 };
static const Rect L_RAM = { 151, 38, 160, 51 };
static const Rect L_NET = { 151, 95, 160, 53 };
static const Rect L_DISK = { 9, 155, 302, 66 };
static const Rect L_NODE = { 154, 12, 50, 18 };
static const Rect L_STATUS = { 206, 12, 46, 18 };
static const Rect L_CLOCK = { 256, 12, 50, 18 };
static const Rect L_CPU_NAME = { 44, 42, 98, 13 };
static const Rect L_CPU_RING = { 16, 60, 66, 66 };
static const Rect L_CPU_TEMP = { 82, 60, 60, 22 };
static const Rect L_CPU_ROWS = { 82, 84, 60, 42 };
static const Rect L_CPU_CORES = { 15, 129, 126, 15 };
static const Rect L_RAM_LIVE = { 155, 41, 152, 45 };
static const Rect L_NET_RATES = { 155, 110, 152, 15 };
static const Rect L_NET_GRAPH = { 158, 126, 146, 19 };
static const Rect L_DISK_NAME = { 78, 158, 228, 12 };
static const Rect L_DISK_RING = { 21, 175, 44, 44 };
static const Rect L_DISK_IO = { 80, 174, 228, 44 };
static const Rect L_FOOT_L = { 76, 225, 100, 12 };
static const Rect L_FOOT_R = { 176, 225, 88, 12 };
static const Rect L_SETUP = { 268, 223, 43, 14 };
static const Rect L_BANNER = { 50, 82, 220, 64 };

#define P_CPU_RING_CX 49
#define P_CPU_RING_CY 93
#define P_CPU_RING_R 31
#define P_CPU_RING_W 6
#define P_DISK_RING_CX 43
#define P_DISK_RING_CY 197
#define P_DISK_RING_R 20
#define P_DISK_RING_W 5
#define P_CPU_ROW_Y0 93
#define P_CPU_ROW_DY 10
#define P_CPU_COL_X 83
#define P_CPU_COL_R 142
#define P_NET_GRAPH_N 48
#define P_RAM_SEGS 36
#define P_NET_SUB_X 217
