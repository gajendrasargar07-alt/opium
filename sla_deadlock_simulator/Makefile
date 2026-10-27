# Resource Allocation Simulator - Deadlock  (GNU C11, gcc, Ubuntu)
# GUI needs the Xlib headers:   sudo apt install build-essential libx11-dev

CC      ?= gcc
CFLAGS  ?= -std=gnu11 -Wall -Wextra -O2
SRC     := src/models.c src/banker.c src/detection.c src/simulator.c src/scenarios.c

all: deadlock_gui deadlock_cli

deadlock_gui: src/gui_x11.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ -lX11

deadlock_cli: src/main_cli.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

test_sim: tests/test_sim.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

bench: tests/bench.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

xdrive: tests/xdrive.c
	$(CC) $(CFLAGS) -o $@ $^ -lX11

test: test_sim
	./test_sim

# same tests with the address and undefined-behaviour sanitizers switched on
test-asan: tests/test_sim.c $(SRC)
	$(CC) -std=gnu11 -Wall -Wextra -g -O1 -fsanitize=address,undefined -o test_sim_asan $^
	./test_sim_asan

run: deadlock_gui
	./deadlock_gui

scaling: bench
	./bench

clean:
	rm -f deadlock_gui deadlock_cli test_sim test_sim_asan bench xdrive table table_rows table_full
	rm -rf screens

.PHONY: all test test-asan run scaling table copycost gui-demo clean

table: tests/scenario_table.c $(SRC)
	$(CC) $(CFLAGS) -o table $^
	./table

# state_copy() copies only the rows in use; -DFULL_COPY copies the whole 64 KB structure
copycost: tests/scenario_table.c $(SRC)
	$(CC) $(CFLAGS) -o table_rows $^
	$(CC) $(CFLAGS) -DFULL_COPY -o table_full $^
	@echo "--- rows in use only"; ./table_rows
	@echo "--- whole structure";  ./table_full

gui-demo:
	./tests/gui_demo.sh
