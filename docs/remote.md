# The remote screen: the rig on a phone or tablet

Built 27 November 2026. This is the control app from the design doc ("Tablet app design"),
built as a page that the rig app itself serves over the network. Nothing is installed on the
phone or tablet: you type an address into its browser. The same page works on a phone held
upright, a phone on its side, a tablet and a laptop.

**It starts switched off, and switched off nothing is open to the network** and the app runs
exactly as it did before. Switching it on does not touch the sound.

A demo that needs no rig at all is at the link in the design doc, and inside the page itself
(see "The demo" below).

## Switching it on

1. Put the phone or tablet on the same Wi-Fi (or the same network cable) as the computer that
   runs GigRig.
2. In GigRig click the **Remote screen** tab (the last one).
3. Tick **Let a phone or tablet on the same network control the rig**. The first time on Windows, a box may ask whether GigRig may use
   the network: click **Allow**.
4. The tab lists one or more addresses that look like `http://192.168.1.20:8080`. Type one
   into the phone's or tablet's browser, exactly as shown. The tab says "A phone, tablet or
   browser is connected" when it worked.
5. To keep it handy: in the phone's browser menu choose **Add to Home screen**.

The tick is remembered, so it is on again the next time the app starts. Untick it to close the
door. If 8080 is taken by something else, change the port number on the tab.

**There is no password.** Anyone on the same network who knows the address can open the page
and move a fader. Use it on your own Wi-Fi or the rig's own network, never on a venue's guest
Wi-Fi. (A password is on the "Not built yet" list.)

**Timing-critical presses belong on the foot switches.** A tap on a phone travels over Wi-Fi
and can arrive a few hundredths of a second late, and late by a different amount each time.
The on-screen switches are for setting up, trying layouts and the odd press, not for
recording loops in time at a gig.

## The screens

On a phone or tablet the **right-hand edge of every screen is a strip to scroll by** (it is
the colour of the background). No slider or fader reaches into it, so a thumb there always scrolls and never moves a level.

The eight buttons along the bottom (down the left side when a phone is on its side):

