import { defineConfig } from 'vite';
import { resolve } from 'path';

export default defineConfig({
    root: '.',
    server: {
        port: 3000,
        open: true,
    },
    build: {
        target: 'esnext',
        outDir: 'dist',
        emptyOutDir: true,
    },
    resolve: {
        alias: {
            '@': resolve(__dirname, './src'),
        },
    },
    base: 'LibRing/gesture-recognition-demo',
});
