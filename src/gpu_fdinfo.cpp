#include "gpu_fdinfo.h"
namespace fs = ghc::filesystem;

std::string GPU_fdinfo::get_drm_engine_type() {
    std::string drm_type = "drm-engine-";

    if (strstr(module, "amdgpu"))
        drm_type += "gfx";
    else if (strstr(module, "i915"))
        drm_type += "render";
    else if (strstr(module, "msm"))
        drm_type += "gpu";
    else
        drm_type += "none";

    return drm_type;
}

void GPU_fdinfo::find_fd() {
    DIR* dir = opendir("/proc/self/fdinfo");
    if (!dir) {
        perror("Failed to open directory");
    }

    for (const auto& entry : fs::directory_iterator("/proc/self/fdinfo")){
        FILE* file = fopen(entry.path().string().c_str(), "r");

        if (!file) continue;

        char line[256];
        bool found_driver = false;
        while (fgets(line, sizeof(line), file)) {
            if (strstr(line, module) != NULL)
                found_driver = true;

            if (found_driver) {
                if(strstr(line, get_drm_engine_type().c_str())) {
                    fdinfo.push_back(file);
                    break;
                }
            }
        }

        if (!found_driver)
            fclose(file);
    }

    closedir(dir);
}

uint64_t GPU_fdinfo::get_gpu_time() {
    char line[256];
    uint64_t total_val = 0;
    for (auto fd : fdinfo) {
        rewind(fd);
        fflush(fd);
        uint64_t val = 0;
        while (fgets(line, sizeof(line), fd)){
            std::string scan_str = get_drm_engine_type() + ": %" SCNu64 " ns";

            if (sscanf(line, scan_str.c_str(), &val) == 1) {
                total_val += val;
                break;
            }
        }
    }

    return total_val;
}

float GPU_fdinfo::get_vram_usage() {
    char line[256];
    float total_val = 0;

    for (auto fd : fdinfo) {
        rewind(fd);
        fflush(fd);

        uint64_t val = 0;

        while (fgets(line, sizeof(line), fd)){
            std::string scan_str = "drm-total-local0: %llu KiB";

            if (sscanf(line, scan_str.c_str(), &val) == 1) {
                total_val += val;
                break;
            }
        }
    }

    return (float)(total_val / 1024 / 1024);
}

void GPU_fdinfo::find_hwmon() {
    std::string hwmon_dir = "/sys/class/hwmon";
    bool found_gpu = false;

    for (const auto& entry : fs::directory_iterator(hwmon_dir)) {
        std::string hwmon = entry.path().filename().string();
        auto name_stream = std::ifstream(hwmon_dir + "/" + hwmon + "/name");
        std::string name;

        std::getline(name_stream, name);

        if (name == "i915") {
            found_gpu = true;
            hwmon_dir += "/" + hwmon;
            break;
        }
    }

    if (found_gpu)
        energy_stream.open(hwmon_dir + "/energy1_input");
}

float GPU_fdinfo::get_power_usage() {
    if (!energy_stream.is_open())
        return 0.f;

    std::string energy_input_str;
    uint64_t energy_input;

    energy_stream.seekg(0);
    std::getline(energy_stream, energy_input_str);
    energy_input = std::stoull(energy_input_str);

    return energy_input / 1'000'000;
}

void GPU_fdinfo::get_load() {
    while (!stop_thread) {
        std::unique_lock<std::mutex> lock(metrics_mutex);
        cond_var.wait(lock, [this]() { return !paused || stop_thread; });

        static uint64_t previous_gpu_time, previous_time, now, gpu_time_now;
        static float power_usage_now, previous_power_usage;

        gpu_time_now = get_gpu_time();
        power_usage_now = get_power_usage();
        now = os_time_get_nano();

        if (gpu_time_now > previous_gpu_time &&
            now - previous_time > METRICS_UPDATE_PERIOD_MS * 1'000'000)
        {
            float time_since_last = now - previous_time;
            float gpu_since_last = gpu_time_now - previous_gpu_time;
            float power_usage_since_last = power_usage_now - previous_power_usage;

            auto result = int((gpu_since_last / time_since_last) * 100);
            if (result > 100)
                result = 100;

            metrics.load = result;
            metrics.memoryUsed = get_vram_usage();
            metrics.powerUsage = power_usage_since_last;

            previous_gpu_time = gpu_time_now;
            previous_time = now;
            previous_power_usage = power_usage_now;
        }
    }
}
