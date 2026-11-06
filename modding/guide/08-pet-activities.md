# Pet activities

A mod starts an activity with `player.startActivity(kind [, level])` (true if it started: false while the pet is busy with something else, in a room, in a dialogue or menu, or the kind is
unknown). Each running activity then takes one step per tick through the system `System.hooks("pet.activity")` (`ctx.kind`, `ctx.step`; change `ctx.step` to jump, 0 ends it;
`replace` skips the game's step so the mod can drive the pet with `player.play`). Checked in a real run: wash, question, potion, colour and feed each go through the steps below.

| kind | start | steps (ctx.step) |
|---|---|---|
| `feed` | `startActivity("feed")` | 1 the first bite plays; 2 chewing, 4..9 more bites (one `step` for all of them); 3 the happy reaction, three times, then idle (+6 mood if it was food and mood < 0x1a) |
| `wash` | `startActivity("wash")` | 1 a sitting pet gets up, then the bubble starts; 2 the bubble plays; 3 the happy reaction (cleanliness +100, the colour goes back to the original one) |
| `potion` | `startActivity("potion")` | 1 turn to the front; 2 the emote reaction with the exclamation bubble, then the shocked one |
| `bark` | `startActivity("bark")` | 1 turn to the front; 2 the species' call |
| `question` | `startActivity("question")` | 1 the "?" bubble grows; 2 it waits (about half a second); 3 the answer bubble |
| `trick` | `startActivity("trick", level)` (1..3) | 1 turn to the front; 2 the trick plays (all repeats, the chain of the panda's level 3) |
| `colour` | `startActivity("colour")` | 1 turn to the front (it ends at once if the action key is not held); 2 the colour cycles while Z is held, and stops on the one showing |
| `dig` | (by Z on bare ground; no start call) | 1 settle; 2..4 the dig animation; 5 the hole and the find |
| `drop` | (a minigame prize, or a friend's gift; no start call) | 1 settle to the front; 2 the pet drops the items |

Notes
- A step is a number the game's own code moves on; setting a step the sequence never uses (or one it only reaches with state it has not set up, like `feed` 2 without a `feedRepeats`) is the
  mod's responsibility. Values outside 0..999 are ignored.
- While any of these runs (step not 0) the pet cannot be controlled, as in the game.
