<script lang="ts">
  import { t } from './lib/i18n';
  // 統計: swipes per day (7 / 30 days) or per week (1 year) as stacked bars in the verdict colours,
  // the like rate as a ring and a few numbers. Hand-made, no chart library.
  import { getJ, type State } from './lib/api';
  import { summarize, swipes, weeks, shortDay, type Day } from './lib/stats';
  import { svg } from './ui/icons';

  let { st, active }: { st: State; active: boolean } = $props();

  const PERIODS = [
    { days: 7, label: t('7日') },
    { days: 30, label: t('30日') },
    { days: 366, label: t('1年') },
  ];
  let period = $state(30);
  interface Stats {
    rated: number;
    liked: number;
    supers: number;
    days: Day[];
  }
  let data = $state.raw<Stats | null>(null);
  let shown = ''; // "version/period" on screen
  let hover: number | null = $state(null);

  $effect(() => {
    const key = `${st.version}/${period}`;
    if (active && key !== shown) {
      shown = key;
      getJ<Stats>('/api/stats', { days: period })
        .then((r) => (data = r))
        .catch(() => (shown = ''));
    }
  });

  const bars = $derived(!data ? [] : period > 31 ? weeks(data.days) : data.days);
  const sum = $derived(summarize(data?.days ?? []));
  const scale = $derived(Math.max(1, ...bars.map(swipes)));
  const overall = $derived(data && data.rated ? data.liked / data.rated : 0);
  const R = 52, C = 2 * Math.PI * R;
  const SEGMENTS = ['like', 'super', 'pass', 'archive', 'hold'] as const;
  const pct = (x: number | null) => (x === null ? '—' : `${Math.round(x * 100)}%`);
  const focus = $derived(hover !== null ? bars[hover] : null);
</script>

