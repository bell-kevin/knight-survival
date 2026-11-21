// Phones: the lobby fits on the screen, the game gets the whole screen (much bigger held
// sideways), the thumb sticks move and fire, and the menu has the invite link.
// Usage: node phone.mjs [site-url] [screenshot-folder]
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later
import {SITE, check, finish, launch, randomRoom, screenToPage, screenshot, sleep, state, waitFor} from './common.mjs'

const browser = await launch()
const phone = {isMobile: true, hasTouch: true, deviceScaleFactor: 3}
const upright = {width: 390, height: 844}
const sideways = {width: 844, height: 390}

// Touches through the browser's own input pipeline (Chrome DevTools Protocol)
async function touch(page) {
  const cdp = await page.context().newCDPSession(page)
  const send = (type, points) => cdp.send('Input.dispatchTouchEvent', {type, touchPoints: points.map(([x, y], id) => ({x, y, id}))})
  return {
    down: points => send('touchStart', points),
    move: points => send('touchMove', points),
    up: () => send('touchEnd', []),
  }
}

const near = (a, b, slack = 2) => Math.abs(a - b) <= slack

for (const [name, viewport] of [['upright', upright], ['sideways', sideways]]) {
  const context = await browser.newContext({viewport, ...phone})
  const page = await context.newPage()
  const player = {name, page, context}
  const errors = []
  page.on('pageerror', e => errors.push(e.message))
  await page.goto(`${SITE}#room=${randomRoom('phone')}`)
  await page.waitForFunction(() => !document.getElementById('play').disabled, null, {timeout: 30000})

  // The lobby: its own card, with nothing cut off or spilling out
  const lobbyFits = await page.evaluate(() => {
    const whole = el => {
      const r = el.getBoundingClientRect()
      return r.width > 0 && r.top >= 0 && r.left >= 0 && r.right <= innerWidth + 0.5 && r.bottom <= innerHeight + 0.5
    }
    const play = document.getElementById('play')
    return whole(document.querySelector('#lobby h2')) && whole(play) && play.scrollWidth <= play.clientWidth + 1
  })
  check(lobbyFits, `${name}: the lobby fits (headline and Play button on screen, the room name inside the button)`)
  await screenshot(page, `phone-${name}-lobby`)

  // Play: the game takes the whole screen
  await page.tap('#play')
  check(await waitFor(`${name}: playing`, async () => (await state(player)).me[0] >= 0, 15000), `${name}: the game starts`)
  const box = await page.locator('#canvas').boundingBox()
  let s = await state(player)
  const fieldWidth = (800 * box.width) / s.screen[2], fieldHeight = (600 * box.height) / s.screen[3]
  check(near(box.width, viewport.width) && near(box.height, viewport.height), `${name}: the game fills the screen (${box.width} x ${box.height})`)
  check(name === 'sideways' ? fieldHeight > viewport.height - 2 : fieldWidth > viewport.width - 2,
    `${name}: the field is as big as the screen allows (${Math.round(fieldWidth)} x ${Math.round(fieldHeight)})`)
  check(s.sticks && s.beside, `${name}: the thumb sticks have room of their own, ${name === 'sideways' ? 'beside' : 'under'} the field`)
  await sleep(2500) // past the spawn protection blink
  await screenshot(page, `phone-${name}-game`)

  const fingers = await touch(page)
  const at = (x, y) => screenToPage(player, x, y, s)

  // The left stick walks the knight (toward the middle of the field)
  s = await state(player)
  const before = s.me
  const way = before[0] < 380 ? 1 : -1
  const [mx, my] = await at(s.move[0], s.move[1])
  const [pushX] = await at(s.move[0] + way * s.radius * 0.9, s.move[1])
  await fingers.down([[mx, my]])
  for (let i = 1; i <= 5; i++) {
    await fingers.move([[mx + ((pushX - mx) * i) / 5, my]])
    await sleep(30)
  }
  await sleep(800)
  await fingers.up()
  const after = (await state(player)).me
  check((after[0] - before[0]) * way > 100 && Math.abs(after[1] - before[1]) < 30, `${name}: the move stick walks the knight (x ${before[0]} -> ${after[0]})`)

  // The right stick keeps firing where it points; a quick tap fires once, at the nearest red knight
  s = await state(player)
  let shots = s.shots
  const [fx, fy] = await at(s.fire[0], s.fire[1])
  const [aimX, aimY] = await at(s.fire[0], s.fire[1] - s.radius * 0.9)
  await fingers.down([[fx, fy]])
  await fingers.move([[(fx + aimX) / 2, (fy + aimY) / 2]])
  await fingers.move([[aimX, aimY]])
  await sleep(1000)
  await fingers.up()
  s = await state(player)
  check(s.shots - shots >= 4, `${name}: holding the fire stick keeps firing (${s.shots - shots} shots in a second)`)
  shots = s.shots
  await fingers.down([[fx, fy]])
  await sleep(80)
  await fingers.up()
  check(await waitFor(`${name}: a tap fires`, async () => (await state(player)).shots === shots + 1, 2000), `${name}: a quick tap on the fire stick fires once`)

  // Both thumbs at once: walk and fire together
  s = await state(player)
  shots = s.shots
  const start = s.me
  const back = -way
  const [pushBackX] = await at(s.move[0] + back * s.radius * 0.9, s.move[1])
  await fingers.down([[mx, my], [fx, fy]])
  await fingers.move([[pushBackX, my], [aimX, aimY]])
  await sleep(700)
  await fingers.up()
  s = await state(player)
  check((s.me[0] - start[0]) * back > 60 && s.shots - shots >= 2, `${name}: moving and firing at the same time (x ${start[0]} -> ${s.me[0]}, ${s.shots - shots} shots)`)

  // The menu button opens the menu: room, invite link, back to the game
  s = await state(player)
  const [bx, by] = await at(s.menu[0] + s.menu[2] / 2, s.menu[1] + s.menu[3] / 2)
  await page.touchscreen.tap(bx, by)
  check(await waitFor(`${name}: menu`, () => page.evaluate(() => !document.getElementById('menu').hidden), 3000), `${name}: the menu button opens the menu`)
  check((await page.inputValue('#menu-invite')) === page.url(), `${name}: the menu has the invite link`)
  const menuFits = await page.evaluate(() => {
    const card = document.querySelector('#menu .card').getBoundingClientRect()
    const copy = document.getElementById('menu-copy')
    return card.left >= 0 && card.right <= innerWidth && card.bottom <= innerHeight &&
      copy.getBoundingClientRect().right <= card.right && copy.scrollWidth <= copy.clientWidth + 1
  })
  check(menuFits, `${name}: the menu fits on the screen, "Copy link" on one line`)
  await screenshot(page, `phone-${name}-menu`)
  await page.tap('#menu-close')
  check(await page.evaluate(() => document.getElementById('menu').hidden), `${name}: and closes again`)

  // Turning the phone: the game follows. (Pressing Play went full screen, and the test browser
  // can't resize a full-screen window, so leave it first; a real phone turns either way.)
  check(await page.evaluate(() => !!document.fullscreenElement), `${name}: Play went full screen`)
  if (name === 'upright') {
    await page.evaluate(() => document.exitFullscreen())
    await sleep(300)
    await page.setViewportSize(sideways)
    check(await waitFor('turned sideways', async () => {
      const turned = await state(player)
      const b = await page.locator('#canvas').boundingBox()
      return near(b.width, sideways.width) && (600 * b.height) / turned.screen[3] > sideways.height - 2 && turned.screen[0] > 100
    }, 5000), 'turning the phone sideways makes the field fill the height, with the sticks at the sides')
  }
  check(errors.length === 0, `${name}: no errors on the page ${errors.join(' | ')}`)
  await context.close()
}

await browser.close()
finish()
