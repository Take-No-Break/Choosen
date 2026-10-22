<script lang="ts">
  import { SEP, t } from './lib/i18n';
  // The folder ranking: one row per folder at the ranking level, best first, each with its four
  // best images. Rows slide in once; the images of a row load when it comes into view (two at a
  // time) and are kept. A row opens its folder in the gallery. A long press selects rows (then a tap
  // adds or removes one) for サブフォルダに分ける. (統計 is hidden for now; Stats.svelte keeps it.)
  import { getJ, thumbUrl, IOS, type Card, type State } from './lib/api';
  import { svg } from './ui/icons';
  import { longpress } from './lib/longpress';
  import Corner from './Corner.svelte';
  import SortSheet from './SortSheet.svelte';

  let { st, active, onopen }: { st: State; active: boolean; onopen: (f: { path: string; name: string }) => void } = $props();

  interface Row {
    path: string;
    name: string;
    depth: number;
    total: number;
    rated: number;
    liked: number;
    supers?: number;
    score: number;
    rank: number;
    rank0?: number;
    artist?: boolean;
  }

  let rows: Row[] = $state.raw([]);
  let query = $state('');
  let shownVersion = -1;
  let previews: Record<string, Card[]> = $state({});
  const asked = new Set<string>();
  const waiting: string[] = [];
  let running = 0;

  const shown = $derived(
    rows
      .filter((r) => r.artist && r.rank > 0)
      .filter((r) => !query || r.name.toLowerCase().includes(query.toLowerCase()))
      .sort((a, b) => a.rank - b.rank),
  );

  async function load() {
    shownVersion = st.version;
    try {
      const r = await getJ<{ folders: Row[] }>('/api/folders');
      rows = r.folders;
      asked.clear(); // new ratings: rows on screen ask again
    } catch {
      /* not ready yet */
    }
  }

  $effect(() => {
    if (active && st.version !== shownVersion) load();
  });

  function want(path: string) {
    if (asked.has(path)) return;
    asked.add(path);
    waiting.push(path);
    pump();
  }
  function pump() {
    while (running < 2 && waiting.length) {
      const path = waiting.shift()!;
      running++;
      best(path)
        .then((c) => (previews = { ...previews, [path]: c }))
        .catch(() => asked.delete(path))
        .finally(() => {
          running--;
          pump();
        });
    }
  }
  // a wide window shows as many of a folder's best pictures as fit beside its name (a narrow one 4);
  // a few more than fit are asked for, the strip shows only whole ones
  const fitFor = (w: number) => (w >= 760 ? Math.min(30, Math.ceil(w / 77)) : 4);
  let fit = $state(fitFor(innerWidth));
  // a wider window: rows already shown ask again for the pictures that now fit
  function resized() {
    const f = fitFor(innerWidth);
    if (f <= fit) return;
    fit = f;
    for (const path of Object.keys(previews)) {
      asked.delete(path);
      want(path);
    }
  }
  async function best(path: string): Promise<Card[]> {
    const get = (filter: string) =>
      getJ<{ items: Card[] }>('/api/items', { filter, scope: path, sort: 'score', limit: fit, offset: 0 }).then((r) => r.items);
    // its liked pictures first, then the rest of the folder by score until the strip is full
    const liked = await get('liked');
    if (liked.length >= fit) return liked;
    const have = new Set(liked.map((c) => c.id));
    return [...liked, ...(await get('all')).filter((c) => !have.has(c.id))].slice(0, fit);
  }

  // a row asks for its images when it comes into view
  function visible(node: HTMLElement, path: string) {
    const io = new IntersectionObserver((es) => es.some((e) => e.isIntersecting) && want(path), { rootMargin: '200px' });
    io.observe(node);
    return { destroy: () => io.disconnect() };
  }

  // ---- selecting folders (a long press starts it)
  let sel = $state(new Set<string>());
  let sorting = $state(false);
  function pick(path: string) {
    const next = new Set(sel);
    if (next.has(path)) next.delete(path);
    else next.add(path);
    sel = next;
  }
  const selNames = $derived(rows.filter((r) => sel.has(r.path)).map((r) => r.name));

  // the logo above the top row: away while the list is scrolled down, back at its top
  let away = $state(false);
  const BRAND_H = 50; // .brand's height
  function onScroll(e: Event) {
    const el = e.currentTarget as HTMLElement;
    const y = el.scrollTop;
    // only when the list can still scroll past the threshold without it, or folding it away would
    // shrink the list's scroll back under the threshold and bring it straight back
    if (!away && y > 24 && el.scrollHeight - el.clientHeight > BRAND_H + 24) away = true;
    else if (y < 4) away = false;
  }

  const medal = (r: number) => (r === 1 ? '#f4c542' : r === 2 ? '#c9ced6' : r === 3 ? '#d2905a' : '');
</script>