| Screen | What it does |
| --- | --- |
| **Looper** | The six tracks (colour and word for what each is doing, how far through its loop, how many lanes, an UNDONE mark), and the nine switches plus CTL3 and CTL4. Touching a switch is the same as a foot on the real one, hold and double-press included. Each switch says what it does in the song's layout and in the bank that is showing. **Clear all loops** asks first. |
| **Songs** | The same list as the Songs tab. Tap a song to load it (it asks first if loops are running). **Keep these sounds for this song**, **Put the song's sounds back** and **New song** do what the same buttons on the Songs tab do. |
| **Build** | The looper arrangement builder. See the next part. |
| **Effects** | A pedalboard for each of the four chains. Every effect is a small drawing of a pedal, a rack unit or an amp, joined by a line that runs from IN to OUT in the order the sound meets them, snaking back along the next row. A lit lamp means the effect is on. Tap a drawing and the screen becomes that effect's own page: its on switch, its settings, **Move earlier**, **Move later** and **Remove** (its settings travel with it), and two arrows to step to the effect before or after it. **‹ Pedalboard** goes back. **Add an effect** lists every effect the rig has, by kind, puts the one you tap on the end of the chain and opens its page. **Presets** opens a sheet to load a preset onto the chain, **Save as a preset**, and **Update its presets** (saves the chain's changes back into the preset or presets it was made from, exactly as the same button on the Effects tab does). |
| **Mixer** | Faders only, in three banks chosen by the tabs along the top: **Inputs**, **Effects** (the four reverbs and delays) and **Outputs**. Every fader is the same width in every bank and nothing scrolls sideways. An upright phone shows five faders to a bank, so there the inputs take two tabs (**In 1 to 5** and **In 6 to 8**) and the faders run most of the height of the screen. Each strip is tinted with its sound's colour: red for the vocal, yellow for the acoustic, blue for the electric, orange for the bass (a channel takes the colour of the effects chain on its input socket), and the same colours mark the chains on the Effects screen. On Inputs the row of buttons above the faders chooses what they send to: the main mix, a monitor, or one of the four effects. Drag a fader from anywhere on it (it never jumps to your finger). Tap a fader to pick it, then the four buttons under the faders move it by 1 dB or 0.1 dB (on a keyboard, the up and down arrows; hold Shift for 0.1). They stand in for the board's Adjust knob. **Open** on a fader leaves the faders for that channel's own page: its signal chain runs down the left as small pictures, top to bottom in the order the sound meets them (Input, each block of processing, Sends), and the one you tap fills the rest of the screen. A green dot on a picture means that block is on. The two arrows step to the next channel and stay on the same block. **‹ Faders** goes back. **Set-ups** loads an act, a venue or a monitor set-up, and saves each one: **Save over** the one that is loaded, or **Save as a new one** under a name you type. If the mixer is off the screen says so and offers to switch it on. |
| **Record** | One big **Record** button with the time, a meter and how many hours fit on the drive. Under it, two big buttons that each open a page: **Channels** (tap a channel, then tap where it is recorded) and **Recording mix** (its balance, pans, EQ, bus compressor, limiter and level, laid out like a mixer channel's page). A third, **Recordings**, lists what is on the drive: tap one to play it back as a virtual soundcheck, keep it, copy it to USB or delete it, and at the bottom copy or clear all of them. Then the mixes to write and the list of files Record will make. What is recorded is locked while it records; the recording mix is not. `docs/recording.md` explains every choice. |
| **Tools** | **Tuner**: on and off, a small note and needle, what goes silent while tuning (the guitars, the whole main mix or nothing), which channels are guitars, which input it listens to and what A is (`docs/tuner.md`). While the tuner is on, the **full-screen strobe** lies over every screen (Done switches the tuner off, Options comes here). **Break music**: choose a mix, Play, From the start, Stop, Bring the music up, Fade it down, its level, the fade times and what counts as starting to play (`docs/break-music.md`). **The venue knob (ENC 5)**: Turn left, Turn right, Push and Hold, with the line the board's screen would show. |
| **Rig** | Whether audio is running, the warnings, and **Copy report**: the same text as the Copy report button in the app. |

The dot at the top left is green when the page is talking to the rig and audio is running,
amber when the rig has no audio device open, red when the rig has stopped answering (the page
says so in a red bar and carries on by itself when the rig comes back).

Whatever is changed on the page shows in the app's own tabs, and the other way round. Two
phones can be open at once.

## Building a looper layout

Until this build a song's looper layout (which switch does what, what each track records)
could only be one copied from the RC-600. The **Build** screen makes and changes them.

It works on a **draft**. Nothing reaches the looper or the disk until you press a button:

- **Try it** puts the draft on the looper so you can play it. Nothing is saved. Loops that are
  running are cleared (it asks first).
- **Save to this song** writes the layout into the song's file.
- **Save as a new song** makes a new song with this layout and the sounds loaded now.
- **Start from another song** copies any song's layout into the draft.
- **Undo my changes** throws the draft away.
- **Go back to the built-in layout** takes the song's own layout out of its file again.

The four pages of the builder:

| Page | What you set |
| --- | --- |
| **Tracks** | For each of the six tracks: what it records (vocal, acoustic, stomp, bass, electric), its length (free, or a number of bars), how it comes back when started again, whether it stops at once or at the end of its loop, and its playback level. |
| **Switches** | Bank 1 and bank 2, the nine switches laid out as on the RC-600, plus CTL3 and CTL4. Tap a switch to choose what it does and on which track. **Add another action to this press** makes one press do two things. **Copy to bank 2** copies a whole bank. |
| **Links** | Something a track does by itself when another track starts: "when track 1 starts to play, track 3 starts to record". |
| **Song options** | Beats in a bar, whether a track goes to play or to overdub after recording, whether a new track waits for the bar, and a tempo to save with the song. |

**Lanes.** A track that records sources from different sockets keeps them apart: each gets
its own lane, loops side by side with the others, and comes back on its own mixer channel.
The builder shows how many lanes each track has. A track keeps up to three; with more sockets
than that, the last ones share the last lane.

**Left out on purpose** (Tom, 27 Nov 2026): reverse, one-shot, fades, replace overdub and half
speed. A layout from the RC-600 that already fades keeps its fade; the builder does not offer
to add one.

A layout and its extra actions can hold 16 links in all, as on the RC-600.

### What it writes in the song file

A saved layout is plain lines in the song's file (`Documents/GigRig/Songs`, see
[songs.md](songs.md)), so it can be read and fixed by hand:

