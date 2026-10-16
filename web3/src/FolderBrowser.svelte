<script lang="ts">
  import { server, t } from './lib/i18n';
  // The Android app's own folder list (/api/dirs): Android's picker cannot choose the top of the
  // storage or of Download, and some places it returns are not folders on the phone at all.
  import { onMount } from 'svelte';
  import { getJ } from './lib/api';
  import { svg } from './ui/icons';

  let { title = t('写真フォルダを選ぶ'), onpick, onclose }: { title?: string; onpick: (path: string) => void; onclose: () => void } = $props();

  interface Dirs {
    path: string;
    dirs: { name: string; path: string }[];
  }
  let cur = $state<Dirs | null>(null);
  let trail: string[] = []; // the folders above, for ←

  async function open(path: string) {
    cur = await getJ<Dirs>('/api/dirs', { path }).catch(() => ({ path, dirs: [] }));
  }
  function into(path: string) {
    trail.push(cur?.path ?? '');
    open(path);
  }
  function up() {
    open(trail.pop() ?? '');
  }
  onMount(() => open(''));

  const shown = (p: string) => p.replace(/^\/storage\/emulated\/0/, t('内部ストレージ')).replace(/^\/storage\//, t('SD カード ')).replaceAll('/', ' › ');
</script>

<!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
<div class="modal" onclick={onclose}>
  <!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
  <div class="dialog" onclick={(e) => e.stopPropagation()}>
    <div class="head">
      <button class="icon" title={t('上のフォルダ')} disabled={!cur?.path} onclick={up}>{@html svg('undo', 18)}</button>
      <div class="where">
        <b>{title}</b>
        <span>{cur?.path ? shown(cur.path) : t('場所を選んでください')}</span>
      </div>
    </div>
    <div class="list">
      {#if cur}
        {#each cur.dirs as d (d.path)}
          <button class="row" onclick={() => into(d.path)}>
            <span class="ic">{@html svg('folder', 18)}</span><span class="name">{cur?.path ? d.name : server(d.name)}</span><span class="chev">›</span>
          </button>
        {:else}
          <p class="empty">{cur.path ? t('この中にフォルダはありません') : t('見つかりません。写真へのアクセスを許可してください。')}</p>
        {/each}
      {/if}
    </div>
    <div class="foot">
      <button class="btn" onclick={onclose}>{t('やめる')}</button>
      <button class="btn primary" disabled={!cur?.path} onclick={() => cur && onpick(cur.path)}>{t('このフォルダにする')}</button>
    </div>
  </div>
</div>

<style>
  .modal {
    position: fixed;
    inset: 0;
    z-index: 40;
    display: grid;
    place-items: center;
    background: rgba(0, 0, 0, 0.6);
    backdrop-filter: blur(8px);
    animation: fade 160ms var(--ease-out);
  }
  @keyframes fade {
    from {
      opacity: 0;
    }
  }
  .dialog {
    width: min(520px, 94vw);
    height: min(640px, 88vh);
    display: flex;
    flex-direction: column;
    border-radius: 22px;
    background: var(--surface);
    border: 1px solid var(--line);
    overflow: hidden;
    animation: pop 220ms var(--ease-out);
  }
  @keyframes pop {
    from {
      scale: 0.95;
      opacity: 0;
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
  .where {
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 2px;
  }
  .where span {
    color: var(--muted);
    font-size: 12.5px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .icon {
    flex: none;
    width: 38px;
    height: 38px;
    border-radius: 50%;
    border: none;
    display: grid;
    place-items: center;
    background: var(--surface-2);
    cursor: pointer;
  }
  .icon:disabled {
    opacity: 0.3;
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
    gap: 12px;
    padding: 13px 12px;
    border: none;
    border-radius: 12px;
    background: transparent;
    text-align: left;
    cursor: pointer;
  }
  .row:active,
  .row:hover {
    background: var(--surface-2);
  }
  .ic {
    display: flex;
    color: var(--accent-ink);
  }
  .name {
    flex: 1;
    min-width: 0;
    font-size: 15px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .chev {
    color: var(--muted);
    font-size: 20px;
  }
  .empty {
    margin: 24px 12px;
    color: var(--muted);
    text-align: center;
    font-size: 14px;
  }
  .btn {
    padding: 10px 18px;
    border-radius: 999px;
    border: 1px solid var(--line);
    background: transparent;
    cursor: pointer;
  }
  .btn.primary {
    background: var(--accent);
    color: var(--on-accent);
    border-color: transparent;
    font-weight: 700;
  }
  .btn:disabled {
    opacity: 0.4;
  }
</style>
