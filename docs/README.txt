================================================================================
                       YT-DLP VPN AUTOMATION ENGINE
================================================================================

This project enables a user on the system to download and watch internet videos
offline. By simply adding a video URL to a batch txt file located in the users
home folder, the system automatically starts a vpn tunnel containerized yt-dlp
process download in the background.

--------------------------------------------------------------------------------
KEY FEATURES
--------------------------------------------------------------------------------

- SECURE NETWORK ISOLATION: The yt-dlp download process executes inside a
  isolated network namespace with traffic routed exclusively bound to a
  WireGuard VPN tunnel. Host IP exposure and DNS leaks are impossible.
  
- YT-DLP PROCESS SHIELDING: yt-dlp process runs inside a dynamically allocated,
  ephemeral system user using systemd's DynamicUser features. It drops all
  Linux capabilities, restricts namespace creation, locks execution
  personalities, and runs with private mount configurations. The host filesystem
  and user credentials is completely secure isolated.

- GENERIC MULTI-USER DYNAMIC MOUNTS: The application is decoupled from any
  specific user. When any user logs in, the system automatically bind-mounts
  the shared download queue and downloaded videos directly into users home folder
  at '~/yt-dlp-backup/conf' and '~/yt-dlp-backup/videos'.

- FLEXIBILITY & SCALABILITY: Multiple independent VPN tunnels, download queues,
  and configurations can run concurrently simply by defining multiple profiles.

- AUTOMATED CONFIGURATION (RECOMMENDED): Includes a C++ helper utility 'vb_generator'
  that parses your JSON profile definitions and WireGuard configurations to
  automatically generate required files, establish directories with secure
  permissions, reload systemd, and enable/start all services automatically.

- EFFICIENT SHARED CACHE REUSE: Concurrent yt-dlp instances safely share a
  common cache directory, enabling immediate reuse of player signature
  decryption files, significantly reducing redundant network traffic and
  minimizing download startup times.
================================================================================
