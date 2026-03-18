# Agent Adapter Layer

This directory is the build and adaptation boundary between the host repository and `btop/`.

## Purpose

- Keep `control-plane/` independent from the C++ runtime.
- Give us a stable place for future overlays, patches, protocol files, and packaging glue.
- Make it easier to replace `btop/` with a real git submodule later.

## Current role

`agent/` now owns the standalone `btop-agent` target:

- transport and auth protocol code live under `agent/src/distributed/common/`
- client runtime code lives under `agent/src/distributed/client/`
- collector access happens by linking upstream `libbtop` and including upstream internal collector headers from `btop/src/`

That is deliberate:

- the repository structure is cleaned up first
- upstream sync can be planned second
- distributed/custom code can be peeled out of `btop/` into `agent/` incrementally
- the remaining custom code in `btop/src/distributed/` is migration reference, not the target architecture

## Next extraction targets

- move compatibility-only assets and packaging glue here
- move custom distributed overlays here where practical
- leave `btop/` as close to upstream as possible
- remove the legacy C++ control-plane/server code path from the future build graph

## Notes

The long-term target is:

```text
vendor or submodule btop/  -> upstream snapshot
agent/                     -> our patch layer and build glue
control-plane/             -> web product
```

Build against a clean upstream checkout with:

```bash
cmake -S /Volumes/Code/btop -B /tmp/btop-agent-check \
  -DBTOP_UPSTREAM_SOURCE_DIR=/path/to/clean/btop \
  -DBUILD_TESTING=OFF
cmake --build /tmp/btop-agent-check --target btop-agent -j4
```
