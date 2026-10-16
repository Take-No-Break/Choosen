<script lang="ts">
  import { server, t } from './lib/i18n';
  // サブフォルダに分ける: the 見送り / 好評 / 超好評 pictures of the chosen folders (or of chosen
  // pictures) go into a folder named after their verdict right next to them ("Artist/見送り/x.jpg").
  // The owner picks the kinds and sees how many first; the ratings stay (they follow the content),
  // and the last sort can be undone. On Android the app needs all files access once (Android's own
  // page); afterwards the phone's gallery is told about the moved files.
  import { onMount } from 'svelte';
  import { getJ, postJ, ANDROID, canMove, askMove, mediaScan } from './lib/api';
  import { buildEntries, type Entry, type FolderRow } from './lib/scope';
  import { svg } from './ui/icons';

  let {
    scope = '',
    ids = [],
    label = '',
    onclose,
  }: {
    scope?: string; // folders (scope tokens), or
    ids?: number[]; // chosen pictures
    label?: string; // what was chosen, for the title
    onclose: () => void;
  } = $props();

  const KINDS: [string, string][] = [
    ['passed', t('見送り')],
    ['likeOnly', t('好評')],
    ['super', t('超好評')],
  ];

  // nothing chosen yet (from 設定): pick a folder first
  // svelte-ignore state_referenced_locally
  let target = $state({ scope, ids: ids.join(','), label });
  let entries = $state.raw<Entry[] | null>(null);
  let query = $state('');
  const shown = $derived(
    !entries ? [] : query ? entries.filter((e) => e.row.name.toLowerCase().includes(query.toLowerCase())) : entries,
  );

  let counts = $state<Record<string, number>>({});
  let undoable = $state(0);
  let kinds = $state(new Set<string>());
  let allowed = $state(canMove());
  const chosen = $derived([...kinds].reduce((n, k) => n + (counts[k] ?? 0), 0));

  async function count() {
    const r = await getJ<Record<string, number>>('/api/sort', { scope: target.scope, ids: target.ids, kinds: 'passed,likeOnly,super' }).catch(() => null);
    if (!r) return;
    counts = r;
    undoable = r.undo ?? 0;
  }
  onMount(() => {
    if (target.scope || target.ids) void count();
    else
      void getJ<{ folders: FolderRow[] }>('/api/folders')
        .then((r) => (entries = buildEntries(r.folders)))
        .catch(() => (entries = []));
    // back from Android's settings page: is the access on now?
    const again = () => !document.hidden && (allowed = canMove());
    document.addEventListener('visibilitychange', again);
    return () => document.removeEventListener('visibilitychange', again);
  });
  function pickFolder(e: Entry) {
    target = { scope: e.token, ids: '', label: e.row.name };
    void count();
  }
  function toggle(k: string) {
    const next = new Set(kinds);
    if (next.has(k)) next.delete(k);
    else next.add(k);
    kinds = next;
  }

  // ---- the job (the export's progress counters)
  interface Job {
    running: boolean;
    done: number;
    failed: number;
    total: number;
  }
  let job: Job | null = $state(null);
  let msg = $state('');
  async function follow(): Promise<{ done: number; failed: number }> {
    for (;;) {
      job = await getJ<Job>('/api/export').catch(() => null);
      const j: Job | null = job;
      if (!j?.running) return { done: j?.done ?? 0, failed: j?.failed ?? 0 };
      await new Promise((r) => setTimeout(r, 400));
    }
  }
  async function tellGallery() {
    if (!ANDROID) return;
    const m = await getJ<{ moves: [string, string][] }>('/api/sort/moves').catch(() => null);
    if (m?.moves.length) mediaScan(m.moves.flat());
  }
  async function start() {
    msg = '';
    const r = await postJ<{ ok: boolean; error?: string }>('/api/sort', { scope: target.scope, ids: target.ids, kinds: [...kinds].join(',') }).catch(() => ({ ok: false, error: t('移動できませんでした') }));
    if (!r.ok) return void (msg = r.error ? server(r.error) : t('移動できませんでした'));
    const res = await follow();
    await tellGallery();
    msg = res.failed ? t('{0} 枚を移動しました（{1} 枚はできませんでした）', res.done, res.failed) : t('{0} 枚を移動しました', res.done);
    kinds = new Set();
    await count();
  }
  async function undo() {
    msg = '';
    const before = ANDROID ? await getJ<{ moves: [string, string][] }>('/api/sort/moves').catch(() => null) : null;
    const r = await postJ<{ ok: boolean; error?: string }>('/api/sort/undo').catch(() => ({ ok: false, error: t('元に戻せませんでした') }));
    if (!r.ok) return void (msg = r.error ? server(r.error) : t('元に戻せませんでした'));
    const res = await follow();
    if (before?.moves.length) mediaScan(before.moves.flat());
    msg = t('{0} 枚を元の場所に戻しました', res.done);
    await count();
  }
  const busy = $derived(!!(job as Job | null)?.running);
</script>

