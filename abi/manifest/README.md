# Mithril ABI manifest

`gl-egl-manifest.json` is the machine-readable contract for public GL, EGL and
GLX entry points. Its format version is independent from the library version.

- `implemented`: the new runtime has behavioral tests for the entry point.
- `provisional`: a public entry point exists, but the locked host/game trace or
  the new implementation evidence is incomplete. It is not a release claim.
- `unsupported`: the symbol is outside GL 3.3 Core or is known to be an empty
  compatibility stub. Procedure lookup must return null for it.

Run `tools/abi/gen_manifest.ps1` after changing a public header. The generator
normalizes declarations, hashes the C signature, classifies explicit legacy
stubs as unsupported, sorts by symbol name and fails when a status is absent.
Adding a no-op only to satisfy feature probing is forbidden.
