#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <regex>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#include <systemd/sd-json.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-journal.h>
#include <systemd/sd-daemon.h>
#include <systemd/sd-event.h>

// Unique pointer deleters for systemd C structs
struct JsonVariantDeleter {
    void operator()(sd_json_variant *v) const {
        sd_json_variant_unref(v);
    }
};
using UniqueJsonVariant = std::unique_ptr<sd_json_variant, JsonVariantDeleter>;

struct BusDeleter {
    void operator()(sd_bus *b) const {
        sd_bus_unref(b);
    }
};
using UniqueBus = std::unique_ptr<sd_bus, BusDeleter>;

struct EventDeleter {
    void operator()(sd_event *e) const {
        sd_event_unref(e);
    }
};
using UniqueEvent = std::unique_ptr<sd_event, EventDeleter>;

// Struct to manage asynchronous D-Bus event loop context
struct DbusContext {
    sd_event *event = nullptr;
    sd_bus *bus = nullptr;
    bool fix_mode = false;
    bool delete_mode = false;
    std::vector<std::string> profiles;
    size_t current_profile_index = 0;
    int current_step = 0;
};

// Forward declaration of state machine
void advance_state(DbusContext *ctx);

// File helper functions
bool file_exists(const std::string &path) {
    return access(path.c_str(), F_OK) == 0;
}

bool directory_exists(const std::string &path) {
    struct stat sb;
    return stat(path.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode);
}

bool write_file(const std::string &path, const std::string &content, mode_t mode) {
    std::ofstream out(path);
    if (!out) {
        sd_journal_print(LOG_ERR, "Failed to open file for writing: %s", path.c_str());
        return false;
    }
    out << content;
    out.close();
    if (chmod(path.c_str(), mode) < 0) {
        sd_journal_print(LOG_ERR, "Failed to chmod %s: %s", path.c_str(), strerror(errno));
        return false;
    }
    return true;
}

bool touch_file(const std::string &path, mode_t mode) {
    if (file_exists(path)) {
        return true;
    }
    return write_file(path, "", mode);
}

bool move_file(const std::string &src, const std::string &dst) {
    if (!file_exists(src)) {
        return false;
    }
    if (rename(src.c_str(), dst.c_str()) < 0) {
        sd_journal_print(LOG_ERR, "Failed to move file %s to %s: %s", src.c_str(), dst.c_str(), strerror(errno));
        return false;
    }
    sd_journal_print(LOG_INFO, "Moved %s to %s", src.c_str(), dst.c_str());
    return true;
}

bool validate_profile_name(const std::string &name) {
    if (name.empty() || name.length() > 15) {
        sd_journal_print(LOG_ERR, "Profile name '%s' invalid: length must be between 1 and 15 characters.", name.c_str());
        return false;
    }
    std::regex re("^[a-zA-Z0-9_]+$");
    if (!std::regex_match(name, re)) {
        sd_journal_print(LOG_ERR, "Profile name '%s' invalid: must contain only alphanumeric characters and underscores.", name.c_str());
        return false;
    }
    return true;
}

// Asynchronous D-Bus reply handler
static int on_dbus_reply(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    DbusContext *ctx = static_cast<DbusContext*>(userdata);

    if (ret_error && sd_bus_error_is_set(ret_error)) {
        sd_journal_print(LOG_ERR, "D-Bus method error: %s (%s)", ret_error->message, ret_error->name);
        sd_notifyf(0, "BUSERROR=%s\nSTATUS=D-Bus Error: %s", ret_error->name, ret_error->message);
        sd_event_exit(ctx->event, -EIO);
        return 0;
    }

    const sd_bus_error *err = sd_bus_message_get_error(m);
    if (err && sd_bus_error_is_set(err)) {
        // In delete mode, NoSuchUnit errors are safely ignored
        if (ctx->delete_mode && (strcmp(err->name, "org.freedesktop.systemd1.NoSuchUnit") == 0 ||
                                 strcmp(err->name, "org.freedesktop.systemd1.NoSuchUnitFile") == 0 ||
                                 strcmp(err->name, "org.freedesktop.systemd1.LoadFailed") == 0)) {
            // Ignored, proceed
        } else {
            sd_journal_print(LOG_ERR, "D-Bus reply failed: %s (%s)", err->message, err->name);
            sd_notifyf(0, "BUSERROR=%s\nSTATUS=D-Bus Error: %s", err->name, err->message);
            sd_event_exit(ctx->event, -EIO);
            return 0;
        }
    }

    ctx->current_step++;
    advance_state(ctx);
    return 0;
}

