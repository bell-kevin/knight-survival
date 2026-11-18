# Vendored JavaScript

`trystero.js` is [Trystero](https://github.com/dmotz/trystero) 0.27.0 (MIT License) together
with its dependency [@noble/secp256k1](https://github.com/paulmillr/noble-secp256k1) 3.2.0
(MIT License), bundled into a single readable (unminified) ES module. It is kept in the
repository so the game never loads code from a CDN and keeps working if a package disappears.
The license texts are next to it.

To rebuild or upgrade it (needs Node.js and npm):

```sh
mkdir /tmp/trystero-bundle && cd /tmp/trystero-bundle
npm init -y
npm install --ignore-scripts trystero@0.27.0 esbuild@0.27.7
echo "export {joinRoom} from 'trystero'" > entry.mjs
npx esbuild entry.mjs --bundle --format=esm --target=es2022 --legal-comments=inline --outfile=trystero.js
```

Then copy `trystero.js` here, keeping the header comment at the top of the current file, and
update the version numbers in it and in this README.
