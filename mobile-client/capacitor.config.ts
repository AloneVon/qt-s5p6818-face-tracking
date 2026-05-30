import type { CapacitorConfig } from '@capacitor/cli';

// Capacitor wraps the built web app (dist/) into a native Android/iOS shell.
// Native platforms are added on a machine with the SDKs:
//   npm run build && npx cap add android   (or ios) && npx cap sync
const config: CapacitorConfig = {
  appId: 'com.sec.mobile',
  appName: 'SEC Mobile',
  webDir: 'dist',
};

export default config;
