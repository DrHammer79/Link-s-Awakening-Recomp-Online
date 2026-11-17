#pragma once
#define LAO_COOP_BUILD 15
#define LAO_COOP_REVISION 2
#define LAO_COOP_RANK (LAO_COOP_BUILD*1000+LAO_COOP_REVISION)
#define LAO_MOD_VERSION "15.2"
/* room compatibility tag. V14.1: every release only shares rooms with the SAME version (mixed versions broke saves and
 * seeds for testers), and the relay refuses versions older than its MIN_CLIENT_VERSION (relay/protocol.py). */
#define LAO_ROOM_BUILD LAO_MOD_VERSION
#define LAO_LAS_BASELINE "beta_5.4"
#define LAO_RELEASE_LABEL "Beta 5.4 CO-OP BETA-V15.2"
