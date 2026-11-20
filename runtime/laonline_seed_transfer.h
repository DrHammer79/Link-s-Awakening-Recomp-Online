// A Co-op Randomizer room's seed, from the host's game to the others' (runtime/laonline_seed_transfer.cpp). LAS Recomp
// Co-Op's port of LA-Online V15.2's seed transfer (its tools/rando/lao_seed_transfer.py, which ran through the Python
// seed helper LAS Recomp no longer ships): the same relay messages and package checks, native.
#pragma once
#include <string>

// The seed folder as a package (a zip: manifest.json, then the folder's own files under seed/ - never game files or
// its saves) at zip_path. world_hex: its world checksum (laonline_seed_identity_folder). false (status says why) for a
// seed made before LAS Recomp's seed record (romfs/ with the changed game files: sent as they are, those would be game
// files), or a folder that isn't a finished seed.
bool lao_seed_package(const std::string &seed_dir, const std::string &zip_path, std::string &world_hex, std::string &status);

// The package into rando_root: checked (its world checksum again, from the files that arrived), then as its own seed
// folder - or the one already here with the same world. ready: that folder's name.
bool lao_seed_unpack(const std::string &zip_path, const std::string &rando_root, std::string &ready, std::string &status);

// The host's seed to the room's relay lobby (lobby: the room's, ".rando" included), or the room's seed from it
// (unpacked into rando_root). One connection each, opcode 0x16 (docs/laonline-protocol.md 6c). status: one line.
bool lao_seed_publish(const std::string &server, const std::string &name, const std::string &lobby,
                      const std::string &password, const std::string &seed_dir, std::string &status);
bool lao_seed_sync(const std::string &server, const std::string &name, const std::string &lobby,
                   const std::string &password, const std::string &rando_root, std::string &ready, std::string &status);
