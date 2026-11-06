#include "core/app.h"
#include "core/log.h"
#include "core/profiler.h"
#include "game/game.h"
#include "js/js_game.h"
#include "net/http_client.h"
#include "content/vfs.h"
#include "content/zipreader.hpp"
#include "platform/platform.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(GAME_PLATFORM_WIN)
#include <windows.h>
#elif defined(GAME_PLATFORM_LINUX) || defined(GAME_PLATFORM_MACOSX)
#include <cstdlib>
#include <unistd.h>
#include <SDL.h>
#if defined(GAME_PLATFORM_MACOSX)
#include <mach-o/dyld.h>
#endif
#endif

#if defined(GAME_PLATFORM_WIN) || defined(GAME_PLATFORM_LINUX) || defined(GAME_PLATFORM_MACOSX)
#define GAME_UPDATER_DESKTOP 1
#endif

struct app_updater_module_t {
    void download_latest_version(pcstr url);
};

namespace {

pcstr updater_default_url() {
#if defined(GAME_PLATFORM_WIN)
    return "https://nightly.link/dalerank/Akhenaten/workflows/akhenaten_windows/master/windows_build.zip";
#elif defined(GAME_PLATFORM_LINUX)
    return "https://nightly.link/dalerank/Akhenaten/workflows/akhenaten_linux/master/linux_build.zip";
#elif defined(GAME_PLATFORM_MACOSX)
#if defined(__aarch64__) || defined(__arm64__)
    return "https://nightly.link/dalerank/Akhenaten/workflows/akhenaten_mac_arm/master/macos_arm_build.zip";
#else
    return "https://nightly.link/dalerank/Akhenaten/workflows/akhenaten_mac_x64/master/macos_x64_build.zip";
#endif
#else
    return nullptr;
#endif
}

#ifdef GAME_UPDATER_DESKTOP

std::string updater_normalize_slashes(std::string path) {
    for (char &c : path) {
        if (c == '\\') {
            c = '/';
        }
    }
    return path;
}

std::string updater_exe_path() {
#if defined(GAME_PLATFORM_WIN)
    char exe_path[MAX_PATH] = { 0 };
    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    return updater_normalize_slashes(exe_path);
#elif defined(GAME_PLATFORM_LINUX)
    char buf[4096] = { 0 };
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        return updater_normalize_slashes(buf);
    }
    char *base = SDL_GetBasePath();
    if (!base) {
        return {};
    }
    std::string path = updater_normalize_slashes(base);
    SDL_free(base);
    if (!path.empty() && path.back() != '/') {
        path += '/';
    }
    path += "akhenaten.linux";
    return path;
#elif defined(GAME_PLATFORM_MACOSX)
    char buf[4096] = { 0 };
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        return updater_normalize_slashes(buf);
    }
    char *base = SDL_GetBasePath();
    if (!base) {
        return {};
    }
    std::string path = updater_normalize_slashes(base);
    SDL_free(base);
    const size_t resources = path.rfind("/Contents/Resources");
    if (resources != std::string::npos) {
        return path.substr(0, resources) + "/Contents/MacOS/akhenaten";
    }
    if (!path.empty() && path.back() != '/') {
        path += '/';
    }
    path += "akhenaten";
    return path;
#else
    return {};
#endif
}

std::string updater_exe_dir() {
    const std::string path = updater_exe_path();
    const size_t slash = path.find_last_of('/');
    return (slash == std::string::npos) ? path : path.substr(0, slash);
}

// Directory that receives staged files (parent of .app on macOS).
std::string updater_install_dir() {
    const std::string exe = updater_exe_path();
    const size_t app = exe.find(".app/");
    if (app != std::string::npos) {
        const size_t slash = exe.rfind('/', app);
        if (slash != std::string::npos) {
            return exe.substr(0, slash);
        }
    }
    return updater_exe_dir();
}

std::string updater_helper_name() {
#if defined(GAME_PLATFORM_WIN)
    return "akhenaten-updater.exe";
#else
    return "akhenaten-updater";
#endif
}