// State machine progression
void advance_state(DbusContext *ctx) {
    if (ctx->delete_mode) {
        std::string X = ctx->profiles[0];
        int r = 0;
        switch (ctx->current_step) {
            case 0:
                sd_journal_print(LOG_INFO, "Stopping path monitor: yt-dlp@%s.path", X.c_str());
                sd_notifyf(0, "STATUS=Stopping path monitor: yt-dlp@%s.path", X.c_str());
                r = sd_bus_call_method_async(ctx->bus, nullptr,
                                             "org.freedesktop.systemd1",
                                             "/org/freedesktop/systemd1",
                                             "org.freedesktop.systemd1.Manager",
                                             "StopUnit",
                                             on_dbus_reply, ctx,
                                             "ss", ("yt-dlp@" + X + ".path").c_str(), "replace");
                break;
            case 1:
                sd_journal_print(LOG_INFO, "Stopping timer: yt-dlp@%s.timer", X.c_str());
                sd_notifyf(0, "STATUS=Stopping timer: yt-dlp@%s.timer", X.c_str());
                r = sd_bus_call_method_async(ctx->bus, nullptr,
                                             "org.freedesktop.systemd1",
                                             "/org/freedesktop/systemd1",
                                             "org.freedesktop.systemd1.Manager",
                                             "StopUnit",
                                             on_dbus_reply, ctx,
                                             "ss", ("yt-dlp@" + X + ".timer").c_str(), "replace");
                break;
            case 2:
                sd_journal_print(LOG_INFO, "Stopping VPN tunnel namespace service: yt-dlp-vpn@%s.service", X.c_str());
                sd_notifyf(0, "STATUS=Stopping VPN service: yt-dlp-vpn@%s.service", X.c_str());
                r = sd_bus_call_method_async(ctx->bus, nullptr,
                                             "org.freedesktop.systemd1",
                                             "/org/freedesktop/systemd1",
                                             "org.freedesktop.systemd1.Manager",
                                             "StopUnit",
                                             on_dbus_reply, ctx,
                                             "ss", ("yt-dlp-vpn@" + X + ".service").c_str(), "replace");
                break;
            case 3:
                sd_journal_print(LOG_INFO, "Disabling path and timer units: %s", X.c_str());
                sd_notifyf(0, "STATUS=Disabling path and timer units for %s", X.c_str());
                r = sd_bus_call_method_async(ctx->bus, nullptr,
                                             "org.freedesktop.systemd1",
                                             "/org/freedesktop/systemd1",
                                             "org.freedesktop.systemd1.Manager",
                                             "DisableUnitFiles",
                                             on_dbus_reply, ctx,
                                             "asb", 2, ("yt-dlp@" + X + ".path").c_str(), ("yt-dlp@" + X + ".timer").c_str(), 0);
                break;
            case 4:
                sd_journal_print(LOG_INFO, "Executing systemd daemon-reload");
                sd_notify(0, "STATUS=Executing systemd daemon-reload...");
                r = sd_bus_call_method_async(ctx->bus, nullptr,
                                             "org.freedesktop.systemd1",
                                             "/org/freedesktop/systemd1",
                                             "org.freedesktop.systemd1.Manager",
                                             "Reload",
                                             on_dbus_reply, ctx,
                                             "");
                break;
            default:
                sd_journal_print(LOG_INFO, "Profile %s services disabled and stopped", X.c_str());
                sd_notifyf(0, "STATUS=Profile %s services disabled and stopped successfully", X.c_str());
                sd_event_exit(ctx->event, 0);
                return;
        }
        if (r < 0) {
            sd_journal_print(LOG_ERR, "Failed to invoke systemd D-Bus API: %s", strerror(-r));
            sd_notifyf(0, "ERRNO=%d\nSTATUS=Failed to invoke systemd D-Bus API", -r);
            sd_event_exit(ctx->event, r);
        }
    } else {
        // Standard / Fix Mode
        if (ctx->current_profile_index >= ctx->profiles.size()) {
            sd_event_exit(ctx->event, 0);
            return;
        }

        std::string X = ctx->profiles[ctx->current_profile_index];
        int r = 0;
        switch (ctx->current_step) {
            case 0:
                sd_journal_print(LOG_INFO, "Executing systemd daemon-reload for profile %s", X.c_str());
                sd_notify(0, "STATUS=Executing systemd daemon-reload...");
                r = sd_bus_call_method_async(ctx->bus, nullptr,
                                             "org.freedesktop.systemd1",
                                             "/org/freedesktop/systemd1",
                                             "org.freedesktop.systemd1.Manager",
                                             "Reload",
                                             on_dbus_reply, ctx,
                                             "");
                break;
            case 1:
                sd_journal_print(LOG_INFO, "Enabling path and timer units: yt-dlp@%s.path / timer", X.c_str());
                sd_notifyf(0, "STATUS=Enabling systemd units: yt-dlp@%s.path / timer", X.c_str());
                r = sd_bus_call_method_async(ctx->bus, nullptr,
                                             "org.freedesktop.systemd1",
                                             "/org/freedesktop/systemd1",
                                             "org.freedesktop.systemd1.Manager",
                                             "EnableUnitFiles",
                                             on_dbus_reply, ctx,
                                             "asbb", 2, ("yt-dlp@" + X + ".path").c_str(), ("yt-dlp@" + X + ".timer").c_str(), 0, 1);
                break;
            case 2:
                sd_journal_print(LOG_INFO, "Starting path monitor: yt-dlp@%s.path", X.c_str());
                sd_notifyf(0, "STATUS=Starting path monitor: yt-dlp@%s.path", X.c_str());
                r = sd_bus_call_method_async(ctx->bus, nullptr,
                                             "org.freedesktop.systemd1",
                                             "/org/freedesktop/systemd1",
                                             "org.freedesktop.systemd1.Manager",
                                             "StartUnit",
                                             on_dbus_reply, ctx,
                                             "ss", ("yt-dlp@" + X + ".path").c_str(), "replace");
                break;
            case 3:
                sd_journal_print(LOG_INFO, "Starting timer: yt-dlp@%s.timer", X.c_str());
                sd_notifyf(0, "STATUS=Starting timer: yt-dlp@%s.timer", X.c_str());
                r = sd_bus_call_method_async(ctx->bus, nullptr,
                                             "org.freedesktop.systemd1",
                                             "/org/freedesktop/systemd1",
                                             "org.freedesktop.systemd1.Manager",
                                             "StartUnit",
                                             on_dbus_reply, ctx,
                                             "ss", ("yt-dlp@" + X + ".timer").c_str(), "replace");
                break;
            default:
                sd_journal_print(LOG_INFO, "Profile %s services successfully activated", X.c_str());
                sd_notifyf(0, "STATUS=Profile %s was successfully created and systemd enabled", X.c_str());
                // Transition to the next profile
                ctx->current_profile_index++;
                ctx->current_step = 0;
                advance_state(ctx);
                return;
        }
        if (r < 0) {
            sd_journal_print(LOG_ERR, "Failed to invoke systemd D-Bus API: %s", strerror(-r));
            sd_notifyf(0, "ERRNO=%d\nSTATUS=Failed to invoke systemd D-Bus API", -r);
            sd_event_exit(ctx->event, r);
        }
    }
}

