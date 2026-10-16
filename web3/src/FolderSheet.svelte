<script lang="ts">
  import { t } from './lib/i18n';
  // The swipe screen's folder button: the photo folders in use, each with オン / オフ (whether this
  // device swipes it; the gallery and the ranking keep it), 「新しいフォルダを追加」 below (the phone's
  // own folder list in the Android app), and サブフォルダで選ぶ for a finer range (ScopePicker).
  import { onMount } from 'svelte';
  import { getJ, postJ, ANDROID, IOS, type State } from './lib/api';
  import { photoFolders, folderName, matchFolder, readOff, saveOff, scopeOfOn, addPhotoFolder, scopeAfterFolderAdd } from './lib/folders';
  import type { FolderRow } from './lib/scope';
  import { svg } from './ui/icons';
  import FolderBrowser from './FolderBrowser.svelte';

  let {
    st,
    custom,
    onapply,
    onsub,
    onclose,
    startAdding = false,
  }: {
    st: State;
    custom: string; // the finer range chosen with サブフォルダで選ぶ ('' = none)
    onapply: (tokens: string[], label: string) => void;
    onsub: () => void;
    onclose: () => void;
    startAdding?: boolean;
  } = $props();

  const roots = $derived(photoFolders(st));
  let off = $state(readOff());
  const onCount = $derived(roots.filter((p) => !off.has(p)).length);

  // the number of pictures in each photo folder (the first one is the folder path "")
  let counts = $state<Record<string, number>>({});
  onMount(async () => {
    const r = await getJ<{ folders: FolderRow[] }>('/api/folders').catch(() => ({ folders: [] as FolderRow[] }));
    const c: Record<string, number> = {};
    for (const f of r.folders) if (f.depth === 0) c[f.path] = f.total;
    counts = c;
  });
  const countOf = (i: number, p: string) => counts[i === 0 ? '' : p];

  function set(p: string, on: boolean) {
    if (on === !off.has(p)) return;
    if (!on && onCount <= 1) return; // one folder always stays on
    const next = new Set(off);
    if (on) next.delete(p);
    else next.add(p);
    // forget folders that are no longer photo folders
    for (const x of next) if (!roots.includes(x)) next.delete(x);
    off = next;
    saveOff(next);
    const s = scopeOfOn(roots, next);
    onapply(s.tokens, s.label);
  }

  let browsing = $state(false);
  let msg = $state('');
  onMount(() => {
    if (startAdding && !st.remote && !IOS) void add();
  });
  async function add() {
    msg = '';
    if (ANDROID) return void (browsing = true);
    const r = await postJ<{ path: string }>('/api/pickfolder').catch(() => ({ path: '' }));
    if (r.path) await addPath(r.path);
  }
  async function addPath(path: string) {
    browsing = false;
    if (!path) return;
    path = matchFolder(roots, path);
    const already = roots.includes(path);
    msg = await addPhotoFolder(st, path);
    if (msg) return;
    const selected = scopeAfterFolderAdd(st, path, off);
    off = selected.off;
    saveOff(off);
    onapply(selected.tokens, selected.label); // clear a finer range and include the chosen folder now
    msg = already ? t('オン') : t('「{0}」を追加しました。読み込みが終わるとスワイプに出てきます', folderName(path));
  }
</script>

