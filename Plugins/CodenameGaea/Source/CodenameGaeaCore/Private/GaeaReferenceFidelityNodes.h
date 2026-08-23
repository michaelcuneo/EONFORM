#pragma once

/**
 * Registers corrected Gaea-facing node contracts/evaluators after the legacy
 * implementation families. Later registration intentionally replaces older
 * placeholder descriptors/evaluators in the runtime registries.
 */
void RegisterGaeaReferenceFidelityNodes();
