# Quester [![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

> ## A modern, visually rich MPD client built with Qt 6 and QML

## Table of Contents

- [About](#about)
- [Features](#features)
- [Prerequisites](#prerequisites)
- [Installation](#installation)
- [Usage](#usage)
- [Visualizer Configuration](#visualizer-configuration)
- [Contributing](#contributing)
- [License](#license)

## About

Quester is a desktop client for the Music Player Daemon (MPD). It provides a fluid user interface focused on album art and visual feedback. Built using C++ and Qt Quick (QML), it aims to offer a lightweight yet visually appealing way to browse and play your music library.

## Features

- **Album Browser:** Cover-flow style navigation and Grid View for your music library.
- **Dual Visualizers:**
  - **Spectrum Analyzer:** Custom bar visualizer using FFTW with customizable color presets.
  - **projectM:** Integrated support for Milkdrop-compatible visualizations.
- **MPRIS Support:** Full D-Bus integration for control via system media keys and widgets. (In development, currently doesn't work)
- **Automatic Artwork:** Fetches album art from MPD (embedded/local) or TheAudioDB API.
- **Playback Control:** Standard controls (Play, Pause, Next, Previous) and seek bar.
- **Queue Management:** Manage your play queue and playlists easily.
- **Touch Ready:** UI elements sized and spaced for touch interaction.

## Gallery

![Screenshot](img/quester-gallery-01.png)
![Screenshot](img/quester-gallery-02.png)

## Prerequisites

To build Quester, you need the following dependencies installed on your system:

- **C++ Compiler** (supporting C++17)
- **CMake** (3.16 or higher)
- **Qt 6** (6.2 or higher; Core, Gui, Qml, Quick, Network, Multimedia, DBus, Widgets)
- **libmpdclient**
- **FFTW3**
- **PulseAudio** (libpulse)
- **PipeWire** (libpipewire-0.3)
- **Highway** (libhwy)
- **libprojectM**

### Ubuntu/Debian

```bash
sudo apt install build-essential cmake \
    qt6-base-dev qt6-declarative-dev qt6-multimedia-dev \
    libmpdclient-dev libfftw3-dev libpulse-dev \
    libpipewire-0.3-dev libhwy-dev libprojectm-dev
```

## Installation

1. Clone the repository:

   ```bash
   git clone https://github.com/your_username/Quester.git
   cd Quester
   ```

2. Create a build directory and configure with CMake:

   ```bash
   mkdir build
   cd build
   cmake ..
   ```

3. Build the application:

   ```bash
   make
   ```

4. (Optional) Install system-wide:

   ```bash
   sudo make install
   ```

## Usage

Ensure your MPD server is running. By default, Quester attempts to connect to `localhost` on port `6600`.

Run the application from the build directory:

```bash
./quester
```

## Visualizer Configuration

### Bar Visualizer Presets

Quester allows you to customize the bar visualizer colors by creating preset files.
To add your own presets, create a directory named `presets` inside your Quester config folder (e.g., `~/.config/Quester/presets/`) and add a `.json` file there.

**JSON Structure:**

The JSON file should contain a single root object where keys are preset names and values are color definitions.

*Simple Color List (Gradient):*

```json
{
  "Rainbow": ["#E50000", "#FF8D00", "#FFEE00", "#028121", "#004CFF", "#770088"]
}
```

*Weighted Gradients:*

```json
{
   "Uneven": {
      "colors": ["#FF0000", "#00FF00", "#0000FF"],
      "weights": [1, 4, 1]
   }
}
```

### projectM

Quester supports projectM visualizations. You can configure the preset path, texture size, and other rendering settings in the application settings dialog.

## Contributing

Contributions are what make the open source community such an amazing place to learn, inspire, and create. Any contributions you make are **greatly appreciated**.

1. Fork the Project
2. Create your Feature Branch (`git checkout -b feature/AmazingFeature`)
3. Commit your Changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the Branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## License

Distributed under the MIT License. See `LICENSE` for more information.
