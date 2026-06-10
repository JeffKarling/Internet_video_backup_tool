# yt-dlp VPN Automation Engine

[![Build RPM Package](https://github.com/JeffKarling/Internet_video_backup_tool/actions/workflows/build-rpm.yml/badge.svg)](https://github.com/JeffKarling/Internet_video_backup_tool/actions/workflows/build-rpm.yml)
[![License: MIT-0](https://img.shields.io/badge/License-MIT--0-blue.svg)](https://opensource.org/licenses/MIT-0)

An automated, secure background video downloader that fetches internet videos offline. By simply adding a URL to a batch file in your home directory, the system spins up an isolated, sandboxed `yt-dlp` process routed exclusively through a dedicated WireGuard VPN namespace.

---

## Key Features

*   **Secure Network Isolation**: The `yt-dlp` download process executes inside an isolated network namespace (`netns_X`) with all traffic routed exclusively through a WireGuard VPN tunnel. Host IP exposure and DNS leaks are mathematically impossible.
*   **Ephemeral Process Shielding**: Sandboxed via systemd's `DynamicUser` configuration. The runner drops all Linux capabilities, restricts namespace creation, locks execution personalities, and runs with private mount configurations.
*   **Multi-User Dynamic Mounts**: Automatically creates and bind-mounts shared download queues and video folders into home directories on user login (`~/yt-dlp-backup/{conf,videos}`).
*   **Automated Configuration (Recommended)**: Utilizes a native C++ utility (`vb_generator`) to generate all WireGuard configs, directories, and permissions, reload the systemd daemon, and enable/start the corresponding services from a single JSON profile sheet.
*   **Shared Cache Reuse**: Safe concurrent caching of YouTube signature decryption rules, minimizing start times and duplicate network handshakes.

---

## Recommended Setup (Automated via `vb_generator`)

The C++ profile generator `vb_generator` is the recommended way to provision, update, or archive configurations.

### 1. Configure Profiles
Locate the template configuration:
```bash
cp ~/yt-dlp-backup/conf/profile_generator/profiles-template.json ~/yt-dlp-backup/conf/profile_generator/profiles.json
```
Edit `profiles.json` to configure your WireGuard credentials, endpoints, nameservers, and profile names.

### 2. Run the Generator
Run the generator using either the systemd oneshot service or by executing the binary directly:

*   **Method A (Systemd service)**:
    ```bash
    sudo systemctl start vb-generator.service
    ```
*   **Method B (Direct execution)**:
    ```bash
    sudo /usr/sbin/vb_generator /var/lib/yt-dlp/conf/profile_generator/profiles.json
    ```

### 3. Verify & Monitor
You can monitor downloading files, path monitors, and logs with:
```bash
# Check service/timer status
systemctl status yt-dlp@<profile_name>.path yt-dlp@<profile_name>.timer

# Add a video to queue
echo "https://www.youtube.com/watch?v=..." >> ~/yt-dlp-backup/conf/<profile_name>.batch.txt

# Tail real-time download output
journalctl -u yt-dlp@<profile_name>.service -f
```

---

## Additional Commands

### Healing Mode
Re-applies and verifies systemd services for all existing profiles defined in the JSON without modifying existing files:
```bash
sudo /usr/sbin/vb_generator --fix /var/lib/yt-dlp/conf/profile_generator/profiles.json
```

### Archival Deletion
Stops and disables all services for a profile, then moves all configuration, keys, queue, and history files into a backup folder under `/var/lib/yt-dlp/conf/deleted_profiles/`:
```bash
sudo /usr/sbin/vb_generator --delete <profile_name>
```

### Display Help Options
To view all available commands and explanations:
```bash
vb_generator --help
```

---

## Detailed Documentation
Refer to files in the `docs` folder for deeper information:
*   [docs/install.txt](docs/install.txt): Complete installation, custom bind-mounts, updates, and FAQ.
*   [docs/design.txt](docs/design.txt): Technical, network namespace, and security architecture layout.
*   [docs/README.txt](docs/README.txt): Decoupled overview of components.
