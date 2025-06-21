#include <chrono>
#include <thread>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <spdlog/spdlog.h>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>

#include "../../mangohud-server/common/socket.hpp"
#include "server_connection.hpp"
#include "hud_elements.h"

std::mutex g_metrics_lock;
mangohud_message g_metrics = {};

static bool create_socket(int& sock) {
    sock = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);

    if (sock < 0) {
        LOG_UNIX_ERRNO_ERROR("Couldn't create socket.");
        return false;
    }

    SPDLOG_DEBUG("Socket created.");
    return true;
}

static bool connect_to_socket(const int sock, const sockaddr_un& addr) {
    int ret = connect(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(sockaddr_un));

    if (ret < 0) {
        LOG_UNIX_ERRNO_ERROR("Failed to connect to server socket.");
        return false;
    }

    SPDLOG_INFO("Connected to server");
    return true;
}

static void poll_server(pollfd& server_poll, bool& connected, int sock) {
    mangohud_message msg = {};
    send_message(server_poll.fd, msg);

    int ret = poll(&server_poll, 1, 1000);

    if (ret < 0) {
        LOG_UNIX_ERRNO_ERROR("poll() failed.");
        return;
    }

    // no new data
    if (ret < 1)
        return;

    // Check events
    if (server_poll.revents == 0)
        return;

    SPDLOG_TRACE("fd {}: revents = {}", server_poll.fd, server_poll.revents);

    if (server_poll.revents & POLLHUP || server_poll.revents & POLLNVAL) {
        connected = false;
        
        {
            std::unique_lock<std::mutex> lock(g_metrics_lock);
            g_metrics = {};
        }

        close(sock);
        return;
    }

    if (!receive_message(server_poll.fd, msg)) {
        SPDLOG_DEBUG("Failed to receive message from server.");
        return;
    }

    {
        std::unique_lock<std::mutex> lock(g_metrics_lock);
        g_metrics = msg;
    }
}

static void client_thread () {
    std::string socket_path = get_socket_path();

    if (socket_path.empty())
        return; 

    SPDLOG_INFO("Socket path: {}", socket_path);

    const sockaddr_un addr = {
        .sun_family = AF_UNIX
    };

    std::strncpy(const_cast<char*>(addr.sun_path), socket_path.c_str(), socket_path.size());

    int sock = 0;
    pollfd server_poll = { .fd = sock, .events = POLLIN };

    std::chrono::time_point<std::chrono::steady_clock> previous_time;

    bool connected = false;

    using namespace std::chrono_literals;

    while (true) {
        if (!connected) {
            SPDLOG_WARN("Lost connection to server. Retrying...");

            if (!create_socket(sock)) {
                std::this_thread::sleep_for(1s);
                continue;
            }

            if (!connect_to_socket(sock, addr)) {
                close(sock);
                std::this_thread::sleep_for(1s);
                continue;
            }

            server_poll.fd = sock;
            connected = true;
        }

        poll_server(server_poll, connected, sock);
        std::this_thread::sleep_for(1s);
    }

    return;
}

void setup_connection_to_server() {
    spdlog::set_level(spdlog::level::level_enum::debug);

    SPDLOG_DEBUG("setup_connection_to_server()");

    std::thread t = std::thread(&client_thread);
    pthread_setname_np(t.native_handle(), "mnghud-srv-conn");
    t.detach();
}

bool get_active_gpu(const mangohud_message& msg, uint8_t& out_idx) {
    if (msg.num_of_gpus < 1)
        return false;

    for (uint8_t i = 0; i < msg.num_of_gpus; i++) {
        if (msg.gpus[i].is_active) {
            out_idx = i;
            return true;
        }
    }

    // if no active gpu found, return last one 
    out_idx = msg.num_of_gpus - 1;
    return true;
}

std::set<uint8_t> selected_gpus(const mangohud_message& msg) {
    std::set<uint8_t> vec;

    const overlay_params* params = HUDElements.params;
    const std::vector<unsigned>& glist = params->gpu_list;
    // const std::string& pci_dev = params->pci_dev;

    for (uint8_t i = 0; i < msg.num_of_gpus; i++) {
        if (!glist.empty()) {
            std::vector<unsigned>::const_iterator it =
                std::find(glist.begin(), glist.end(), static_cast<unsigned>(i));
    
            if (it == glist.end())
                continue;
        }

        // if (!pci_dev.empty())

        vec.emplace(i);
    }

    return vec;
}

std::string get_gpu_text(const mangohud_message& msg, uint8_t idx) {
    if (HUDElements.params->gpu_text.size() > idx)
        return HUDElements.params->gpu_text[idx];

    if (msg.num_of_gpus < 2)
        return "GPU";

    return "GPU" + std::to_string(idx);
}
