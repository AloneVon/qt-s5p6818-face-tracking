<script setup lang="ts">
import { useTerminal } from '../stores/terminal';

const term = useTerminal();

function fmtSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  const kb = bytes / 1024;
  if (kb < 1024) return `${kb.toFixed(1)} KB`;
  return `${(kb / 1024).toFixed(1)} MB`;
}

function fmtTime(mtime: number): string {
  // mtime is seconds since epoch.
  return new Date(mtime * 1000).toLocaleString();
}
</script>

<template>
  <fieldset class="gallery" :disabled="!term.connected">
    <div class="bar">
      <button @click="term.refreshFiles()">Refresh</button>
      <button @click="term.snapshot()">Snapshot</button>
    </div>

    <ul v-if="term.files.length" class="files">
      <li v-for="f in term.files" :key="f.name">
        <div class="meta">
          <span class="name">{{ f.name }}</span>
          <span class="sub">{{ fmtSize(f.size) }} · {{ fmtTime(f.mtime) }}</span>
        </div>
        <div class="acts">
          <button @click="term.downloadFile(f.name)">Save</button>
          <button class="danger" @click="term.deleteFile(f.name)">Delete</button>
        </div>
      </li>
    </ul>
    <p v-else class="empty">No files.</p>
  </fieldset>
</template>
