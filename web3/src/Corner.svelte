<script lang="ts">
  import { t } from './lib/i18n';
  // The top-right corner of the ranking: ⚙ opens 設定 (the gallery's top line is all its own tools);
  // on a phone using the PC's library it holds that phone's own settings. In the Android app showing
  // a PC's library, the phone icon beside it leads back to the phone's own library.
  import { ANDROID, leavePc, type State } from './lib/api';
  import { svg } from './ui/icons';
  import { nav } from './lib/nav.svelte';

  let { st }: { st: State } = $props();
</script>

{#if st.remote && ANDROID}
  <button class="corner" title={t('スマホのライブラリに戻る')} onclick={leavePc}>{@html svg('phone', 23)}</button>
{/if}
<button class="corner" title={t('設定')} onclick={() => nav.go('settings')}>{@html svg('gear', 25)}</button>

<style>
  .corner {
    flex: none;
    width: 44px;
    height: 44px;
    border-radius: 50%;
    border: none;
    display: grid;
    place-items: center;
    background: transparent;
    color: var(--muted);
    cursor: pointer;
  }
  .corner:hover {
    background: var(--surface-2);
    color: var(--text);
  }
</style>
