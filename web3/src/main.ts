import './ui/tokens.css';
import { mount } from 'svelte';
import App from './App.svelte';
import { applyTheme } from './lib/theme.svelte';
import { initHello } from './lib/hello';

applyTheme(); // before the first paint: no flash of the other look

mount(App, { target: document.getElementById('app')! });
initHello(); // the Android app's launch screen steps aside (and plays again on each return)
