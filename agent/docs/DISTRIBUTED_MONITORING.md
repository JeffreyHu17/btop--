# Distributed Monitoring Build System

This document describes the build system setup for btop++ distributed monitoring features.

## Build Targets

### Core Targets
- `btop` - Enhanced btop client with optional distributed monitoring support
- `btop-server` - Centralized monitoring server (requires distributed monitoring dependencies)

### Library Targets
- `libbtop` - Core btop functionality (unchanged)
- `libbtop_distributed` - Common distributed monitoring components
- `libbtop_client` - Client-side distributed monitoring features
- `libbtop_server` - Server-side distributed monitoring features  
- `libbtop_gpu` - Enhanced GPU monitoring capabilities

## Build Options

### BTOP_DISTRIBUTED
- **Default**: ON
- **Description**: Enable distributed monitoring support
- **Dependencies**: libcurl, nlohmann/json
- **Usage**: `-DBTOP_DISTRIBUTED=ON|OFF`

If dependencies are not found, distributed monitoring will be automatically disabled with a warning.

## Dependencies

### Required for Distributed Monitoring
- **libcurl**: HTTP/HTTPS networking support
- **nlohmann/json**: JSON serialization/deserialization

### Installation Examples

#### macOS (Homebrew)
```bash
brew install curl nlohmann-json
```

#### Ubuntu/Debian
```bash
sudo apt-get install libcurl4-openssl-dev nlohmann-json3-dev
```

#### Fedora/RHEL
```bash
sudo dnf install libcurl-devel json-devel
```

## Directory Structure

```
src/
├── distributed/
│   ├── common/          # Shared components (MetricsData, Config)
│   ├── client/          # Client-side components (NetworkClient, DaemonManager)
│   └── server/          # Server-side components (SessionManager, DataAggregator)
├── gpu/                 # Enhanced GPU monitoring
└── [existing btop files]
```

## Build Examples

### Standard Build (with distributed monitoring if dependencies available)
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Force Disable Distributed Monitoring
```bash
mkdir build && cd build
cmake -DBTOP_DISTRIBUTED=OFF ..
make -j$(nproc)
```

### Static Build with Distributed Monitoring
```bash
mkdir build && cd build
cmake -DBTOP_STATIC=ON -DBTOP_DISTRIBUTED=ON ..
make -j$(nproc)
```

## Status

✅ Project structure created
✅ CMake configuration updated  
✅ Build targets defined
✅ Dependency detection implemented
✅ Placeholder interfaces created
⏳ Implementation pending (subsequent tasks)

The build system is ready for implementing the distributed monitoring features in subsequent tasks.