<script setup lang="ts">
import { ref, watch } from 'vue';
import { useTerminal } from '../stores/terminal';

const term = useTerminal();
const host = ref('127.0.0.1');
const port = ref(8889);
const token = ref('');
const tls = ref(false);

// Convenience: hop between the known default ports when toggling TLS, but never
// clobber a port the user typed themselves.
watch(tls, (on) => {
  if (on && port.value === 8889) port.value = 8444;
  else if (!on && port.value === 8444) port.value = 8889;
});

function toggle() {
  if (term.connected || term.status === 'connecting') {
    term.disconnect();
  } else {
    term.connect(host.value, Number(port.value), token.value, tls.value);
  }
}
</script>

<template>
  <header class="conn">
    <span class="dot" :class="term.status" :title="term.status"></span>
    <input
      class="host"
      v-model="host"
      :disabled="term.status !== 'disconnected'"
      placeholder="host"
      inputmode="decimal"
      autocomplete="off"
      autocapitalize="off"
      spellcheck="false"
    />
    <input
      class="port"
      v-model.number="port"
      :disabled="term.status !== 'disconnected'"
      placeholder="port"
      inputmode="numeric"
    />
    <input
      class="token"
      v-model="token"
      :disabled="term.status !== 'disconnected'"
      type="password"
      placeholder="token (if required)"
      autocomplete="off"
      autocapitalize="off"
      spellcheck="false"
    />
    <label class="tls" title="Encrypt the link (wss). The device must trust the cert.">
      <input
        type="checkbox"
        v-model="tls"
        :disabled="term.status !== 'disconnected'"
      />
      TLS
    </label>
    <button class="go" :class="{ on: term.connected }" @click="toggle">
      {{ term.status === 'disconnected' ? 'Connect' : 'Disconnect' }}
    </button>
    <span v-if="term.error" class="err">{{ term.error }}</span>
  </header>
</template>
