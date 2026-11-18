// net.js - Carries the game's text messages between browsers.
//
// This file only moves strings around. The game itself, including who hosts the world and what
// the messages mean, is C++: see src/session_web.cpp, which calls the functions at the bottom.
//
// Players in the same room find each other through public Nostr relays (an open, decentralized
// protocol: no accounts, no sign-up) and then talk directly, browser to browser, over WebRTC
// data channels. The relays only help with that first introduction; game traffic never touches
// them. Trystero (MIT licensed, vendored in ./vendor) does the WebRTC and relay work.
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later
import {joinRoom} from './vendor/trystero.js'

// Every room name is namespaced by this, so other apps using the same relays never mix with us.
const APP_ID = 'github.com/bell-kevin/knight-survival'

// Public Nostr relays that introduce players to each other (everyone must use the same ones).
// Picked from Trystero's own default list, keeping well-established relays that were answering
// when this was written. If players stop finding each other, check these still work.
const RELAYS = [
  'wss://relay.primal.net',
  'wss://relay.snort.social',
  'wss://relay.nostr.net',
  'wss://bucket.coracle.social',
  'wss://relay.ditto.pub',
  'wss://nostr.overmind.lol',
]

// STUN servers tell each browser its public address so that peers behind home routers can
// reach each other. Community and free-software projects first, big providers as fallbacks.
const ICE_SERVERS = [
  {urls: 'stun:stun.nextcloud.com:443'},
  {urls: 'stun:stun.freeswitch.org:3478'},
  {urls: 'stun:stun.l.google.com:19302'},
  {urls: 'stun:stun.cloudflare.com:3478'},
]

const MAX_QUEUE = 5000 // events waiting for the game (it pauses in hidden tabs)
const MAX_MESSAGE = 32768 // longest message we accept from a peer, in characters

const queue = []
let room = null
let channel = null
let selfPeerId = ''

// Past MAX_QUEUE, the oldest message is dropped (the game only needs the latest world anyway),
// but never a "connected"/"disconnected" event.
const push = event => {
  if (queue.length >= MAX_QUEUE) {
    const oldest = queue.findIndex(e => e[0] === 'M')
    if (oldest < 0) return
    queue.splice(oldest, 1)
  }
  queue.push(event)
}

function connect(roomName) {
  try {
    room = joinRoom(
      {appId: APP_ID, identity: 'none', relayConfig: {urls: RELAYS}, rtcConfig: {iceServers: ICE_SERVERS}},
      roomName,
    )
  } catch (err) {
    console.warn('[net] could not join room:', err)
    room = null
    return
  }
  channel = room.makeAction('msg')
  channel.onMessage((data, {peerId}) => {
    if (typeof data === 'string' && data.length <= MAX_MESSAGE)
      push(`M ${peerId} ${data.replace(/[\r\n]/g, ' ')}`)
  })
  room.onPeerJoin(({peerId}) => push(`J ${peerId}`))
  room.onPeerLeave(({peerId}) => push(`L ${peerId}`))
  room.onError(err => console.warn('[net]', err.code, err.message))
  room.getSelf().then(self => (selfPeerId = self.peerId), () => {})
}

// Note: when the tab is hidden the browser pauses the game but we stay connected. The others
// notice the silence and carry on without us; the C++ side sorts out who hosts when we're back.

// The interface the C++ code uses (see the EM_JS functions in src/session_web.cpp).
// Events handed to the game by poll():
//   "J <peer>"            a peer connected
//   "L <peer>"            a peer disconnected
//   "M <peer> <message>"  a peer sent us a message
window.KnightNet = {
  join(name) {
    if (!room) connect(name)
  },
  poll() {
    return queue.shift()
  },
  send(peer, message) {
    channel?.send(message, {target: peer || null}).catch(() => {})
  },
  selfId() {
    return selfPeerId
  },
  peerCount() {
    return room ? Object.keys(room.getPeers()).length : 0
  },
}
window.dispatchEvent(new Event('knightnet-ready'))
