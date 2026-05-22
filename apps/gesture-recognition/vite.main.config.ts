import { defineConfig } from 'vite';

// https://vitejs.dev/config
export default defineConfig({
    build: {
        rollupOptions: {
            external: ['native-sound-mixer', 'koffi'],
        },
    },
});
