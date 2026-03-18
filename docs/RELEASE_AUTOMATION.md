# Release Automation

This repository publishes release assets when a push to `main` contains exactly one commit whose subject matches `Vx.x.x`.

Examples:

- `V1.0.0`
- `V1.2.3-rc.1`

The workflow lives at `.github/workflows/release.yml` and publishes:

- control-plane Docker tarball for `linux/amd64`
- control-plane Docker tarball for `linux/arm64`
- `btop-agent` tarball for `linux/x86_64`
- `btop-agent` tarball for `macos/x86_64`
- `btop-agent` tarball for `macos/arm64`
- `install.sh`
- `SHA256SUMS.txt`
