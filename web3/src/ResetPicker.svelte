<script lang="ts">
  import { t } from './lib/i18n';
  // 設定 → ランキングデータを初期化: one folder at a time (as the ranking counts them), never the whole
  // library. Pick a folder, see how many pictures and ratings it has, then はい / いいえ. The core
  // withdraws the folder's ratings (archived pictures stay archived); the app cannot bring them back.
  import { onMount } from 'svelte';
  import { getJ, postJ } from './lib/api';
  import { buildEntries, type Entry, type FolderRow } from './lib/scope';
  import { svg } from './ui/icons';

  let { onclose }: { onclose: () => void } = $props();

  let entries = $state.raw<Entry[] | null>(null);
  let query = $state('');
  onMount(async () => {
    const r = await getJ<{ folders: FolderRow[] }>('/api/folders').catch(() => ({ folders: [] as FolderRow[] }));
    entries = buildEntries(r.folders);
  });
  const shown = $derived(
    !entries ? [] : query ? entries.filter((e) => e.row.name.toLowerCase().includes(query.toLowerCase())) : entries,
  );

  // the folder chosen, with its counts (asked of the core without changing anything)
  let ask: { entry: Entry; images: number; ratings: number } | null = $state(null);
  let busy = $state(false);
  let done = $state('');
  async function choose(entry: Entry) {
    done = '';
    const r = await getJ<{ images: number; ratings: number }>('/api/reset', { scope: entry.token }).catch(() => null);
    if (r) ask = { entry, images: r.images, ratings: r.ratings };
  }
  async function reset() {
    if (!ask) return;
    busy = true;
    const r = await postJ<{ reset?: boolean; images?: number }>('/api/reset', { scope: ask.entry.token }).catch(() => null);
    busy = false;
    done = r?.reset ? t('「{0}」のランキングデータを初期化しました（{1} 枚）', ask.entry.row.name, r.images ?? 0) : r ? t('評価された画像はありませんでした') : t('初期化できませんでした');
    ask = null;
  }
</script>

<!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
<div class="modal" onclick={() => (ask ? (ask = null) : onclose())}>
  <!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
  <div class="dialog" onclick={(e) => e.stopPropagation()}>
    {#if ask}
      <div class="ask">
        <b>{t('「{0}」のランキングデータを初期化しますか？', ask.entry.row.name)}</b>
        {#if ask.images}
          <p>{t('{0} 枚（{1} 件の評価）が未評価に戻り、ランキングから外れます。アーカイブはそのままです。', ask.images, ask.ratings)}</p>
          <p class="warn">{t('消したデータは取り戻せません。')}</p>
          <div class="row">
            <button class="btn" onclick={() => (ask = null)}>{t('いいえ')}</button>
            <button class="btn danger" disabled={busy} onclick={reset}>{t('はい')}</button>
          </div>
        {:else}
          <p>{t('このフォルダに評価された画像はありません。')}</p>
          <div class="row"><button class="btn" onclick={() => (ask = null)}>{t('戻る')}</button></div>
        {/if}
      </div>
    {:else}
      <div class="head">
        <b>{t('ランキングデータを初期化')}</b>
        <span class="sub">{t('フォルダを 1 つ選んでください（全体はまとめて消せません）')}</span>
        <span class="search">{@html svg('folder', 16)}<input bind:value={query} placeholder={t('フォルダ名')} /></span>
      </div>
      <div class="list">
        {#each shown as e (e.token)}
          <button class="frow" class:top={e.top} style:padding-left={`${12 + (query ? 0 : e.indent * 18)}px`} onclick={() => choose(e)}>
            <span class="name">{e.row.name}</span>
            <span class="n num">{e.row.total}</span>
          </button>
        {:else}
          <p class="empty">{entries ? t('該当なし') : '…'}</p>
        {/each}
      </div>
      {#if done}<p class="done">{done}</p>{/if}
      <div class="foot"><button class="btn" onclick={onclose}>{t('閉じる')}</button></div>
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
    max-height: min(640px, 88vh);
    display: flex;
    flex-direction: column;
    border-radius: 20px;
    background: var(--surface);
    border: 1px solid var(--line);
    overflow: hidden;
  }
  .head {
    display: flex;
    flex-direction: column;
    gap: 6px;
    padding: 16px 16px 10px;
  }
  .head b {
    font-size: 16px;
  }
  .sub {
    color: var(--muted);
    font-size: 12.5px;
  }
  .search {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-top: 4px;
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
    overflow-y: auto;
    padding: 0 8px 8px;
  }
  .frow {
    width: 100%;
    display: flex;
    align-items: center;
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
  .empty,
  .done {
    margin: 8px 16px;
    color: var(--muted);
    font-size: 13px;
  }
  .foot {
    display: flex;
    justify-content: flex-end;
    padding: 10px 14px;
    border-top: 1px solid var(--line);
  }
  .ask {
    display: flex;
    flex-direction: column;
    gap: 12px;
    padding: 20px;
  }
  .ask p {
    margin: 0;
    color: var(--muted);
    font-size: 14px;
    line-height: 1.6;
  }
  .ask .warn {
    color: var(--pass);
    font-weight: 700;
  }
  .row {
    display: flex;
    justify-content: flex-end;
    gap: 8px;
  }
  .btn {
    padding: 9px 18px;
    border-radius: 999px;
    border: 1px solid var(--line);
    background: transparent;
    cursor: pointer;
  }
  .btn.danger {
    background: var(--pass);
    border-color: transparent;
    color: #fff;
    font-weight: 700;
  }
  .btn:disabled {
    opacity: 0.5;
  }
</style>
