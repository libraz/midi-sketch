#ifndef MIDISKETCH_CORE_MELODY_TEMPLATES_H
#define MIDISKETCH_CORE_MELODY_TEMPLATES_H

#include "core/types.h"

namespace midisketch {

// Get a melody template by ID.
// @param id Template identifier
// @returns Reference to the template
const MelodyTemplate& getTemplate(MelodyTemplateId id);

// Get default template ID for a vocal style and section type.
// @param style Vocal style preset
// @param section Section type
// @returns Recommended template ID
MelodyTemplateId getDefaultTemplateForStyle(VocalStylePreset style,
                                             SectionType section);

// Get aux configurations for a template.
// @param id Template identifier
// @param out_configs Output array of AuxConfig (max 3)
// @param out_count Output number of configs
void getAuxConfigsForTemplate(MelodyTemplateId id,
                               AuxConfig* out_configs,
                               uint8_t* out_count);

// Template count (excluding Auto)
constexpr uint8_t MELODY_TEMPLATE_COUNT = 7;

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_MELODY_TEMPLATES_H
