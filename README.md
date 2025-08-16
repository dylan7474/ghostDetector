# Ghost Detector

Paranormal Audio Research Console (PARC) is a C/SDL2 application for capturing and analysing ultrasonic audio events. It provides real-time FFT visualisation and pattern logging for paranormal research experiments.

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
- **Close Window**: exit the program.

## Roadmap
- Calibrate FFT display for different sample rates.
- Add network logging of detected events.
- Package prebuilt binaries for popular platforms.