```
looper beats = 4
looper order = record play overdub
looper new track = waits for the bar
looper track 1 = acoustic + stomp / free / in place / stops at once / level 100
looper track 2 = bass / 4 bars / from its first bar / stops at the end of its loop / level 96
looper switch 1.1 = track 1 / record play, hold undo
looper switch 1.9 = all start stop
looper link = switch 1.3 / track 1 / play stop
looper link = track 1 plays / track 3 / record play
```

| Line | Meaning |
| --- | --- |
| `looper beats = 4` | Beats in a bar, 1 to 16. |
| `looper order = record play overdub` | After recording a track plays. `record overdub play` makes it overdub first. |
| `looper new track = waits for the bar` | Or `starts at once`. |
| `looper track 1 = sources / length / comes back / stops / level` | Sources: `vocal`, `acoustic`, `stomp`, `bass`, `electric` joined with `+`, or `nothing`. Length: `free` or `4 bars`. Comes back: `in place`, `from its first bar`, `from its first note`. Stops: `stops at once`, `stops at the end of its loop` (and `fades out`, kept for old layouts). Level: `level 0` to `level 200`. |
| `looper switch 1.3 = track 1 / record play, hold undo` | Bank 1, switch 3. The track is `track 1` to `track 6` or `current track`. The actions are the ones in the builder's list, in these words: `record play`, `record play, hold undo`, `record play, hold undo, double stop`, `record play, hold undo or clear, double stop`, `play stop`, `play stop, hold undo`, `play stop, hold undo or clear`, `stop`, `stop, hold clear`, `stop, double clear`, `clear`, `undo redo`. With no track: `all start stop`, `all start stop, hold clear all`, `all start stop, double clear all`, `clear all`, `bank`, `nothing`. |
| `looper link = switch 1.3 / track 1 / play stop` | An extra action on a press. `switch CTL3` and `switch CTL4` work in both banks. |
| `looper link = track 1 plays / track 3 / record play` | A link between tracks: `track 1 plays` or `track 1 records`, then the track and what it does (`record play`, `play stop`, `stop`, `clear`, `undo redo`), or `all start stop`. |
| `looper links = none` | The layout has no links. |
| `rc 201`, `rc 15 134` | Something from the RC-600 that the words cannot say, kept as its RC-600 number so nothing is lost. |

A song with these lines uses them in place of the built-in layout it would otherwise borrow.
A line the rig cannot use is reported on the Songs tab and changes nothing.

**These words are saved on Tom's disk: never rename one, only add.**

**Going back to an older build:** a build from before 27 Nov 2026 does not know the `looper`
lines. It reports them as lines it could not use and plays the song with its built-in layout.
Nothing is lost; the lines work again on a newer build.

## The demo

The page has a demo inside it: a pretend rig kept in the page, for trying the screens with no
rig and no risk. It runs when the page is opened from the published demo link, or by adding
`#demo` to the rig's address (`http://192.168.1.20:8080/#demo`). An orange **DEMO** mark shows
at the top.

- The songs, sounds and mixer are a copy of Tom's laptop set-up, taken from the real app.
- The looper is a simple stand-in. It follows the switches closely enough to try a layout
  (the first loop sets the bar, a new track waits for the bar, one recording at a time, holds
  and double presses, links, banks). The loops are pretend and the meters are made up.
  **Never judge timing from the demo.**
- Loading a preset or a mixer set-up only says what would happen. Everything else can be
  changed, built, tried and saved, and is forgotten when the demo starts again (Rig screen).

## In the report

The Copy report text has a "Remote screen" part: on or off, the port, the addresses, how many
requests were answered, how many commands were carried out, and when a page last asked.

## What the second phase added (27 Nov 2026)

