# Ghost Detector

Paranormal Audio Research Console (PARC) is a C/SDL2 application for capturing and analysing ultrasonic audio events. It provides real-time FFT visualisation and pattern logging for paranormal research experiments.

The console now includes basic EVP detection. When audio resembling human speech is detected, the relevant segment is automatically saved as a timestamped WAV file in the working directory for later review.

## Building

Run the `configure` script to verify required tools and libraries before building.

### Linux
```bash
./configure
make
```

### Windows (cross-compilation)
```bash
./configure
make -f Makefile.windows
```

## Controls
- **Up/Down Arrow**: increase or decrease input gain.
- **Left/Right Arrow**: raise or lower the burst detection threshold.
- **Space**: pause or resume monitoring.
- **C**: clear the event log.
- **F or F11**: toggle fullscreen mode.
- **Close Window**: exit the program.

## Roadmap
- Calibrate FFT display for different sample rates.
- Add network logging of detected events.
- Package prebuilt binaries for popular platforms.
