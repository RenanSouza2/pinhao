#ifndef CONFIG_H
#define CONFIG_H

#define LOCK_DISK_IO

// A sig result's path carries no size -- P, Q and R are exact integers -- so it
// stays valid for a later run at any precision, and one consumed by a join is
// kept as a cross-run cache. Undefine to reclaim that disk instead.
#define KEEP_PIECES

#endif
