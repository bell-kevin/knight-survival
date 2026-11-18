// Three browsers join one room, then we check who hosts, host hand-over when the host leaves,
// hidden tabs (the browser pauses the game) and frozen tabs (no JavaScript runs at all).
// Usage: node multiplayer.mjs [site-url] [screenshot-folder]
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later
import {check, finish, launch, log, openPlayer, randomRoom, screenshot, sleep, state, states, together, waitFor} from './common.mjs'

const browser = await launch()
const room = randomRoom('test')

const A = await openPlayer(browser, 'A', room)
await sleep(1500)
const first = await state(A)
check(first.role === 'host' && first.players === 1, 'A starts out hosting its own world')

const B = await openPlayer(browser, 'B', room)
check(await waitFor('A and B share one world', () => together([A, B])), 'A and B end up in one world')
const C = await openPlayer(browser, 'C', room)
check(await waitFor('A, B and C share one world', () => together([A, B, C])), 'three players end up in one world')
let all = await states([A, B, C])
const epoch = all[0].epoch
check(all[0].role === 'host', 'the oldest world (A) won')

// B walks diagonally for a second; everybody should see it
await B.page.click('#canvas', {position: {x: 5, y: 5}})
const before = (await state(B)).me
await B.page.keyboard.down('d')
await B.page.keyboard.down('s')
await sleep(1000)
await B.page.keyboard.up('d')
await B.page.keyboard.up('s')
const after = (await state(B)).me
check(after[0] > before[0] && after[1] > before[1], `B moved (${before} -> ${after})`)
await sleep(500)
await screenshot(A.page, 'multiplayer-host-view')
await screenshot(B.page, 'multiplayer-guest-view')

// The host closes its tab: someone else takes over the same world
log('closing host A')
await A.context.close()
check(await waitFor('B and C carry on without A', () => together([B, C]), 30000), 'host hand-over after the host left')
all = await states([B, C])
check(all.every(x => x.epoch === epoch), 'the world survived the hand-over')

// A hidden tab: the browser stops the game loop (but keeps the connection), like pauseMainLoop()
const newHost = all[0].role === 'host' ? B : C
const other = newHost === B ? C : B
log(`hiding the host ${newHost.name}`)
await newHost.page.evaluate(() => Module.pauseMainLoop())
check(await waitFor(`${other.name} takes over`, async () => (await state(other)).role === 'host', 15000),
  'the other player takes over while the host tab is hidden')
const generation = (await state(other)).gen
await newHost.page.evaluate(() => Module.resumeMainLoop())
check(await waitFor('back together', () => together([B, C]), 15000), 'the returning host joins the game that moved on')
all = await states([B, C])
check(all.every(x => x.epoch === epoch && x.gen === generation), 'same world, still run by the newer host')
check((await state(newHost)).role === 'guest', 'the returning host stepped down')

// A hidden guest: its knight leaves the field (no frozen decoys) and comes back with the tab
const guest = (await state(B)).role === 'guest' ? B : C
const host = guest === B ? C : B
log(`hiding guest ${guest.name}`)
await guest.page.evaluate(() => Module.pauseMainLoop())
check(await waitFor('the hidden guest leaves the field', async () => (await state(host)).alive === 1, 10000),
  'a hidden guest is taken out of the world')
await guest.page.evaluate(() => Module.resumeMainLoop())
check(await waitFor('the guest is back', async () => (await state(host)).alive === 2, 10000), 'and comes back with the tab')

// A frozen tab (a phone in a pocket): pausing the debugger stops all JavaScript on the page
log(`freezing the host ${host.name}`)
const cdp = await host.context.newCDPSession(host.page)
await cdp.send('Debugger.enable')
await cdp.send('Debugger.pause')
check(await waitFor('the frozen host is replaced', async () => (await state(guest)).role === 'host', 15000),
  'a frozen host is replaced')
await cdp.send('Debugger.resume')
check(await waitFor('together after unfreezing', () => together([B, C]), 30000), 'the unfrozen player rejoins')
check((await states([B, C])).every(x => x.epoch === epoch), 'still the same world at the end')

await browser.close()
finish()
