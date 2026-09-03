/**
 * @file arpeggio_types.h
 * @brief Arpeggio-related type definitions.
 *
 * `ArpeggioPattern`, `ArpeggioSpeed`, `ArpeggioParams` and `ArpeggioStyle` are
 * all declared in preset_types.h, which owns the config field list that drives
 * JSON serialization.  This header exists only so that a translation unit
 * asking for the arpeggio types by name gets the same definitions rather than a
 * second copy of them: an independent copy here would be an ODR violation, and
 * would silently omit the `Auto` sentinel that `ArpeggioParams` defaults to.
 */

#ifndef MIDISKETCH_CORE_ARPEGGIO_TYPES_H
#define MIDISKETCH_CORE_ARPEGGIO_TYPES_H

#include "core/preset_types.h"

#endif  // MIDISKETCH_CORE_ARPEGGIO_TYPES_H
