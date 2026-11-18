// A bot plays as a guest, so every action crosses the network to the host: it collects coins
// until red knights appear, shoots them, gets caught and respawns. Then a phone tries the touch
// controls. Usage: node gameplay.mjs [site-url] [screenshot-folder]
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later
import {check, finish, launch, openPlayer, randomRoom, screenshot, sleep, state, toPage, together, waitFor} from './common.mjs'

const browser = await launch()
const room = randomRoom('play')
const phoneSize = {viewport: {width: 390, height: 844}, isMobile: true, hasTouch: true, deviceScaleFactor: 3}

const A = await openPlayer(browser, 'A', room)
const B = await openPlayer(browser, 'B', room)
const C = await openPlayer(browser, 'C', room)
check(await waitFor('three players together', () => together([A, B, C])), 'three players share a world')
check((await state(B)).role === 'guest', 'the bot (B) is a guest, so it plays over the network')
for (const p of [A, B, C]) await p.page.click('#canvas', {position: {x: 2, y: 2}}) // focus the game

const held = new Set()
async function steer(p, dx, dy) {
  const want = new Set()
  if (dx > 6) want.add('d')
  if (dx < -6) want.add('a')
  if (dy > 6) want.add('s')
  if (dy < -6) want.add('w')
  for (const key of held) if (!want.has(key)) { await p.page.keyboard.up(key); held.delete(key) }
  for (const key of want) if (!held.has(key)) { await p.page.keyboard.down(key); held.add(key) }
}

// The bot plays like a person: walk to the nearest coin, dodge red knights that get close,
// shoot the ones in range (if allowed), and press a key to respawn when caught.
let biggestJump = 0
async function play(p, done, {shoot = true, timeout = 60000, onShot} = {}) {
  const end = Date.now() + timeout
  let lastShot = 0
  let lastScore = (await state(p)).score
  while (Date.now() < end) {
    const s = await state(p)
    biggestJump = Math.max(biggestJump, s.score - lastScore)
    lastScore = s.score
    if (done(s)) break
    if (s.me[0] < 0) {
      await steer(p, 0, 0)
      await sleep(900)
      await p.page.keyboard.press('Space')
      continue
    }
    const [mx, my] = [s.me[0] + 20, s.me[1] + 20]
    const dist = ([x, y]) => Math.hypot(x - mx, y - my)
    const enemy = s.enemies.map(([x, y]) => [x + 20, y + 20]).sort((a, b) => dist(a) - dist(b))[0]
    if (shoot && enemy && dist(enemy) < 300 && Date.now() - lastShot > 220) {
      const [px, py] = await toPage(p, enemy[0], enemy[1])
      await p.page.mouse.click(px, py)
      lastShot = Date.now()
      if (onShot) await onShot()
    }
    if (enemy && dist(enemy) < 120) {
      await steer(p, mx - enemy[0], my - enemy[1]) // get away
    } else {
      const coin = s.coins.map(([x, y]) => [x + 7, y + 7]).sort((a, b) => dist(a) - dist(b))[0]
      if (coin) await steer(p, coin[0] - mx, coin[1] - my)
    }
    await sleep(60)
  }
  await steer(p, 0, 0)
  return state(p)
}

let s = await play(B, x => x.score >= 5, {shoot: false})
check(s.score >= 5, `coins collected over the network: team score ${s.score}`)
check(await waitFor('a red knight appears', async () => (await state(B)).enemies.length >= 1, 3000), 'every 5 points brings a red knight')

let pictured = false
const picture = async () => {
  if (pictured) return
  await sleep(120)
  const now = await state(B)
  if (now.bullets > 0 && now.enemies.length > 0) {
    await screenshot(B.page.locator('#canvas'), 'gameplay-action')
    pictured = true
  }
}
s = await play(B, x => x.score >= 30 && biggestJump >= 10, {onShot: picture, timeout: 90000})
check(biggestJump >= 10, `shooting a red knight is worth 10 (score ${s.score})`)

// Walk into a red knight on purpose: game over, then any key brings us back
await play(B, x => x.enemies.length > 0, {shoot: false})
let caught = false
const deadline = Date.now() + 20000
while (Date.now() < deadline) {
  s = await state(B)
  if (s.me[0] < 0) {
    caught = true
    break
  }
  const e = s.enemies[0]
  if (e) await steer(B, e[0] - s.me[0], e[1] - s.me[1])
  await sleep(60)
}
await steer(B, 0, 0)
check(caught, 'touching a red knight ends your run')
await sleep(400)
await screenshot(B.page.locator('#canvas'), 'gameplay-game-over')
await sleep(600)
await B.page.keyboard.press('Space')
check(await waitFor('respawned', async () => (await state(B)).me[0] >= 0, 3000), 'any key respawns you')

// A phone joins the busy room, then tries the touch controls in a quiet room of its own
const phone = await openPlayer(browser, 'phone', room, phoneSize)
check(await waitFor('phone joins', async () => (await state(phone)).players === 4), 'a touch-screen player joins')
await screenshot(phone.page, 'gameplay-phone')
await phone.context.close()

const touch = await openPlayer(browser, 'touch', room + '-touch', phoneSize)
await sleep(2500) // past the spawn protection blink
const cdp = await touch.context.newCDPSession(touch.page)
const start = await state(touch)
const [kx, ky] = await toPage(touch, start.me[0] + 20, start.me[1] + 20)
const [gx] = await toPage(touch, start.me[0] + 20 + (start.me[0] < 400 ? 220 : -220), 0)
await cdp.send('Input.dispatchTouchEvent', {type: 'touchStart', touchPoints: [{x: kx, y: ky}]})
for (let i = 1; i <= 10; i++) {
  await cdp.send('Input.dispatchTouchEvent', {type: 'touchMove', touchPoints: [{x: kx + ((gx - kx) * i) / 10, y: ky}]})
  await sleep(60)
}
await sleep(900)
await cdp.send('Input.dispatchTouchEvent', {type: 'touchEnd', touchPoints: []})
const moved = await state(touch)
check(moved.me[0] >= 0 && Math.abs(moved.me[0] - start.me[0]) > 150, `dragging the knight moves it (x ${start.me[0]} -> ${moved.me[0]})`)
const [tx, ty] = await toPage(touch, 400, moved.me[1] > 300 ? 100 : 500)
await cdp.send('Input.dispatchTouchEvent', {type: 'touchStart', touchPoints: [{x: tx, y: ty}]})
await cdp.send('Input.dispatchTouchEvent', {type: 'touchEnd', touchPoints: []})
check(await waitFor('a bullet flies', async () => (await state(touch)).bullets > 0, 2000), 'tapping away from the knight shoots')
check(Math.abs((await state(touch)).me[0] - moved.me[0]) < 5, 'the tap did not drag the knight')

await browser.close()
finish()
