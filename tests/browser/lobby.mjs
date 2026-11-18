// The landing page: loads without errors in light and dark mode, and "Create private room"
// gives you an invite link that works for a friend. Usage: node lobby.mjs [site-url] [screenshot-folder]
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later
import {SITE, check, finish, launch, screenshot} from './common.mjs'

const browser = await launch()
const ready = page => page.waitForFunction(() => !document.getElementById('play').disabled, null, {timeout: 30000})

for (const colorScheme of ['dark', 'light']) {
  const page = await (await browser.newContext({viewport: {width: 1280, height: 900}, colorScheme})).newPage()
  const errors = []
  page.on('pageerror', e => errors.push(e.message))
  page.on('console', m => m.type() === 'error' && errors.push(m.text()))
  await page.goto(SITE)
  await ready(page)
  check((await page.textContent('#play')) === 'Play now', `${colorScheme}: the Play button is ready`)
  check(errors.length === 0, `${colorScheme}: no errors while loading ${errors.join(' | ')}`)
  await screenshot(page, `lobby-${colorScheme}`)
}

const context = await browser.newContext({permissions: ['clipboard-read', 'clipboard-write']})
const page = await context.newPage()
await page.goto(SITE)
await ready(page)
await page.click('#create')
await page.waitForTimeout(500)
const invite = await page.inputValue('#invite')
check(/#room=[a-z0-9]{6}$/.test(page.url()), `a private room goes in the address bar (${page.url()})`)
check(invite === page.url(), 'the invite link is the address')
await page.click('#copy')
check((await page.evaluate(() => navigator.clipboard.readText())) === invite, 'Copy invite link copies it')
check(await page.evaluate(() => document.activeElement.id === 'canvas'), 'the keyboard goes back to the game')

const friend = await (await browser.newContext()).newPage()
await friend.goto(invite)
await ready(friend)
check((await friend.textContent('#play')).startsWith('Join room'), 'a friend opening the link is offered that room')

await browser.close()
finish()
