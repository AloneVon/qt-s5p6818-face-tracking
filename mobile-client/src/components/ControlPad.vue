<script setup lang="ts">
import { ref } from 'vue';
import { useTerminal } from '../stores/terminal';

const term = useTerminal();
const step = ref(5);

// +pan = right, +tilt = up (matches the terminal's servo sign convention).
function up() {
  term.nudge(0, step.value);
}
function down() {
  term.nudge(0, -step.value);
}
function left() {
  term.nudge(-step.value, 0);
}
function right() {
  term.nudge(step.value, 0);
}
</script>

<template>
  <fieldset class="pad" :disabled="!term.connected">
    <div class="dpad">
      <button class="up" @click="up">▲</button>
      <button class="left" @click="left">◀</button>
      <button class="home" @click="term.home()">⌂</button>
      <button class="right" @click="right">▶</button>
      <button class="down" @click="down">▼</button>
    </div>

    <label class="step">
      <span>Step</span>
      <input type="range" min="0.5" max="30" step="0.5" v-model.number="step" />
      <output>{{ step }}°</output>
    </label>

    <label class="auto">
      <input
        type="checkbox"
        :checked="term.autoTrack"
        @change="term.setMode(($event.target as HTMLInputElement).checked)"
      />
      <span>Auto-track</span>
    </label>
  </fieldset>
</template>