<section class="stats">
  <div class="periods">
    {#each PERIODS as p (p.days)}
      <button class:on={period === p.days} onclick={() => (period = p.days)}>{p.label}</button>
    {/each}
  </div>

  {#if data}
    <div class="top">
      <div class="ring" title={t('評価済みの画像のうち好評・超好評の割合')}>
        <svg viewBox="0 0 120 120">
          <circle cx="60" cy="60" r={R} class="track" />
          <circle cx="60" cy="60" r={R} class="like" style:stroke-dasharray={`${C * overall} ${C}`} />
          <circle cx="60" cy="60" r={R} class="super" style:stroke-dasharray={`${C * (data.rated ? data.supers / data.rated : 0)} ${C}`} />
        </svg>
        <div class="mid">
          <b class="num">{pct(data.rated ? overall : null)}</b>
          <span>{t('好評率')}</span>
        </div>
      </div>
      <div class="tiles">
        <div class="tile"><b class="num">{data.rated.toLocaleString()}</b><span>{t('評価済み')}</span></div>
        <div class="tile"><b class="num" style:color="var(--like)">{data.liked.toLocaleString()}</b><span>{@html svg('heart', 13)} {t('好評以上')}</span></div>
        <div class="tile"><b class="num" style:color="var(--super)">{data.supers.toLocaleString()}</b><span>{@html svg('star', 13)} {t('超好評')}</span></div>
        <div class="tile"><b class="num">{sum.total.toLocaleString()}</b><span>{t('この期間のスワイプ')}</span></div>
        <div class="tile"><b class="num">{sum.perActiveDay.toLocaleString()}</b><span>{t('1日平均（使った日）')}</span></div>
        <div class="tile"><b class="num">{sum.streak}</b><span>{t('連続日数')}</span></div>
        <div class="tile"><b class="num">{pct(sum.likeRate)}</b><span>{t('この期間の好評率')}</span></div>
        <div class="tile"><b class="num">{sum.max.toLocaleString()}</b><span>{t('最多')} {sum.best ? shortDay(sum.best.day) : ''}</span></div>
      </div>
    </div>

    <div class="chart">
      <div class="info num">
        {#if focus}
          <span>{period > 31 ? t('{0} からの週', shortDay(focus.day)) : shortDay(focus.day)}</span>
          <span>{t('{0} スワイプ', swipes(focus))}</span>
          <span style:color="var(--like)">{@html svg('heart', 13)}{focus.like}</span>
          <span style:color="var(--super)">{@html svg('star', 13)}{focus.super}</span>
          <span style:color="var(--pass)">{@html svg('x', 13)}{focus.pass}</span>
          <span style:color="var(--archive)">{@html svg('archive', 13)}{focus.archive}</span>
        {:else}
          <span class="muted">{period > 31 ? t('週ごとのスワイプ') : t('日ごとのスワイプ')} {t('· 最大 {0}', scale)}</span>
        {/if}
      </div>
      {#key `${period}/${data.days.at(-1)?.day}`}
        <!-- svelte-ignore a11y_no_static_element_interactions -->
        <div class="bars" style:--n={bars.length} onpointerleave={() => (hover = null)}>
          {#each bars as b, i (b.day)}
            <!-- svelte-ignore a11y_no_static_element_interactions -->
            <div class="col" class:hot={hover === i} style:--d={`${Math.min(i, 40) * 12}ms`} onpointerenter={() => (hover = i)}>
              <div class="stack" style:height={`${(swipes(b) / scale) * 100}%`}>
                {#each SEGMENTS as s (s)}
                  {#if b[s]}<div class="seg {s}" style:flex-grow={b[s]}></div>{/if}
                {/each}
              </div>
            </div>
          {/each}
        </div>
      {/key}
      <div class="axis num">
        <span>{bars.length ? shortDay(bars[0].day) : ''}</span>
        <span>{bars.length ? shortDay(bars[Math.floor(bars.length / 2)].day) : ''}</span>
        <span>{bars.length ? shortDay(bars[bars.length - 1].day) : ''}</span>
      </div>
    </div>
  {/if}
</section>

<style>
  .stats {
    position: absolute;
    inset: 0;
    overflow-y: auto;
    padding: 6px 16px 20px;
    display: flex;
    flex-direction: column;
    gap: 16px;
  }
  .periods {
    align-self: center;
    display: flex;
    padding: 3px;
    border-radius: 999px;
    background: var(--surface);
    border: 1px solid var(--line);
  }
  .periods button {
    padding: 6px 18px;
    border: none;
    border-radius: 999px;
    background: transparent;
    color: var(--muted);
    cursor: pointer;
    transition: background 150ms;
  }
  .periods button.on {
    background: var(--surface-2);
    color: var(--text);
  }
  .top {
    display: flex;
    align-items: center;
    gap: 24px;
  }
  .ring {
    flex: none;
    position: relative;
    width: 150px;
    height: 150px;
  }
  .ring svg {
    width: 100%;
    height: 100%;
    rotate: -90deg;
  }
  .ring circle {
    fill: none;
    stroke-width: 12;
    stroke-linecap: round;
    transition: stroke-dasharray 700ms var(--ease-out);
    animation: draw 900ms var(--ease-out);
  }
  @keyframes draw {
    from {
      stroke-dasharray: 0 400;
    }
  }
  .ring .track {
    stroke: var(--surface-2);
    animation: none;
  }
  .ring .like {
    stroke: var(--like);
    filter: drop-shadow(0 0 6px color-mix(in srgb, var(--like) 60%, transparent));
  }
  .ring .super {
    stroke: var(--super);
    filter: drop-shadow(0 0 6px color-mix(in srgb, var(--super) 60%, transparent));
  }
  .mid {
    position: absolute;
    inset: 0;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
  }
  .mid b {
    font-size: 30px;
    font-weight: 800;
  }
  .mid span,
  .tile span {
    color: var(--muted);
    font-size: 12px;
  }
  .tiles {
    flex: 1;
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(140px, 1fr));
    gap: 8px;
  }
  .tile {
    display: flex;
    flex-direction: column;
    gap: 4px;
    padding: 12px 14px;
    border-radius: 16px;
    background: var(--surface);
    border: 1px solid var(--line);
    animation: rise 320ms var(--ease-out) backwards;
  }
  .tile b {
    font-size: 22px;
    font-weight: 800;
  }
  .tile span {
    display: flex;
    align-items: center;
    gap: 4px;
  }
  @keyframes rise {
    from {
      opacity: 0;
      translate: 0 10px;
    }
  }
  .chart {
    display: flex;
    flex-direction: column;
    gap: 8px;
    padding: 14px 16px 10px;
    border-radius: 20px;
    background: var(--surface);
    border: 1px solid var(--line);
  }
  .info {
    min-height: 20px;
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 12px;
    font-size: 13px;
  }
  .info span {
    display: flex;
    align-items: center;
    gap: 3px;
  }
  .muted {
    color: var(--muted);
  }
  .bars {
    height: clamp(160px, 32vh, 300px);
    display: grid;
    grid-template-columns: repeat(var(--n), 1fr);
    align-items: end;
    gap: clamp(1px, calc(240px / var(--n)), 6px);
  }
  .col {
    height: 100%;
    display: flex;
    align-items: flex-end;
    border-radius: 6px;
    cursor: default;
  }
  .col.hot {
    background: rgba(255, 255, 255, 0.05);
  }
  .stack {
    width: 100%;
    min-height: 0;
    display: flex;
    flex-direction: column-reverse;
    border-radius: 5px 5px 2px 2px;
    overflow: hidden;
    transform-origin: bottom;
    animation: grow 600ms var(--ease-out) backwards;
    animation-delay: var(--d);
    transition: filter 120ms;
  }
  .col.hot .stack {
    filter: brightness(1.25) drop-shadow(0 0 8px rgba(255, 255, 255, 0.2));
  }
  @keyframes grow {
    from {
      scale: 1 0;
    }
  }
  .seg {
    flex-basis: 0;
    min-height: 1px;
  }
  .seg.like {
    background: var(--like);
  }
  .seg.super {
    background: var(--super);
  }
  .seg.pass {
    background: var(--pass);
  }
  .seg.archive {
    background: var(--archive);
  }
  .seg.hold {
    background: #555a63;
  }
  .axis {
    display: flex;
    justify-content: space-between;
    color: var(--muted);
    font-size: 11.5px;
  }
  @media (max-width: 759px) {
    .top {
      flex-direction: column;
      gap: 14px;
    }
    .tiles {
      width: 100%;
      grid-template-columns: repeat(2, 1fr);
    }
  }
</style>
