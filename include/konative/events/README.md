# include/konative/events/

Every event type Konative dispatches, plus the one shared `entt::dispatcher` wrapper
(`dispatcher.hpp`) every one of them flows through.

## Hard rules — read before adding ANY event

- **One event type, one file. No exceptions, ever.** A new event always means a new `.hpp` file
  in the right feature-area subfolder (`lifecycle/`, `window/`, `input/`, `persistence/`, or a new
  subfolder if the event doesn't fit an existing category). Never add a second `struct ...Event { ... };` to an
  existing header, even if it seems trivially related to what's already there — this is the single
  rule this folder exists to enforce (`ARCHITECTURE.md` §2), and it's a deliberately *stricter*
  rule than either of this project's own inspirations (GameHub's `libs/events/` batches all event
  types into one header; Hazel's `Events/` folder batches by category, e.g. one `KeyEvent.h` holds
  four related event structs) — don't rationalize back into either of those looser patterns.
- **An event is a plain aggregate struct — no base class, no virtual, no constructor logic.**
  Every event must satisfy `konative::core::EventType`
  (`core/type_traits.hpp`) — if a "clever" event needs a constructor or inheritance, it's not an
  event, it's something else.
- **`dispatcher.hpp` is the shared event-bus wrapper, not an event type itself** (like
  `next_event_awaiter.hpp`/`next_event_awaiter_self_check.hpp`, ARCHITECTURE.md §9's libcoro spike,
  also generic machinery rather than event types). None of these must ever grow event-specific
  logic (no per-event-type special cases inside them) — new behavior that's specific to one event
  type belongs in whatever system consumes that event, not in shared machinery here.
- **Naming**: `PascalCaseNoun` + `Event` suffix matching the struct name exactly
  (`WindowResizedEvent.hpp` for `struct WindowResizedEvent`) — this mirrors Hazel's own filename
  convention even though Hazel doesn't follow the one-per-file rule strictly; Konative's stricter
  rule and Hazel's naming convention are independent choices, both worth keeping.
- **Pick `trigger()` (immediate) vs. `enqueue()`+`update()` (deferred, once-per-frame) deliberately
  per call site, not by habit** — see `dispatcher.hpp`'s own comment and `ARCHITECTURE.md` §5 for
  when each is correct. Cross-thread event posting never calls `enqueue()`/`trigger()` directly —
  it goes through a `concurrentqueue` first (`scheduling/`), drained by the single frame thread.

## Adding a new feature-area subfolder

If an event doesn't fit `lifecycle/`, `window/`, or `input/`, add a new subfolder named for the
feature area (e.g. `audio/`, `network/`) rather than shoehorning it into an existing one — this
folder is meant to grow wide (many subfolders), not have any one subfolder grow to hold unrelated
concerns.