std::string updater_helper_path() {
    return updater_exe_dir() + "/" + updater_helper_name();
}

std::string updater_restart_target() {
#if defined(GAME_PLATFORM_MACOSX)
    const std::string exe = updater_exe_path();
    const size_t app = exe.find(".app/");
    if (app != std::string::npos) {
        return exe.substr(0, app + 4); // include ".app"
    }
#endif
    return updater_exe_path();
}

#if defined(GAME_PLATFORM_WIN)
std::string updater_to_native(std::string path) {
    for (char &c : path) {
        if (c == '/') {
            c = '\\';
        }
    }
    return path;
}
#endif

bool updater_write_file(const std::string &path, const void *data, size_t size) {
    FILE *f = vfs::file_open_os(path.c_str(), "wb");
    if (!f) {
        logs::error("[updater] cannot create file %s", path.c_str());
        return false;
    }
    const size_t written = (size > 0) ? fwrite(data, 1, size, f) : 0;
    fclose(f);
    if (written != size) {
        logs::error("[updater] short write to %s (%zu/%zu)", path.c_str(), written, size);
        return false;
    }
    return true;
}

bool updater_download(const std::string &zip_path, const std::string &url) {
    logs::info("[updater] downloading from %s", url.c_str());
    const http_get_result response = http_get(url.c_str(), 120, false);
    if (!response.ok || response.body.empty()) {
        logs::error("[updater] download failed (HTTP code: %ld)", response.http_code);
        return false;
    }

    if (!updater_write_file(zip_path, response.body.data(), response.body.size())) {
        return false;
    }

    logs::info("[updater] downloaded %zu bytes -> %s", response.body.size(), zip_path.c_str());
    return true;
}

bool updater_extract(const std::string &zip_path, const std::string &staging_dir) {
    vfs::ZipArchive archive(vfs::path(zip_path.c_str()));
    if (!archive.isValid()) {
        logs::error("[updater] invalid or empty archive %s", zip_path.c_str());
        return false;
    }

    const std::vector<xstring> &entries = archive.entries();
    int extracted = 0;
    for (unsigned int i = 0; i < entries.size(); ++i) {
        pcstr name = entries[i].c_str();
        if (!name || name[0] == 0) {
            continue;
        }

        const size_t len = std::strlen(name);
        if (name[len - 1] == '/' || name[len - 1] == '\\') {
            continue;
        }

        vfs::reader r = archive.createAndOpenFile(i, "rb");
        if (!r) {
            logs::warn("[updater] failed to decompress %s", name);
            continue;
        }

        const std::string out_path = staging_dir + "/" + name;
        const size_t slash = out_path.find_last_of('/');
        if (slash != std::string::npos) {
            vfs::create_folders(out_path.substr(0, slash).c_str());
        }

        if (!updater_write_file(out_path, r->data(), (size_t)r->size())) {
            return false;
        }
        ++extracted;
    }

    if (extracted == 0) {
        logs::error("[updater] no files extracted from %s", zip_path.c_str());
        return false;
    }

    logs::info("[updater] extracted %d files -> %s", extracted, staging_dir.c_str());
    return true;
}

