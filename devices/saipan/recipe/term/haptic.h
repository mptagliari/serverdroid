/* A short vibration pulse from the phone's haptic motor (the force-feedback input device "regulator-haptic"). */
/* SPDX-License-Identifier: GPL-3.0-only */
/* SPDX-FileCopyrightText: 2026 Matheus Tagliari */
#ifndef TERM_HAPTIC_H
#define TERM_HAPTIC_H
int haptic_open(int pulse_ms);		/* a descriptor, or -1 when there is no haptic device */
void haptic_pulse(int fd);
void haptic_close(int fd);
#endif
