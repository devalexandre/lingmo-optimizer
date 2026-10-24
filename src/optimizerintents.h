#pragma once

// optimizer.analyze and optimizer.clean.caches (Lingmo Intents) on
// com.lingmo.Optimizer /Intents. `lingmo-optimizer --intents` (D-Bus
// activation) answers without a window and quits when idle.
namespace OptimizerIntents {
bool requested(int argc, char *argv[]);
int run(int argc, char *argv[]);
}
