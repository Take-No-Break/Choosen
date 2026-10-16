<script lang="ts">
  import { server, t } from './lib/i18n';
  // First run: no photo folder yet. A short show of what Choosen does (a card stack that swipes
  // itself: like, super like, pass) and one button that picks the folder; then the swipe screen.
  import { getJ, postJ, ANDROID, type State } from './lib/api';
  import { svg } from './ui/icons';
  import FolderBrowser from './FolderBrowser.svelte';

  let { onready }: { onready: (s: State) => void } = $props();

  let busy = $state(false);
  let browsing = $state(false);
  let error = $state('');

  async function choose() {
    error = '';
    if (ANDROID) return void (browsing = true); // the app's own folder list
    const r = await postJ<{ path: string }>('/api/pickfolder').catch(() => ({ path: '' }));
    if (r.path) await use(r.path);
  }
  async function use(path: string) {
    browsing = false;
    busy = true;
    const r = await postJ<{ ok: boolean; error?: string }>('/api/config', { root: path }).catch(() => ({ ok: false, error: '' }));
    if (!r.ok) {
      busy = false;
      error = r.error ? server(r.error) : t('選べませんでした。もう一度お試しください。');
    }
    // the new state at once: a live push comes only once the folder changes something
    else onready(await getJ<State>('/api/state'));
  }
</script>