// Print command-line usage help
void print_usage(const char *prog) {
    std::cerr << "Usage:\n";
    std::cerr << "  Standard creation: sudo " << prog << " <json_path>\n";
    std::cerr << "  Healing mode:      sudo " << prog << " --fix <json_path>\n";
    std::cerr << "  Archival delete:   sudo " << prog << " --delete <profile_name>\n";
    std::cerr << "  Help:              " << prog << " --help | -h\n";
}

void print_help(const char *prog) {
    std::cout << "videoBackup Profile Generator (vb_generator)\n\n"
              << "Automates WireGuard configuration, resolv files, yt-dlp configuration files,\n"
              << "and manages the activation, healing, and deletion of corresponding systemd services.\n\n"
              << "Usage:\n"
              << "  sudo " << prog << " <json_path>\n"
              << "  sudo " << prog << " --fix <json_path>\n"
              << "  sudo " << prog << " --delete <profile_name>\n"
              << "  " << prog << " --help | -h\n\n"
              << "Options & Arguments:\n"
              << "  <json_path>              [Standard Mode] Path to a JSON configuration file containing\n"
              << "                           profile definitions. Generates WireGuard and yt-dlp configs,\n"
              << "                           sets up folders, and starts systemd path/timer units.\n\n"
              << "  --fix <json_path>        [Healing Mode] Re-applies and verifies systemd services (path\n"
              << "                           and timer) for all profiles defined in the JSON file. Use this\n"
              << "                           if the configuration files already exist on disk but services\n"
              << "                           are disabled or stopped.\n\n"
              << "  --delete <profile_name>  [Archival Delete Mode] Stops and disables systemd path, timer,\n"
              << "                           and VPN tunnel services for the specified profile. Moves all\n"
              << "                           associated configuration files to the archival deleted folder:\n"
              << "                           /var/lib/yt-dlp/conf/deleted_profiles/\n\n"
              << "  --help, -h               Show this detailed help message and exit.\n";
}

