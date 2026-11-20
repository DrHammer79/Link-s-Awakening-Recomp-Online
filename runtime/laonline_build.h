#pragma once
/* V16.0: V15.2 on LAS Recomp v0.6.0 with local co-op (docs/laonline-port.md) - a version of its own, as V14.1's rule
 * below asks: its saves come from another LAS Recomp and its shared items are granted differently, so it shares rooms
 * only with V16.0. */
#define LAO_COOP_BUILD 16
#define LAO_COOP_REVISION 0
#define LAO_COOP_RANK (LAO_COOP_BUILD*1000+LAO_COOP_REVISION)
#define LAO_MOD_VERSION "16.0"
/* room compatibility tag. V14.1: every release only shares rooms with the SAME version (mixed versions broke saves and
 * seeds for testers), and the relay refuses versions older than its MIN_CLIENT_VERSION (relay/protocol.py). */
#define LAO_ROOM_BUILD LAO_MOD_VERSION
#define LAO_LAS_BASELINE "v0.6.0"
#define LAO_RELEASE_LABEL "LAS Recomp v0.6.0 + local co-op, LA-Online V16.0"
/* LA-Online's own relay ("host:port"): always on the Online page's Relay tab, never removable, the one used when
 * laonline.ini names none. Empty until its developer gives its public address (the tab says it comes with an update);
 * until then laonline.ini's server, or a relay added or hosted on that tab. */
#define LAO_OFFICIAL_RELAY ""
