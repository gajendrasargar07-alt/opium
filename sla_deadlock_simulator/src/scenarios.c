#include "scenarios.h"

const Scenario SCENARIOS[N_SCENARIOS] = {
    { "Textbook safe state",
      "3 3 2",
      "7 5 3\n3 2 2\n9 0 2\n2 2 2\n4 3 3",
      "0 1 0\n2 0 0\n3 0 2\n2 1 1\n0 0 2",
      "P1: 1 0 2\nP4: 3 3 0\nP0: 0 2 0" },
    { "Light load",
      "4 3",
      "3 2\n2 2\n2 1",
      "0 0\n0 0\n0 0",
      "P0: 3 2\nP1: 2 2\nP2: 2 1" },
    { "Deadlock-prone",
      "2",
      "2\n2\n2",
      "0\n0\n0",
      "P0: 1\nP1: 1\nP0: 1\nP1: 1" },
    { "Circular wait",
      "0 0",
      "2 2\n2 2",
      "1 0\n0 1",
      "P0: 0 1\nP1: 1 0" },
};
