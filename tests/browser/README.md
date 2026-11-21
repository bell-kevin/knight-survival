# Browser tests

These open the web build in real headless browsers that play together through the real public
Nostr relays and WebRTC, the same way people play on the website. They check that:

- players who open the same room end up in one world, and the oldest world wins,
- the game carries on (same world, same score) when the host leaves, hides its tab or freezes,
  and a host that comes back steps down instead of bringing back an old copy of the world,
- coins, red knights, shooting, game over and respawning all work for a guest, over the network,
- collecting a level's coins opens its door, and walking through it takes the whole team to the
  next level (everybody starting on the new map's floor, not on top of each other),
- touch controls work (drag your knight, tap to shoot),
- the landing page loads cleanly and private-room invite links work.

They need Node.js and an internet connection (the relays are public servers on the internet).

```sh
# 1. From the top folder: build the web version and serve it
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release && cmake --build build-web
python3 -m http.server -d build-web/site 8080 &

# 2. Run the tests
cd tests/browser
npm install
npx playwright install chromium     # or point CHROME=/path/to/chrome at a browser you have
npm test
```

Each test also runs on its own and can save screenshots:
`node multiplayer.mjs http://localhost:8080/ /tmp/screenshots`

The tests peek into the game with `Module.ccall('knight_debug_state', 'string')`, which returns
what that browser's game thinks is going on (see `debugJson` in `src/session_web.cpp`).
