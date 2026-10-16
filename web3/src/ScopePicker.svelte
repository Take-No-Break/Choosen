<script lang="ts">
  import { t } from './lib/i18n';
  // 範囲: the folders to swipe or show. A searchable tree of the photo folders' folders with check marks;
  // nothing checked = everything. Returns the scope tokens /api/next reads ("/*" = the first photo
  // folder itself when there are several).
  import { onMount } from 'svelte';
  import { getJ } from './lib/api';
  import { buildEntries, scopeLabel, type Entry, type FolderRow } from './lib/scope';
  import { svg } from './ui/icons';

  let { current, onpick, onclose }: { current: string[]; onpick: (tokens: string[], label: string) => void; onclose: () => void } = $props();

  let entries = $state.raw<Entry[] | null>(null);
  // svelte-ignore state_referenced_locally
  let sel = $state(new Set(current)); // where it opens; then the user's choice
  let query = $state('');

  onMount(async () => {
    const r = await getJ<{ folders: FolderRow[] }>('/api/folders').catch(() => ({ folders: [] as FolderRow[] }));
    entries = buildEntries(r.folders);
  });

  const shown = $derived(
    !entries ? [] : query ? entries.filter((e) => e.row.name.toLowerCase().includes(query.toLowerCase())) : entries,
  );

  function toggle(t: string) {
    const s = new Set(sel);
    if (s.has(t)) s.delete(t);
    else s.add(t);
    sel = s;
  }

  function done() {
    const tokens = [...sel];
    onpick(tokens, scopeLabel(tokens, entries ?? []));
  }
</script>

<!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
<div class="modal" onclick={onclose}>
  <!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
  <div class="dialog" onclick={(e) => e.stopPropagation()}>
    <div class="head">
      <span class="search">{@html svg('folder', 16)}<input bind:value={query} placeholder={t('フォルダ名')} /></span>
      <span class="count num">{sel.size || t('すべて')}</span>
    </div>
    <div class="list">
      {#each shown as e (e.token)}
        <button class="row" class:on={sel.has(e.token)} class:top={e.top} style:padding-left={`${12 + (query ? 0 : e.indent * 18)}px`} onclick={() => toggle(e.token)}>
          <span class="check">{#if sel.has(e.token)}{@html svg('check', 14)}{/if}</span>
          <span class="name">{e.row.name}</span>
          <span class="n num">{e.row.total}</span>
        </button>
      {:else}
        <p class="empty">{entries ? t('該当なし') : '…'}</p>
      {/each}
    </div>
    <div class="foot">
      <button class="btn" onclick={() => onpick([], t('すべて'))}>{t('すべて')}</button>
      <button class="btn primary" onclick={done}>{t('決定')}</button>
    </div>
  </div>
</div>

<style>
  .modal {
    position: fixed;
    inset: 0;
    z-index: 30;
    display: grid;
    place-items: center;
    background: rgba(0, 0, 0, 0.55);
    backdrop-filter: blur(6px);
    animation: fade 160ms var(--ease-out);
  }
  @keyframes fade {
    from {
      opacity: 0;
    }
  }
  .dialog {
    width: min(520px, 94vw);
    height: min(600px, 86vh);
    display: flex;
    flex-direction: column;
    border-radius: 20px;
    background: var(--surface);
    overflow: hidden;
    animation: pop 200ms var(--ease-out);
  }
  @keyframes pop {
    from {
      scale: 0.95;
    }
  }
  .head,
  .foot {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 12px 14px;
  }
  .foot {
    justify-content: flex-end;
    border-top: 1px solid var(--line);
  }
  .search {
    flex: 1;
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
  .count {
    color: var(--muted);
    font-size: 13px;
  }
  .list {
    flex: 1;
    overflow-y: auto;
    padding: 0 8px;
  }
  .row {
    width: 100%;
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 8px 12px;
    border: none;
    border-radius: 10px;
    background: transparent;
    cursor: pointer;
    text-align: left;
  }
  .row:hover {
    background: var(--surface-2);
  }
  .row.on {
    background: color-mix(in srgb, var(--accent) 14%, transparent);
  }
  .row.top .name {
    font-weight: 700;
  }
  .check {
    width: 18px;
    height: 18px;
    border-radius: 6px;
    border: 1px solid var(--line);
    display: grid;
    place-items: center;
    color: var(--accent-ink);
  }
  .name {
    flex: 1;
    font-size: 13.5px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .n {
    color: var(--muted);
    font-size: 12px;
  }
  .empty {
    color: var(--muted);
    text-align: center;
  }
  .btn {
    padding: 8px 18px;
    border-radius: 999px;
    border: 1px solid var(--line);
    background: transparent;
    cursor: pointer;
  }
  .btn.primary {
    background: var(--accent);
    color: var(--on-accent);
    border-color: transparent;
    font-weight: 600;
  }
</style>