The state gained `fxCatalogue` (every effect that can be added: `id`, `name`, `group`),
`fxMaxSlots`, `breakMusic` and `tuner`; the live part gained `breakMusic` (playing, up, how far
through the fade, the status line, the knob's line) and `tuner` (on, heard, the note, cents,
Hz, the socket). `fxCatalogue` and `fxMaxSlots` sit at the top of the state, not inside
`effects`, because a song's sounds replace `effects` whole.

**28 Nov 2026, the strobe tuner:** the state's `tuner` gained `silence`, `monitors`, `fullScreen` and
`channels` (every mixer channel that is on: `name`, `silent`); the live `tuner` gained `strobing`
and `strength`. The page's strobe (`GR.strobe` in `remote/screens.js`, called from the live tick
in `core.js`) moves its bands by the cents and Hz in the live part.

The state never holds a folder path or anything else from the computer's own disk layout: the
demo's data is a copy of a real state and is committed to a repository that may be public.

When the state gains a part and the self-test says the demo lacks it, and the machine at hand
does not have Tom's songs, run a throwaway app and top the demo's data up instead of capturing
it again (a capture would replace Tom's songs with whatever that machine has):

```
HOME=/some/empty/folder GIGRIG_REMOTE=8080 xvfb-run -a build/GigRig_artefacts/Release/GigRig &
python3 tools/remote_demo.py top-up http://127.0.0.1:8080
```

It keeps everything `remote/demo-data.js` has, adds only the parts it lacks, and describes
every effect in `fxCatalogue` so the demo can add any of them.

## Pages inside a screen (28 Nov 2026)

Tom's rule for the final screens: many pages with few, big controls are better than one crowded
screen that scrolls, as long as the way in and the way back are obvious. The first step toward
that is in the Mixer and Effects screens. It changes only what the page shows: it uses the same
orders as before, and nothing about the sound.

- **Mixer.** The faders are a page of their own, in three banks (Inputs, Effects, Outputs) like
  the CQ12T's tabs. **Open** on a fader goes to that channel's page, which has the signal chain
  down the left and one block at a time on the right.
- **Effects.** The chain is a pedalboard picture, and each effect has its own page. It looks
  different from a mixer channel on purpose, so nobody mistakes one for the other.
- **One colour for one sound.** `chainColours` in `remote/screens.js` holds Tom's chain colours, the same ones planned for the foot switch screens. `tint` sets `--chain` on a strip, a page or the pedalboard and the last rules of `remote/app.css` paint it. The stomp's green and the duo partner's purple are stand-ins until Tom chooses.
- **The drawings are stand-ins.** A pedal's drawing is a shape and a colour chosen in
  `pedalLook` in `remote/screens.js`, not a likeness. The finished look of each piece of gear
  comes last (see "The gear look sample" below).
- **The picked fader.** The buttons under the faders and the arrow keys stand in for the board's
  Adjust knob (ENC 6), which will turn whichever fader was last touched.
- For a developer: a page is kept in `GR.ui.mixPage` (`{ kind: 'ch' | 'fx' | 'out', i }`) or
  `GR.ui.fxPage` (`{ c, i }`), and `chainPage`, `blockItem` and `pageHead` in `remote/screens.js`
  build one. A new kind of page should start with `pageHead` so the way back is always in the
  same place.

## Not built yet

- A password or a "look but do not touch" mode.
- The page as the rig's own built-in touchscreen (it is the same page in a browser on the rig).
- Tap tempo (a tap over Wi-Fi arrives late by a different amount each time, so it belongs on a foot switch).
- Choosing which effect goes where in one go (drag and drop); for now an effect moves one place at a time.

## For developers

```
remote/index.html, app.css      the page and its look
remote/core.js                  what every screen shares: talking to the rig, drawing, faders, sheets
remote/screens.js               Looper, Songs, Effects, Mixer, Tools, Rig
remote/builder.js               the arrangement builder
remote/demo.js                  the pretend rig (answers the same three questions as the real one)
remote/demo-data.js             the demo's copy of a real state. MADE BY tools/remote_demo.py, never by hand
src/RemoteApi.*                 the rig as JSON, and JSON back into actions. No network in it
src/RemoteServer.*              the small web server (off by default) and the files built into the app
src/RemotePanel.*               the Remote screen tab
src/LooperLayout.*              a layout as song-file lines and as numbers for the page
src/RemoteSelfTest.cpp          the checks for all of the above
tools/remote_demo.py            captures the demo's data from a running app (or tops it up); builds the one-file demo page
```

The page is plain JavaScript with no libraries and no build step, so the rig serves it with no
internet. The files are built into the app (the `GigRigData` list in `CMakeLists.txt`): a new
file must be added there and to `index.html`, and the self-test fails if the two disagree.

**How the page talks to the rig.** Three addresses:

- `GET /api/state` gives `{ "v": 12, "s": { ... } }`: everything that changes only when somebody
  changes it (songs, the layout, the chains, the mixer). `v` goes up when it changes.
- `GET /api/live` gives `{ "v": 12, "l": { ... } }`: what moves by itself (the tracks, the beat,
  the meters). The page asks about eight times a second, and fetches the state again when `v`
  has moved on. The rig only works these out while a page has asked in the last four seconds.
- `POST /api/cmd` with one JSON order, answered with `{ "ok": true, "note": "..." }` or
  `{ "ok": false, "error": "..." }`.
- `GET /api/report` gives the report as text.

The orders (`RemoteApi::command`; the demo answers the same ones in `remote/demo.js`):

| Order | Fields |
| --- | --- |
| `pedal` | `sw` (0 to 8 = switches 1 to 9, 9 = CTL3, 10 = CTL4), `down` |
| `looper.clearAll`, `report` | |
| `song.load` | `index` |
| `song.program` | `program` |
| `song.keep`, `song.again` | |
| `song.new` | `name`, `program` |
| `layout.of` | `index`; the answer has `layout` |
| `layout.try`, `layout.save` | `layout`, `withTempo` |
| `layout.saveAs` | `name`, `program`, `layout`, `withTempo` |
| `layout.forget` | |
| `fx.enabled`, `fx.chainOn` | `on`; `chain`, `on` |
| `fx.preset` | `chain`, `name` |
| `fx.slotOn`, `fx.param` | `chain`, `slot`, `on`; `chain`, `slot`, `id`, `value` |
| `fx.add` | `chain`, `type` (an `id` from the state's `fxCatalogue`), `at` (the place, 0 first; left out = the end). Refused when the chain holds `fxMaxSlots` |
| `fx.remove` | `chain`, `slot` |
| `fx.move` | `chain`, `slot`, `to` (the place it ends up in) |
| `fx.save` | `chain`, `name`: the whole chain as one preset |
| `fx.update` | `chain`: the chain's changes back into the presets it was made from |
| `mix.on` | `on` |
| `mix.ch` | `c`, `key` (`fader`, `mute`, `pan`, `trim`, `on`, `mon0`, `mon1`, `fx0` to `fx3`, `recLevel`, `recPan`, and `rec` whose value is `none`, `raw`, `effects`, `looper` or `processed`), `value` |
| `mix.out` | `o`, `key` (`level`, `mute`), `value` |
| `mix.fx` | `f`, `key` (`ret`, `on`, `mon0`, `mon1`, `lowCut`, `highCut`, `recLevel`), `value` |
| `mix.param` | `where` (`ch`, `out`, `fx`, `fxeq`, `rec` for the recording mix's own chain), `i`, `slot`, `id` (`on` switches the block), `value` |
| `mix.preset` | `kind` (`act`, `venue`, `monitors`), `name` |
| `mix.save` | `kind` (`act`, `venue`, `monitors`), `name` |
| `mix.keepFx` | `f` |
| `rec.start`, `rec.stop` | Start a recording, and stop it. Refused when it is already so |
| `rec.set` | `key` (`main`, `mix`, `fx`: which mixes are written; `check`: the raw copy for a virtual soundcheck; `level`: the recording mix's level in dB), `value` |
| `check.start` | `name` (a recording from the state's `recordings.list`), `loop` (round and round; left out = yes): plays its raw files through the rig as a virtual soundcheck |
| `check.stop` | Stops the soundcheck. Refused when none is playing |
| `recs.keep` | `name`, `on`: a kept recording is never cleared or deleted |
| `recs.delete` | `name`. Refused for a kept one, and for one that is being recorded or played |
| `recs.clear` | `copied` (yes = only the ones copied to USB; no = all): deletes every recording that is not kept |
| `recs.copy` | `names` (left out or empty = every one not copied yet), `drive` (a name from `recordings.drives`). Refused while recording or soundchecking |
| `recs.cancel` | Stops a copy |
| `break.play` | `name` (a mix from the state's `breakMusic.mixes`; left out = the last one), `fromStart` |
| `break.stop`, `break.up`, `break.down` | |
| `break.level` | `value` (dB) |
| `break.set` | `key` (`fadeDown`, `fadeUp`, `onSwitch`, `onSong`, `onSound`, `soundDb`), `value` |
| `knob` | `do` (`left`, `right`, `push`, `hold`): the venue knob, ENC 5 |
| `tuner.on` | `on` |
| `tuner.set` | `key`, `value`. Keys: `socket` (0-based, -1 = whichever guitar is played), `reference` (Hz), `silence` (`"guitars"`, `"main"` or `"nothing"`), `channel` (with `name`: a mixer channel's name; `value` true = it goes silent while tuning), `monitors` (true = the monitors keep the guitars), `fullScreen`, and the older `mutes` (true = the guitars, false = nothing) |

Every value is checked and limited by the rig, whatever the page sends. A venue loaded from
the page goes through the same safe load as on the Mixer tab (it fades in and never jumps up).

**Rules that keep it safe.**

- The server is off unless Tom ticks the box. Off, no socket is open and no timer runs.
- Nothing in it runs on the audio thread. A switch press goes in the same way as a click on
  the app's own on-screen switches; everything else is carried out on the message thread.
- The page's meters take the mixer's peak readings, the same ones the Mixer tab shows, so with
  both open each sees some of the peaks. That only affects how the meters look.
- A layout from the page is read by `layout::fromJson`, which limits every number, before it
  reaches the looper. The looper itself was not changed for the builder.

**Adding an order:** add it in `RemoteApi::command` (or one of its three helpers), add the
same order to `remote/demo.js`, a row to the table above, and a check in `RemoteSelfTest.cpp`.

**Changing what the state holds:** change `RemoteApi::state`, then run the app and
`python3 tools/remote_demo.py capture http://127.0.0.1:8080` on a machine with the songs and
presets the demo should show, so the demo's copy has the new part. The self-test fails if the
demo's copy is missing a part the real state has. (The capture loads every song in turn:
never run it against a rig that is in use.)

**Trying it in a cloud container** (no screen, no audio):

```
GIGRIG_REMOTE=8080 xvfb-run -a build/GigRig_artefacts/Release/GigRig &
curl --noproxy '*' http://127.0.0.1:8080/api/live
python3 tools/remote_demo.py build demo.html      # the one-file demo page
```

`GIGRIG_REMOTE=<port>` switches the server on for that run without touching the saved tick.

### The gear look sample (demo only)

Tom's idea (27 Nov 2026) is that each effect should look like the gear it copies. One sample
exists so he can judge the look: `remote/gear-sample.js` draws the 1176 style compressor
(effect id `comp`) as a black studio face with knobs that turn by dragging, ratio buttons and
a backlit VU meter whose needle follows the gain reduction.

- It is **in the demo only**. It is not in `index.html` and not in the app's built-in files;
  `tools/remote_demo.py build` adds it to the one-file demo page when the file exists.
- It changes no other file: it wraps `GR.block` and brings its own styles. Taking it out is
  deleting the file.
- In the demo the needle follows a pretend signal, because the demo has no sound. A real
  version reads the figure the engine already reports (`live.mix.gr` for a mixer channel).
  A compressor in an effects chain has no such figure in `RemoteApi::live` yet: it would need
  one, read from a value the effect already keeps and passed across without any work on the
  audio thread.
- The drawing costs the audio nothing: it is done in the browser, one needle turn per screen
  frame, only while a meter is on screen and the page is showing.
- To make it real: move the look into the page proper, give every effect type a face (or a
  plain rack face for ones that copy nothing), print each knob's scale as the real gear does,
  and take the names shown from Tom's local names file (design doc, "Real gear names").
