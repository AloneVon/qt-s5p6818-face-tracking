<script setup lang="ts">
import { ref } from 'vue';
import { useTerminal } from '../stores/terminal';

const term = useTerminal();
const host = ref('127.0.0.1');
const port = ref(8889);

function toggle() {
  if (term.connected || term.status === 'connecting') {
    term.disconnect();
  } else {
    term.connect(host.value, Number(port.value));
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
    <button class="go" :class="{ on: term.connected }" @click="toggle">
      {{ term.status === 'disconnected' ? 'Connect' : 'Disconnect' }}
    </button>
    <span v-if="term.error" class="err">{{ term.error }}</span>
  </header>
</template>
