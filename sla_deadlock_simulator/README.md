# Resource Allocation Simulator - Deadlock (C + Xlib)

SLA project, Operating Systems. Compares two deadlock-handling strategies on the same input:

* **Algorithm 1** - Banker's algorithm (avoidance): looks ahead before every grant.
* **Algorithm 2** - Deadlock detection for multiple instances: grants freely, checks afterwards.

Written in GNU C11. The algorithms do not depend on any user interface, so the same code is used by the GUI, the console program and the tests.

## Build and run (Ubuntu)

    sudo apt install build-essential libx11-dev
    make                 # builds deadlock_gui and deadlock_cli
    ./deadlock_gui       # the GUI (needs a desktop session / DISPLAY)
    ./deadlock_gui 2     # start with sample 2 loaded
    ./deadlock_cli 3     # console version of sample 3

## Tests

    make test            # 15 unit tests
    make test-asan       # same tests with AddressSanitizer + UBSan
    make table           # scenario results used in the report
    make scaling         # worst-case scaling test (CSV)
    make copycost        # whole-struct copy vs rows-in-use copy
    make gui-demo        # drives the GUI with real X events on a virtual display
                         # (needs: sudo apt install xvfb imagemagick) -> screens/*.png

## Files

| File | Job |
|---|---|
| src/models.h, models.c | `SystemState` (pointer-free), `state_copy`, text parsing, validation |
| src/banker.h, banker.c | `is_safe()` and `request_resources()` |
| src/detection.h, detection.c | `detect_deadlock()` |
| src/simulator.h, simulator.c | feeds a request list to an algorithm, log, metrics, median timing |
| src/scenarios.h, scenarios.c | the four built-in samples |
| src/gui_x11.c | Xlib GUI |
| src/main_cli.c | console front end |
| tests/test_sim.c | unit tests |
| tests/bench.c | scaling test |
| tests/scenario_table.c | prints the numbers for the report |
| tests/xdrive.c, gui_demo.sh | GUI test driver (sends X events) and demo script |

## Input format

* Available: one line, e.g. `3 3 2`
* Max and Allocation: one row per process, e.g. `7 5 3`
* Requests: one per line, `P1: 1 0 2`
* Numbers may be separated by spaces or commas. Limits: 1024 processes, 8 resource types, 2048 requests, values up to 1,000,000.

## GUI keys

Tab / Shift+Tab next box - Ctrl+A select all - Ctrl+V or middle-click paste - F5 or Ctrl+Enter run - mouse wheel / scroll-bar / PgUp / PgDn scroll the log - Esc or Enter closes a dialog.

## Notes

* Run time in the table is the **median of 101 silent runs** (logging switched off) of the whole request queue.
* "Vector comparisons" counts only the `Need <= Work` / `Request <= Work` tests inside the safety and detection loops. "Checks" counts how many times the safety or detection algorithm was actually run.
* A process that holds exactly its Max is assumed to finish and release everything (see report, assumptions).