{#if browsing}
  <FolderBrowser title={t('写真フォルダを追加')} onpick={addPath} onclose={() => (browsing = false)} />
{:else}
  <!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
  <div class="modal" onclick={onclose}>
    <!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
    <div class="sheet" onclick={(e) => e.stopPropagation()}>
      <div class="grip"></div>
      <div class="head">
        <b>{t('写真フォルダ')}</b>
        <span class="sub">{t('オフのフォルダはスワイプに出ません')}</span>
      </div>
      <div class="list">
        {#each roots as p, i (p)}
          <div class="row" class:off={off.has(p)}>
            <span class="ic">{@html svg('folder', 18)}</span>
            <span class="name">{folderName(p)}</span>
            {#if countOf(i, p) !== undefined}<span class="n num">{countOf(i, p)}</span>{/if}
            <span class="seg" role="group" aria-label={t('{0}をスワイプする', folderName(p))}>
              <button class:on={!off.has(p)} onclick={() => set(p, true)}>{t('オン')}</button>
              <button class:on={off.has(p)} disabled={!off.has(p) && onCount <= 1} onclick={() => set(p, false)}>{t('オフ')}</button>
            </span>
          </div>
        {:else}
          <p class="empty">{t('写真フォルダがありません')}</p>
        {/each}
        {#if !st.remote && !IOS}
          <button class="row add" onclick={add}>
            <span class="ic">{@html svg('folderPlus', 18)}</span><span class="name">{t('新しいフォルダを追加')}</span>
          </button>
        {/if}
        {#if msg}<p class="msg">{msg}</p>{/if}
      </div>
      <button class="row finer" onclick={onsub}>
        <span class="ic">{@html svg('layers', 16)}</span>
        <span class="name">{t('サブフォルダで選ぶ')}</span>
        {#if custom}<span class="chip">{custom}</span>{/if}
        <span class="chev">›</span>
      </button>
    </div>
  </div>
{/if}

<style>
  .modal {
    position: fixed;
    inset: 0;
    z-index: 30;
    display: flex;
    align-items: flex-end;
    justify-content: center;
    background: rgba(0, 0, 0, 0.5);
    backdrop-filter: blur(4px);
    animation: fade 160ms var(--ease-out);
  }
  @keyframes fade {
    from {
      opacity: 0;
    }
  }
  .sheet {
    width: min(520px, 100%);
    max-height: 80vh;
    display: flex;
    flex-direction: column;
    padding: 6px 12px calc(14px + env(safe-area-inset-bottom, 0px));
    border-radius: 22px 22px 0 0;
    background: var(--surface);
    box-shadow: 0 -10px 40px rgba(0, 0, 0, 0.35);
    animation: up 240ms var(--ease-out);
  }
  @media (min-width: 700px) {
    .modal {
      align-items: center;
    }
    .sheet {
      border-radius: 22px;
      animation-name: fade;
    }
  }
  @keyframes up {
    from {
      translate: 0 40%;
    }
  }
  .grip {
    width: 38px;
    height: 4px;
    margin: 4px auto 10px;
    border-radius: 2px;
    background: var(--line);
  }
  .head {
    display: flex;
    flex-direction: column;
    gap: 2px;
    padding: 0 6px 10px;
  }
  .head b {
    font-size: 16px;
  }
  .sub {
    color: var(--muted);
    font-size: 12.5px;
  }
  .list {
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    gap: 6px;
  }
  .row {
    display: flex;
    align-items: center;
    gap: 10px;
    width: 100%;
    min-height: 52px;
    padding: 8px 10px 8px 12px;
    border-radius: 14px;
    border: 1px solid var(--line);
    background: var(--surface-2);
    text-align: left;
    color: var(--text);
  }
  .row.off .name,
  .row.off .ic {
    opacity: 0.45;
  }
  .ic {
    flex: none;
    display: flex;
    color: var(--accent-ink);
  }
  .name {
    flex: 1;
    min-width: 0;
    font-size: 14px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .n {
    flex: none;
    color: var(--muted);
    font-size: 12.5px;
  }
  .seg {
    flex: none;
    display: flex;
    padding: 3px;
    gap: 2px;
    border-radius: 999px;
    background: var(--bg);
    border: 1px solid var(--line);
  }
  .seg button {
    min-width: 48px;
    padding: 6px 12px;
    border: none;
    border-radius: 999px;
    background: transparent;
    color: var(--muted);
    font-size: 13px;
    cursor: pointer;
    transition:
      background-color 140ms,
      color 140ms;
  }
  .seg button.on {
    background: var(--accent);
    color: var(--on-accent);
    font-weight: 700;
  }
  .row.off .seg button.on {
    background: var(--strong-line, var(--line));
    color: var(--text);
  }
  .seg button:disabled {
    opacity: 0.35;
    cursor: default;
  }
  .row.add {
    border-style: dashed;
    background: transparent;
    cursor: pointer;
  }
  .row.add .name {
    color: var(--accent-ink);
    font-weight: 600;
  }
  .row.finer {
    margin-top: 10px;
    min-height: 44px;
    border: none;
    background: transparent;
    cursor: pointer;
    color: var(--muted);
  }
  .row.finer .ic {
    color: var(--muted);
  }
  .chip {
    flex: none;
    max-width: 40%;
    padding: 3px 10px;
    border-radius: 999px;
    background: color-mix(in srgb, var(--accent) 16%, transparent);
    color: var(--accent-ink);
    font-size: 12px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .chev {
    flex: none;
    color: var(--muted);
    font-size: 18px;
  }
  .empty,
  .msg {
    margin: 4px 6px;
    color: var(--muted);
    font-size: 13px;
    line-height: 1.5;
  }
</style>
