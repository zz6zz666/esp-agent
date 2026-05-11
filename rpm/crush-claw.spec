%define __jar_repack 0

Name:           crush-claw
Version:        1.1.0
Release:        1%{?dist}
Summary:        Desktop simulator for the esp-claw embedded AI agent framework

License:        MIT
URL:            https://github.com/zz6zz666/crush-claw
Source0:        %{name}-%{version}.tar.gz

Requires:       glibc >= 2.34
Requires:       libcurl >= 7.81
Requires:       lua >= 5.4
Requires:       SDL2 >= 2.0
Requires:       SDL2_ttf
Requires:       json-c >= 0.15
Requires:       libpng >= 1.6
Requires:       wqy-zenhei-fonts
Recommends:     google-noto-color-emoji-fonts
Recommends:     gtk3 >= 3.24
Recommends:     libayatana-appindicator

%description
Crush Claw provides a complete Linux desktop test environment for
the esp-claw AI agent framework, requiring no ESP32 hardware.

It includes:
 - Full FreeRTOS-to-POSIX compatibility layer
 - Unix socket CLI REPL (replaces hardware UART console)
 - Optional SDL2 simulated LCD display
 - All standard esp-claw capabilities (Lua, files, IM, memory, skills)
 - Systemd user service for automatic startup

After installation, run 'crush-claw config' for first-time setup,
then 'crush-claw start' to launch the agent.

%prep
# Package is assembled from pre-built binary - no source tarball
echo "Assembling crush-claw RPM package..."

%install
rm -rf %{buildroot}

# Binary
mkdir -p %{buildroot}%{_bindir}
install -m 755 %{_sourcedir}/esp-claw-desktop %{buildroot}%{_bindir}/esp-claw-desktop
install -m 755 %{_sourcedir}/crush-claw %{buildroot}%{_bindir}/crush-claw

# Data directory
mkdir -p %{buildroot}%{_datadir}/crush-claw/defaults
cp -r %{_sourcedir}/defaults/* %{buildroot}%{_datadir}/crush-claw/defaults/

# Icons
mkdir -p %{buildroot}%{_datadir}/icons/hicolor/scalable/apps
mkdir -p %{buildroot}%{_datadir}/pixmaps
install -m 644 %{_sourcedir}/lobster.svg %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/lobster.svg
install -m 644 %{_sourcedir}/lobster.png %{buildroot}%{_datadir}/pixmaps/lobster.png

mkdir -p %{buildroot}%{_datadir}/applications
install -m 644 %{_sourcedir}/crush-claw.desktop %{buildroot}%{_datadir}/applications/crush-claw.desktop

mkdir -p %{buildroot}%{_datadir}/doc/%{name}
install -m 644 %{_sourcedir}/README.md %{buildroot}%{_datadir}/doc/%{name}/README.md

# Systemd user service
mkdir -p %{buildroot}%{_prefix}/lib/systemd/user
install -m 644 %{_sourcedir}/crush-claw.service %{buildroot}%{_prefix}/lib/systemd/user/crush-claw.service

# Emote assets
mkdir -p %{buildroot}%{_datadir}/crush-claw/assets
cp -r %{_sourcedir}/assets_284_240 %{buildroot}%{_datadir}/crush-claw/assets/284_240

%post
# Update icon cache
if [ -x /usr/bin/gtk-update-icon-cache ]; then
    gtk-update-icon-cache -f -t %{_datadir}/icons/hicolor 2>/dev/null || true
fi

# Update desktop database
if [ -x /usr/bin/update-desktop-database ]; then
    update-desktop-database -q %{_datadir}/applications 2>/dev/null || true
fi

echo ""
echo "=== Crush Claw has been installed ==="
echo ""
echo "Quick start:"
echo "  1. Run 'crush-claw config' to set up your LLM keys"
echo "  2. Run 'crush-claw start' to launch the agent"
echo "  3. Run 'crush-claw ask \"Hello\"' to interact with the agent"
echo ""
echo "Systemd user service (auto-start on login):"
echo "  crush-claw service enable"
echo ""
echo "Data is stored in ~/.crush-claw/ (created on first run)."
echo ""

%preun
# Stop any running user instances
for pid_file in /home/*/.crush-claw/agent.pid; do
    if [ -f "$pid_file" ]; then
        pid=$(cat "$pid_file" 2>/dev/null || true)
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            echo "Stopping Crush Claw (PID $pid)..."
            kill "$pid" 2>/dev/null || true
            sleep 1
            kill -0 "$pid" 2>/dev/null && kill -9 "$pid" 2>/dev/null || true
        fi
        rm -f "$pid_file"
    fi
done

# Stop systemd user services
for home in /home/*; do
    user=$(basename "$home")
    if id "$user" >/dev/null 2>&1; then
        su - "$user" -c "systemctl --user stop crush-claw.service 2>/dev/null || true" 2>/dev/null || true
        su - "$user" -c "systemctl --user disable crush-claw.service 2>/dev/null || true" 2>/dev/null || true
    fi
done

%files
%{_bindir}/esp-claw-desktop
%{_bindir}/crush-claw
%{_datadir}/crush-claw/
%{_datadir}/icons/hicolor/scalable/apps/lobster.svg
%{_datadir}/pixmaps/lobster.png
%{_datadir}/applications/crush-claw.desktop
%{_datadir}/doc/%{name}/
%{_prefix}/lib/systemd/user/crush-claw.service

%changelog
* Sun May 11 2025 Crush Claw team <zz6zz666@qq.com> - 1.1.0-1
- Initial RPM package release
