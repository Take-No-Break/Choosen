<script lang="ts">
  import { LANGS, lang, server, setLang, t } from './lib/i18n';
  // Settings: photo folders, controls, review share, works, effects, phones (QR) and export.
  // Everything is stored by the core (POST /api/config ...), shared with the Version 1 page.
  import { matchFolder } from './lib/folders';
  import { getJ, postJ, ANDROID, IOS, openIosSettings, openPc, lastPc, iconColor, setIconColor, type State } from './lib/api';
  import { type Intensity } from './lib/burst';
  import { fx, setFx } from './lib/fx.svelte';
  import { prefs, setLayout, toggleAutoNext, setButtonStyle, type ButtonStyle } from './lib/prefs.svelte';
  import { theme, setTheme, type Theme } from './lib/theme.svelte';
  import { svg } from './ui/icons';
  import FolderBrowser from './FolderBrowser.svelte';
  import ResetPicker from './ResetPicker.svelte';
  import SortSheet from './SortSheet.svelte';

  let { st }: { st: State } = $props();
  const s = $derived(st as State & Record<string, any>);

  let busy = $state(false);
  let toast = $state('');
  function say(t: string) {
    toast = t;
    setTimeout(() => toast === t && (toast = ''), 2500);
  }
  async function config(q: Record<string, string | number>) {
    try {
      const r = await postJ<{ ok: boolean; error?: string }>('/api/config', q);
      if (!r.ok) say(r.error ? server(r.error) : t('保存できませんでした'));
      return r.ok;
    } catch {
      say(t('保存できませんでした'));
      return false;
    }
  }

  // ---- photo folders
  const folders = $derived([s.root, ...((s.moreRoots as string[]) ?? [])].filter(Boolean) as string[]);
  const depths = $derived((s.maxDepths as number[]) ?? []);
  async function saveFolders(list: string[], ds: number[]) {
    busy = true;
    await config({ root: list[0] ?? '', more_roots: list.slice(1).join('|'), max_depths: ds.join(',') });
    busy = false;
  }
  let browsing = $state(false); // Android: the app's own folder list
  async function addFolder() {
    if (ANDROID) return void (browsing = true);
    const r = await postJ<{ path: string }>('/api/pickfolder').catch(() => ({ path: '' }));
    if (r.path) await addPath(r.path);
  }
  async function addPath(path: string) {
    browsing = false;
    path = matchFolder(folders, path); // as the core keeps it (Android picks with "/", case of A-Z ignored)
    if (!path || folders.includes(path)) return;
    await saveFolders([...folders, path], [...folders.map((_, i) => depths[i] ?? -1), -1]);
  }
  const removeFolder = (i: number) => saveFolders(folders.filter((_, k) => k !== i), folders.map((_, k) => depths[k] ?? -1).filter((_, k) => k !== i));
  // subfolders on / off: the icon pops and says for a moment what it does now
  let subTip: { i: number; on: boolean; key: number } | null = $state(null);
  let subTipTimer = 0;
  function toggleSub(i: number) {
    const on = (depths[i] ?? -1) === 0; // what it becomes
    subTip = { i, on, key: (subTip?.key ?? 0) + 1 };
    clearTimeout(subTipTimer);
    subTipTimer = window.setTimeout(() => (subTip = null), 1600);
    void saveFolders(folders, folders.map((_, k) => (k === i ? (on ? -1 : 0) : (depths[k] ?? -1))));
  }

  // ---- controls
  const MOUSE: [number, string, string][] = [
    [1, '1', t('つかんで投げる')],
    [2, '2', t('クリックとホイール')],
    [3, '3', t('フリック')],
  ];
  const KEYS: [number, string, string][] = [
    [1, 'A', '→ ← ↑ ↓ Enter'],
    [2, 'B', t('→ 長押し ← ↓ ↑')],
    [3, 'C', 'D A W ⇧ E'],
  ];
  const SHARES: [number, string][] = [
    [0, t('なし')],
    [0.05, t('20 枚に 1 枚')],
    [0.067, t('15 枚に 1 枚')],
    [0.1, t('10 枚に 1 枚')],
    [0.2, t('5 枚に 1 枚')],
    [0.35, t('3 枚に 1 枚')],
    [0.5, t('2 枚に 1 枚')],
    [1, t('再評価のみ')],
  ];
  const share = $derived(SHARES.reduce((a, b) => (Math.abs(b[0] - st.reviewShare) < Math.abs(a[0] - st.reviewShare) ? b : a))[0]);



  // ---- phones: one button shows the QR code; the window closes once a phone has paired
  interface Remote {
    on: boolean;
    listening: boolean;
    error: string;
    devices: number;
    paired: number;
    urls: string[];
    code: string;
    codeSeconds: number;
    qr?: string[];
  }
  let remote: Remote | null = $state(null);
  let sorting = $state(false); // 評価ごとにサブフォルダへ分ける
  let resetting = $state(false); // ランキングデータを初期化 (one folder at a time)
  let pairing = $state(false);
  let pairedAt = 0;
  let pollTimer = 0;
  async function refreshRemote() {
    remote = await getJ<Remote>('/api/remote', { ui: 'v3' }).catch(() => null);
  }
  $effect(() => {
    refreshRemote();
  });
  async function startPhone() {
    await refreshRemote();
    if (remote && !remote.on && !(await config({ remote_access: 1 }))) return;
    pairedAt = remote?.paired ?? 0;
    await postJ('/api/remote/pair').catch(() => {});
    pairing = true;
    poll();
  }
  async function poll() {
    clearTimeout(pollTimer);
    if (!pairing) return;
    await refreshRemote();
    if (remote && remote.paired > pairedAt) {
      say(t('つながりました'));
      pairing = false;
      return;
    }
    pollTimer = window.setTimeout(poll, 1000);
  }
  function qrPath(rows: string[]): string {
    let d = '';
    rows.forEach((row, y) => {
      for (let x = 0; x < row.length; x++) {
        if (row[x] !== '1') continue;
        let w = 1;
        while (row[x + w] === '1') w++;
        d += `M${x + 4} ${y + 4}h${w}v1h-${w}z`;
        x += w - 1;
      }
    });
    return d;
  }

  // ---- export (書き出し)
  const KINDS: [string, string][] = [
    ['archived', t('アーカイブ')],
    ['passed', t('見送り')],
    ['liked', t('好評と超好評')],
    ['likeOnly', t('好評')],
    ['super', t('超好評')],
  ];
  let what = $state('archived');
  let move = $state(false);
  let dest = $state('');
  let job: { running: boolean; done: number; failed: number; total: number; folder: string } | null = $state(null);
  const canMove = $derived(what === 'archived' || what === 'passed');
  async function pickDest() {
    const r = await postJ<{ path: string }>('/api/pickfolder').catch(() => ({ path: '' }));
    if (r.path) dest = r.path;
  }
  async function runExport() {
    const mode = move && canMove ? 'move' : 'copy';
    const c = await getJ<{ count: number }>('/api/export', { what, mode }).catch(() => ({ count: 0 }));
    if (!c.count) return say(t('該当する画像がありません'));
    const text = mode === 'move' ? t('{0} 枚を写真フォルダから移動します。元の場所からはなくなります。', c.count) : t('{0} 枚を「{1}」へコピーします。', c.count, dest);
    if (!confirm(text)) return;
    const r = await postJ<{ ok: boolean; error?: string }>('/api/export', { what, to: dest, mode }).catch(() => ({ ok: false, error: t('書き出せませんでした') }));
    if (!r.ok) return say(r.error ? server(r.error) : t('書き出せませんでした'));
    watchJob();
  }
  async function watchJob() {
    job = await getJ<typeof job>('/api/export').catch(() => null);
    if (job?.running) setTimeout(watchJob, 500);
  }


  // Android: the PC's library inside the app (the PC's V3 → 設定 → スマホでスワイプ shows its
  // address and a 6-digit code; a phone paired once needs only the address)
  // Android: the home screen icon, blue or the earlier purple ("" elsewhere: no row)
  let icon = $state('');
  void iconColor().then((c) => (icon = c));
  let pcAddr = $state(ANDROID ? lastPc() : '');
  let pcCode = $state('');
  let pcMsg = $state('');
  function connectPc() {
    const r = openPc(pcAddr, pcCode.replace(/\D/g, ''));
    pcMsg = r === 'ok' ? '' : t('アドレスを確かめてください（例: 192.168.1.20:47801）');
  }

  async function quit() {
    if (confirm(t('Choosen を終了します'))) await postJ('/api/quit').catch(() => {});
  }
