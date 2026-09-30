#pragma once
// History page for one metric: current value, the last 60 s as an anti-aliased graph, and
// MIN / AVG / MAX. Opened by tapping a panel on the dashboard; the arrows step through
// CPU load, CPU temperature, memory, GPU, network and NVMe activity (GPU is only reachable
// from here - the dashboard layout has no GPU panel).
#include <stdint.h>
#include "ui/monitor_model.h"
#include "ui/monitor_ui.h"

namespace detail_ui {

enum Action : uint8_t { ACT_NONE, ACT_BACK, ACT_METRIC };   // ACT_METRIC: `metric` was changed

int  metricForZone(monitor_ui::Zone z);                     // panel tapped -> Metric
void draw(const MonitorView &v, int metric);                // full repaint
// Changes whenever draw() would produce a different picture (new sample, link change...)
uint32_t contentKey(const MonitorView &v, int metric);
Action tap(int x, int y, int &metric);

}  // namespace detail_ui
