// Players the relays introduce, but whose networks won't let them connect to each other (some
// guest Wi-Fi and mobile networks don't, and there is no relay server to fall back on): instead
// of "waiting for players", both are told they can't connect, with a tip to try another network.
// When one of them can get through after all, they end up together and the tip goes away.
// Usage: node blocked.mjs [site-url] [screenshot-folder]
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later
import {check, finish, launch, log, openPlayer, randomRoom, screenshot, sleep, state, states, together, waitFor} from './common.mjs'

const HINT = "CAN'T CONNECT TO A PLAYER\nTRY MOBILE DATA OR OTHER WI-FI"

// What the top right corner says, below the room's name
const said = async player => (await state(player)).status.split('\n').slice(1).join('\n')

// Lets the test move the game's clock forward (the C++ side reads it through performance.now)
function clockControl() {
  const now = performance.now.bind(performance)
  let ahead = 0
  performance.now = () => now() + ahead
  window.skipAhead = ms => (ahead += ms)
}

// A network that lets nothing through. Trystero makes every connection with new
// RTCPeerConnection; made relay-only, with no relay (TURN) server, one can't reach anybody.
// Setting window.blocked to false is switching to a network that works.
function blockedNetwork() {
  window.blocked = true
  const Real = window.RTCPeerConnection
  window.RTCPeerConnection = function (config, ...rest) {
    return new Real(window.blocked ? {...config, iceTransportPolicy: 'relay'} : config, ...rest)
  }
  window.RTCPeerConnection.prototype = Real.prototype
}

// Made-up events from web/net.js, handed to the game ahead of the real ones
function fakeEvents() {
  window.fake = []
  const poll = KnightNet.poll
  KnightNet.poll = () => (fake.length ? fake.shift() : poll())
}

const browser = await launch()

// 1. The rules, with made-up "couldn't connect" events, for a player alone in a room of its own
const solo = await openPlayer(browser, 'solo', randomRoom('solo'), {}, [clockControl])
await solo.page.evaluate(fakeEvents)
await sleep(1000)
check((await said(solo)) === 'WAITING FOR PLAYERS', 'alone, the game waits for players')
await solo.page.evaluate(() => fake.push('F ghost1'))
check(await waitFor('the hint', async () => (await said(solo)) === HINT, 5000),
  "found a player but couldn't connect: the game says so, with the tip")
await screenshot(solo.page, 'cant-connect')
await solo.page.evaluate(() => fake.push('F ghost2'))
check(await waitFor('two', async () => (await said(solo)).startsWith("CAN'T CONNECT TO 2 PLAYERS\n"), 5000),
  "two players it can't connect to are counted")
await solo.page.evaluate(() => fake.push('J ghost1'))
check(await waitFor('one connected', async () => (await state(solo)).unreachable === 1, 5000),
  'one that connects after all is taken off')
await solo.page.evaluate(() => skipAhead(140000))
await sleep(500)
check((await said(solo)) === HINT, 'the other is remembered for a while (it gets tried again)')
await solo.page.evaluate(() => skipAhead(20000))
check(await waitFor('forgotten', async () => (await said(solo)) === 'WAITING FOR PLAYERS', 5000),
  'after 2.5 minutes without news it must have gone, and the game waits for players again')
await solo.context.close()

// 2. For real, over the relays: B's network lets nothing through
const room = randomRoom('blocked')
const A = await openPlayer(browser, 'A', room)
const B = await openPlayer(browser, 'B', room, {}, [blockedNetwork])
check(
  await waitFor("both told they can't connect",
    async () => (await states([A, B])).every(s => s.unreachable === 1 && s.players === 1), 120000),
  "two players who can't connect are both told so")
const [a, b] = await states([A, B])
check(a.status.endsWith(HINT) && b.status.endsWith(HINT), 'both get the tip to try another network')
await screenshot(A.page, 'cant-connect-for-real')

// B switches to a network that works: the next try gets through
log('B switches networks')
await B.page.evaluate(() => (window.blocked = false))
check(await waitFor('together after switching', () => together([A, B]), 240000),
  'once B can get through, the two end up in one world')
const after = await states([A, B])
check(after.every(s => s.unreachable === 0 && s.status.endsWith('\n2 PLAYERS')), 'and the tip is gone')

await browser.close()
finish()
