# Graphics and shaders

Everything is a call on `g` inside a hook: `Scene.hooks(id).onDraw(g)` (the whole screen is the script's), `Game.hooks().onHud(g)` / `Area.hooks(id).onHud(g)` (over the world) and
`Game.hooks().onPostDraw(g)` (over the finished frame, whole window; `g.w`, `g.h` = 320 x 240, or 426 x 240 in 16:9). At most 4000 calls a frame. Colours are `{ r, g, b [, a] }`, 0..255.

## Basics (as before)
`g.clear(c)` (scenes), `g.rect(x, y, w, h, c [, fill = true [, mode]])`, `g.sprite(id, x, y [, opts])`, `g.text(id, x, y [, opts])`, `g.num(n, x, y [, opts])`.
`mode` = `"blend"` (default), `"add"` (lighten) or `"mul"` (darken / tint).

## Sprite and blit options
`{ scale = 2 | scaleX, scaleY, angle = degrees, pivot = { x, y }, tint = { r, g, b }, alpha = 0..1, mode, flip }`. A sprite keeps its anchor at x, y and turns / scales around it
(`pivot` = a point from the picture's top left instead).

## Shapes
- `g.line(x1, y1, x2, y2, c [, width = 1 .. 64])`
- `g.circle(x, y, radius, c [, fill = true [, mode]])`
- `g.poly({ x1, y1, x2, y2, ... }, c [, fill = true [, mode]])`: 3 to 64 corners, convex (filled as a fan from the first).

## Canvases: offscreen pictures
`g.canvas(name, w, h, function() ... end)`: the `g` calls inside draw into an empty offscreen picture of that size (1..2048 a side), in its own pixels. Canvases do not nest; the 8 used
last are kept. Show one with `g.blit(name, x, y [, { w, h, scale, angle, pivot, tint, alpha, mode, flip }])` (w, h fit it to a size; otherwise its own size) or on triangles with `g.mesh`.
A canvas is drawn the frame it is made in (make it again each frame, or keep drawing it: it is yours until 8 newer ones push it out).

## The frame as a picture: `"@frame"`
`g.blit("@frame", ...)` and `g.mesh("@frame", ...)` use the picture as it was when the list began. In `onPostDraw` that is the finished frame (world, menus, dialogue, other mods' HUDs),
whole: what you cannot do with rectangles, you can do by drawing it again: zoom, rotate, shift, tint, blur (several offset blits with alpha), ghost trails, a split screen.
Read back from the screen the frame it is asked for (not when no script uses it).

## Meshes: warps
`g.mesh(name, vertices [, triangles [, { mode }]])`: vertices = `{ { x, y, u, v [, colour] }, ... }` (u, v 0..1 over the picture; a colour tints that vertex), up to 4096;
triangles = `{ i1, i2, i3, ... }` (1-based; default: three vertices at a time). A grid of vertices moved by a sine is a water ripple; the demo is `mods/gfxdemo`.

## The world camera: zoom and rotation
`System.hooks('camera.view')`: `ctx.zoom` (1 = as is; 0.25 to 4: below 1 more of the area shows) and `ctx.angle` (degrees) scale and turn the world around the middle of the view; the screens, dialogue and HUD
stay as they are. The world is drawn into a picture of (view / zoom) and shown over the view, so zoomed pixels are big and crisp. Rotation shows black corners.

## The front end (title, load, options)
The scripts of the mods are loaded for the title screens too, and the same `g` works there: `Game.hooks().onHud` and `onPostDraw` draw over every front-end screen with everything above.
More than drawing:
- **Ticks and keys:** `Game.hooks().onTickStart / onTickEnd` run there 30 times a second (`ctx.state.menu` is true, `ctx.state.scene` says if a scene is up, `ctx.input` / `ctx.held` are Z, X and the arrows;
  there is no world, so `ctx.area.id` is empty and `ctx.pet` zeros).
- **Scenes:** `scene.open("mymod:scene/id")` (from a tick hook, or from a `System.hooks("frontend.menu")` hook that took the choice over) opens a `Scene.hooks(id)` of the mod over the whole screen:
  its `onTick(ctx)` gets the keys (`ctx.input.z`, `x`, `left`, `right`, `up`, `down`, and `held`), `onDraw(g)` draws, `scene.close()` ends it. While a scene is up the front end gets no keys.
  So a mod can add whole screens to the title: a "Mods" page, a custom new-game flow, a gallery. Example: a `frontend.menu` hook that, for choice "options", does `scene.open(...)` and returns false
  (cancel) to replace the options screen with its own.
- Still fixed C++ there: the starter pick, name entry, the intro, the options screen's rows and the boot sequence (see docs/engine-map.md).

## Shaders (OpenGL, GLSL 1.20)
A mod ships `shaders/<name>.frag`, registers it with `{ "type": "shader", "mode": "add", "id": "mymod:shader/wave", "file": "shaders/wave.frag" }` and runs it from a script with
`g.shader("mymod:shader/wave", { strength = 0.5, tint = { 1, 0.9, 0.8 } })` (best in `onPostDraw`; it runs over everything drawn so far, so draws before it are affected and draws after it are not).
The file is the shader's body: `void main() { gl_FragColor = ...; }`. Leave out `#version` (the game adds `#version 120`). The game gives it:

| name | meaning |
|---|---|
| `uv` | 0..1 over the screen; y = 0 at the TOP |
| `frame(vec2 p)` | the picture so far, sampled at p (same orientation as `uv`) |
| `resolution` | the logical screen's size (320 x 240, or 426 x 240) |
| `time` | seconds since the game started |
| your names | `uniform float / vec2 / vec3 / vec4`, set from the table you pass (up to 16) |

- The renderer: SDL's own default on Windows is Direct3D, which cannot run GLSL. The game switches to SDL's OpenGL renderer when any mod has a `shaders/*.frag` (or with `GP_RENDERER=opengl`);
  `GP_RENDERER=default` keeps SDL's choice. If OpenGL cannot start, or the renderer is not opengl, shader passes are skipped and the log says so once.
- A shader that does not compile is reported in the log with the driver's message the first time it is used, and skipped from then on.
- Passes are GPU to GPU (the frame is copied inside the card, no read back). Several `g.shader` calls in a row work (each sees the result of the one before).
- It works in the front end too. The demo is `mods/gfxdemo/shaders/pp.frag` (colour fringing, vignette, grade).

## Layers: shading the world, the screens, or only one of them
The frame is built in layers, and a script may run drawing and shaders at two points:
1. `Game.hooks().onPostWorld(g)`: right after the world is drawn (zoomed / turned too), BEFORE the screens, dialogue, menus, the stat notice and the mods' `onHud`. A `g.shader` here shades the world only.
2. `Game.hooks().onPostDraw(g)`: after everything. A `g.shader` here shades everything so far.

To act on the screens only: in `onPostWorld` save the world with `g.keep("world")` (a canvas of the whole picture, pixel for pixel), then in `onPostDraw` run a shader that gets it as a second texture:
`g.shader("mymod:shader/ui", {}, { textures = { world = "world" } })`. In the shader, `uniform sampler2D world;` and `canvas(world, uv)` read it (up to 4 extra textures, any canvas name);
where `frame(uv)` differs from `canvas(world, uv)` the pixel was drawn by the screens (that is `mods/gfxdemo/shaders/uitint.frag`: world sepia from onPostWorld, screens inverted from onPostDraw).
Order matters: `g.keep` saves the picture as it is at that point of the list, so keep after the world shader if you want the shaded world. A shader after another sees its result.
In the front end there is no world: only onHud / onPostDraw.

## Shaders on one thing of the world
`world.shade(target, shaderId | nil [, uniforms])`: a shader on the pet (`"player"`), on an NPC (its npc id) or on every object of a type (the object type id), until cleared with `nil`. The thing is
drawn alone onto a transparent picture (384 x 384 px, feet at the middle / three quarters down) and that picture is drawn again over the same place through your shader, blended over the world.
In the shader `frame(uv)` is the thing's own pixel (with its alpha), `resolution` the picture's size (384 x 384), `time` and your uniforms as usual: a glow outline, a recolour, a dissolve,
a wobble. Its shadow, bubbles and overhead pictures are drawn separately and are not shaded. It works under a zoomed or turned camera too (the thing is shaded inside the world's picture). Limit: it needs the OpenGL renderer. Not saved: set it from a script when the game loads. The demo: `mods/gfxdemo/shaders/glow.frag` (a pulsing outline on the pet).

## Not here yet
Shaders on a thing's shadow or bubbles; vertex shaders; `time` is wall-clock.

