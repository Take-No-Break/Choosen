<script lang="ts">
  import { fx } from './lib/fx.svelte';
  import { t } from './lib/i18n';
  // Choosen V3: the screens and the bar between them (a side rail on a PC, a bottom bar on a
  // phone): ギャラリー · Choosen · ランキング, Choosen in the middle. 設定 opens from the ⚙ in the
  // ranking's corner (and at the foot of the PC's rail). Every screen stays mounted, so switching
  // keeps its place.
  import { onMount } from 'svelte';
  import { getJ, watchState, whenUnpaired, pairFromLink, useScreenFit, ANDROID, PHONE_APP, leavePc, type State } from './lib/api';
  import Pair from './Pair.svelte';
  import Welcome from './Welcome.svelte';
  import { svg } from './ui/icons';
  import Swipe from './Swipe.svelte';
  import Gallery from './Gallery.svelte';
  import Ranking from './Ranking.svelte';
  import Settings from './Settings.svelte';
  import { nav, type Tab } from './lib/nav.svelte';

  const BAR: { id: Tab; icon: 'logo' | 'layers' | 'crown'; label: string }[] = [
    { id: 'gallery', icon: 'layers', label: t('ギャラリー') },
    { id: 'swipe', icon: 'logo', label: 'Choosen' },
    { id: 'ranking', icon: 'crown', label: t('ランキング') },
  ];
  // a folder to open in the gallery (from the ranking); n changes on every request
  let folderRequest: { path: string; name: string; n: number } | null = $state(null);
  function openFolder(f: { path: string; name: string }) {
    folderRequest = { ...f, n: (folderRequest?.n ?? 0) + 1 };
    show('gallery');
  }

  let st = $state<State | null>(null);
  let failed = $state(false);
  let unpaired = $state(false);
  let cardButtons = $state(true); // ▼ on the card hides the bottom bar too (phones)
  let linkFailed = $state(false);
  // a phone on the PC's library: settings stay on the PC (the server refuses them too)
  const shown = $derived(nav.tab);
  // the effects level (演出) for CSS: html[data-fx] (the background's waves follow it)
  $effect(() => {
    document.documentElement.dataset.fx = fx.level;
  }); // a phone on the PC's library has 設定 too (its own settings only)
  const show = (t: Tab) => nav.go(t);

  // only /api/state says whether this is a phone; the live pushes do not repeat it
  let phone = false;
  // The Android app's back button asks the page first (one press): an open full view, menu or
  // dialog closes; another tab goes back to スワイプ; on スワイプ the previous picture comes back.
  // false = nothing to do here, so the app goes to the home screen (as two quick presses always do).
  let swipe: Swipe | undefined = $state();
  (window as unknown as { choosenBack?: () => boolean }).choosenBack = () => {
    if (document.querySelector('.viewer')) {
      dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', code: 'Escape' }));
      return true;
    }
    const overlay = document.querySelector<HTMLElement>('.modal, .scrim');
    if (overlay) {
      overlay.click(); // their backgrounds close them
      return true;
    }
    if (shown !== 'swipe') {
      show('swipe');
      return true;
    }
    return swipe?.systemBack() ?? false;
  };

  function arrive(s: State) {
    st = phone ? { ...s, remote: true } : s;
  }

  onMount(() => {
    whenUnpaired(() => (unpaired = true));
    let stop = () => {};
    let gone = false;
    (async () => {
      linkFailed = (await pairFromLink()) === false; // before the first request, so it carries the token
      try {
        const first = await getJ<State>('/api/state');
        phone = !!first.remote;
        useScreenFit(phone || PHONE_APP);
        arrive(first);
      } catch {
        failed = !unpaired;
        return;
      }
      if (!gone) stop = watchState(arrive);
    })();
    return () => {
      gone = true;
      stop();
    };
  });
</script>

{#if unpaired}
  <Pair expired={linkFailed} />
{:else if st && st.hasRoot}
  <div class="shell" class:immersive={shown === 'swipe' && !cardButtons}>
    <nav>
      {#each BAR as t (t.id)}
        <button class:on={shown === t.id} title={t.label} onclick={() => show(t.id)}>
          {@html svg(t.icon, 22)}<span>{t.label}</span>
        </button>
      {/each}
      <span class="foot"></span>
      {#if !st.remote}
        <button class="rail-only" class:on={shown === 'settings'} title={t('設定')} onclick={() => show('settings')}>{@html svg('gear', 22)}<span>{t('設定')}</span></button>
      {:else if ANDROID}
        <button class="rail-only" title={t('スマホのライブラリに戻る')} onclick={leavePc}>{@html svg('phone', 22)}<span>{t('スマホ')}</span></button>
      {/if}
    </nav>
    <main>
      <div class="screen" class:hidden={shown !== 'swipe'}><Swipe bind:this={swipe} {st} active={shown === 'swipe'} onbuttons={(v) => (cardButtons = v)} /></div>
      <div class="screen" class:hidden={shown !== 'ranking'}><Ranking {st} active={shown === 'ranking'} onopen={openFolder} /></div>
      <div class="screen" class:hidden={shown !== 'gallery'}><Gallery {st} active={shown === 'gallery'} request={folderRequest} /></div>
      <div class="screen" class:hidden={shown !== 'settings'}><Settings {st} /></div>
    </main>
  </div>
{:else if st && st.remote}
  <div class="empty">{t('PC の Choosen で写真フォルダを選んでください')}</div>
{:else if st}
  <Welcome onready={arrive} />
{:else if failed}
  <div class="empty">{t('Choosen に接続できません')}</div>
{/if}

<style>
  .shell {
    position: fixed;
    inset: 0;
    display: flex;
  }
  nav {
    flex: none;
    width: 76px;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 6px;
    padding-top: 14px;
    background: var(--nav-bg);
    border-right: 1px solid var(--line);
    z-index: 10;
  }
  nav button {
    width: 64px;
    padding: 8px 0 6px;
    border: none;
    border-radius: 14px;
    background: transparent;
    color: var(--muted);
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 3px;
    font-size: 11px;
    cursor: pointer;
    transition: background 150ms;
  }
  nav button.on {
    color: var(--text);
    background: var(--surface-2);
  }
  nav .foot {
    flex: 1;
  }
  nav .rail-only {
    margin-bottom: 12px;
  }
  main {
    position: relative;
    flex: 1;
    min-width: 0;
  }
  .screen {
    position: absolute;
    inset: 0;
  }
  /* below the camera on a phone (the swipe screen handles the top itself: its picture runs under it) */
  .screen:not(:first-child) {
    top: var(--safe-top, env(safe-area-inset-top, 0px));
  }
  .screen.hidden {
    visibility: hidden;
    pointer-events: none;
  }
  /* a phone: the bar goes to the bottom */
  @media (max-width: 759px) {
    .shell {
      flex-direction: column-reverse;
    }
    nav {
      width: auto;
      height: calc(62px + env(safe-area-inset-bottom));
      flex-direction: row;
      justify-content: space-around;
      padding: 4px 0 env(safe-area-inset-bottom);
      border-right: none;
      border-top: 1px solid var(--line);
    }
    .shell.immersive nav {
      display: none;
    }
    nav .foot,
    nav .rail-only {
      display: none; /* on a phone 設定 opens from the ⚙ in a screen's corner */
    }
  }
  .empty {
    height: 100%;
    display: grid;
    place-items: center;
    color: var(--muted);
  }
</style>
