# The runtime

neowall runs on a small Elm-style loop in C (`src/runtime/rt.c`, API in
`include/neowall/runtime/rt.h`). It replaced `eventloop.c`: one 1,380-line
`poll()` loop that waited on the OS, decided what happened, changed state and
drew, all in one function, with each new feature adding another atomic flag
polled somewhere far away.

```
  OS: fds, timers, signals            program (src/app.c)
  ────────────────────────            ───────────────────
  rt wait ── signal / fd / post ──▶   update(model, msg)      the only place
                                         │                    state changes
                                         ▼
                  timers  ◀────────   subscribe(model)        reconciled by key
                                         │
                                         ▼
                                      present(model)          GL, EGL, swap
```

## rt: the loop

`rt` owns the thread. It knows nothing about wallpapers. Each step:

1. **prepare**: the host flushes and says whether it already holds events
   poll can't see (Xlib's queue). If it does, the wait doesn't block.
2. **wait**: `ppoll` on the mailbox eventfd, the signalfd and every watched
   fd, until the next timer deadline, to the nanosecond. A deadline in the
   future never becomes a 0 wait (that spins); with no deadline it blocks.
3. **turn what happened into messages**, in a fixed order: signals, fd
   callbacks, the host's after-wait hook (Wayland dispatch), the mailbox,
   due timers.
4. **fold** every message through `update`, re-run `subscribe`, then
   `present`. A step that folded nothing doesn't draw.

Rules it enforces:

- **One thread changes state.** Other threads `rt_post()` a message; it is
  folded on the loop thread, in order, never dropped. After the loop stops,
  `app_post` returns false, and `app_run` waits for posts already in flight
  before freeing the loop.
- **Wakes are coalesced.** A terminal's reader thread sets `needs_redraw` and
  only posts a wake when the loop armed one before sleeping (and none is
  armed while a frame callback is pending, since that wakes us anyway). A
  child printing flat out costs one wake per frame, not one per read.
- **Children start clean.** Every spawned child resets the blocked signals
  and SIGPIPE (`include/neowall/child_signals.h`), or it can't be stopped.
- **No work in signal handlers.** The command signals are blocked in every
  thread at startup (`main.c`, before any thread exists) and read from a
  signalfd. Crash signals (SEGV, BUS, ILL, FPE, ABRT) keep a plain
  async-signal-safe handler in `main.c`; SIGPIPE is ignored.
- **A timer is a subscription.** `subscribe` declares the timers that should
  exist given the model, keyed by `(kind, id)`. A key kept across steps
  keeps its phase; a key that disappears is cancelled. Nothing re-arms a
  timerfd by hand. A repeating timer that fell behind (stall, suspend) fires
  once and re-phases, never a burst.

`tests/test_runtime.c` checks these: timers never fire early, a 16.67 ms
period isn't padded to 17, every posted message arrives, signals become
messages, an idle loop sleeps.

## The program (`src/app.c`)

**Messages** (`include/neowall/app.h`):

| message | from |
|---|---|
| `QUIT` | SIGINT, SIGTERM, a compositor error or hangup |
| `RELOAD` | SIGHUP |
| `NEXT`, `CYCLE_PAUSE`, `CYCLE_RESUME` | SIGUSR1, SIGUSR2, SIGCONT |
| `SET_INDEX`, `SHADER_PAUSE`, `SHADER_RESUME`, `SET_TERMINAL` | SIGRTMIN+0..3 (the CLI writes the argument to a file first) |
| `CYCLE_DUE`, `FRAME_DUE`, `TRANSITION` | timers from `subscribe` |
| `DISPLAY` | the compositor fd was serviced |
| `OUTPUTS_NEW` | the Wayland registry saw a new output |
| `WATCH` | `neowall watch`: the shader file changed |
| `REDRAW` | any thread (`app_request_redraw`) |

Control-socket requests (below) are answered on the loop thread and then go
through the same messages.

**The model** (`struct app`) holds what used to be atomics on
`neowall_state`: queued `next` requests, the requested index and terminal
command, cycle pause, shader pause, pending output bring-up. Only `update`
writes them.

**Subscriptions**: one `CYCLE_DUE` per cycling output, due when its duration
is up; one `FRAME_DUE` per vsync-off animated output at the pacer's next
deadline; a 60 Hz `TRANSITION` tick while any transition runs.

**present** is `render_outputs()` (`src/app_render.inc`), unchanged from the
old loop. It runs in three phases, which is what makes it safe against
hotplug:

1. **Snapshot** the output list under the read lock, taking a **reference**
   on each output, then drop the lock.
2. **Render** each output (cycling, transitions, `eglMakeCurrent`, draw),
   with no lock held.
3. **Present**: damage, `wp_presentation` feedback, swap (with damage where
   supported), commit, then move the frame pacer on.

Finally it unrefs every snapshotted output.

