import type { Register } from 'claude-code'

// Esc で止めたときや API エラーで終わったときは Stop のフックが動かないので、ターンの終わりに送る
// Esc は自分で止めて画面の前にいるので off、エラーは目を離しているあいだに止まるので done
export const register: Register = on => {
  on('turn.complete', async ($, e, next) => {
    if (!e.agentId && e.reason !== 'answer') {
      const state = e.reason === 'aborted' ? 'off' : 'done'
      const stdin = JSON.stringify({ session_id: await $.session.id() })
      const lamp = (sh: string) => $.process.run([sh, `${$.plugin.root}/hooks/lamp.sh`, state], { stdin })
      try {
        await lamp('sh')
      } catch {
        // Windows: sh が PATH にないので、git.exe の場所から Git for Windows の bash を探す
        const git = (await $.process.run(['where', 'git'])).stdout.split(/\r?\n/)[0]
        await lamp(git.replace(/\\(cmd|bin|mingw64\\bin)\\git\.exe$/i, '\\bin\\bash.exe')).catch(() => {})
      }
    }
    return next(e)
  })
}
