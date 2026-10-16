import { describe, expect, it, vi, beforeEach } from 'vitest';
vi.mock('../src/lib/api', () => ({ postJ: vi.fn() }));
import { postJ, type State } from '../src/lib/api';
import { addPhotoFolder, scopeAfterFolderAdd } from '../src/lib/folders';

const state = { root: '\\test\\first', moreRoots: ['\\test\\second'], maxDepths: [0, 2] } as State;
beforeEach(() => { vi.mocked(postJ).mockReset(); });
describe('folder addition from empty swipe', () => {
  it('preserves the primary folder and depths when adding', async () => {
    vi.mocked(postJ).mockResolvedValue({ ok: true });
    expect(await addPhotoFolder(state, '/test/new')).toBe('');
    expect(postJ).toHaveBeenCalledWith('/api/config', { root: '\\test\\first', more_roots: '\\test\\second|\\test\\new', max_depths: '0,2,-1' });
  });
  it('cancelled or existing folder does not rewrite configuration', async () => {
    await addPhotoFolder(state, '');
    await addPhotoFolder(state, '/test/second');
    expect(postJ).not.toHaveBeenCalled();
  });
  it('reports a save failure instead of success', async () => {
    vi.mocked(postJ).mockRejectedValue(new Error('offline'));
    expect(await addPhotoFolder(state, '/test/new')).not.toBe('');
  });
  it('new folder is included when another folder is switched off', () => {
    const selected = scopeAfterFolderAdd(state, '/test/new', new Set(['\\test\\second']));
    expect(selected.tokens).toEqual(['/*', '\\test\\new']);
    expect(selected.off.has('\\test\\second')).toBe(true);
  });
  it('choosing an existing off folder turns it on without duplicating it', () => {
    const selected = scopeAfterFolderAdd(state, '/test/second/', new Set(['\\test\\second']));
    expect(selected.off.size).toBe(0);
    expect(selected.tokens).toEqual([]);
  });
});

describe('a picked folder matches a stored one as the core does', () => {
  it('maps Android slashes and other letter case to the stored spelling', async () => {
    const { matchFolder } = await import('../src/lib/folders');
    expect(matchFolder(['\\test\\Second'], '/test/second')).toBe('\\test\\Second');
    expect(matchFolder(['D:\\Photos'], 'd:\\photos\\')).toBe('D:\\Photos');
    expect(matchFolder(['D:\\Photos'], 'D:\\Other')).toBe('D:\\Other');
    expect(matchFolder(['\\x\\Ä'], '\\x\\ä')).toBe('\\x\\ä'); // only A-Z, as the core
  });
});
