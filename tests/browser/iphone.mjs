// iPhones: no browser on an iPhone can put a page in full screen, but they all slide their bars
// away when the page scrolls. This plays on a pretend iPhone (no full screen, an iPhone's screen
// and browser name) held sideways, and checks that the page asks for a swipe up while the bars
// are showing, that the swipe scrolls the page instead of reaching the game, and that the layer
// goes away (and comes back) as the bars do. A real browser's bars can't be tested here, so
// "the bars slid away" is the window getting taller, which is what it looks like to the page.
// Usage: node iphone.mjs [site-url] [screenshot-folder]
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later
import {writeFile} from 'node:fs/promises'
import {SHOTS, SITE, check, finish, launch, randomRoom, screenToPage, sleep, state, waitFor} from './common.mjs'

const browser = await launch()
const withBars = {width: 844, height: 340}    // an iPhone held sideways, with the browser's bars
const withoutBars = {width: 844, height: 390} // ...and once they've slid away
const context = await browser.newContext({
  viewport: withBars,
  screen: {width: 390, height: 844},
  isMobile: true,
  hasTouch: true,
  deviceScaleFactor: 3,
  userAgent: 'Mozilla/5.0 (iPhone; CPU iPhone OS 18_6 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) ' +
    'Version/18.6 Mobile/15E148 DuckDuckGo/7 Safari/605.1.15',
})
await context.addInitScript(() => { // like an iPhone: only videos can go full screen
  delete Element.prototype.requestFullscreen
  delete Element.prototype.webkitRequestFullscreen
  Object.defineProperty(Document.prototype, 'fullscreenEnabled', {get: () => false})
})
const page = await context.newPage()
const player = {name: 'iphone', page, context}
const errors = []
page.on('pageerror', e => errors.push(e.message))
await page.goto(`${SITE}#room=${randomRoom('iphone')}`)
await page.waitForFunction(() => !document.getElementById('play').disabled, null, {timeout: 30000})

const layerUp = () => page.evaluate(() => !document.getElementById('swipe').hidden)
const waitLayer = (up, what, timeout = 3000) => waitFor(what, async () => (await layerUp()) === up, timeout)
const cdp = await context.newCDPSession(page)
const touch = (type, points) => cdp.send('Input.dispatchTouchEvent', {type, touchPoints: points.map(([x, y], id) => ({x, y, id}))})
// The window changes size (the bars come and go, or the phone turns), but the screen doesn't.
// (Playwright's setViewportSize would make the screen as big as the window.)
const resize = ({width, height}) =>
  cdp.send('Emulation.setDeviceMetricsOverride', {width, height, deviceScaleFactor: 3, mobile: true, screenWidth: 390, screenHeight: 844})
// (and Playwright's own screenshots would put the window back to the size it thinks it is)
async function screenshot(name) {
  if (SHOTS) await writeFile(`${SHOTS}/${name}.png`, Buffer.from((await cdp.send('Page.captureScreenshot')).data, 'base64'))
}
async function swipeUp() {
  const x = withBars.width / 2
  await touch('touchStart', [[x, 300]])
  for (let i = 1; i <= 8; i++) {
    await touch('touchMove', [[x, 300 - i * 30]])
    await sleep(16)
  }
  await touch('touchEnd', [])
}

await page.tap('#play')
check(await waitFor('playing', async () => (await state(player)).me[0] >= 0, 15000), 'the game starts')
check(await page.evaluate(() => document.body.classList.contains('swipe') && !document.fullscreenElement),
  'no full screen to be had, so the page is ready for a swipe instead')
check(await waitLayer(true, 'swipe layer'), 'sideways with the bars showing, a layer asks for a swipe up')
await sleep(300)
await screenshot('iphone-swipe-up')
check(await page.evaluate(() => document.documentElement.scrollHeight > innerHeight + 200), 'the page can scroll under the game')
check(await page.evaluate(() => KnightUI.covered()), 'nothing can hurt your knight while the layer covers the game')

// The swipe scrolls the page (it doesn't reach the game), and then the bars slide away
const shotsBefore = (await state(player)).shots
await swipeUp()
check(await waitFor('page scrolled', () => page.evaluate(() => scrollY > 50), 2000), 'swiping up on the layer scrolls the page')
check((await state(player)).shots === shotsBefore, 'the swipe did not fire the gun')
await resize(withoutBars)
check(await waitLayer(false, 'layer gone'), 'once the bars slide away, the layer goes')
const box = await page.locator('#canvas').boundingBox()
check(Math.abs(box.height - withoutBars.height) <= 2 && Math.abs(box.width - withoutBars.width) <= 2,
  `the game has the whole screen (${box.width} x ${box.height})`)
check(await page.evaluate(() => !KnightUI.covered()), 'and can be played again')
await sleep(2600) // past the spawn protection
await screenshot('iphone-full-screen')

// The thumb sticks work: the touches reach the game again
let s = await state(player)
const before = s.me
const way = before[0] < 380 ? 1 : -1
const [mx, my] = await screenToPage(player, s.move[0], s.move[1], s)
await touch('touchStart', [[mx, my]])
for (let i = 1; i <= 5; i++) {
  await touch('touchMove', [[mx + way * i * s.radius * 0.18 * (box.width / s.screen[2]), my]])
  await sleep(30)
}
await sleep(700)
await touch('touchEnd', [])
s = await state(player)
check((s.me[0] - before[0]) * way > 60, `the move stick walks the knight again (x ${before[0]} -> ${s.me[0]})`)

// The bars come back (say a tap at the edge of the screen): so does the layer. "Not now" puts it away.
await resize(withBars)
check(await waitLayer(true, 'layer back'), 'when the bars come back, so does the layer')
check(await page.evaluate(() => scrollY === 0), 'with the page back at the top, so there is room to swipe')
await page.tap('#swipe-skip')
check(await waitLayer(false, 'not now'), '"Not now" puts it away')
await resize({width: 844, height: 330})
await sleep(300)
check(!(await layerUp()), 'and it stays away')

// The menu offers full screen again, and the way to get it every time
s = await state(player)
const [bx, by] = await screenToPage(player, s.menu[0] + s.menu[2] / 2, s.menu[1] + s.menu[3] / 2, s)
await page.touchscreen.tap(bx, by)
check(await waitFor('menu', () => page.evaluate(() => !document.getElementById('menu').hidden), 3000), 'the menu opens')
check(await page.isVisible('#menu-full') && await page.isVisible('#menu-install'),
  'it has a Full screen button, and how to add the game to the home screen')
await page.tap('#menu-full')
check(await waitLayer(true, 'asked again'), 'Full screen asks for the swipe again')

// A browser that keeps a bar: the most room a swipe gives is the new normal
await page.evaluate(() => scrollTo(0, 120))
await resize({width: 844, height: 352})
check(await waitLayer(false, 'layer gone after a partial hide', 3000), 'when a swipe gives some room but not all, that is as good as it gets')
await resize({width: 844, height: 300})
check(await waitLayer(true, 'bar back'), 'and if the room goes again, the layer is back')

// A browser that doesn't hide its bars at all: after one swipe that changes nothing, stop asking
await page.evaluate(() => scrollTo(0, 150))
check(await waitLayer(false, 'gave up', 3000), 'a swipe that gives no room makes it stop asking')
await resize({width: 844, height: 290})
await sleep(300)
check(!(await layerUp()), 'for good')

// Upright, the layer only comes when asked for
await resize({width: 390, height: 700})
await sleep(300)
check(!(await layerUp()), 'upright, it does not ask by itself')
check(errors.length === 0, `no errors on the page ${errors.join(' | ')}`)

await browser.close()
finish()
