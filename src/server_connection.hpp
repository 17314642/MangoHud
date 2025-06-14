#pragma once

#include <mutex>
#include <set>
#include "../../MangoHud-2/common/gpu_metrics.hpp"

bool get_active_gpu(const mangohud_message& msg, uint8_t& out_idx);
std::set<uint8_t> selected_gpus(const mangohud_message& msg);
std::string get_gpu_text(const mangohud_message& msg, uint8_t idx);
void setup_connection_to_server();

extern std::mutex g_metrics_lock;
extern mangohud_message g_metrics;