<svelte:window onresize={resized} />

<section class="ranking">
  <div class="brand" class:away aria-hidden={away}>
    <!-- the app's mark (its icon's two cards and star) and its name -->
    <svg class="mark" viewBox="30 26 54 58" width="26" height="28" aria-hidden="true">
      <g transform="rotate(-13 49 52)"><rect x="33" y="30" width="32" height="44" rx="4" fill="currentColor" opacity="0.45" /></g>
      <g transform="rotate(9 62 54)">
        <rect x="45" y="31" width="34" height="46" rx="4" fill="currentColor" />
        <path transform="translate(51.8 43.9) scale(0.85)" fill="var(--bg, #fff)" d="M12,2.8l2.8,6l6.5,0.7l-4.9,4.4l1.4,6.4L12,17l-5.8,3.3l1.4,-6.4l-4.9,-4.4l6.5,-0.7z" />
      </g>
    </svg>
    <span class="name">Choosen</span>
  </div>
  <div class="bar">
    <span class="search">{@html svg('folder', 16)}<input bind:value={query} placeholder={t('フォルダ名')} /></span>
    <span class="count num">{shown.length}</span>
    <Corner {st} />
  </div>
  <div class="list" onscroll={onScroll}>
    {#each shown as r, i (r.path)}
      {@const m = medal(r.rank)}
      {@const move = r.rank0 && r.rank0 > 0 ? r.rank0 - r.rank : 0}
      <button
        class="row"
        class:top={!!m}
        class:sel={sel.has(r.path)}
        style:--m={m || 'transparent'}
        style:--d={`${Math.min(i, 12) * 30}ms`}
        use:visible={r.path}
        use:longpress={{ onlong: () => !sel.has(r.path) && pick(r.path) }}
        onclick={() => (sel.size ? pick(r.path) : onopen({ path: r.path, name: r.name }))}
      >
        {#if sel.size}<span class="tick" class:on={sel.has(r.path)}>{#if sel.has(r.path)}{@html svg('check', 14)}{/if}</span>{/if}
        <span class="rank num" class:medal={!!m}>{r.rank}</span>
        <span class="who">
          <span class="name">{r.name}</span>
          <span class="stats num">
            <span style:color="var(--like)">{@html svg('heart', 13)}</span>{r.liked}
            <span style:color="var(--super)">{@html svg('star', 13)}</span>{r.supers ?? 0}
            {#if move}<span class:up={move > 0} class:down={move < 0}>{move > 0 ? `▲${move}` : `▼${-move}`}</span>{/if}
          </span>
        </span>
        <span class="strip">
          {#each previews[r.path] ?? [] as c (c.id)}
            <img src={thumbUrl(c, 256)} alt="" loading="lazy" />
          {:else}
            {#each Array.from({ length: Math.min(fit, 8) }, (_, k) => k) as k (k)}<span class="ph"></span>{/each}
          {/each}
        </span>
        <span class="score num" style:color={m || 'var(--text)'}>{r.score.toFixed(1)}</span>
      </button>
    {/each}
  </div>
  {#if sel.size}
    <div class="selbar">
      <button class="x" title={t('選択をやめる')} onclick={() => (sel = new Set())}>{@html svg('x', 18)}</button>
      <span class="num">{t('{0} フォルダ', sel.size)}</span>
      {#if !st.remote && !IOS}<button class="act" onclick={() => (sorting = true)}>{@html svg('folderPlus', 17)}<span class="lbl">{t('サブフォルダに分ける§button')}</span></button>{/if}
    </div>
  {/if}
</section>

{#if sorting}
  <SortSheet
    scope={[...sel].join('|')}
    label={selNames.length <= 2 ? selNames.join(SEP) : t('{0} ほか{1}', selNames[0], selNames.length - 1)}
    onclose={() => ((sorting = false), (sel = new Set()))}
  />
{/if}

<style>
  .ranking {
    position: absolute;
    inset: 0;
    display: flex;
    flex-direction: column;
  }
  .brand {
    flex: none;
    display: flex;
    align-items: center;
    gap: 9px;
    height: 50px;
    padding: 6px 18px 0;
    color: var(--accent);
    overflow: hidden;
    transition:
      height 260ms var(--ease-out),
      opacity 200ms linear,
      padding 260ms var(--ease-out);
  }
  .brand.away {
    height: 0;
    padding-top: 0;
    opacity: 0;
  }
  .brand .mark {
    flex: none;
  }
  .brand .name {
    font-size: 23px;
    font-weight: 800;
    letter-spacing: 0.01em;
  }
  .bar {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 10px 14px 6px;
  }
  .search {
    flex: 1;
    min-width: 0;
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
    padding: 4px 12px 16px;
    display: flex;
    flex-direction: column;
    gap: 8px;
  }
  .row {
    display: flex;
    align-items: center;
    gap: 14px;
    padding: 10px 16px 10px 10px;
    border-radius: 18px;
    border: 1px solid var(--line);
    background: linear-gradient(90deg, color-mix(in srgb, var(--m) 16%, transparent), transparent 60%), var(--surface);
    color: inherit;
    text-align: left;
    cursor: pointer;
    content-visibility: auto;
    contain-intrinsic-size: 96px;
    animation: slide 320ms var(--ease-out) backwards;
    animation-delay: var(--d);
    transition: transform 150ms var(--ease-out);
  }
  .row.top {
    border-color: color-mix(in srgb, var(--m) 50%, transparent);
  }
  .row:hover {
    transform: translateX(4px);
  }
  @keyframes slide {
    from {
      opacity: 0;
      translate: 0 12px;
    }
  }
  .rank {
    flex: none;
    width: 44px;
    text-align: center;
    font-size: 22px;
    font-weight: 800;
  }
  .rank.medal {
    width: 40px;
    height: 40px;
    line-height: 40px;
    border-radius: 50%;
    background: var(--m);
    color: #15171b;
    font-size: 18px;
    box-shadow: 0 0 18px color-mix(in srgb, var(--m) 55%, transparent);
  }
  .who {
    flex: 0 1 240px;
    min-width: 120px;
    display: flex;
    flex-direction: column;
    gap: 5px;
  }
  .name {
    font-weight: 700;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .stats {
    display: flex;
    align-items: center;
    gap: 4px;
    font-size: 12.5px;
    color: var(--muted);
  }
  .stats span {
    display: flex;
    margin-left: 6px;
  }
  .up {
    color: var(--like);
  }
  .down {
    color: var(--pass);
  }
  /* as many whole pictures as fit: the ones that do not fit go to rows of no height */
  .strip {
    flex: 1 1 0;
    min-width: 0;
    display: grid;
    grid-template-columns: repeat(auto-fill, 72px);
    grid-auto-rows: 72px;
    height: 72px;
    overflow: hidden;
    gap: 5px;
  }
  .strip img,
  .ph {
    width: 72px;
    height: 72px;
    border-radius: 10px;
    object-fit: cover;
    background: var(--surface-2);
    animation: pop 220ms var(--ease-out);
  }
  @keyframes pop {
    from {
      opacity: 0;
      scale: 0.9;
    }
  }
  .score {
    flex: none;
    width: 56px;
    text-align: right;
    font-size: 20px;
    font-weight: 800;
  }
  /* 白: the medal colours are too pale for text on white */
  :global([data-theme='light']) .score {
    filter: brightness(0.72) saturate(1.3);
  }
  @media (max-width: 759px) {
    .row {
      flex-wrap: wrap;
      gap: 8px 12px;
    }
    .who {
      flex: 1;
    }
    .strip {
      order: 4;
      flex: none;
      display: flex;
      height: auto;
      width: 100%;
      padding-left: 56px;
    }
    .strip img,
    .ph {
      width: calc((100% - 15px) / 4);
      height: auto;
      aspect-ratio: 1;
    }
  }
  /* selecting folders */
  .row.sel {
    background: color-mix(in srgb, var(--accent) 14%, var(--surface));
    outline: 1px solid color-mix(in srgb, var(--accent) 55%, transparent);
  }
  .tick {
    flex: none;
    width: 20px;
    height: 20px;
    border-radius: 50%;
    border: 1.5px solid var(--line);
    display: grid;
    place-items: center;
    color: var(--on-accent);
  }
  .tick.on {
    background: var(--accent);
    border-color: var(--accent);
  }
  .selbar {
    position: absolute;
    left: 10px;
    right: 10px;
    bottom: 10px;
    z-index: 5;
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 8px 8px 8px 6px;
    border-radius: 999px;
    background: var(--menu-bg);
    border: 1px solid var(--line);
    backdrop-filter: blur(14px);
    box-shadow: 0 10px 30px rgba(0, 0, 0, 0.35);
    animation: selin 200ms var(--ease-out);
  }
  @keyframes selin {
    from {
      opacity: 0;
      translate: 0 12px;
    }
  }
  .selbar .num {
    flex: 1 0 auto;
    font-size: 13.5px;
  }
  .selbar .x {
    width: 34px;
    height: 34px;
    border: none;
    border-radius: 50%;
    display: grid;
    place-items: center;
    background: transparent;
    color: var(--muted);
    cursor: pointer;
  }
  .selbar .act {
    display: flex;
    align-items: center;
    min-width: 0;
    gap: 6px;
    padding: 9px 14px;
    border: none;
    border-radius: 999px;
    background: var(--accent);
    color: var(--on-accent);
    font-weight: 700;
    cursor: pointer;
  }
  .selbar .act :global(svg) {
    flex-shrink: 0;
  }
  .selbar .act .lbl {
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
  }
</style>
