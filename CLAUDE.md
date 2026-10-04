# Thornswood fork: fix the cause, no band-aids

This is Thornswood's fork of SkyMP (branch `thornswood/engine-changes`). The
Thornswood repository's Developer-Guide.md is the rule book for work here
too, and one of its rules matters most in the engine. Decided by Patrick on
4 Oct: "lets ban patching or bandaids for fixes and actually fix things".

A fix finds why something goes wrong and changes that, here in the engine
when that is where the cause is. Not a fix: code that notices the damage and
undoes it (re-equip loops, repairing state on a timer, a second ask in case
the first was ignored), a retry or wait that hopes a race comes out right
instead of ordering the steps, taking something off and putting it back so
it is drawn again, or a layer over an earlier band-aid. Before a fix is
written, its issue on the Thornswood board says what causes the problem and
how that was measured. While the cause is unknown, finding it is the work and
nothing ships for it. A retry is allowed only for failures outside our
control that are expected (the network, a file another program holds), with
the reason beside it, and it stops and says so instead of looping.


# Build & Test Tips

All commands below must be run **inside the build directory**  
(e.g., `mkdir build && cd build && cmake ..`).

## Build
```bash
cmake --build .
````

This compiles the project

## Test

```bash
ctest --verbose
```

Runs all tests with detailed output.

## Test Partuicular Unit Test

This example runs tests with only [Respawn] tag. Tags you can see in test files (.cpp).
If you see more than 1 unit test failed, please select one to work on and iterate with the following command.
```bash
cd build
./unit/unit [Respawn]
```
