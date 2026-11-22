// Shared helpers for the browser tests: real headless browsers playing the web build together,
// over the real public relays and WebRTC. See README.md in this folder.
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later
import {chromium} from 'playwright'

export const SITE = process.argv[2] || 'http://localhost:8080/'
export const SHOTS = process.argv[3] // optional folder for screenshots

const t0 = Date.now()
export const log = (...args) => console.log(`[${((Date.now() - t0) / 1000).toFixed(1)}s]`, ...args)
export const sleep = ms => new Promise(resolve => setTimeout(resolve, ms))
export const randomRoom = prefix => `${prefix}-${Math.random().toString(36).slice(2, 8)}`

let failures = 0
export function check(ok, what) {
  log(ok ? 'PASS' : 'FAIL', what)
  if (!ok) failures++
}
export function finish() {
  log(failures === 0 ? 'ALL PASSED' : `${failures} FAILED`)
  process.exit(failures === 0 ? 0 : 1)
}

export const launch = () =>
  chromium.launch({
    executablePath: process.env.CHROME || undefined, // any Chrome/Chromium, or Playwright's own
    headless: true,
    args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--disable-features=WebRtcHideLocalIpsWithMdns'],
  })

export async function screenshot(target, name) {
  if (SHOTS) await target.screenshot({path: `${SHOTS}/${name}.png`})
}

// Opens the game in a fresh browser profile and presses Play. `before` are functions to run in
// the page before any of its own scripts (Playwright's addInitScript).
export async function openPlayer(browser, name, room, contextOptions = {}, before = []) {
  const context = await browser.newContext({viewport: {width: 1000, height: 860}, ...contextOptions})
  for (const script of before) await context.addInitScript(script)
  const page = await context.newPage()
  page.on('console', m => ['error', 'warning'].includes(m.type()) && log(`[${name}] ${m.type()}: ${m.text()}`))
  page.on('pageerror', e => log(`[${name}] PAGE ERROR: ${e.message}`))
  await page.goto(`${SITE}#room=${room}`)
  await page.waitForFunction(() => !document.getElementById('play').disabled, null, {timeout: 30000})
  await page.click('#play')
  return {name, page, context}
}

// What the game in that browser thinks is going on (src/session_web.cpp, debugJson)
export const state = player => player.page.evaluate(() => JSON.parse(Module.ccall('knight_debug_state', 'string')))
export const states = players => Promise.all(players.map(state))

export async function waitFor(what, condition, timeout = 45000) {
  const end = Date.now() + timeout
  while (Date.now() < end) {
    try {
      if (await condition()) {
        log('ok:', what)
        return true
      }
    } catch {}
    await sleep(300)
  }
  log('TIMEOUT:', what)
  return false
}

// Every browser agrees: one host, the same world, everybody in it with a player id.
export async function together(players) {
  const s = await states(players)
  return (
    s.filter(x => x.role === 'host').length === 1 &&
    s.every(x => x.epoch === s[0].epoch && x.players === players.length && x.myId > 0)
  )
}

// Screen units (the whole canvas: the field and the space around it, see Screen in src/main.cpp)
// -> page coordinates
export async function screenToPage(player, x, y, s) {
  const box = await player.page.locator('#canvas').boundingBox()
  const [, , width, height] = (s || (await state(player))).screen
  return [box.x + (x * box.width) / width, box.y + (y * box.height) / height]
}

// Field coordinates (800x600) -> page coordinates, wherever the game put the field on the canvas
export async function toPage(player, x, y) {
  const s = await state(player)
  return screenToPage(player, s.screen[0] + x, s.screen[1] + y, s)
}

// Finding the way around the walls of the level in a state, the way the red knights do it
// (src/level.cpp): a knight needs 2 x 2 free cells of 32 px to stand on (a "spot"), and walks
// from spot to spot. Positions are a knight's top-left corner.
const CELL = 32, OFFSET = 12, KNIGHT = 40
export function wayFinder(s) {
  const rows = s.walls.length, cols = s.walls[0].length
  const wall = (c, r) => c < 0 || r < 0 || c >= cols || r >= rows || s.walls[r][c] === '#'
  const fits = (c, r) =>
    !wall(c, r) && !wall(c + 1, r) && !wall(c, r + 1) && !wall(c + 1, r + 1) &&
    c * CELL + OFFSET + KNIGHT <= 800 && r * CELL + OFFSET + KNIGHT <= 600
  const position = ({c, r}) => [c * CELL + OFFSET, r * CELL + OFFSET]
  const spotOf = ([x, y]) => {
    const c0 = Math.round((x - OFFSET) / CELL), r0 = Math.round((y - OFFSET) / CELL)
    let best = null
    for (let r = r0 - 1; r <= r0 + 1; r++)
      for (let c = c0 - 1; c <= c0 + 1; c++) {
        const d = Math.hypot(c * CELL + OFFSET - x, r * CELL + OFFSET - y)
        if (fits(c, r) && (!best || d < best.d)) best = {c, r, d}
      }
    return best
  }
  return {
    // The next place to head for on the way from `from` to `to`, staying off spots where
    // keepOut([x, y]) is true (straight at `to` once it's close, or if there is no way round)
    next(from, to, keepOut = () => false) {
      const a = spotOf(from), b = spotOf(to)
      if (!a || !b || (a.c === b.c && a.r === b.r)) return to
      const key = ({c, r}) => r * cols + c
      const steps = new Map([[key(b), 0]])
      const queue = [b]
      while (queue.length) {
        const here = queue.shift()
        for (const [dc, dr] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
          const n = {c: here.c + dc, r: here.r + dr}
          if (fits(n.c, n.r) && !steps.has(key(n)) && (key(n) === key(a) || !keepOut(position(n)))) {
            steps.set(key(n), steps.get(key(here)) + 1)
            queue.push(n)
          }
        }
      }
      const left = steps.get(key(a))
      if (left === undefined) return to
      for (const [dc, dr] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
        const n = {c: a.c + dc, r: a.r + dr}
        if (steps.get(key(n)) === left - 1) return position(n)
      }
      return to
    },
    // Is a knight with its top-left corner here inside a wall?
    inWall: ([x, y]) => {
      for (let r = Math.floor(y / CELL); r <= Math.ceil((y + KNIGHT) / CELL) - 1; r++)
        for (let c = Math.floor(x / CELL); c <= Math.ceil((x + KNIGHT) / CELL) - 1; c++) if (wall(c, r)) return true
      return false
    },
  }
}

// Does a knight with its top-left corner at [x, y] come within `margin` px of a door?
export const nearDoor = (s, [x, y], margin = 0) =>
  s.doors.some(([dx, dy, dw, dh]) => x < dx + dw + margin && dx - margin < x + KNIGHT && y < dy + dh + margin && dy - margin < y + KNIGHT)
