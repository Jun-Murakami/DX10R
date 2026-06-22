import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'
import { viteSingleFile } from 'vite-plugin-singlefile'

// https://vite.dev/config/
export default defineConfig(({ command }) => ({
  // 相対パス (./) で出力する。プラグインは index.html を file:// / in-memory で読むため
  // 絶対 (/) パスだとアセット参照が解決できない。
  base: './',
  plugins: [
    react(),
    // 本番ビルドのみ全アセットを単一 index.html にインライン化する。プラグイン側の
    // LoadIndexHtml(__FILE__, ...) が plugin/resources/web/index.html を読むため、
    // 単一自己完結ファイルである必要がある。dev server (HMR) では無効化。
    ...(command === 'build' ? [viteSingleFile()] : []),
  ],
  server: {
    port: 5173,
    host: '127.0.0.1',
    cors: true,
    headers: {
      'Access-Control-Allow-Origin': '*',
    },
  },
  build: {
    // iPlug2 の LoadIndexHtml(__FILE__, ...) は plugin/ 親から resources/web/index.html
    // を引く。Vite の本番出力をそのままそこへ書き、プラグイン再ビルドで埋め込む。
    outDir: '../plugin/resources/web',
    emptyOutDir: true,
    cssCodeSplit: false,
    assetsInlineLimit: Number.MAX_SAFE_INTEGER,
  },
}))
