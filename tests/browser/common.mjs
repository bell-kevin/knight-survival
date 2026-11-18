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

// Opens the game in a fresh browser profile and presses Play.
export async function openPlayer(browser, name, room, contextOptions = {}) {
  const context = await browser.newContext({viewport: {width: 1000, height: 860}, ...contextOptions})
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

// Game coordinates (800x600) -> page coordinates on the scaled canvas
export async function toPage(player, x, y) {
  const box = await player.page.locator('#canvas').boundingBox()
  const k = box.width / 800
  return [box.x + x * k, box.y + y * k]
}
