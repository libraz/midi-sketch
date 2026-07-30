import { afterEach, describe, expect, it, vi } from 'vitest';
import { downloadMidi } from '../../js/src/utils';

describe('downloadMidi', () => {
  afterEach(() => {
    vi.useRealTimers();
    vi.unstubAllGlobals();
  });

  it('releases its object URL after the download task runs', () => {
    vi.useFakeTimers();
    const click = vi.fn();
    const createObjectURL = vi.fn(() => 'blob:midi-sketch');
    const revokeObjectURL = vi.fn();
    const anchor = { href: '', download: '', click };

    vi.stubGlobal('document', { createElement: vi.fn(() => anchor) });
    vi.stubGlobal('URL', { createObjectURL, revokeObjectURL });

    downloadMidi(new Uint8Array([0x4d, 0x54, 0x68, 0x64]), 'song.mid');

    expect(anchor.download).toBe('song.mid');
    expect(anchor.href).toBe('blob:midi-sketch');
    expect(click).toHaveBeenCalledOnce();
    expect(revokeObjectURL).not.toHaveBeenCalled();

    vi.runAllTimers();
    expect(revokeObjectURL).toHaveBeenCalledWith('blob:midi-sketch');
  });

  it('rejects non-browser use clearly', () => {
    vi.stubGlobal('document', undefined);

    expect(() => downloadMidi(new Uint8Array())).toThrow(/browser environment/);
  });
});
