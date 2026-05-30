<script setup lang="ts">
import { useTerminal } from '../stores/terminal';

const term = useTerminal();

// Tap to aim: translate the tap point into a [-1, 1] offset from frame center,
// accounting for the letterbox the <img object-fit:contain> introduces when
// the video aspect differs from the element's.
function onTap(ev: PointerEvent) {
  if (!term.connected || !term.resW || !term.resH) return;
  const el = ev.currentTarget as HTMLElement;
  const box = el.getBoundingClientRect();
  if (box.width === 0 || box.height === 0) return;

  const videoAspect = term.resW / term.resH;
  const boxAspect = box.width / box.height;
  let drawW = box.width;
  let drawH = box.height;
  if (boxAspect > videoAspect) {
    // Pillarboxed: full height, narrower width.
    drawW = box.height * videoAspect;
  } else {
    // Letterboxed: full width, shorter height.
    drawH = box.width / videoAspect;
  }
  const offX = (box.width - drawW) / 2;
  const offY = (box.height - drawH) / 2;

  const px = ev.clientX - box.left - offX;
  const py = ev.clientY - box.top - offY;
  if (px < 0 || py < 0 || px > drawW || py > drawH) return; // tap on the bars

  const dx = (px / drawW) * 2 - 1;
  const dy = (py / drawH) * 2 - 1;
  term.aim(dx, dy);
}
</script>

<template>
  <div class="video" @pointerdown="onTap">
    <img v-if="term.frameUrl" :src="term.frameUrl" alt="live video" draggable="false" />
    <div v-else class="nosig">no signal</div>
    <div v-if="term.connected" class="hud">
      <span v-if="term.resW">{{ term.resW }}×{{ term.resH }}</span>
      <span>{{ term.fps }} fps</span>
    </div>
  </div>
</template>