<!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
<div class="modal" onclick={() => !busy && onclose()}>
  <!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
  <div class="dialog" onclick={(e) => e.stopPropagation()}>
    <div class="head">
      <b>{t('サブフォルダに分ける')}</b>
      {#if target.label}<span class="what">{target.label}</span>{/if}
    </div>
    {#if !target.scope && !target.ids}
      <p class="sub">{t('分けるフォルダを選んでください')}</p>
      <span class="search">{@html svg('folder', 16)}<input bind:value={query} placeholder={t('フォルダ名')} /></span>
      <div class="list">
        {#each shown as e (e.token)}
          <button class="frow" class:top={e.top} style:padding-left={`${12 + (query ? 0 : e.indent * 18)}px`} onclick={() => pickFolder(e)}>
            <span class="name">{e.row.name}</span><span class="n num">{e.row.total}</span>
          </button>
        {:else}
          <p class="sub">{entries ? t('該当なし') : '…'}</p>
        {/each}
      </div>
    {:else}
      <p class="sub">{t('選んだ評価の写真を、それぞれ今あるフォルダの中の「見送り」「好評」「超好評」フォルダへ移します。評価はそのまま残り、あとで元に戻せます。')}</p>
      <div class="kinds">
        {#each KINDS as [k, name] (k)}
          <button class="kind" class:on={kinds.has(k)} disabled={busy || !counts[k]} onclick={() => toggle(k)}>
            <span class="ck">{#if kinds.has(k)}{@html svg('check', 15)}{/if}</span>{name}<span class="num n">{counts[k] ?? '…'}</span>
          </button>
        {/each}
      </div>
      {#if ANDROID && !allowed}
        <div class="allow">
          <p>{t('写真を移動するには、最初に一度だけ「すべてのファイルへのアクセス」を許可してください（Android の設定画面が開きます）。')}</p>
          <button class="btn primary" onclick={askMove}>{t('許可する')}</button>
        </div>
      {/if}
      {#if job?.running}
        <div class="progress"><span style:width={`${job.total ? ((job.done + job.failed) / job.total) * 100 : 0}%`}></span></div>
      {/if}
      {#if msg}<p class="msg">{msg}</p>{/if}
      <div class="row">
        {#if undoable}<button class="btn" disabled={busy} onclick={undo}>{t('元に戻す（{0} 枚）', undoable)}</button>{/if}
        <span class="grow"></span>
        <button class="btn" disabled={busy} onclick={onclose}>{t('閉じる')}</button>
        <button class="btn primary" disabled={busy || !chosen || (ANDROID && !allowed)} onclick={start}>{t('移動する')}{chosen ? t('（{0} 枚）', chosen) : ''}</button>
      </div>
    {/if}
  </div>
</div>

<style>
  .modal {
    position: fixed;
    inset: 0;
    z-index: 40;
    display: grid;
    place-items: center;
    padding: 16px;
    background: rgba(0, 0, 0, 0.6);
    backdrop-filter: blur(6px);
  }
  .dialog {
    width: min(520px, 100%);
    max-height: min(680px, 90vh);
    display: flex;
    flex-direction: column;
    gap: 12px;
    padding: 18px;
    border-radius: 20px;
    background: var(--surface);
    border: 1px solid var(--line);
    overflow: hidden;
  }
  .head {
    display: flex;
    flex-direction: column;
    gap: 2px;
  }
  .head b {
    font-size: 16px;
  }
  .what {
    color: var(--accent-ink);
    font-size: 13px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .sub,
  .msg,
  .allow p {
    margin: 0;
    color: var(--muted);
    font-size: 13px;
    line-height: 1.6;
  }
  .msg {
    color: var(--text);
  }
  .search {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 6px 14px;
    border-radius: 999px;
    border: 1px solid var(--line);
    color: var(--muted);
  }
  .search input {
    flex: 1;
    border: none;
    background: transparent;
    color: var(--text);
    font: inherit;
    outline: none;
  }
  .list {
    flex: 1;
    min-height: 120px;
    overflow-y: auto;
  }
  .frow {
    width: 100%;
    display: flex;
    gap: 10px;
    padding: 10px 12px;
    border: none;
    border-radius: 10px;
    background: transparent;
    text-align: left;
    cursor: pointer;
  }
  .frow:hover {
    background: var(--surface-2);
  }
  .frow.top .name {
    font-weight: 700;
  }
  .name {
    flex: 1;
    min-width: 0;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .n {
    color: var(--muted);
    font-size: 12.5px;
  }
  .kinds {
    display: flex;
    flex-direction: column;
    gap: 6px;
  }
  .kind {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 11px 14px;
    border-radius: 12px;
    border: 1px solid var(--line);
    background: var(--surface-2);
    text-align: left;
    cursor: pointer;
  }
  .kind .n {
    margin-left: auto;
  }
  .kind.on {
    border-color: var(--accent);
    background: color-mix(in srgb, var(--accent) 14%, var(--surface-2));
  }
  .kind:disabled {
    opacity: 0.45;
    cursor: default;
  }
  .ck {
    width: 18px;
    height: 18px;
    border-radius: 6px;
    border: 1px solid var(--line);
    display: grid;
    place-items: center;
    color: var(--accent-ink);
  }
  .allow {
    display: flex;
    flex-direction: column;
    gap: 8px;
    padding: 12px;
    border-radius: 12px;
    background: color-mix(in srgb, var(--accent) 10%, transparent);
  }
  .allow .btn {
    align-self: flex-start;
  }
  .progress {
    height: 6px;
    border-radius: 3px;
    background: var(--surface-2);
    overflow: hidden;
  }
  .progress span {
    display: block;
    height: 100%;
    background: var(--accent);
    transition: width 300ms;
  }
  .row {
    display: flex;
    align-items: center;
    gap: 8px;
    flex-wrap: wrap;
  }
  .grow {
    flex: 1;
  }
  .btn {
    padding: 9px 16px;
    border-radius: 999px;
    border: 1px solid var(--line);
    background: transparent;
    cursor: pointer;
    white-space: nowrap;
  }
  .btn.primary {
    background: var(--accent);
    border-color: transparent;
    color: var(--on-accent);
    font-weight: 700;
  }
  .btn:disabled {
    opacity: 0.45;
    cursor: default;
  }
</style>
