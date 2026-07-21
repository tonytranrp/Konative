# examples/

Small, self-contained reference apps proving the framework actually works end to end — currently
`minimal_triangle/`, a desktop-buildable smoke test of the ECS/events/app lifecycle core (no Android
target required). Despite the name, it doesn't currently render anything — it's the bare
`Application` subclass skeleton (`start()`/`tick()`/`destroy()`), with a comment marking where a real
app would install systems/components. Real rendering is JVM-hosted Compose now
(`ARCHITECTURE.md` §6.6/§6.7, `embedded_kotlin/`), which needs a real Android Activity to host a
`ComposeView` — not something this desktop-only example can exercise; see `testapp/` for that.

## Hard rules

- **Every example is one `Application` subclass plus a `main()`, nothing more.** If an example
  needs a helper that isn't itself example-specific, that helper is missing from
  `include/konative/`, not something to build inline in the example.
- **Examples must build on desktop (`desktop-debug` preset) whenever possible**, even though the
  framework's real target is Android — this is what makes an example a fast, CI-friendly smoke
  test of the ECS/events/app core rather than something that only proves anything on a real
  device. Anything that genuinely needs Android (real rendering, real lifecycle) belongs in
  `testapp/`'s verification loop instead, not forced into an `examples/` entry that can't build
  without a device.
- **An example is documentation, not a place to prototype framework changes.** If you're editing
  `include/konative/**.hpp` to make an example work, make sure that change is something the
  framework should genuinely support, not a one-off hack scoped to the example.

## Adding a new example

Copy `minimal_triangle/`'s shape: its own subfolder, its own `CMakeLists.txt` producing one
executable, one `main.cpp`. Add it to `examples/CMakeLists.txt`'s `add_subdirectory()` list.
