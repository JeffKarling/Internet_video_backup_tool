Name:           yt-dlp-vpn-automation
Version:        2.1.2
Release:        1%{?dist}
Summary:        Automated generic multi-VPN yt-dlp backup system
License:        MIT-0

BuildRequires:  gcc-c++
BuildRequires:  systemd-devel
Requires:       wireguard-tools
Requires:       systemd
Requires:       acl
Requires:       nodejs

%description
An automated video backup engine using yt-dlp isolated within WireGuard network namespaces.
Dynamic user mounts map download configurations and directories dynamically into user home folders on login.

%prep
# Nothing to prepare - files are gathered by build script

%build
g++ -O3 -std=c++17 -o vb_generator "%{_sourcedir}/vb_generator.cpp" -lsystemd

%install
mkdir -p "%{buildroot}/usr/libexec/yt-dlp-vpn-automation"
mkdir -p "%{buildroot}/usr/sbin"
mkdir -p "%{buildroot}/usr/lib/systemd/system"
mkdir -p "%{buildroot}/usr/lib/systemd/system/user@.service.d"
mkdir -p "%{buildroot}/usr/share/doc/yt-dlp-vpn-automation"
mkdir -p "%{buildroot}/usr/share/licenses/yt-dlp-vpn-automation"
mkdir -p "%{buildroot}/var/lib/yt-dlp/conf/profile_generator"
mkdir -p "%{buildroot}/etc/yt-dlp-vpn"

# Copy LICENSE
cp "%{_sourcedir}/LICENSE" "%{buildroot}/usr/share/licenses/yt-dlp-vpn-automation/"

# Copy yt-dlp binary
cp "%{_sourcedir}/yt-dlp" "%{buildroot}/usr/libexec/yt-dlp-vpn-automation/"

# Copy vpn-helper script
cp "%{_sourcedir}/vpn-helper.sh" "%{buildroot}/usr/libexec/yt-dlp-vpn-automation/"

# Copy vb_generator binary
cp vb_generator "%{buildroot}/usr/sbin/vb_generator"

# Copy default profile_generator template
cp "%{_sourcedir}/profiles-template.json" "%{buildroot}/var/lib/yt-dlp/conf/profile_generator/profiles-template.json"


# Copy systemd unit files
cp "%{_sourcedir}/yt-dlp@.service" "%{buildroot}/usr/lib/systemd/system/"
cp "%{_sourcedir}/yt-dlp@.path" "%{buildroot}/usr/lib/systemd/system/"
cp "%{_sourcedir}/yt-dlp@.timer" "%{buildroot}/usr/lib/systemd/system/"
cp "%{_sourcedir}/yt-dlp-vpn@.service" "%{buildroot}/usr/lib/systemd/system/"
cp "%{_sourcedir}/yt-dlp-user-mount@.service" "%{buildroot}/usr/lib/systemd/system/"
cp "%{_sourcedir}/yt-dlp.target" "%{buildroot}/usr/lib/systemd/system/"
cp "%{_sourcedir}/yt-dlp-update.service" "%{buildroot}/usr/lib/systemd/system/"
cp "%{_sourcedir}/yt-dlp-update.timer" "%{buildroot}/usr/lib/systemd/system/"
cp "%{_sourcedir}/vb-generator.service" "%{buildroot}/usr/lib/systemd/system/"


# Copy user session mount hook
cp "%{_sourcedir}/user-mount-hook.conf" "%{buildroot}/usr/lib/systemd/system/user@.service.d/yt-dlp-mount.conf"

# Copy documentation templates
cp "%{_sourcedir}/README.txt" "%{buildroot}/usr/share/doc/yt-dlp-vpn-automation/"
cp "%{_sourcedir}/design.txt" "%{buildroot}/usr/share/doc/yt-dlp-vpn-automation/"
cp "%{_sourcedir}/install.txt" "%{buildroot}/usr/share/doc/yt-dlp-vpn-automation/"
cp "%{_sourcedir}/yt-dlp-template.conf" "%{buildroot}/usr/share/doc/yt-dlp-vpn-automation/"

%post
# Create shared storage folders
mkdir -p /var/lib/yt-dlp/conf /var/lib/yt-dlp/videos /var/lib/yt-dlp/conf/profile_generator
chmod 0777 /var/lib/yt-dlp /var/lib/yt-dlp/conf /var/lib/yt-dlp/videos /var/lib/yt-dlp/conf/profile_generator

# Apply default ACLs to ensure any files created inside remain read/write/traverse for all users
setfacl -d -m u::rwx,g::rwx,o::rwx /var/lib/yt-dlp/conf /var/lib/yt-dlp/videos /var/lib/yt-dlp/conf/profile_generator || true
setfacl -m u::rwx,g::rwx,o::rwx /var/lib/yt-dlp/conf /var/lib/yt-dlp/videos /var/lib/yt-dlp/conf/profile_generator || true

# Reload systemd configuration
systemctl daemon-reload

%preun
if [ $1 -eq 0 ]; then
  # Disable main target
  systemctl disable --now yt-dlp.target || true
  
  # Cleanly unmount all active user session bind mounts
  for unit in $(systemctl list-units --all --no-legend "yt-dlp-user-mount@*" | awk '{print $1}'); do
    systemctl stop "$unit" || true
  done
fi

%postun
if [ $1 -eq 0 ]; then
  # Reload systemd after removal
  systemctl daemon-reload
fi

%files
%dir /usr/libexec/yt-dlp-vpn-automation
%dir /usr/share/doc/yt-dlp-vpn-automation
%dir /usr/share/licenses/yt-dlp-vpn-automation
%license /usr/share/licenses/yt-dlp-vpn-automation/LICENSE
%attr(0755, root, root) /usr/libexec/yt-dlp-vpn-automation/yt-dlp
%attr(0755, root, root) /usr/libexec/yt-dlp-vpn-automation/vpn-helper.sh
%attr(0755, root, root) /usr/sbin/vb_generator
/usr/lib/systemd/system/vb-generator.service
/usr/lib/systemd/system/yt-dlp@.service
/usr/lib/systemd/system/yt-dlp@.path
/usr/lib/systemd/system/yt-dlp@.timer
/usr/lib/systemd/system/yt-dlp-vpn@.service
/usr/lib/systemd/system/yt-dlp-user-mount@.service
/usr/lib/systemd/system/yt-dlp.target
/usr/lib/systemd/system/yt-dlp-update.service
/usr/lib/systemd/system/yt-dlp-update.timer
/usr/lib/systemd/system/user@.service.d/yt-dlp-mount.conf
/usr/share/doc/yt-dlp-vpn-automation/README.txt
/usr/share/doc/yt-dlp-vpn-automation/design.txt
/usr/share/doc/yt-dlp-vpn-automation/install.txt
/usr/share/doc/yt-dlp-vpn-automation/yt-dlp-template.conf
%dir /var/lib/yt-dlp/conf/profile_generator
/var/lib/yt-dlp/conf/profile_generator/profiles-template.json
%dir /etc/yt-dlp-vpn
