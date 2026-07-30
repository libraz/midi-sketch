/**
 * Utility functions for midi-sketch
 */

import { getApi } from './internal';

/**
 * Get library version
 */
export function getVersion(): string {
  return getApi().version();
}

/**
 * Download MIDI data as a file (browser only)
 */
export function downloadMidi(midiData: Uint8Array, filename = 'output.mid'): void {
  if (typeof document === 'undefined') {
    throw new Error('downloadMidi is only available in a browser environment.');
  }
  if (typeof URL === 'undefined' || typeof URL.createObjectURL !== 'function') {
    throw new Error('downloadMidi requires URL.createObjectURL support.');
  }

  // Copy to regular ArrayBuffer to ensure compatibility
  const buffer = new ArrayBuffer(midiData.length);
  new Uint8Array(buffer).set(midiData);
  const blob = new Blob([buffer], { type: 'audio/midi' });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = filename;
  anchor.click();
  setTimeout(() => URL.revokeObjectURL(url), 0);
}
