# Event Loop

The whole daemon is one `poll()` loop driven by file descriptors. No worker
threads, no callbacks scheduled on a timer — every wakeup is a real OS event.

Source: [`src/eventloop.c`](../../src/eventloop.c).

---

## The poll set

| fd | what it signals | who emits |
|---|---|---|
| `signal_fd` | SIGTERM, SIGINT, SIGUSR1, SIGUSR2, SIGCONT, SIGHUP, SIGRTMIN+0/1/2 | the kernel, via `signalfd(2)` |
| `timer_fd` | next wallpaper-cycle deadline | `timerfd_settime` armed by `update_cycle_timer()` |
| `wakeup_fd` | "another thread asked us to wake up right now" | `eventfd(2)`, written by render threads on completion |
| `backend->get_fd()` | compositor events (Wayland display fd, X11 connection fd) | the compositor backend |
| per-output render fds | "this output finished a frame" | the GL renderer threads (one per output) |

The set is rebuilt every iteration because outputs come and go. Cap:
`BASE_FD_COUNT + MAX_OUTPUTS` (see `enum` in eventloop.c).

There is **no polling thread**. There is **no busy loop**. If nothing has
anything to say, the kernel parks us on `poll()` and the daemon uses 0 CPU.

---

## One iteration

```
1. backend->prepare_events()         ; Wayland's prepare-read dance, NULL on X11
2. rebuild poll set                  ; outputs may have hot-plugged
3. poll(fds, n, -1)                  ; block until any fd fires
4. for each ready fd:
       signal_fd     → handle_signal_from_fd(state, signum)
       timer_fd      → trigger wallpaper cycle, re-arm timer
       wakeup_fd     → drain (no-op; just unblocks)
       backend fd    → backend->read_events() ; backend->dispatch_events()
       render fds    → mark output ready for next frame
5. backend->occlusion_update()       ; skip render if any output is fully covered
6. for each non-occluded output that needs a frame:
       schedule render               ; render thread does the GL work
7. backend->flush()                  ; send any queued requests to the compositor
8. goto 1
```

`occlusion_update` is the entire smart-pause story. When a fullscreen game
covers the wallpaper, the backend marks `output->occluded = true` and step 6
becomes a no-op — neowall draws zero frames until the window is dismissed.

---

## Signal handling

Signals are blocked process-wide via `pthread_sigmask` at startup and
delivered through `signalfd`. This is race-free vs the old `sigaction` +
`volatile sig_atomic_t` pattern: a signal can never arrive between "we
checked the flag" and "we slept in poll".

Crash signals (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT) still use
traditional `sigaction` because they have to act before the next poll
return. Their handler is async-signal-safe (write(2), `_exit`, atomic store
on the running flag) — see `handle_crash` in `main.c`.

SIGPIPE is ignored: a dying parec child must not take the daemon with it.

---

## Pause state machine

Three flavours of pause, all atomic flags on `struct neowall_state`:

| Flag | What it stops | How to set |
|---|---|---|
| `running` | the whole loop. Set false → loop exits, daemon shuts down. | SIGTERM, SIGINT |
| `paused` | wallpaper cycling (timer_fd is left disarmed when true). | SIGUSR2 sets; SIGCONT clears. |
| `shader_paused` | the GLSL animation clock. Renderer still runs but `iTime` is frozen. | SIGRTMIN+1 sets; SIGRTMIN+2 clears. |
| `output->occluded` | per-output render. Set by the backend's occlusion path. | wlr-foreign-toplevel state changes, EWMH `_NET_WM_STATE_FULLSCREEN`, Hyprland IPC. |

These are orthogonal: a paused wallpaper with a frozen shader on an
occluded output draws no frames at all and updates no clocks. The CPU/GPU
draw is genuinely zero.

---

## Hot-plug

`on_output_added` / `on_output_removed` fire from the backend's
`dispatch_events`. Each maps to:

1. Take the writer lock on `state->output_list_lock`.
2. Mutate the linked list of `output_state`.
3. Drop the lock.
4. Re-evaluate `update_cycle_timer` (a new output may want cycling).
5. On reconnect, `config_apply_to_output` re-walks the parsed config to
   find the right block for the new output's connector name.

The cycle timer is in milliseconds because seconds is too coarse for short
durations — `update_cycle_timer_locked` walks every output to find the
soonest deadline and arms `timer_fd` exactly there. No millisecond is
spent before it needs to be.

---

## Performance properties

- One `poll(2)` per wakeup. Wakeups happen exactly when needed (signal,
  timer, compositor event, render completion).
- Zero allocations on the hot path. The poll set lives in a stack array.
- Zero string formatting unless `LOG_LEVEL_DEBUG` is set.
- 0% CPU when idle on an occluded wallpaper. Verified by `top` on a
  fullscreen game.

---

## See also

- [`docs/architecture/BACKEND_ABI.md`](BACKEND_ABI.md) — the backend ops
  that the loop calls into.
- [`docs/ARCHITECTURE.md`](../ARCHITECTURE.md) — higher-level overview.
