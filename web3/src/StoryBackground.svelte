<script lang="ts">
  import { onDestroy } from 'svelte';

  let { src }: { src: string } = $props();
  let current = $state('');
  let previous = $state('');
  let retire = 0;
  let shownAt = -1e9;

  // Keep the decoded background until the next one is ready, including a slow/failed load.
  $effect(() => {
    let cancelled = false;
    const next = src;
    if (!next) {
      current = previous = '';
      clearTimeout(retire);
      return;
    }
    const image = new Image();
    image.src = next;
    image.decode().then(() => {
      if (cancelled || next === current) return;
      clearTimeout(retire);
      // the one still fading in gives way; the one fully shown before it stays underneath
      if (performance.now() - shownAt >= 600 || !previous) previous = current;
      current = next;
      shownAt = performance.now();
      retire = window.setTimeout(() => (previous = ''), 650);
    }).catch(() => {});
    return () => { cancelled = true; };
  });
  onDestroy(() => clearTimeout(retire));
</script>

<div class="story-background" aria-hidden="true">
  {#if previous}<img src={previous} alt="" />{/if}
  {#if current}
    {#key current}<img class="current" src={current} alt="" />{/key}
  {/if}
</div>

<style>
  .story-background {
    position: absolute;
    inset: 0;
    z-index: 0;
    overflow: hidden;
    pointer-events: none;
  }
  img {
    position: absolute;
    inset: -48px;
    width: calc(100% + 96px);
    height: calc(100% + 96px);
    object-fit: cover;
    filter: blur(24px) brightness(0.7);
    pointer-events: none;
  }
  .current { animation: arrive 600ms ease-out; }
  @keyframes arrive { from { opacity: 0; } to { opacity: 1; } }
  @media (prefers-reduced-motion: reduce) { .current { animation: none; } }
</style>
