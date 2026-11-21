# ZN manifest check

`make zn-manifest-check NDK_PATH=/path/to/ndk ARCH=arm64-v8a` builds a standalone diagnostic under `build/diagnostics/arm64-v8a`. Pass the manifest on standard input. The output counts supported declarations and explicit companion flags; a malformed or unsupported line exits nonzero.

The diagnostic accepts a deliberately narrow subset: `name=` or absolute `path=`, an optional `companion` token, and a module-relative `.so` path with no dot or parent components. Blank lines and whole-line comments are accepted. This is not a complete manifest grammar. It does not resolve targets or paths, authenticate modules, load libraries, or change engine state. It is not included in the boot artifacts.