bool updater_spawn_swap_and_restart(const std::string &work_dir, const std::string &staging_dir, const std::string &zip_path) {
    const std::string helper_src = updater_helper_path();
    if (!std::filesystem::exists(helper_src)) {
        logs::error("[updater] helper not found: %s", helper_src.c_str());
        return false;
    }

#if defined(GAME_PLATFORM_WIN)
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = (unsigned long)getpid();
#endif

    // Keep the running helper outside update_tmp so --cleanup can wipe staging safely.
    std::error_code ec;
    const std::string helper_dir = (std::filesystem::temp_directory_path(ec) / ("akhenaten-apply-" + std::to_string(pid))).string();
    const std::string helper_run = helper_dir + "/" + updater_helper_name();
    std::filesystem::create_directories(helper_dir, ec);
    std::filesystem::copy_file(helper_src, helper_run, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        logs::error("[updater] cannot stage helper: %s", ec.message().c_str());
        return false;
    }
#if !defined(GAME_PLATFORM_WIN)
    std::filesystem::permissions(helper_run,
        std::filesystem::perms::owner_all | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add,
        ec);
#endif

    const std::string install_dir = updater_install_dir();
    const std::string restart = updater_restart_target();

    // Drop zip before swap; staging stays until helper finishes copy.
    std::filesystem::remove(zip_path, ec);

#if defined(GAME_PLATFORM_WIN)
    const std::string n_helper = updater_to_native(helper_run);
    const std::string n_staging = updater_to_native(staging_dir);
    const std::string n_install = updater_to_native(install_dir);
    const std::string n_restart = updater_to_native(restart);
    const std::string n_cleanup = updater_to_native(work_dir);

    std::string cmd = "\"" + n_helper + "\"";
    cmd += " --pid " + std::to_string(pid);
    cmd += " --staging \"" + n_staging + "\"";
    cmd += " --install \"" + n_install + "\"";
    cmd += " --restart \"" + n_restart + "\"";
    cmd += " --cleanup \"" + n_cleanup + "\"";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> cmdline(cmd.begin(), cmd.end());
    cmdline.push_back('\0');

    if (!CreateProcessA(n_helper.c_str(), cmdline.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, n_install.c_str(), &si, &pi)) {
        logs::error("[updater] CreateProcess failed (%lu)", GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
#elif defined(GAME_PLATFORM_IOS) || defined(IOS_BUILD)
    (void)helper_run;
    (void)staging_dir;
    (void)install_dir;
    (void)restart;
    (void)work_dir;
    (void)pid;
    return false;
#else
        + " --pid " + std::to_string(pid)
        + " --staging \"" + staging_dir + "\""
        + " --install \"" + install_dir + "\""
        + " --restart \"" + restart + "\""
        + " --cleanup \"" + work_dir + "\""
        + " >/dev/null 2>&1 &";
    if (::system(cmd.c_str()) != 0) {
        logs::error("[updater] failed to launch helper");
        return false;
    }
#endif

    logs::info("[updater] helper launched, waiting for game to exit");
    return true;
}

void updater_open_url_fallback(const std::string &url) {
    game.add_frame_end_event([url]() {
        platform.open_url(url.c_str(), "");
    });
}

void updater_run(const std::string &url) {
    const std::string work_dir = updater_install_dir() + "/update_tmp";
    const std::string staging_dir = work_dir + "/staging";
    const std::string zip_path = work_dir + "/update_build.zip";

    std::error_code ec;
    std::filesystem::remove_all(work_dir, ec);
    vfs::create_folders(staging_dir.c_str());

    if (!updater_download(zip_path, url)) {
        updater_open_url_fallback(url);
        return;
    }

    if (!updater_extract(zip_path, staging_dir)) {
        updater_open_url_fallback(url);
        return;
    }

    if (!updater_spawn_swap_and_restart(work_dir, staging_dir, zip_path)) {
        updater_open_url_fallback(url);
        return;
    }

    game.add_frame_end_event([]() {
        app_post_event(USER_EVENT_QUIT);
    });
}

#endif // GAME_UPDATER_DESKTOP

} // namespace

void app_updater_module_t::download_latest_version(pcstr url) {
    std::string url_str = (url && *url) ? url : (updater_default_url() ? updater_default_url() : "");
    if (url_str.empty()) {
        logs::error("[updater] no update URL for this platform");
        return;
    }

#ifdef GAME_UPDATER_DESKTOP
    game.mt.detach_task([url_str]() {
        updater_run(url_str);
    });
#else
    platform.open_url(url_str.c_str(), "");
#endif
}

bool __platform_can_auto_update() {
#ifdef GAME_UPDATER_DESKTOP
    return true;
#else
    return false;
#endif
}
ANK_FUNCTION(__platform_can_auto_update)

void __game_download_latest_version(pcstr url) {
    static app_updater_module_t mod;
    mod.download_latest_version(url);
}
ANK_FUNCTION_1(__game_download_latest_version)

void ANK_REGISTER_APPLICATION_MODULE(register_app_updater_module) {
}