## Frame pacing

- **vsync=true, Wayland**: each output renders only when its own
  `wl_surface.frame` callback has come back, and swaps with interval 0, so a
  late monitor can't stall the others (#85). The loop waits on the
  compositor fd for the callback; `wait_hint` bounds the wait so a callback
  that never comes (hidden surface) times out.
- **vsync=true, X11**: there are no frame callbacks, so the frame timer
  paces the loop at the refresh rate and the swap keeps interval 1, which
  lands each present on the refresh.
- **vsync=false**: a phase-locked pacer. The next deadline is one period
  after the last present (anchored to the real flip time with
  `wp_presentation`, quantised to whole refreshes), and it's a `FRAME_DUE`
  subscription. Overruns snap the phase forward instead of bursting. The
  pacer moves on once per frame, after the swap; a frame that won't render
  (an idle terminal, a paused shader) moves it on from `FRAME_DUE` instead.

## Shader time

Pacing decides *when* a frame is drawn; the frame clock decides *what time
it shows*. Every animated Wayland frame requests `wp_presentation` feedback.
The feedback gives the vblank grid (phase and refresh), and
`src/output/frame_clock.c` learns, by majority vote over recent frames, how
many refreshes after the nearest vblank a frame drawn now is shown. Each
frame's `iTime` is that predicted present time, chained in whole refreshes
from the previous one, so it steps by exactly one refresh per refresh no
matter how much the draw start jitters. It resyncs only if it drifts more
than 1/8 refresh from the grid, and a one-off missed vblank is one vote, so
it doesn't move the latency. X11, or before the first present, falls back to
the wall clock.

`iTime` is a float32, so it is reset before it gets coarse
(`src/shader/shader_clock.c`): past an hour, for free while the output is
hidden or paused; past two hours on screen, by keeping the last frame,
resetting on the next, and cross-fading the kept frame out over 1.2 s.

## Wakes from other threads

A terminal's reader thread sets `needs_redraw` and calls
`app_request_output_redraw`, which posts a wake only if the loop armed one
before it slept. The loop arms it only when some output has no frame
callback in flight; otherwise the callback wakes it anyway and `present`
sees the flag. A child printing flat out costs one wake per frame instead
of one per read.

## Pausing

These are independent: a paused wallpaper with a frozen shader on an
occluded output draws nothing and wakes only for its timers.

A terminal wallpaper idles too: once its child has been quiet past the FX
settle window it clears its dirty bit and the loop sleeps. The terminal's
reader thread calls `on_output` when the child writes, which posts a redraw
for that output, so new output still shows within a frame.

| state | stops | set by |
|---|---|---|
| cycle pause | wallpaper cycling: no `CYCLE_DUE` subscriptions | `pause` / `resume` |
| shader pause | the shader clock and its draws; resumes on the same frame | `pause-shader` / `resume-shader` |
| `output->occluded` | that output's draws and its frame timer | the backend's occlusion signals |

## The control socket

The CLI drives a running daemon over `$XDG_RUNTIME_DIR/neowall.sock`
(`src/ipc.c`). One request line, one reply line:

```
$ printf 'next\n' | socat - UNIX:$XDG_RUNTIME_DIR/neowall.sock
ok skipping to next wallpaper
```

| request | reply |
|---|---|
| `ping` | `ok neowall <version> pid <pid>` |
| `next`, `pause`, `resume` | `ok ...`, or `err nothing to cycle...` |
| `set <n>` | `ok`, or `err index n is out of range (0-k)` |
| `reload` | `ok`, or `err reload failed; the previous configuration is still in effect` |
| `pause-shader`, `resume-shader` | `ok ...` |
| `set-terminal <cmd>` | `ok`, or `err no output is running a terminal wallpaper` |
| `kill` | `ok stopping` |

The listening socket and each connection are watched fds on the loop. A
complete request is answered on the loop thread in the same step, and the
command goes through the same message a signal would, so each command has
one path. A client that doesn't finish its line in 2 s is dropped, a request
over 1 KiB is refused, and only the daemon's own uid may connect
(`SO_PEERCRED`, on top of the 0600 socket in a 0700 dir). A second daemon
finds the socket answering and refuses to start; a socket file left by a
crashed daemon (nobody listening) is replaced. `preview`/`watch` run beside
the daemon and never bind it.

The old signals (SIGUSR1 = next, SIGRTMIN = set, ...) still work, for a CLI
older than the socket; the CLI itself only falls back to them when nothing
answers on the socket.

## Hot-plug

A new `wl_output` posts `OUTPUTS_NEW`. After that step's dispatch has
delivered its configure, `present` brings it up (`reinit_reconnected_outputs`:
compositor surface, EGL surface, GL state, then the matching config block by
connector name). Removal happens during dispatch under the output list's
write lock; the reference taken by `render_outputs` keeps a removed output
alive until the frame using it ends.
