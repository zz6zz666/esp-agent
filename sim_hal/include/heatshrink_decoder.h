/* Stub for heatshrink_decoder.h */
#pragma once
#include <stddef.h>
#include <stdint.h>
/* Minimal stub — heatshrink compressed motion data will fail to decode on
   simulator, but the system won't crash.  Real emote uses .eaf files which
   don't use heatshrink for the main eye animation. */
typedef void *heatshrink_decoder_t;