</script>

<section class="settings">
  <div class="inner">
    {#if st.remote}
      <p class="note">{@html svg('phone', 16)}{t('ここではこのスマホだけの設定を変えられます。写真フォルダなどの設定は PC で行います。')}</p>
    {:else if IOS}
      <p class="note">{@html svg('folder', 16)}{t('写真アプリのアルバムをフォルダとして読みます。写真を変更・削除することはありません。')}</p>
      <div class="group">
        <button class="item add" onclick={openIosSettings}>{@html svg('folder', 18)}<span>{t('写真へのアクセスを変更')}</span></button>
      </div>
    {:else}
    <p class="note">{@html svg('folder', 16)}{t('写真フォルダの中身を変更・削除することはありません（「書き出し」の移動を除く）。')}</p>

    <div class="group">
      <div class="item head"><span class="label">{t('フォルダ')}</span></div>
      {#each folders as f, i (f)}
        <div class="item folder">
          <span class="path">{f}</span>
          <span class="subwrap">
            {#if subTip?.i === i}
              {#key subTip.key}<span class="tip" class:on={subTip.on}>{subTip.on ? t('サブフォルダも読み込む') : t('このフォルダだけ')}</span>{/key}
            {/if}
            <button
              class="toggle"
              class:on={subTip?.i === i ? subTip.on : (depths[i] ?? -1) !== 0}
              title={t('サブフォルダも読む')}
              disabled={busy}
              onclick={() => toggleSub(i)}
            >
              {#key subTip?.i === i ? subTip.key : 0}<span class="pop">{@html svg('layers', 16)}</span>{/key}
            </button>
          </span>
          <button class="icon" title={t('外す（評価は残ります）')} disabled={busy} onclick={() => removeFolder(i)}>{@html svg('x', 16)}</button>
        </div>
      {/each}
      <button class="item add" disabled={busy} onclick={addFolder}>{@html svg('folderPlus', 18)}<span>{t('追加')}</span></button>
      {#if !st.remote}
        <button class="item add" onclick={() => (sorting = true)}>{@html svg('layers', 18)}<span>{t('評価ごとにサブフォルダへ分ける')}</span></button>
      {/if}
    </div>

    {/if}

    <div class="group">
      <div class="item">
        <span class="label">{t('モデル')}</span>
        <span class="pills">
          <button class:on={prefs.layout === 'cards'} onclick={() => setLayout('cards')}>{t('カード')}</button>
          <button class:on={prefs.layout === 'scroll'} onclick={() => setLayout('scroll')}>{t('スクロール')}</button>
        </span>
      </div>
      {#if prefs.layout === 'scroll'}
        <div class="item">
          <span class="label">{t('オートスクロール')}</span>
          <button class="sw" class:on={prefs.autoNext} title={t('好評・超好評で自動で次へ')} aria-label={t('オートスクロール')} onclick={toggleAutoNext}><span class="knob"></span></button>
        </div>
      {/if}
      <div class="item">
        <span class="label">{t('言語')}</span>
        <span class="pills">
          {#each LANGS as [v, label] (v)}
            <button class:on={lang === v} lang={v === 'pt' ? 'pt-BR' : v} onclick={() => setLang(v)}>{label}</button>
          {/each}
        </span>
      </div>
      {#if icon}
        <div class="item">
          <span class="label">{t('アイコンの色')}</span>
          <span class="pills">
            {#each [['blue', t('ブルー')], ['purple', t('パープル')]] as [v, label] (v)}
              <button class:on={icon === v} onclick={() => (setIconColor(v as 'blue' | 'purple'), (icon = v))}>{label}</button>
            {/each}
          </span>
        </div>
      {/if}
      <div class="item">
        <span class="label">{t('テーマ')}</span>
        <span class="pills">
          {#each [['dark', t('ブラック')], ['light', t('ホワイト')], ['auto', t('システム')]] as [v, label] (v)}
            <button class:on={theme.value === v} onclick={() => setTheme(v as Theme)}>{label}</button>
          {/each}
        </span>
      </div>
      <div class="item">
        <span class="label">{t('ボタン')}</span>
        <span class="pills">
          {#each [['line', t('ライン')], ['glass', t('グラス')], ['classic', t('クラシック')]] as [v, label] (v)}
            <button class:on={prefs.buttons === v} onclick={() => setButtonStyle(v as ButtonStyle)}>{label}</button>
          {/each}
        </span>
      </div>
      <div class="item">
        <span class="label">{t('エフェクト')}</span>
        <span class="pills">
          {#each [['normal', t('フル')], ['calm', t('ライト')], ['off', t('オフ')]] as [v, label] (v)}
            <button class:on={fx.level === v} onclick={() => setFx(v as Intensity)}>{label}</button>
          {/each}
        </span>
      </div>
      {#if !st.remote}
      <div class="item">
        <span class="label">{t('オーバーポイント')}<small>{t('100 点を超えた分も 100（+N）で表示')}</small></span>
        <button class="sw" class:on={s.overPoints} aria-label={t('オーバーポイント')} onclick={() => config({ over_points: s.overPoints ? 0 : 1 })}><span class="knob"></span></button>
      </div>
      <div class="item">
        <span class="label">{t('グループ')}<small>{t('連番の画像（p0, p1 …）を 1 枚に')}</small></span>
        <button class="sw" class:on={s.groupWorks} aria-label={t('グループ')} onclick={() => config({ group_works: s.groupWorks ? 0 : 1 })}><span class="knob"></span></button>
      </div>
      <div class="item">
        <span class="label">{t('再評価')}</span>
        <select value={share} onchange={(e) => config({ review_share: (e.currentTarget as HTMLSelectElement).value })}>
          {#each SHARES as [v, label] (v)}<option value={v}>{label}{v === 0.1 ? t('（標準）') : ''}</option>{/each}
        </select>
      </div>
      {/if}
    </div>

    {#if ANDROID && !st.remote}
      <div class="group">
        <div class="item head"><span class="label">{t('PC で使う')}</span></div>
        <p class="how">{t('PC に Choosen をインストールして、PC の写真をこのスマホでランキングしよう。評価は PC に保存されます。')}</p>
        <ol class="how">
          <li>{t('PC とこのスマホを同じ Wi-Fi につなぐ')}</li>
          <li>{t('PC の Choosen で 設定 → スマホでスワイプ → QR を押す')}</li>
          <li>{t('出てきたアドレスと 6 桁の番号を下に入れて「つなぐ」')}</li>
        </ol>
        <div class="item pc">
          <input class="field" bind:value={pcAddr} inputmode="url" placeholder="192.168.1.20:47801" />
          <input class="field pin num" bind:value={pcCode} inputmode="numeric" maxlength="7" placeholder={t('番号')} />
          <button class="btn primary" disabled={!pcAddr.trim()} onclick={connectPc}>{@html svg('pc', 16)}{t('つなぐ')}</button>
        </div>
        {#if pcMsg}<div class="item"><span class="muted">{pcMsg}</span></div>{/if}
      </div>
    {:else if !st.remote && !IOS}
      <div class="group">
        <div class="item">
          <span class="label">{t('マウス')}</span>
          <span class="pills">
            {#each MOUSE as [id, mark, label] (id)}
              <button class:on={st.mouseMode === id} title={label} onclick={() => config({ mouse_mode: id })}>{mark}</button>
            {/each}
            {#if s.customMouse}<button class:on={st.mouseMode === 4} title={t('Version 1 の割り当て')} onclick={() => config({ mouse_mode: 4 })}>{t('自分')}</button>{/if}
          </span>
        </div>
        <div class="item">
          <span class="label">{t('キーボード')}</span>
          <span class="pills">
            {#each KEYS as [id, mark, label] (id)}
              <button class:on={st.keyMode === id} title={label} onclick={() => config({ key_mode: id })}>{mark}</button>
            {/each}
          </span>
        </div>
      </div>

      <div class="group">
        <div class="item">
          <span class="label">{t('スマホでスワイプ')}{#if remote?.devices}<small class="num">{t('{0} 台', remote.devices)}</small>{/if}</span>
          <span class="ctl">
            {#if remote?.on}<button class="btn" onclick={() => config({ remote_access: 0 }).then(refreshRemote)}>{t('オフ')}</button>{/if}
            <button class="btn primary" onclick={startPhone}>QR</button>
          </span>
        </div>
      </div>

      <div class="group">
        <div class="item head"><span class="label">{t('エクスポート')}</span></div>
        <div class="item">
          <select bind:value={what} onchange={() => !canMove && (move = false)}>
            {#each KINDS as [k, label] (k)}<option value={k}>{label}</option>{/each}
          </select>
          <span class="pills">
            <button class:on={!move} onclick={() => (move = false)}>{t('コピー')}</button>
            <button class:on={move && canMove} disabled={!canMove} onclick={() => (move = true)}>{t('移動')}</button>
          </span>
        </div>
        <div class="item">
          <button class="btn" onclick={pickDest}>{@html svg('folder', 16)}{t('保存先')}</button>
          <span class="path muted">{dest}</span>
          <button class="btn primary" disabled={!dest || !!job?.running} onclick={runExport}>{t('書き出す')}</button>
        </div>
        {#if job}
          <div class="item col">
            <div class="progress"><span style:width={`${job.total ? ((job.done + job.failed) / job.total) * 100 : 0}%`}></span></div>
            <p class="muted num">{job.done + job.failed} / {job.total}{job.failed ? t('（失敗 {0}）', job.failed) : ''}{!job.running ? ` — ${job.folder}` : ''}</p>
          </div>
        {/if}
      </div>

      <button class="btn quiet" onclick={quit}>{t('Choosen を終了')}</button>
    {/if}
    {#if !st.remote}
      <div class="group">
        <button class="item reset" onclick={() => (resetting = true)}>
          <span class="label">{t('ランキングデータを初期化')}</span>
          <span class="muted">{t('フォルダごと ›')}</span>
        </button>
      </div>
    {/if}
    <div class="group about">
      <div class="item head"><span class="label">{t('About this app')}</span></div>
      <div class="item col">
        <p class="brand"><b>Choosen</b><span class="num">{t('Version {0}', __APP_VERSION__)}</span></p>
        <p class="muted">{t('写真をスワイプで評価して、好きな一枚とフォルダのランキングを作るアプリです。')}</p>
        <p class="muted">{t('写真と評価はこの端末（とつないだ自分の PC）の中だけにあり、どこにも送られません。写真フォルダの中身は変更しません（エクスポートの移動を除く）。')}</p>
      </div>
    </div>
  </div>
</section>

{#if sorting}
  <SortSheet onclose={() => (sorting = false)} />
{/if}

{#if resetting}
  <ResetPicker onclose={() => (resetting = false)} />
{/if}

{#if browsing}
  <FolderBrowser title={t('写真フォルダを追加')} onpick={addPath} onclose={() => (browsing = false)} />
{/if}

{#if pairing && remote}
  <!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
  <div class="modal" onclick={() => (pairing = false)}>
    <!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
    <div class="dialog" onclick={(e) => e.stopPropagation()}>
      {#if remote.error}
        <p>{server(remote.error)}</p>
      {:else if remote.qr?.length}
        {@const n = remote.qr.length + 8}
        <svg viewBox={`0 0 ${n} ${n}`} shape-rendering="crispEdges"><rect width={n} height={n} fill="#fff" /><path d={qrPath(remote.qr)} fill="#000" /></svg>
        <div class="code num">{remote.code}</div>
        {#if remote.urls?.[0]}<p class="addr num">{remote.urls[0].replace(/^https?:\/\//, '').replace(/\/$/, '')}</p>{/if}
        <p class="muted num">{Math.floor(remote.codeSeconds / 60)}:{String(remote.codeSeconds % 60).padStart(2, '0')}</p>
      {:else}
        <p class="muted">{t('準備中…')}</p>
      {/if}
      <button class="btn" onclick={() => (pairing = false)}>{t('閉じる')}</button>
    </div>
  </div>
{/if}

{#if toast}<div class="toast">{toast}</div>{/if}

<style>
  .group {
    margin: 0 0 14px;
    border-radius: 16px;
    border: 1px solid var(--line);
    background: var(--surface);
    overflow: hidden;
  }
  .item {
    width: 100%;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    padding: 11px 14px;
    border: none;
    border-top: 1px solid var(--line);
    background: transparent;
    text-align: left;
  }
  .item:first-child {
    border-top: none;
  }
  .item:has(> .pills) {
    flex-wrap: wrap; /* longer choices (English, Portuguese) move under the label on a narrow phone */
  }
  .item.head {
    padding-bottom: 6px;
  }
  .item.reset {
    cursor: pointer;
  }
  .item.reset .label {
    color: var(--pass); /* it removes ratings */
  }
  .item.col {
    flex-direction: column;
    align-items: stretch;
  }
  .item.pc {
    display: grid;
    grid-template-columns: minmax(0, 1fr) 84px auto; /* address · code · button on one line */
    gap: 8px;
  }
  .about p {
    margin: 0;
    font-size: 13px;
    line-height: 1.6;
  }
  .brand {
    display: flex;
    align-items: baseline;
    gap: 10px;
  }
  .brand b {
    font-size: 16px;
  }
  .brand span {
    color: var(--muted);
    font-size: 12.5px;
  }
  .item.add {
    justify-content: flex-start;
    gap: 8px;
    color: var(--accent-ink);
    cursor: pointer;
  }
  .label {
    display: flex;
    flex-direction: column;
    gap: 2px;
    font-size: 14px;
    font-weight: 600;
  }
  .label small {
    color: var(--muted);
    font-size: 11.5px;
    font-weight: 400;
  }
  .ctl {
    display: flex;
    gap: 6px;
  }
  .pills {
    flex: none;
    margin-left: auto;
    display: flex;
    gap: 2px;
    padding: 3px;
    border-radius: 999px;
    background: var(--surface-2);
  }
  .pills button {
    padding: 6px 12px;
    border: none;
    border-radius: 999px;
    background: transparent;
    color: var(--muted);
    font-size: 13px;
    white-space: nowrap;
    cursor: pointer;
  }
  .pills button.on {
    background: var(--surface);
    color: var(--text);
    box-shadow: 0 1px 4px rgba(0, 0, 0, 0.25);
  }
  .pills button:disabled {
    opacity: 0.35;
  }
  .sw {
    flex: none;
    padding: 0;
    border: none;
    background: transparent;
    cursor: pointer;
  }
  .sw.on .knob {
    background: var(--accent); /* the main colour (水色) */
  }
  .sw.on .knob::after {
    translate: 20px 0;
  }

  .settings {
    position: absolute;
    inset: 0;
    overflow-y: auto;
  }
  .inner {
    max-width: 720px;
    padding: 16px 18px 40px;
  }
  .note {
    display: flex;
    gap: 8px;
    align-items: center;
    padding: 10px 12px;
    border-radius: 12px;
    background: var(--surface);
    color: var(--muted);
    font-size: 12.5px;
  }
  .folder {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 8px 10px;
    margin-bottom: 6px;
    border-radius: 12px;
    background: var(--surface);
  }
  .path {
    flex: 1;
    font-size: 13px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .icon,
  .toggle {
    display: grid;
    place-items: center;
    width: 32px;
    height: 32px;
    border-radius: 50%;
    border: none;
    background: transparent;
    color: var(--muted);
    cursor: pointer;
  }
  .toggle.on {
    color: var(--accent-ink);
    background: color-mix(in srgb, var(--accent) 16%, transparent);
  }
  /* subfolders switched: the icon pops out and back, a label says what it does now */
  .subwrap {
    position: relative;
    display: flex;
  }
  .pop {
    display: flex;
    animation: popback 420ms cubic-bezier(0.3, 1.6, 0.5, 1);
  }
  @keyframes popback {
    0% {
      scale: 0;
    }
    55% {
      scale: 1.35;
    }
  }
  .tip {
    position: absolute;
    right: calc(100% + 6px); /* beside the icon, over the path for a moment */
    top: 50%;
    margin-top: -13px;
    z-index: 3;
    padding: 5px 11px;
    border-radius: 999px;
    background: var(--surface-2);
    border: 1px solid var(--line);
    color: var(--muted);
    font-size: 12px;
    white-space: nowrap;
    pointer-events: none;
    animation: tip 1600ms ease forwards;
  }
  .tip.on {
    background: var(--accent);
    border-color: transparent;
    color: var(--on-accent);
    font-weight: 700;
  }
  @keyframes tip {
    0% {
      opacity: 0;
      scale: 0.6;
      translate: 8px 0;
    }
    14% {
      opacity: 1;
      scale: 1.05;
      translate: 0 0;
    }
    22% {
      scale: 1;
    }
    80% {
      opacity: 1;
    }
    100% {
      opacity: 0;
    }
  }
  .btn {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 8px 16px;
    border-radius: 999px;
    border: 1px solid var(--line);
    background: var(--surface);
    cursor: pointer;
    font-size: 13px;
  }
  .btn.primary {
    background: var(--accent);
    color: var(--on-accent);
    border-color: transparent;
    font-weight: 600;
  }
  .btn:disabled {
    opacity: 0.4;
  }
  .btn.quiet {
    margin-top: 30px;
    color: var(--muted);
  }
  select {
    padding: 8px 12px;
    border-radius: 10px;
    border: 1px solid var(--line);
    background: var(--surface);
    color: var(--text);
    font: inherit;
    font-size: 13px;
  }
  .knob {
    display: block;
    width: 44px;
    height: 24px;
    border-radius: 12px;
    background: var(--line);
    position: relative;
    transition: background 150ms;
  }
  .knob::after {
    content: '';
    position: absolute;
    top: 3px;
    left: 3px;
    width: 18px;
    height: 18px;
    border-radius: 50%;
    background: #fff;
    transition: translate 180ms var(--ease-out);
  }
  .addr {
    margin: 0;
    font-size: 15px;
    letter-spacing: 0.04em;
  }
  .field {
    width: 100%;
    min-width: 0;
    padding: 9px 12px;
    border-radius: 12px;
    border: 1px solid var(--line);
    background: var(--surface);
    color: var(--text);
    font: inherit;
    outline: none;
  }
  .field.pin {
    min-width: 0;
    text-align: center;
    letter-spacing: 0.15em;
  }
  .how {
    margin: 0;
    padding: 0 14px 8px;
    color: var(--muted);
    font-size: 12.5px;
    line-height: 1.6;
  }
  ol.how {
    padding-left: 32px;
  }
  .muted {
    color: var(--muted);
    font-size: 12.5px;
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
  .modal {
    position: fixed;
    inset: 0;
    z-index: 30;
    display: grid;
    place-items: center;
    background: rgba(0, 0, 0, 0.6);
    backdrop-filter: blur(6px);
  }
  .dialog {
    display: grid;
    justify-items: center;
    gap: 10px;
    padding: 22px;
    border-radius: 20px;
    background: var(--surface);
    animation: pop 200ms var(--ease-out);
  }
  @keyframes pop {
    from {
      opacity: 0;
      scale: 0.94;
    }
  }
  .dialog svg {
    width: min(260px, 70vw);
    height: auto;
    border-radius: 8px;
  }
  .code {
    font-size: 34px;
    font-weight: 800;
    letter-spacing: 8px;
  }
  .toast {
    position: fixed;
    left: 50%;
    bottom: 80px;
    translate: -50% 0;
    padding: 9px 18px;
    border-radius: 999px;
    background: var(--surface-2);
    z-index: 40;
    animation: pop 200ms var(--ease-out);
  }

</style>
