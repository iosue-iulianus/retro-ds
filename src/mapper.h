/* The button mapper (Settings > Map Buttons): asks for each button SNES DS
 * uses in turn, by where it is on a GBA-style layout (A the right face
 * button, B the bottom one, X the top, Y the left), and records which raw
 * button, hat or axis of the controller it is. The result is an SDL game
 * controller mapping for that controller, used at once and kept in
 * <state_dir>/controllers.txt (with a comment naming the device and the
 * system), and written to the log so it can be built into
 * dist/gamecontrollerdb.txt for everyone with that device. */
#ifndef SNESDS_MAPPER_H
#define SNESDS_MAPPER_H

#include "config.h"

void mapper_run(const Config* cfg);

#endif
