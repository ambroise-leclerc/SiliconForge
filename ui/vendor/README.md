Third-party files of the interface, copied unmodified (the source-map comment removed):

- `xterm/` — [xterm.js](https://github.com/xtermjs/xterm.js) 5.5.0 (`@xterm/xterm`) and its fit addon 0.10.0 (`@xterm/addon-fit`), MIT, see `xterm/LICENSE`.
- `webfront-pty/pty.mjs` — the xterm.js adapter of WebFront's PTY (`js/pty/index.mjs`, WebFront develop at e40133d), Apache-2.0 OR CECILL-B.
- `prism/` — [Prism](https://prismjs.com) 1.29.0 (core and the grammars of C, C++, Python, JSON, Bash, CMake, JavaScript, CSS, HTML/XML, Markdown, YAML, Makefile, TOML, INI and Rust, with `clike` and `markup` they extend), MIT, see `prism/LICENSE`. Loaded with `data-manual`: the editor calls `Prism.highlight` itself.
