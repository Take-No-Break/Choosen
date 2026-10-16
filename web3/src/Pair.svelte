<script lang="ts">
  import { t } from './lib/i18n';
  // A phone that is not paired yet: the 6-digit code shown on the PC (設定 → スマホでスワイプ).
  import { pair } from './lib/api';
  import { svg } from './ui/icons';

  let { expired = false }: { expired?: boolean } = $props();

  let code = $state('');
  let busy = $state(false);
  // svelte-ignore state_referenced_locally
  let msg = $state(
    expired ? t('この QR コードは期限切れか使用済みです。PC でもう一度「スマホでスワイプ」を押してください。') : t('PC の Choosen の 設定 → スマホでスワイプ に出る 6 桁の番号を入れてください。'),
  );
  // svelte-ignore state_referenced_locally
  let bad = $state(expired);

  async function send(e: Event) {
    e.preventDefault();
    const c = code.replace(/\D/g, '');
    if (c.length !== 6) {
      msg = t('6 桁の番号を入れてください');
      bad = true;
      return;
    }
    busy = true;
    if (await pair(c)) return location.reload();
    busy = false;
    bad = true;
    msg = t('番号が違うか、期限が切れています。PC で新しい番号を表示してください。');
  }
</script>

<div class="pair">
  <div class="card">
    <span class="logo">{@html svg('heart', 34)}</span>
    <h1>Choosen</h1>
    <p class:bad>{msg}</p>
    <form onsubmit={send}>
      <!-- svelte-ignore a11y_autofocus -->
      <input class="num" bind:value={code} inputmode="numeric" autocomplete="one-time-code" maxlength="7" placeholder="000000" autofocus />
      <button disabled={busy}>{t('つなぐ')}</button>
    </form>
  </div>
</div>

<style>
  .pair {
    position: fixed;
    inset: 0;
    display: grid;
    place-items: center;
    padding: 16px;
    background: var(--bg, #0e0f11);
  }
  .card {
    width: min(380px, 100%);
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 12px;
    padding: 28px 22px;
    border-radius: 24px;
    background: var(--surface);
    border: 1px solid var(--line);
    text-align: center;
    animation: rise 320ms var(--ease-out);
  }
  @keyframes rise {
    from {
      opacity: 0;
      translate: 0 14px;
    }
  }
  .logo {
    color: var(--like);
    filter: drop-shadow(0 0 14px color-mix(in srgb, var(--like) 60%, transparent));
  }
  h1 {
    margin: 0;
    font-size: 22px;
  }
  p {
    margin: 0;
    color: var(--muted);
    font-size: 14px;
    line-height: 1.6;
  }
  p.bad {
    color: var(--pass);
  }
  form {
    width: 100%;
    display: flex;
    gap: 8px;
    margin-top: 6px;
  }
  input {
    flex: 1;
    min-width: 0;
    padding: 12px 14px;
    border-radius: 14px;
    border: 1px solid var(--line);
    background: var(--surface-2);
    color: var(--text);
    font-size: 22px;
    letter-spacing: 0.25em;
    text-align: center;
    outline: none;
  }
  button {
    padding: 0 18px;
    border-radius: 14px;
    border: none;
    background: var(--accent);
    color: var(--on-accent);
    font-weight: 700;
    cursor: pointer;
  }
  button:disabled {
    opacity: 0.5;
  }
</style>
