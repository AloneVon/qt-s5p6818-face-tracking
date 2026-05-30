<script setup lang="ts">
import { ref } from 'vue';
import ConnectionBar from './components/ConnectionBar.vue';
import VideoView from './components/VideoView.vue';
import ControlPad from './components/ControlPad.vue';
import FileGallery from './components/FileGallery.vue';

type Tab = 'control' | 'files';
const tab = ref<Tab>('control');
</script>

<template>
  <div class="app">
    <ConnectionBar />
    <VideoView />

    <nav class="tabs">
      <button :class="{ active: tab === 'control' }" @click="tab = 'control'">Control</button>
      <button :class="{ active: tab === 'files' }" @click="tab = 'files'">Files</button>
    </nav>

    <main class="panel">
      <ControlPad v-show="tab === 'control'" />
      <FileGallery v-show="tab === 'files'" />
    </main>
  </div>
</template>
