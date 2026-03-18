# btop Host Repository

This repository is being reorganized into three product-facing areas:

```text
.
├── control-plane/   # FastAPI backend, Vite frontend, Docker/deploy files
├── agent/           # Adaptation layer and build entry for our custom distribution
└── btop/            # Upstream btop source tree, tracked as a submodule in the host repo
```

## What each directory owns

- `control-plane/`
  - Python backend
  - React frontend
  - deployment files
  - control-plane docs
- `agent/`
  - adaptation/build entrypoint
  - agent/distributed integration notes
  - future patches or overlays that should stay separate from upstream
- `btop/`
  - the current btop codebase and C++ build tree
  - current distributed and monitoring extensions that still need to be peeled away from the vendored tree

## Current status

- `control-plane/` is already separated.
- `btop/` is now a clean upstream `btop` checkout at `v1.4.6`.
- `agent/` now owns the standalone `btop-agent` sources and links against upstream `libbtop`.
- `btop/` is the upstream-only dependency boundary for the host repository.
- The remaining long-term task is to keep shrinking any migration-reference code that still lives outside `agent/` or `control-plane/`.

## Build entry

From the repository root:

```bash
cmake -S . -B build-host
cmake --build build-host -j4
```

This delegates to `agent/`, which builds `btop-agent` as an external adapter target on top of `libbtop`.

To point the build at a clean upstream checkout instead of the in-repo `btop/` tree:

```bash
cmake -S . -B build-host -DBTOP_UPSTREAM_SOURCE_DIR=/path/to/clean/btop
cmake --build build-host --target btop-agent -j4
```

## Local development

- Control plane: see [control-plane/README.md](/Volumes/Code/btop/control-plane/README.md)
- btop core tree: see [btop/README.md](/Volumes/Code/btop/btop/README.md)
- adaptation notes: see [agent/README.md](/Volumes/Code/btop/agent/README.md)

## Release automation

Push a commit to `main` whose subject is exactly a version string such as `V1.0.0`.
The release workflow will rebuild the control-plane Docker tarballs and the supported
`btop-agent` binaries, then create or update a GitHub Release with those assets.

See [RELEASE_AUTOMATION.md](/Volumes/Code/btop/docs/RELEASE_AUTOMATION.md).

## Installers

Interactive installer:

- [install.sh](/Volumes/Code/btop/install.sh)