<div class="welcome">
  <div class="stage" aria-hidden="true">
    <div class="card c3"></div>
    <div class="card c2"></div>
    <div class="card c1">
      <span class="badge like">{@html svg('heart', 54)}</span>
      <span class="badge super">{@html svg('star', 54)}</span>
      <span class="badge pass">{@html svg('x', 54)}</span>
      <span class="shine"></span>
    </div>
  </div>

  <div class="copy">
    <h1>Choosen</h1>
    <p class="tag">{t('スワイプで、好きな一枚を選び抜く')}</p>
    <div class="steps">
      <span>{@html svg('folder', 16)}{t('フォルダを選ぶ')}</span>
      <i>›</i>
      <span>{@html svg('heart', 16)}{t('スワイプ')}</span>
      <i>›</i>
      <span>{@html svg('chart', 16)}{t('ランキング')}</span>
    </div>
    <button class="go" disabled={busy} onclick={choose}>
      {#if busy}<span class="spin"></span>{t('読み込み中…')}{:else}{@html svg('folder', 20)}{t('写真フォルダを選ぶ')}{/if}
    </button>
    {#if error}<p class="err">{error}</p>{/if}
    <p class="fine">{t('写真は変更も送信もされません')}</p>
  </div>
</div>

{#if browsing}
  <FolderBrowser onpick={use} onclose={() => (browsing = false)} />
{/if}

<style>
  .welcome {
    position: fixed;
    inset: 0;
    overflow: hidden;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: clamp(18px, 5vh, 44px);
    padding: 24px 16px;
    background: #0b0c0f;
  }

  /* a card stack that swipes itself: right (like), up (super like), left (pass) */
  .stage {
    position: relative;
    width: min(56vw, 230px);
    aspect-ratio: 3 / 4;
    animation: rise 700ms var(--ease-out) both;
  }
  .card {
    position: absolute;
    inset: 0;
    border-radius: 22px;
    box-shadow:
      0 2px 8px rgba(0, 0, 0, 0.4),
      0 30px 70px rgba(0, 0, 0, 0.5);
  }
  .c3 {
    background: #1c1f25;
    rotate: -8deg;
    scale: 0.9;
    translate: -14px 10px;
  }
  .c2 {
    background: #272b33;
    rotate: 6deg;
    scale: 0.95;
    translate: 12px 6px;
  }
  .c1 {
    overflow: hidden;
    background: #0096fa;
    animation: swipe 7.5s cubic-bezier(0.55, 0, 0.35, 1) infinite;
  }
  .shine {
    position: absolute;
    inset: -40%;
    background: linear-gradient(105deg, transparent 40%, rgba(255, 255, 255, 0.28) 50%, transparent 60%);
    animation: shine 7.5s linear infinite;
  }
  .badge {
    position: absolute;
    inset: 0;
    display: grid;
    place-items: center;
    opacity: 0;
    filter: drop-shadow(0 0 18px currentColor);
  }
  .badge.like {
    color: var(--like);
    animation: b-like 7.5s linear infinite;
  }
  .badge.super {
    color: var(--super);
    animation: b-super 7.5s linear infinite;
  }
  .badge.pass {
    color: var(--pass);
    animation: b-pass 7.5s linear infinite;
  }
  @keyframes swipe {
    0%, 6% { transform: none; opacity: 1; }
    20% { transform: translate(18%, -2%) rotate(9deg); opacity: 1; }
    28% { transform: translate(170%, -8%) rotate(24deg); opacity: 0; }
    29% { transform: translate(0, 30%) scale(0.9); opacity: 0; }
    35%, 39% { transform: none; opacity: 1; }
    53% { transform: translate(0, -14%); opacity: 1; }
    61% { transform: translate(0, -170%) scale(0.95); opacity: 0; }
    62% { transform: translate(0, 30%) scale(0.9); opacity: 0; }
    68%, 72% { transform: none; opacity: 1; }
    86% { transform: translate(-18%, -2%) rotate(-9deg); opacity: 1; }
    94% { transform: translate(-170%, -8%) rotate(-24deg); opacity: 0; }
    95% { transform: translate(0, 30%) scale(0.9); opacity: 0; }
    100% { transform: none; opacity: 1; }
  }
  @keyframes b-like {
    0%, 8% { opacity: 0; scale: 0.6; }
    20%, 27% { opacity: 1; scale: 1; }
    29%, 100% { opacity: 0; }
  }
  @keyframes b-super {
    0%, 41% { opacity: 0; scale: 0.6; }
    53%, 60% { opacity: 1; scale: 1; }
    62%, 100% { opacity: 0; }
  }
  @keyframes b-pass {
    0%, 74% { opacity: 0; scale: 0.6; }
    86%, 93% { opacity: 1; scale: 1; }
    95%, 100% { opacity: 0; }
  }
  @keyframes shine {
    from {
      translate: -60% 0;
    }
    to {
      translate: 60% 0;
    }
  }

  .copy {
    position: relative;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 14px;
    text-align: center;
    animation: rise 700ms 120ms var(--ease-out) both;
  }
  @keyframes rise {
    from {
      opacity: 0;
      translate: 0 18px;
    }
  }
  h1 {
    margin: 0;
    font-size: clamp(40px, 11vw, 64px);
    font-weight: 900;
    letter-spacing: 0.02em;
    color: #0096fa;
  }
  .tag {
    margin: -6px 0 0;
    color: rgba(255, 255, 255, 0.78);
    font-size: 15px;
  }
  .steps {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    justify-content: center;
    gap: 6px;
    color: rgba(255, 255, 255, 0.7);
    font-size: 12.5px;
  }
  .steps span {
    display: flex;
    align-items: center;
    gap: 5px;
    padding: 5px 10px;
    border-radius: 999px;
    background: rgba(255, 255, 255, 0.07);
    border: 1px solid rgba(255, 255, 255, 0.1);
  }
  .steps i {
    font-style: normal;
    color: rgba(255, 255, 255, 0.35);
  }
  .go {
    margin-top: 8px;
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 16px 30px;
    border: none;
    border-radius: 999px;
    background: #0096fa;
    color: #ffffff;
    font-size: 17px;
    font-weight: 800;
    cursor: pointer;
    box-shadow:
      0 10px 36px rgba(0, 150, 250, 0.4);
    animation: glow 2.6s ease-in-out infinite;
    transition: scale 120ms var(--ease-out);
  }
  .go:active {
    scale: 0.96;
  }
  .go:disabled {
    animation: none;
    opacity: 0.8;
  }
  @keyframes glow {
    50% {
      box-shadow:
        0 10px 52px rgba(0, 150, 250, 0.7);
    }
  }
  .spin {
    width: 16px;
    height: 16px;
    border-radius: 50%;
    border: 2px solid rgba(0, 0, 0, 0.2);
    border-top-color: #15171b;
    animation: turn 700ms linear infinite;
  }
  @keyframes turn {
    to {
      rotate: 360deg;
    }
  }
  .err {
    margin: 0;
    color: var(--pass);
    font-size: 13.5px;
  }
  .fine {
    margin: 0;
    color: rgba(255, 255, 255, 0.45);
    font-size: 12px;
  }
  @media (prefers-reduced-motion: reduce) {
    .c1,
    .shine,
    .badge,
    .go {
      animation: none;
    }
  }
</style>
