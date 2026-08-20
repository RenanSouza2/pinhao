#ifndef CONFIG_H
#define CONFIG_H

// Build-time on/off toggles. Each is read by a single library, but they all
// live here so a run's configuration can be read off in one place. Flip and
// rebuild to experiment.

// lib/tree: whether a forked task pins itself to the processor matching the
// scheduler slot it was given. Off leaves placement to the OS.
// Presence toggle: comment out to disable.
// #define LOCK_IN_PLACE

// lib/big: whether disk reads and writes are serialised behind a single file
// lock. Only worth it on a spinning disk, where random seeks are expensive;
// on an SSD it is pure contention with no benefit.
// Presence toggle: comment out to disable.
#define LOCK_DISK_IO

// lib/tree: whether a leaf is exempt from the memory budget. A leaf has no
// join, so there are no operand sizes to estimate from. Exempt, it is charged
// nothing and launches into any free slot; not exempt, it is charged one byte
// -- negligible against the total, but enough that a full budget holds it
// back like any other node.
// Value toggle: it is used as a value, not tested with #ifdef, so it must
// stay defined -- set it to false to disable, never comment it out.
#define TREE_LEAF_EXEMPT false

#endif
