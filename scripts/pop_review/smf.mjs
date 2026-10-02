/**
 * Minimal Standard MIDI File reader.
 *
 * Returns note spans grouped by (track, channel) plus the tempo map, which is
 * all the review needs from a reference file. SMPTE time division is refused
 * rather than guessed at. Mirrors `parse_smf` in `scripts/compare_midi_profile.py`.
 */

import { readFileSync } from 'node:fs';

/**
 * @typedef {{ pitch: number, start: number, end: number, velocity: number }} SmfNote
 * @typedef {{ track: number, channel: number, name: string, notes: SmfNote[] }} SmfPart
 * @typedef {{ tick: number, usPerBeat: number }} SmfTempo
 * @typedef {{ ppq: number, parts: SmfPart[], tempos: SmfTempo[] }} Smf
 */

function readVarLen(data, start) {
  let value = 0;
  let pos = start;
  for (;;) {
    const byte = data[pos];
    pos += 1;
    value = (value << 7) | (byte & 0x7f);
    if ((byte & 0x80) === 0) {
      return [value, pos];
    }
  }
}

/**
 * Parse an SMF file.
 *
 * @param {string} path
 * @returns {Smf}
 */
export function readSmf(path) {
  const data = readFileSync(path);
  if (data.toString('latin1', 0, 4) !== 'MThd') {
    throw new Error(`${path} is not an SMF file`);
  }
  const headerLen = data.readUInt32BE(4);
  const trackCount = data.readUInt16BE(10);
  const division = data.readUInt16BE(12);
  if (division === 0 || division & 0x8000) {
    throw new Error(`${path} uses an unsupported time division`);
  }
  let pos = 8 + headerLen;

  /** @type {Map<string, SmfPart>} */
  const parts = new Map();
  /** @type {SmfTempo[]} */
  const tempos = [];

  for (let track = 0; track < trackCount; track += 1) {
    if (data.toString('latin1', pos, pos + 4) !== 'MTrk') {
      throw new Error(`${path} has an invalid track header at track ${track}`);
    }
    const end = pos + 8 + data.readUInt32BE(pos + 4);
    pos += 8;
    let tick = 0;
    let runningStatus = 0;
    let trackName = '';
    /** @type {Map<number, {start: number, velocity: number}[]>} */
    const active = new Map();

    const partFor = (channel) => {
      const key = `${track}:${channel}`;
      let part = parts.get(key);
      if (!part) {
        part = { track, channel, name: trackName, notes: [] };
        parts.set(key, part);
      }
      return part;
    };
    const release = (channel, pitch) => {
      const stack = active.get(channel * 128 + pitch);
      const open = stack?.shift();
      if (open && tick > open.start) {
        partFor(channel).notes.push({
          pitch,
          start: open.start,
          end: tick,
          velocity: open.velocity,
        });
      }
    };

    while (pos < end) {
      let delta;
      [delta, pos] = readVarLen(data, pos);
      tick += delta;
      let status = data[pos];
      if (status < 0x80) {
        status = runningStatus;
      } else {
        pos += 1;
        if (status < 0xf0) {
          runningStatus = status;
        }
      }

      if (status === 0xff) {
        const type = data[pos];
        let length;
        [length, pos] = readVarLen(data, pos + 1);
        if (type === 0x03) {
          trackName = data.toString('utf8', pos, pos + length);
        } else if (type === 0x51 && length === 3) {
          tempos.push({ tick, usPerBeat: data.readUIntBE(pos, 3) });
        }
        pos += length;
        continue;
      }
      if (status === 0xf0 || status === 0xf7) {
        let length;
        [length, pos] = readVarLen(data, pos);
        pos += length;
        continue;
      }

      const kind = status & 0xf0;
      const channel = status & 0x0f;
      if (kind === 0xc0 || kind === 0xd0) {
        pos += 1;
        continue;
      }
      const a = data[pos];
      const b = data[pos + 1];
      pos += 2;
      if (kind === 0x90 && b > 0) {
        const key = channel * 128 + a;
        const stack = active.get(key) ?? [];
        stack.push({ start: tick, velocity: b });
        active.set(key, stack);
      } else if (kind === 0x80 || (kind === 0x90 && b === 0)) {
        release(channel, a);
      }
    }
    pos = end;
  }

  tempos.sort((x, y) => x.tick - y.tick);
  return {
    ppq: division,
    parts: [...parts.values()].filter((part) => part.notes.length > 0),
    tempos,
  };
}