int main(int argc, char *argv[]) {
    // Check for help flags
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_help(argv[0]);
        return 0;
    }

    // Determine command-line options
    bool fix_mode = false;
    bool delete_mode = false;
    std::string target_arg;

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--fix") == 0) {
        if (argc < 3) {
            print_usage(argv[0]);
            return 1;
        }
        fix_mode = true;
        target_arg = argv[2];
    } else if (strcmp(argv[1], "--delete") == 0) {
        if (argc < 3) {
            print_usage(argv[0]);
            return 1;
        }
        delete_mode = true;
        target_arg = argv[2];
    } else {
        target_arg = argv[1];
    }

    // Safety checks against the default template JSON
    if (!delete_mode) {
        if (target_arg.find("profiles-template.json") != std::string::npos) {
            sd_journal_print(LOG_ERR, "Error: Releasing profiles using the default template JSON direct path is blocked.");
            sd_notify(0, "ERRNO=22\nSTATUS=Error: Template path is blocked");
            return EINVAL;
        }

        // Read the file and search for template placeholder keys
        std::ifstream file(target_arg);
        if (file) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();
            if (content.find("\"yAuD...\"") != std::string::npos || 
                content.find("\"8bJp...\"") != std::string::npos ||
                content.find("\"profileA\"") != std::string::npos) {
                sd_journal_print(LOG_ERR, "Error: JSON config file contains placeholder keys (yAuD..., 8bJp..., profileA). Please configure actual values.");
                sd_notify(0, "ERRNO=22\nSTATUS=Error: Placeholder keys detected");
                return EINVAL;
            }
        } else {
            sd_journal_print(LOG_ERR, "Error: Unable to open input JSON file: %s", target_arg.c_str());
            sd_notifyf(0, "ERRNO=%d\nSTATUS=Error: File not found", errno);
            return errno;
        }
    }

    // Initialize systemd notify status
    sd_notify(0, "READY=1\nSTATUS=Initializing vb_generator...");

    DbusContext ctx;
    ctx.fix_mode = fix_mode;
    ctx.delete_mode = delete_mode;

    if (delete_mode) {
        std::string X = target_arg;
        if (!validate_profile_name(X)) {
            sd_notify(0, "ERRNO=22\nSTATUS=Error: Invalid profile name");
            return EINVAL;
        }
        ctx.profiles.push_back(X);

        sd_journal_print(LOG_INFO, "Executing archival delete for profile: %s", X.c_str());

        // Perform file archivals
        std::string delete_dir = "/var/lib/yt-dlp/conf/deleted_profiles";
        if (!directory_exists(delete_dir)) {
            if (mkdir(delete_dir.c_str(), 0777) < 0) {
                sd_journal_print(LOG_ERR, "Failed to create directory: %s: %s", delete_dir.c_str(), strerror(errno));
                sd_notifyf(0, "ERRNO=%d\nSTATUS=Error: Failed to create deleted_profiles folder", errno);
                return errno;
            }
            system(("setfacl -d -m u::rwx,g::rwx,o::rwx " + delete_dir).c_str());
            system(("setfacl -m u::rwx,g::rwx,o::rwx " + delete_dir).c_str());
        }

        move_file("/var/lib/yt-dlp/conf/" + X + ".conf", delete_dir + "/" + X + ".conf");
        move_file("/var/lib/yt-dlp/conf/" + X + ".batch.txt", delete_dir + "/" + X + ".batch.txt");
        move_file("/var/lib/yt-dlp/conf/" + X + ".history.txt", delete_dir + "/" + X + ".history.txt");
        move_file("/etc/wireguard/" + X + ".conf", delete_dir + "/" + X + ".wg.conf");
        move_file("/etc/yt-dlp-vpn/" + X + ".resolv", delete_dir + "/" + X + ".resolv");

    } else {
        // Generation / Healing Mode
        FILE *f = fopen(target_arg.c_str(), "r");
        if (!f) {
            sd_journal_print(LOG_ERR, "Failed to open config file %s: %s", target_arg.c_str(), strerror(errno));
            sd_notifyf(0, "ERRNO=%d\nSTATUS=Error: Failed to open JSON config file", errno);
            return errno;
        }

        sd_json_variant *v_raw = nullptr;
        unsigned error_line = 0, error_column = 0;
        int r = sd_json_parse_file(f, target_arg.c_str(), static_cast<sd_json_parse_flags_t>(0), &v_raw, &error_line, &error_column);
        fclose(f);

        if (r < 0) {
            sd_journal_print(LOG_ERR, "JSON Parse failed on %s:%u:%u: %s", target_arg.c_str(), error_line, error_column, strerror(-r));
            sd_notifyf(0, "ERRNO=%d\nSTATUS=Error: JSON parsing failed", -r);
            return -r;
        }
        UniqueJsonVariant v(v_raw);

        sd_json_variant *profiles = sd_json_variant_by_key(v.get(), "profiles");
        if (!profiles || !sd_json_variant_is_array(profiles)) {
            sd_journal_print(LOG_ERR, "JSON format error: 'profiles' key missing or not an array");
            sd_notify(0, "ERRNO=22\nSTATUS=Error: JSON profiles key missing/invalid");
            return EINVAL;
        }

        size_t n = sd_json_variant_elements(profiles);
        for (size_t i = 0; i < n; ++i) {
            sd_json_variant *profile = sd_json_variant_by_index(profiles, i);
            if (!profile || !sd_json_variant_is_object(profile)) {
                continue;
            }

            sd_json_variant *name_v = sd_json_variant_by_key(profile, "name");
            if (!name_v || !sd_json_variant_is_string(name_v)) {
                sd_journal_print(LOG_ERR, "JSON format error: profile has missing or non-string name");
                continue;
            }
            std::string X = sd_json_variant_string(name_v);
            if (!validate_profile_name(X)) {
                continue;
            }

            std::string wg_conf_path = "/etc/wireguard/" + X + ".conf";
            std::string ytdlp_conf_path = "/var/lib/yt-dlp/conf/" + X + ".conf";

            // If files exist, skip generation
            bool wg_exists = file_exists(wg_conf_path);
            bool conf_exists = file_exists(ytdlp_conf_path);

            if (wg_exists || conf_exists) {
                if (fix_mode) {
                    sd_journal_print(LOG_INFO, "Profile %s already exists. Registering for service healing.", X.c_str());
                    ctx.profiles.push_back(X);
                } else {
                    sd_journal_print(LOG_WARNING, "Profile %s already exists on disk. Skipping generation.", X.c_str());
                }
                continue;
            }

            // Parse WireGuard details
            sd_json_variant *wg = sd_json_variant_by_key(profile, "wireguard");
            if (!wg || !sd_json_variant_is_object(wg)) {
                sd_journal_print(LOG_ERR, "Profile %s missing 'wireguard' config block", X.c_str());
                continue;
            }

            sd_json_variant *pk_v = sd_json_variant_by_key(wg, "private_key");
            sd_json_variant *addr_v = sd_json_variant_by_key(wg, "address");
            sd_json_variant *pubk_v = sd_json_variant_by_key(wg, "public_key");
            sd_json_variant *ep_v = sd_json_variant_by_key(wg, "endpoint");
            sd_json_variant *allowed_v = sd_json_variant_by_key(wg, "allowed_ips");
            sd_json_variant *dns_v = sd_json_variant_by_key(wg, "dns");

            if (!pk_v || !addr_v || !pubk_v || !ep_v || 
                !sd_json_variant_is_string(pk_v) || !sd_json_variant_is_string(addr_v) ||
                !sd_json_variant_is_string(pubk_v) || !sd_json_variant_is_string(ep_v)) {
                sd_journal_print(LOG_ERR, "Profile %s has invalid/missing WireGuard parameters", X.c_str());
                continue;
            }

            std::string private_key = sd_json_variant_string(pk_v);
            std::string address = sd_json_variant_string(addr_v);
            std::string public_key = sd_json_variant_string(pubk_v);
            std::string endpoint = sd_json_variant_string(ep_v);
            std::string allowed_ips = "0.0.0.0/0, ::/0";
            if (allowed_v && sd_json_variant_is_string(allowed_v)) {
                allowed_ips = sd_json_variant_string(allowed_v);
            }

            // 1. Generate WireGuard configuration
            std::string wg_content = "[Interface]\n";
            wg_content += "PrivateKey = " + private_key + "\n";
            wg_content += "Address = " + address + "\n\n";
            wg_content += "[Peer]\n";
            wg_content += "PublicKey = " + public_key + "\n";
            wg_content += "Endpoint = " + endpoint + "\n";
            wg_content += "AllowedIPs = " + allowed_ips + "\n";

            if (!write_file(wg_conf_path, wg_content, 0600)) {
                continue;
            }

            // 2. Generate DNS resolver resolv file
            if (dns_v && sd_json_variant_is_string(dns_v)) {
                std::string dns_str = sd_json_variant_string(dns_v);
                std::stringstream ss(dns_str);
                std::string token;
                std::string resolv_content = "";
                while (std::getline(ss, token, ',')) {
                    // Strip whitespace
                    token.erase(0, token.find_first_not_of(" \t"));
                    token.erase(token.find_last_not_of(" \t") + 1);
                    if (!token.empty()) {
                        resolv_content += "nameserver " + token + "\n";
                    }
                }
                if (!directory_exists("/etc/yt-dlp-vpn")) {
                    mkdir("/etc/yt-dlp-vpn", 0755);
                }
                write_file("/etc/yt-dlp-vpn/" + X + ".resolv", resolv_content, 0644);
            }

            // 3. Generate yt-dlp configuration using default template
            std::string template_content = 
                "--batch-file /var/lib/yt-dlp/conf/" + X + ".batch.txt\n"
                "--download-archive /var/lib/yt-dlp/conf/" + X + ".history.txt\n"
                "-o \"/var/lib/yt-dlp/videos/" + X + "/%(uploader)s/%(upload_date)s - %(title)s - %(id)s.%(ext)s\"\n\n"
                "-S \"res:720,fps:30\"\n"
                "--no-playlist\n"
                "--compat-options no-youtube-channel-redirect\n"
                "--embed-metadata\n"
                "--js-runtimes node\n";

            write_file(ytdlp_conf_path, template_content, 0666);

            // 4. Touch batch and history files
            touch_file("/var/lib/yt-dlp/conf/" + X + ".batch.txt", 0666);
            touch_file("/var/lib/yt-dlp/conf/" + X + ".history.txt", 0666);

            // 5. Create video folder with open permissions and default ACLs
            std::string video_dir = "/var/lib/yt-dlp/videos/" + X;
            if (!directory_exists(video_dir)) {
                mkdir(video_dir.c_str(), 0777);
                chmod(video_dir.c_str(), 0777);
                system(("setfacl -d -m u::rwx,g::rwx,o::rwx " + video_dir).c_str());
                system(("setfacl -m u::rwx,g::rwx,o::rwx " + video_dir).c_str());
            }

            sd_journal_print(LOG_INFO, "Generated profile configuration files for: %s", X.c_str());
            ctx.profiles.push_back(X);
        }
    }

    if (ctx.profiles.empty()) {
        sd_journal_print(LOG_INFO, "No profiles to configure or process. Exiting.");
        sd_notify(0, "STOPPING=1\nSTATUS=No profiles to process. Finished.");
        return 0;
    }

    // Setup systemd D-Bus and Event Loop
    sd_event *event_raw = nullptr;
    int r = sd_event_default(&event_raw);
    if (r < 0) {
        sd_journal_print(LOG_ERR, "Failed to create systemd event loop: %s", strerror(-r));
        sd_notifyf(0, "ERRNO=%d\nSTATUS=Error: Failed to create event loop", -r);
        return -r;
    }
    UniqueEvent event(event_raw);
    ctx.event = event.get();

    sd_bus *bus_raw = nullptr;
    r = sd_bus_default_system(&bus_raw);
    if (r < 0) {
        sd_journal_print(LOG_ERR, "Failed to connect to system D-Bus: %s", strerror(-r));
        sd_notifyf(0, "ERRNO=%d\nSTATUS=Error: Failed to connect to system D-Bus", -r);
        return -r;
    }
    UniqueBus bus(bus_raw);
    ctx.bus = bus.get();

    r = sd_bus_attach_event(ctx.bus, ctx.event, SD_EVENT_PRIORITY_NORMAL);
    if (r < 0) {
        sd_journal_print(LOG_ERR, "Failed to attach D-Bus connection to event loop: %s", strerror(-r));
        sd_notifyf(0, "ERRNO=%d\nSTATUS=Error: Failed to attach D-Bus to event loop", -r);
        return -r;
    }

    // Trigger first state transition
    advance_state(&ctx);

    // Run the event loop
    r = sd_event_loop(ctx.event);

    sd_notify(0, "STOPPING=1");

    if (r < 0) {
        sd_journal_print(LOG_ERR, "Event loop finished with error: %s", strerror(-r));
        sd_notifyf(0, "ERRNO=%d\nSTATUS=Error: Event loop failure", -r);
        return -r;
    }

    sd_journal_print(LOG_INFO, "Profile generator operation completed successfully.");
    sd_notify(0, "STATUS=All profile operations completed successfully.");
    return 0;
}
