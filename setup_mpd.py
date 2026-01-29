#!/usr/bin/env python3

import os
import sys
import subprocess
import shutil
from pathlib import Path

def run_command(command):
    try:
        subprocess.run(command, check=True, shell=True)
    except subprocess.CalledProcessError as e:
        print(f"Error running command: {command}")
        sys.exit(1)

def install_dependencies():
    if os.path.exists("/etc/debian_version"):
        print("Detected Debian/Ubuntu")
        run_command("sudo apt update && sudo apt install -y mpd mpc")
    elif os.path.exists("/etc/fedora-release"):
        print("Detected Fedora")
        run_command("sudo dnf install -y mpd mpc")
    elif os.path.exists("/etc/arch-release"):
        print("Detected Arch Linux")
        run_command("sudo pacman -S --noconfirm mpd mpc")
    else:
        print("Unsupported distribution. Please install MPD manually.")
        sys.exit(1)

def setup_config():
    home = Path.home()
    mpd_config_dir = home / ".config" / "mpd"
    mpd_data_dir = home / ".local" / "share" / "mpd"
    mpd_playlist_dir = mpd_data_dir / "playlists"

    # Create directories
    mpd_config_dir.mkdir(parents=True, exist_ok=True)
    mpd_playlist_dir.mkdir(parents=True, exist_ok=True)

    mpd_conf_path = mpd_config_dir / "mpd.conf"

    if not mpd_conf_path.exists():
        print(f"Creating default configuration at {mpd_conf_path}")
        config_content = """music_directory     "~/Music"
playlist_directory  "~/.local/share/mpd/playlists"
db_file             "~/.local/share/mpd/database"
log_file            "syslog"
pid_file            "~/.local/share/mpd/pid"
state_file          "~/.local/share/mpd/state"
sticker_file        "~/.local/share/mpd/sticker.sql"

auto_update "yes"
bind_to_address "localhost"
port "6600"
restore_paused "yes"

audio_output {
    type            "pulse"
    name            "Pulse Audio"
}

audio_output {
    type            "fifo"
    name            "Visualizer Feed"
    path            "/tmp/mpd.fifo"
    format          "44100:16:2"
}
"""
        with open(mpd_conf_path, "w") as f:
            f.write(config_content)
    else:
        print(f"Configuration file already exists at {mpd_conf_path}. Skipping creation.")

def main():
    print("Setting up MPD...")
    install_dependencies()
    setup_config()

    if shutil.which("systemctl"):
        print("Enabling and starting MPD user service...")
        run_command("systemctl --user enable --now mpd")
        print("MPD started. Run 'mpc update' to scan your music library.")
    else:
        print("Systemd not found. Please start MPD manually.")

if __name__ == "__main__":
    main()