# Gig Rig

Software for Tom's custom all-in-one gig rig: one box to replace the pedalboard,
the Boss RC-600 looper and the Allen & Heath CQ12T mixer. The design and the
staged plan live in the project's design doc; this repository holds the code.

The app is built in stages. Each stage adds to the same app, `GigRig`.

| Stage | What the app does | Status |
| --- | --- | --- |
| 0 | Device check: lists every audio and MIDI device, and says whether it can see the CQ mixer and the RC-600 | Built |
| 1 | Audio through the CQ: a round-trip latency meter, buffer-size choice, play-through with a dropout watch, and a test tone for each return | Built |
| 2 | The RC-600 as the pedal: every switch by name, and press, 2-second hold and double-press told apart | Built |
| 3 | The looper: six tracks that copy how the RC-600 loops, driven by the RC-600's switches, for a side-by-side test | Built, first version |
| 4 | Songs: the looper set-ups Tom uses from his RC-600 backup, each calling up its own sounds, loaded by a program number from Songbook Pro | Built |
| 5 and 6 | The effects: four chains (electric, acoustic, bass, vocal) of drives, amp models, compressors, EQs, delays, reverbs, modulation, wah, whammy, vocal harmony and the Bass9 copy | Built |
| Mixer | The app's own mixer: channels with gate, EQ and compressors, reverbs and delays after the looper, monitor mixes, act and venue presets, recording. Off until you switch it on | Built, first version |
| Kick pad | A test: the SPD::ONE Kick over USB plays a kick made inside the app | Built as a test |
| Later | Tablet app wired to the app, the rig computer, the board itself | Planned |

**New here, or back after a break?** Go straight to
[Start here: play your songs through the whole rig](#start-here-play-your-songs-through-the-whole-rig).
The sections after it are the test steps for each stage in the order they were built. They are
kept as a record and for checking one part by itself.

The rig computer itself runs Linux. [docs/linux-setup.md](docs/linux-setup.md) sets one up with a
single script, including a free trial on the laptop from a USB stick.

## Getting the app (no tools needed)

1. Open the [latest release](https://github.com/TomBoothMusic/gig-rig/releases/latest). If the repository is private at the time, you need to be signed in to GitHub first.
2. Under **Assets**, click **GigRig-Windows.exe** to download it.
3. Double-click the downloaded file. The first time, Windows shows a blue "Windows protected your PC" box, because the app is home-made and not signed. Click **More info**, then **Run anyway**.

Nothing is installed. The app is a single file; delete it to remove it.

## Start here: play your songs through the whole rig

This is the everyday way to use the app at home now that the looper, the effects, the songs and
the mixer all exist: the laptop does everything, the CQ12T is only the sockets and the way to
the speakers, and the RC-600 is only the footswitches. Each step names the section further down
that explains it in full.

### Plugging in (10 minutes the first time)

1. **Straight into the CQ, with the pedalboard left out:** microphone into socket 6, acoustic
   into socket 7, electric into socket 8. Set each gain so the CQ's meter is healthy and clear
   of red.
2. **CQ12T** to the laptop by USB. On the CQ: **CONFIG**, **USB/SD**, **Sample Rate 96 kHz**.
3. **RC-600** to the laptop by USB, on memory **65** (MIDI CTRL). Footswitches in its CTL 3/4
   socket if you use CTL3 and CTL4.
4. **SPD::ONE Kick** to the laptop by USB (a micro-USB lead that carries data), with its own
   power adaptor or batteries. See [docs/kick-pad.md](docs/kick-pad.md).
5. On the CQ, pull the faders **right down** on sockets 6, 7 and 8, and on channels 9, 10 and
   ST1. Bring the **USB stereo channel** up to 0 dB. That is where the app's mix arrives, and
   it should be the only thing you hear.

### In the app (2 minutes, every time)

1. Open **one** copy of the app. It refuses to open a second window, because two copies both
   hear the pedal.
2. **Latency and audio test** tab: the box should say **READY**, **Rate** 96 kHz and
   **Buffer** 64. If the buffer shows anything else, choose 64.
3. **Looper** tab: if the line under the song box says **TIMING NOT MEASURED**, go back to the
   first tab and click **Measure latency** once.
4. **Effects** tab: **Effects on** ticked. None of your four chain buttons along the top
   should say **(off)**.
5. **Mixer** tab: tick **Mixer on**. Sing and play your loudest, and for each channel raise
   **Trim** until the peaks sit in the lighter band between the two marks on its meter.
6. **Songs** tab: choose a song in the **Song** box, or tap one in Songbook Pro if its MIDI
   reaches the laptop.

### Playing a song through

For each song, play it the way you would at a gig, start to finish. Things worth noticing:

- **The sounds.** Are the right sounds there the moment the song loads, on every instrument?
- **The loops.** Does each switch do what it does on the RC-600 for this song? Does each loop
  come back in time, at the level you played it, with a clean join?
- **The effect switches.** On the **Looper** tab, a switch the song has given to an effect says
  **Effects:** and what it does, in place of its looper job.
- **The mixer.** A loop follows the fader, mute and reverb of the channel it was recorded from.
- **Changing song.** Delays and reverbs should ring out under the new sound, with no click.

When something is wrong, carry on if you can and press **Copy report** (top right) as soon as
the song ends. It holds every tab's part, and the looper keeps its last 150 lines, so a
report taken straight after a fault shows what happened. Paste it to Claude with the song's
name and what you heard.

### If something is not right

| What you see or hear | Try this |
| --- | --- |
| No sound at all | CQ's USB stereo channel fader up; first tab says READY; **Mixer on** ticked, Main not muted |
| You hear yourself twice, or hollow | A CQ fader for socket 6, 7 or 8 is still up. Pull it right down |
| Everything feels late | First tab: **Buffer** must be 64. Change it to 128 and back to 64 |
| A switch does nothing | **Pedal (RC-600)** tab should say CONNECTED and the RC-600 should be on memory 65. On the **Looper** tab, see whether the switch's tile says **Effects:** |
| Loops are early or late | First tab, **Measure latency**, then record the loop again |
| A song loads with the wrong sounds | **Songs** tab: the left box shows that song's file, and any line it could not use is marked **PROBLEM:** |
| Loops doubled or drifting | A second copy of an older build may be open. Close every Gig Rig window and open one |

## Stage 1: the latency and audio test

The app opens on the **Latency and audio test** tab and starts the CQ by itself.
The second tab, **Device check**, is the stage 0 screen.

The rig sends eight signals back to the mixer, called **returns**. On the CQ12T
they arrive like this:

| Return | Arrives on the CQ at |
| --- | --- |
| 1 | Channel 9 |
| 2 | Channel 10 |
| 3 and 4 | ST1, left and right |
| 5 and 6 | The USB stereo channel, left and right |
| 7 and 8 | The Bluetooth stereo channel, left and right |

### Before you start

1. Close Reaper and any other audio program. The CQ's driver only lets one program use it at a time.
2. Switch the PA speakers off, or unplug them. The test plays short chirps through the main outputs.
3. On the CQ, go to **CONFIG**, then the **USB/SD** tab, and check: **Sample Rate** 96 kHz (the setting decided on 20 November 2026; the first tests were at 48 kHz), **Stream Mode** Multitrack, **Source Point** Post Preamp.

### Part A: measure the latency (about 5 minutes)

1. Take an ordinary microphone cable. Plug one end into **MAIN OUT L** on the back of the CQ and the other end into the XLR socket of **input 10**. Any spare input from 1 to 10 works; the app finds out which one by itself.
2. On the CQ, for that input channel: make sure **48V** is off, turn its **Gain** right down, and pull its **fader right down** (use the fader, not mute). That stops the sound going round in a circle inside the mixer.
3. On the CQ, find the **USB** stereo channel (in the stereo inputs bank). Unmute it and set its fader to 0 dB. Set the main fader to 0 dB.
4. Open the app. The box near the top should say **READY** and "Running: 96 kHz".
5. Click **Measure every size, 8 to 256**. You will hear about 36 short chirps over 45 seconds or so. Leave everything alone until the box changes.
6. The box now shows the round trip at each buffer size. Click **Copy report** (top right) and paste the result to Claude.

If the box says **NOT HEARD**, the chirp is not reaching the main output: check step 3. If it says **NO CABLE**, the chirp went out but did not come back through the cable: check steps 1 and 2.

### Part B: play through it for 30 minutes

1. **Unplug the loopback cable first.** Leaving it in while play-through is on can howl.
2. Plug everything into its usual inputs (any of inputs 1 to 10). On the CQ, pull the faders for channels 1 to 10 right down, so the only sound you hear is the copy that has been through the computer. Use the faders, not mute.
3. In the app, choose the **Buffer** size to test (start with the smallest one that showed no dropouts in part A).
4. Set **Play-through** to **Inputs 1 to 10 mixed to the CQ's USB stereo channel**.
5. Switch the speakers back on, at a low level to begin with, and play for 30 minutes. Listen for clicks or gaps, and glance at the **Dropout watch** line now and then.
6. Click **Copy report** and paste it to Claude, with a note on anything you heard.

If the sound ever sticks at full level, the app switches play-through off by itself and says so.

### Part B2: the glitch check (leave it running, no playing needed)

Some glitches happen in the driver or the USB link, where the Dropout watch cannot see them. The glitch check catches those too. It sends a steady two-note tone out to the mixer, listens for it coming back, and counts every break in it.

1. No cable is needed. On the CQ, keep the **USB** stereo channel's fader up, and pull the faders for channels 1 to 10 right down.
2. Turn your speakers down or off if you do not want to listen to the tone. The check works either way.
3. In the app, click **Glitch check**. Within a couple of seconds the line beside it should read **Tone watched**, with a running time and **Glitches: 0**.
4. Leave it for 30 minutes and use the laptop as you normally would at a gig.
5. Click **Copy report** and paste it to Claude. The report lists how many glitches there were and when each one happened.
6. Click **Stop glitch check** when you are done. Play-through is silent while the check runs and comes back when you stop it.

If the line says the tone is not coming back cleanly, check the USB stereo channel is not muted and its fader is up.

### Part C: check each return arrives where it should (about 10 minutes)

This part changes where some CQ channels take their sound from, so **save your scene on the CQ first** and recall it afterwards.

1. On the CQ, go to **CONFIG**, then **INPUTS**. Set the **Input Source** of channels 9 and 10, ST1 and the Bluetooth channel to **USB**.
2. In the app, click the **Test tone on return** buttons **1** to **8**, one at a time. Each plays a steady tone on one return; click the button again to stop it.
3. For each button, look at the CQ's meters and note which channel shows the tone. It should match the table above.
4. Tell Claude which ones matched, then recall your saved scene.

## Stage 2: the RC-600 as the pedal

The **Pedal (RC-600)** tab shows the RC-600's nine switches, and CTL3 and CTL4, laid out as they are on the pedal.

### Part A: every switch (about 10 minutes)

1. Plug the RC-600 into the laptop by USB and switch it on. On the RC-600 choose memory **65** (MIDI CTRL).
2. Open the app and click the **Pedal (RC-600)** tab. The box at the top should say **CONNECTED**. If it says **NO RC-600**, go to the **Device check** tab, click **Scan again**, and come back.
3. For each switch in turn: press it once, press it twice quickly, then hold it down for 2 seconds. Under the switch's name the words **press**, **hold** and **double** fill in green as the app sees each one.
4. **Double-press: two presses within** starts on **one beat of the song**, so two presses a beat apart (say on beats 3 and 4) count as a double-press at any tempo. With no loop running it uses the chosen song's saved tempo. You can pick a fixed time instead. Note which setting feels right.
5. When all nine are filled in, click **Copy pedal results** and paste them to Claude. (CTL3 and CTL4 only fill in if footswitches are plugged into the RC-600's CTL 3/4 socket.)

You can also click a switch on the screen with the mouse. That does the same as a foot on the real one.

### Part B: glitch check with the RC-600 plugged in (30 minutes, no playing needed)

This is the same glitch check as stage 1, run again now that the RC-600 shares the laptop's USB.

1. Leave the RC-600 plugged in on memory 65. Set the CQ up as decided in stage 1: 96 kHz, buffer 64, safe mode on.
2. On the **Latency and audio test** tab click **Glitch check** and leave it for 30 minutes. Press a few RC-600 switches now and then.
3. Click **Copy report** (top right) and paste it to Claude. The report now also says how many switch messages the RC-600 sent while the check ran.
4. Click **Stop glitch check**.

## Stage 3: the looper

The **Looper** tab is a six-track looper that copies how the RC-600 behaves, so the two can be compared. The RC-600's switches drive it.

How it keeps time:

- The first loop you record sets the length of a bar, and so the tempo. For one of your own songs the saved tempo is the guide to how many bars that loop was: a 2-bar loop played near the saved tempo is counted as 2 bars at that tempo, not 1 bar at half of it. You never have to match the saved tempo exactly.
- Every later track is a whole number of bars. If you press mid-bar it waits for the next bar to start recording, and when you press to finish it ends on the nearest bar.
- A track with a preset length (as in some of your RC-600 memories) finishes by itself.
- Each recording is shifted by the round-trip delay measured in stage 1, so a loop plays back exactly where you played it.

### Setting up (5 minutes)

1. Plug in as you would for a gig, with the instruments going into the CQ. You hear yourself through the mixer as normal; the app only adds the loops.
2. On the CQ, bring up the **USB** stereo channel to 0 dB. The loops come back on that channel.
3. RC-600 on USB, memory **65**.
4. In the app, on the **Latency and audio test** tab: set **Play-through** to off, make sure the glitch check is stopped, and click **Measure latency** once (no cable needed; you will hear a few chirps). This gives the looper its timing. The app remembers the result for this buffer size and safe mode setting, so it only needs doing again if you change one of those. If it has not been done, the Looper tab says **TIMING NOT MEASURED** in amber. It does not matter if a microphone or guitar hears the chirps from a speaker: the app spots that (it arrives too late to be a cable) and uses the copy inside the mixer instead.

   The app also keeps a note of every delay figure the CQ's driver has given. If the driver's delay is well above what it was before at the same setting, the Looper tab says **DELAY HIGHER THAN BEFORE** in amber, and the report lists the figures with the date each was first seen. Loops still line up when that happens; restarting the computer is the first thing to try.
5. Click the **Looper** tab. The line under the song box should name the CQ and say **Timing correction ... from the latency test**. If it shows an amber message instead, it says what to fix.
6. Check the **socket** boxes on the right match where things are plugged in (vocal 6, acoustic and stomp 7, electric 8, bass 9).

### Part A: the basics, with the song set to "Standard layout"

Switches: 1 = track 1 record/play, 2 = track 1 stop, 3 and 4 = track 2, 5 and 6 = track 3, 7 = bank, 8 = undo/redo, 9 = all start/stop. Holding a record/play switch for 2 seconds undoes; holding a stop switch clears the track. Holding the record/play switch from the very press that started a new recording throws that take away.

1. Play a simple 2 or 4 bar guitar part. Press switch 1 on the first beat and again on the first beat as it comes round. Track 1 turns green and the loop plays. Listen to the join: is it clean, and is the loop in time with where you played it?
2. Play along with the loop for a minute. Does it stay in time?
3. Press switch 3 a little before a bar, play a bass line, and press switch 3 again near the end of a bar. Is track 2 in time with track 1?
4. Press switch 1 again to overdub on track 1, play something, press again to finish. Hold switch 1 for 2 seconds to undo it, and hold again to bring it back.
5. Press switch 9 to stop everything and again to start everything. Everything stops the moment you press, wherever in the bar that is, and the second press starts every loop from beat 1 straight away without waiting for a bar line.
6. Click **Copy looper results** and paste them to Claude, with a note on what you heard at each step.

### Part A2: one track, two sources kept apart

A track that listens to two input sockets records each into its own lane. The lanes share one length and the same switch presses, so they cannot drift apart, but each can come back on its own mixer channel.

1. On the CQ, set the ST1 channel's Input Source to USB and bring its fader up, as well as the USB stereo channel.
2. In the **Song** box choose **Two sources per track**. In **Loops play on** choose **two-source tracks split: first on 3+4, second on 5+6**. Track 1 now hears vocal and acoustic, track 2 acoustic and bass. (A track can keep up to three sources apart: track 6 of this layout, on the second bank, hears vocal, acoustic and electric.) Its tile says "(kept apart)".
3. Sing and play a verse together. Press switch 1 at the start and again at the end.
4. On the CQ, pull the ST1 fader down: the looped vocal goes and the looped guitar stays. Bring it back and pull the USB stereo channel down instead: the guitar goes and the vocal stays. Are the two in time with each other, exactly as you played them?
5. Overdub on track 1 (switch 1 again), then hold switch 1 to undo: the overdub should come off both.
6. Click **Copy looper results** and paste them to Claude with what you heard.

### Part B: side by side with the RC-600

1. In the **Song** box choose one of your songs (they are read from your RC-600 backup): Dirty Slap (the general set-up), Wicked Game, Dreams, Cocaine, Come As You, Yellow, Throw Arms, Never Tear U, Dancing Moon, Livin, Boiz, Lucky, Black & Gold, Kiss Me, Time After T or Zombie. The switches, each track's inputs and any preset lengths are then the same as in that RC-600 memory. The panel at the bottom left lists them.
2. Play the song's loops the way you do on the RC-600. Note anything that feels different: a switch that does the wrong thing, a loop that ends in the wrong place, timing that feels early or late.
3. Click **Copy looper results** and paste them to Claude with your notes.

If loops sound slightly early or late against your playing, move **Timing trim** a millisecond at a time and say which value sounded right.

Not copied yet: the RC-600's effects, rhythm, memory up/down, and tap tempo. A switch set to one of those shows as "Effect (later)" or similar and does nothing.

## The effects, first batch: drives in your pedalboard

New in this build: an **Effects** tab and an **Amp capture** tab. The Effects tab starts
switched off, so the looper works exactly as before until you tick **Effects on**.

The app's drives go into your pedalboard in place of the OD200, like any other pedal. The
rest of your board, your Dream '65 and everything after it stay exactly as they are, so you
compare one drive against the other and nothing else changes.

### Setting up (about 10 minutes)

1. Unplug the lead going **into** the OD200 (or coming out of it, if you want the app's drives
   after it) and plug that lead into **CQ socket 10** instead. This is the signal the app's
   drives work on.
2. Run a lead from a free **CQ aux output** (for example Aux 1) back into the pedalboard, where
   the lead you just moved came from (the OD200's input, or the next pedal after it).
3. On the CQ:
   - Channel 10: turn it right down in the main mix and in every aux, so you only hear what
     comes back through the pedalboard. The USB send stays post preamp, as it is now. Set the
     channel 10 gain so the meter is healthy but well clear of red when you dig in.
   - Channel **ST1**: Input Source = USB (as for the looper). Processing off. Send it to that
     aux only, not to the main mix. Start with the aux output turned **well down**: it is a
     line-level output going into a pedal input. Set its send to that aux to **pre-fader**, so
     it still works with the ST1 fader down.
   - **Every other send to that aux off.** Open the aux's mix and turn every channel's send to
     it right down except ST1: channel 8 (your board coming back) and the **FX returns** (the
     CQ's own reverb) especially. If either sends to the aux, the sound goes round in a circle
     and howls, or rings in the reverb tail (Tom found both on 23 November 2026).
4. In the app, open the **Effects** tab. Tick **Effects on** and **This chain on**. Input should
   say Socket 10 and Out to should say Returns 3+4 (ST1).
5. Switch the OD200 off and play. You should hear the Tube Screamer copy through the rest of
   your board. With every drive in the app switched off you should hear your normal clean
   sound: if it is louder or quieter than with the app unplugged, set the aux output level
   until it matches.

To compare with the OD200 by feel: switch the app's drives off and the OD200 on, and back.
The app adds about 9 ms at this point in the chain (the mixer's USB round trip); say if you
can feel it.

### Part A: try the drives (as long as you like)

- The row of boxes is the chain, in order: Tube Screamer, Blues Driver, Klon and
  Marshall in a box. Tick **On** under each to switch it in or out.
- Click a box to see its knobs below. They work like the pedal's knobs while you play.
  Double-click a slider to put it back to its starting point.
- **Move left** and **Move right** change the order. **Add** puts in another effect: the new
  **Fuzz Face style** fuzz, the Clean boost, or the same drive twice. **Remove** takes one out.
- To use a drive as a boost: gain or drive down, level up.
- **Effects / Dry** flips between the chain and your plain guitar. **Level match** (play for
  8 seconds) makes the chain as loud as the dry sound, so the comparison is fair.
- When you like a sound, type a name under **Presets** (for example "Blues") and press
  **Save as**. **Load** brings it back.
- Optional: under **RC-600 switches for the effects**, choose CTL4 and an action (for example
  slot 2 on/off) to switch it with your foot. That switch stops working for the looper
  while the effects are on.
- If you want Claude to hear a sound, press **Capture**, play for up to 3 minutes, press it
  again, and send the WAV from Documents\GigRig\Effect captures.

### Part B: capture your Dream '65 settings (3 minutes each)

For this part the app plays a test sound into the Dream instead of the guitar. Move the
aux lead from step 2 into the **Dream's input**, and plug the **Dream's output** straight into
**CQ socket 8** in place of the pedalboard's lead (so the RC-600 and anything after the Dream
are left out). Then open the **Amp capture** tab and follow the six steps on screen, once for
each Dream setting you use (edge of breakup, and the sparkly clean one with the vibrato
channel). Set the Dream as you play it, but with its **reverb and vibrato/tremolo depth off**:
an amp model cannot learn effects that move over time, and the app adds those back later as
separate effects. Leave the Dream's speaker emulation on. Put the leads back afterwards. Send Claude the WAV
files and the .txt files beside them from Documents\GigRig\Amp captures. Claude turns each
one into an amp model for the next build.

### Part C (optional): the Bass9 copy

The Bass9 copy built on 20 November is now one of the effects. To try it: pick **Chain 2**,
tick **This chain on**, set Input to the socket with the Bass9's DRY OUT, choose
**Bass9 PRECISION copy** and press **Add**. Set the reference to the socket with the real
Bass9 and use **Effects / Reference** to compare.

### What to send Claude

Press **Copy effects results** on the Effects tab and paste it, with what you liked and
didn't. Mention which drives, which order and which settings.

## The effects, second batch: your Dream '65 as amp models

New in this build:
- **Amp model (NAM)**: an effect that plays your Dream '65 from the three captures you made on
  23 November (Break Up, Clean and Driven). They come built into the app and are copied to
  Documents\GigRig\Amp models the first time it opens. Any other `.nam` file you put in that
  folder shows up in the list too.
- **Noise gate**: turns the hiss and hum down when you stop playing. Put it after the drives.
  Its Threshold sets how quiet the guitar has to be before it closes; Depth sets how far it
  turns down.
- The **Marshall in a box** has less low end, and the **Blues Driver** saturates more evenly.
  Your saved presets still load, but they will sound a little different, so check them.

### Setting up the A/B against your real Dream (about 10 minutes)

Keep the leads as they were for the amp captures: OD200 output into CQ socket 10, Aux 1 into
the Dream's input, and the Dream's output straight into CQ socket 8.

1. On the CQ, pull **channel 8's fader** down (the app plays the real Dream for you), and make
   sure the **USB stereo channel** (where the loops come back) is up in the main mix.
2. In the app, open the **Effects** tab. Tick **Effects on**.
3. **Chain 1** ("Drive test") feeds the real Dream: tick **This chain on** and click **Dry**.
4. Click **Chain 2**. Tick **This chain on**. Set:
   - Input: **Socket 10**
   - Out to: **Returns 5+6 (the USB stereo channel)**
   - Looper hears it as: **Not recorded**
   - The box beside the **Reference** button: **Socket 8** (the real Dream)
5. In Chain 2, choose **Amp model (NAM)** on the right and press **Add**. Click its box: the
   **Model** list shows the three Dream captures. Pick the one that matches how the Dream is
   set right now (for example Break Up).
6. Click **Effects** to hear the model, **Reference** to hear the real Dream. Press **Level match**
   with Reference selected and play for 8 seconds, so both are equally loud.

The model's **Input** knob at 0 dB drives it exactly as hard as during the capture, as long as
channel 10's gain hasn't moved since then.

To try drives in front of both: set up the drives in Chain 1 and save them as a preset, load
the same preset in Chain 2, then add the amp model after them. Both paths then get the
same drives, one through the real Dream and one through the model.

### What to send Claude

Press **Copy effects results** and paste it, with how each model compares with the real Dream
(tone, feel, how it cleans up when you play softer, any fizz or noise) and how the gate behaves.

## The effects, third batch: compressor and graphic EQ

New in this build:
- **1176 style compressor**: a copy of the 1176 Rev E "blackface" studio compressor, like the
  Purified Audio 1176 you know. It works like the real one:
  - **Input** pushes your guitar harder into it: more Input, more squash. There is no
    threshold knob, just as on the 1176.
  - **Output** sets how loud it comes out afterwards.
  - **Attack** and **Release** go from 1 to 7, and **7 is the fastest**, as on the front panel.
  - **Ratio** is the four buttons: 4, 8, 12 and 20. **All** is the all-buttons-in trick:
    harder, with a bit of grit.
  - **Mix** blends your dry guitar back in. 100% is the plain 1176.
  - The **Turning down by:** line under it is the needle: how many dB it is squashing right
    now. The proper VU meter look comes later, with the pedal faces.
- **Graphic EQ (GE-7 style)**: seven sliders, 100 Hz to 6.4 kHz, each up or down 15 dB, and a
  **Level** slider. To trim the lows, pull 100 Hz (and maybe 200 Hz) down.
- A new chain now starts with its **Reference** set to **Socket 8**, where the board and the
  Dream come back.

### Stacking presets

**Load** replaces everything in the chain with the preset. **Add to chain** keeps what is
already there and adds the preset's effects to it, in front of the amp model if there is one.
So you can keep "Comp & GE7" as its own preset and stack a drive preset on top:

1. Choose **Comp & GE7 for Electric** and press **Load**.
2. Add the **Amp model** if you want it.
3. Choose a drive preset (for example **Blues Drive + Klon Boost**) and press **Add to chain**.
   The drives land after the compressor and EQ and in front of the amp. If a **Volume pedal**
   sits right before the amp, they land in front of the pedal, as on your pedalboard.

A chain holds at most 18 effects.

### Trying them (about 15 minutes)

Use the same leads and set-up as the second batch (the A/B with the amp model).

1. In **Chain 2**, load one of your drive presets and add the **Amp model** at the end, as in
   the second batch.
2. Pick **1176 style compressor** on the right and press **Add**, then **Graphic EQ (GE-7
   style)** and **Add**. Click each one's box and press **Move left** until it sits in front of
   the drives, so the order is compressor, EQ, drives, gate, amp, like your pedalboard.
3. Click the compressor's box. Turn **Input** up until **Turning down by:** shows about 3 to
   6 dB when you dig in, then set **Output** so switching it on and off sounds about as loud.
4. Try **Ratio 4** against **All**, and Attack and Release at 3 and at 7.
5. Click the EQ's box and pull **100 Hz** down until the low end sits right.

### What to send Claude

Press **Copy effects results** and paste it, with how the compressor feels against your Ego
76 or the Purified Audio 1176 (attack, how it lets go, any pumping or grit), and whether the
EQ does what you need. Save any settings you like as a preset and attach the file.

## The effects, fourth batch: delays, reverb, tremolo and modulation

New in this build, each its own effect that can go anywhere in any chain:

| Effect | What it is | Its knobs |
|---|---|---|
| **Analog delay** | Short, warm slapback, each repeat darker and a little dirty (DM-2 / Carbon Copy). Not synced. Your acoustic delay. | Time, Repeats, Mix, Tone, Mod |
| **Digital delay** | Clean echo with a touch of chorus on the repeats. Follows the tempo in note values, or its own Time. | Follow tempo, Note value, Time, Repeats, Mix, Tone, Mod depth, Mod speed |
| **Tape delay** | Tape echo (Space Echo / Echoplex): the tape wobbles, repeats get darker and saturate. Made for vocals, fine on guitar. | Follow tempo, Note value, Time, Repeats, Mix, Wow, Flutter, Age |
| **Spring reverb** | Two-spring tank like a Deluxe Reverb's, with the drip. Put it after the amp. | Reverb, Decay, Tone, Dwell |
| **Tremolo** | The amp's pulsing volume (the Dream's "vibrato"). Put it after the amp. | Speed, Intensity, Shape (smooth to choppy) |
| **Vibrato** | True pitch wobble (VB-2 style). | Speed, Depth |
| **Chorus** | Analog-style chorus (CE-2). | Speed, Depth, Mix |

**Tempo for the synced delays.** With **Follow tempo** ticked, a delay plays the chosen note
value (1/4, dotted 1/8 ...) at the song's tempo:
- Once you record a loop, the loop's tempo is used.
- With no loop, tap the beat on **Tap tempo** (next to Capture), at least twice. You can also
  put tap tempo on a switch: under **RC-600 switches for the effects**, pick CTL3 or CTL4 and
  **Tap tempo (for synced delays)**.
- With no loop and no taps, or with Follow tempo unticked, the delay uses its **Time** knob.

The line beside the Tap button, and the effects report, say which tempo is in use and where it
came from.

When a delay or the reverb is switched off, its tail stops at once for now. Letting the tail
ring out after switching off comes later.

### Trying them (as long as you like)

Same leads and set-up as before (Chain 2 into the amp model, listening on **Effects**).

1. Load **Comp & GE7 for Electric**, add the **Amp model**, then **Add to chain** a drive preset.
2. After the amp, add **Spring reverb** and **Tremolo** (pick them and press **Add**; new
   effects go at the end). Set your go-to amount of each, then **Save as** a preset, for example
   "Electric standard".
3. Try the delays one at a time before the amp: **Analog delay** for slapback, **Digital delay**
   with Follow tempo ticked while you tap a tempo, and the **Tape delay**.
4. For the vocal tape delay, a chain on the vocal socket (socket 6) works the same way.

A chain holds 18 effects at most.

### What to send Claude

Press **Copy effects results** and paste it, with what you'd change about each one (the
spring's drip, how the tape wobbles, the analog delay's darkness). Attach any presets you save.

## The whammy (just for fun)

A pitch-bending pedal in the style of the DigiTech Whammy. In the **Effects** tab pick
**Whammy style pedal** and press **Add**. Put it early in the chain, before any drive
and the amp, as you would the real pedal.

| Knob | What it does |
|---|---|
| **Mode** | What the pedal does. **Whammy** modes bend the whole guitar: 1 or 2 octaves up, a 5th or 4th up, a 2nd, 4th, 5th, 1 or 2 octaves down, and a dive bomb. **Harmony** modes keep your guitar and add a second voice that the pedal moves between two notes (for example an octave below to an octave above). **Detune** modes bring in a slightly sharp copy for a thick, chorus-like sound. |
| **Pedal (heel to toe)** | The pedal itself. Drag it with the mouse while a note rings: 0 is heel down (no bend), 100 is toe down (the full bend). |
| **Bend time** | How long the bend takes to travel the whole way. Leave it short for the mouse. Make it longer (300 to 800 ms) for the switch below. |
| **Bent sound** | How much of the bent sound you hear. 100 is how the real pedal behaves. |

**Hands-free, with an RC-600 switch.** With no expression pedal yet, a switch can do the bend:
under **RC-600 switches for the effects**, pick CTL3 or CTL4 and **Chain 2: whammy bend while
held** (use the chain your whammy is in). Hold the switch and the pedal travels to the toe at
the **Bend time** speed; let go and it comes back. Try 2 octaves up with a Bend time of 500 ms.

**What to expect.** Single notes bend cleanly. Chords warble a little, as they do on the real
pedal. With the pedal at the heel in a Whammy mode your guitar passes through untouched, so it
can stay switched on. While it is bending, the sound runs a few hundredths of a second behind.

## The wah

A wah in the style of the Cry Baby, with the things you would otherwise have to mod the pedal
for brought out as sliders. In the **Effects** tab pick **Wah pedal** and press **Add**. Put it
first in the electric chain, before the compressor and the drives, where your Cry Baby sits.

| Knob | What it does |
|---|---|
| **Pedal (heel to toe)** | The pedal itself. Drag it with the mouse while you play: 0 is heel down (dark), 100 is toe down (bright). |
| **Heel frequency** | Where the sweep starts, heel down. Lower is darker and growlier. A Cry Baby sits around 400 Hz. |
| **Toe frequency** | Where the sweep ends, toe down. Higher is brighter and sharper. A Cry Baby sits around 2200 Hz. Together these two are the length of the sweep. |
| **Q** | How narrow the peak is. Low (2 to 4) is wide and gentle. High (8 and up) is narrow, sharp and vocal. |
| **Peak depth** | How far the peak stands out above the rest of the guitar. Low is a subtle colour, high is a strong "wah". |
| **Level** | Louder or quieter overall, to match the guitar with the wah off. |
| **Sweep time** | How long the pedal takes to travel heel to toe. Leave it short for the mouse. Make it longer (150 to 600 ms) for the switch below. |

**Hands-free, with an RC-600 switch.** Until there is an expression pedal, a switch can rock
it: under **RC-600 switches for the effects**, pick CTL3 or CTL4 and **Chain 1: wah sweep while
held** (use the chain your wah is in). Hold the switch and the pedal travels to the toe at the
**Sweep time** speed; let go and it comes back to the heel. Tapping the switch in time gives a
passable rocking wah for dialling in the sound.

**A good way to dial it in.** Record a **test take** of some wah-style playing (see the fifth
batch below), play it round, and rock the Pedal slider with the mouse while you set the other
sliders. Park the Pedal half way to hear a "cocked wah". Save what you like as a preset.

**What to send Claude.** Your Heel, Toe, Q and Peak depth once it feels like your Cry Baby (the
**Copy effects results** button lists them), and anything that still feels wrong: the sweep
bunching up at one end of the pedal, too thin toe down, too boomy heel down.

## The effects, fifth batch: acoustic, vocal and bass chains

This build has every effect the acoustic, vocal and bass chains need, so that all three chains
(with the electric one) exist before effects are tied to songs. Nothing here changes the looper
or the effects you have already set up.

New effects, each its own box that can go anywhere in any chain:

| Effect | What it is | Its knobs |
|---|---|---|
| **Acoustic body** | Makes a pickup sound more like the guitar's wooden body and less like a quack, in the spirit of the BodyRez. This is Claude's own voicing, not a measured copy of your pedal. | Body, Flip phase (for feedback), Level |
| **Response file** | The measured copy: plays back the tone of a tone-only pedal captured on the Amp capture tab (your BodyRez at your setting). A straight wire until you choose a file. | Response file, Mix, Level |
| **Channel EQ** | Mixer-style EQ: low cut, high cut, low and high shelves, two sweepable mids. For the finger-style boost and EQ, and for the voice. | Low cut, High cut, Low, High, Low-mid at, Low-mid, High-mid at, High-mid, Mid width, Level |
| **Uni-Vibe** | The swirling, throbbing sound for Lightning Crashes. | Speed, Intensity, Mode (Chorus or Vibrato), Level |
| **Flanger** | Jet-plane sweep (BF-2 style), for the vocal in Come As You Are. | Speed, Depth, Manual, Resonance, Mix |
| **Vocal harmony** | Sings a second voice with yours, in the song's key. Your own voice passes through untouched. First version: see Part D. | Key, Voice, Harmony level |
| **Plate reverb** | Bright, smooth studio plate for the voice. | Reverb, Decay, Pre-delay, Tone, Low cut |
| **Room reverb** | A small room, a room or a hall. | Reverb, Size, Decay, Tone, Low cut |

The acoustic chain uses the **Analog delay** and **Chorus** you already have, as you suggested.

**Where the vocal delays and reverbs will live.** As decided on 25 November, vocal delay and
reverb belong in the mixer stage, after the looper. The plate, the room and the delays are in
the vocal chain here only so you can hear them now; the same effects move to the mixer's sends
when that stage is built. The harmony stays in the chain, before the looper.

Also new:

- **Test take.** Play (and sing) once, then listen to it go round and round through the chains
  while you turn knobs with both hands free. See Part A.
- **Three starter presets** appear in the Presets list: **Starter - Acoustic**,
  **Starter - Vocal** and **Starter - Bass (Bass9 copy)**. They are starting points. If you
  change one and save it under the same name, the app never overwrites yours.
- **Processor figures for each chain** on the status line and in the report, so we can see
  what four full chains cost on your laptop.
- A line that says what the chosen effect is doing (for the harmony: "hears A3, sings C4").

### Setting up (about 10 minutes)

The app needs your acoustic and your microphone **raw**, before the RC-600 and your pedals
touch them, because the app is now doing that job.

1. Plug the **acoustic** straight into a spare CQ socket (socket 10 is the one used for the
   drive test). Plug the **microphone** straight into another spare socket (socket 6, say).
   Set each gain so the meter is healthy and clear of red.
2. On the CQ, turn those two channels **right down in the main mix and every aux**. You listen
   to what comes back from the app, not to the channel itself. (Both at once would sound
   hollow, because the app's copy arrives 9 ms later.)
3. In the app, on the **Effects** tab, tick **Effects on**. Then:
   - Click a spare chain, tick **This chain on**, name it **Acoustic**, Input = the acoustic's
     socket, **Out to = Returns 5+6 (the USB stereo channel)**, which already goes to your
     speakers for the looper. Choose **Starter - Acoustic** under Presets and press **Load**.
   - Click another chain, tick **This chain on**, name it **Vocal**, Input = the microphone's
     socket, Out to = Returns 5+6. Load **Starter - Vocal**.
4. Play and sing. You should hear both through the speakers with their effects. Keep the
   speakers well away from the microphone while reverb is on: a ringing tail means feedback.

### Part A: the test take

1. With the Acoustic and Vocal chains on, press **Record a take**. Play and sing a verse and a
   chorus (up to a minute). Press the button again (it says **Stop recording**).
2. Press **Play the take**. Your performance now goes round and round into both chains in
   place of the sockets. Put the guitar down and turn knobs.
3. **Stop the take** gives the chains their sockets back.

Good to know:

- A take records every chain that is **on**. A chain that was off stays live, so you can record
  a take of the guitar alone and then sing over it with the Vocal chain.
- Takes are saved in Documents\GigRig\Test takes and listed in the box beside the buttons, so a
  good one can be used again another day without picking up the guitar.
- Hands full? Under **RC-600 switches for the effects**, choose CTL3 or CTL4 and
  **Test take: record, then play it round, then stop**. One press records, the next keeps it
  and plays it, the next stops.
- Level match and Capture work on a take the same as on live playing.

### Part B: the acoustic chain (20 minutes)

1. Play a take through **Starter - Acoustic**. Click **Acoustic body** and move **Body** between
   0 (nothing) and 10. Say what you would change: more low end, less top, more or less of the
   squeeze on hard strums (the line "Turning down by" shows it working).
2. Tick the others on one at a time: **Channel EQ** (set up as a finger-style boost: change it
   to taste), **Chorus**, **Tremolo**, **Vibrato**, **Uni-Vibe**, and **Room reverb** as a
   stand-in for the mixer's reverb. The **Analog delay** is on with your electric settings.
3. Save what you like as your own preset.

### Part C: a measured copy of your BodyRez (15 minutes, optional but worth doing)

The app can measure your BodyRez and play its exact tone back.

1. Set the BodyRez knob where you always have it. Patch a free CQ aux output into the
   **BodyRez's input** and the **BodyRez's output** into a CQ socket (socket 8 is fine), the
   same way you patched the Dream for its captures. Aux output level **well down** to start.
2. Open the **Amp capture** tab. Name it **BodyRez**. Choose the return and the socket.
   Press **Set from my playing** and play the acoustic (on its own socket) for ten seconds.
3. Tick **This is a tone-only pedal (BodyRez, an EQ)...** and press **Start capture**. It takes
   about three minutes.
4. When it finishes, the app makes the file by itself and says what it found: how late and how
   loud the pedal was, and **how much of what the pedal did a file can explain**. Near 100%
   means the file is a close copy. Much lower means the pedal also squashes the sound, which a
   file cannot hold; the Acoustic body effect has a squeeze for that.
5. Back on the Effects tab, click **Response file** in the Acoustic chain, choose
   **BodyRez.wav** in its **Response file** box and tick it **On**. Untick Acoustic body, or
   keep a little of both.

To compare with the real pedal on the very same playing: make an empty chain called
**To BodyRez** with the acoustic's socket as its Input and **Out to** the return that feeds the
BodyRez. Set the Acoustic chain's Reference to the socket the BodyRez comes back on. Record a
take with both chains on, play it, and flip **Effects / Reference**.

### Part D: the vocal chain and the harmony (20 minutes)

1. Record a take of singing (or sing live) through **Starter - Vocal**. The **Channel EQ** has
   a 100 Hz low cut. **Plate reverb** is on as a stand-in for the mixer's.
2. Tick **Vocal harmony** on. Set **Key** to the song's key (each key is shown with its minor,
   which uses the same notes: for a song in A minor choose **C / Am**). Set **Voice**: a 3rd
   above is the classic one; also 3rd below, 5th, 6th, octave, or **Unison (double)**.
   **Harmony level** sets how loud the second voice is.
3. Watch the status line: it says which note it hears and which it sings. Try Cocaine in F and
   Lucky in A against what the RC-600's Harmonist does.
   **Harmony only while you hold a switch:** under **RC-600 switches for the effects**, choose
   a switch and **Chain 3: harmony while held** (use the number of your Vocal chain). The
   harmony sings while the switch is down and stops when you let go. For a song, set the
   **Key**, leave the harmony unticked, and keep the song: the first press wakes it.
4. Tick **Flanger** on for Come As You Are. Try the **Tape delay** and **Digital delay**.
5. **The delay throw** (the "after love, after love, after love" in Believe). Under
   **RC-600 switches for the effects**, choose CTL3 or CTL4 and **Chain 3: delay throw while
   held** (use the number of your Vocal chain). Make sure **Digital delay** is ticked **On** in
   the chain. Now sing: the voice is clean. Hold the switch while you sing "after love" and let
   go: the delay catches only those words, and they repeat and die away on their own while
   everything you sing next stays clean. **Repeats** sets how many times they come back,
   **Mix** how loud, and **Note value** the rhythm (tap the tempo first, or the looper sets it).
   The delay's **Throw** box shows what it is doing: *Ready* while it waits, *Catching* while
   the switch is held. Set it back to *Off* to have an ordinary always-on delay again.

This is a first version of the harmony, tested here only on a made-up voice, so your ears are
the real test. Things to listen for and tell Claude: wrong notes or warbling (and on which
notes or words), a robotic or grainy sound, the harmony arriving late (it sings about 24 ms
behind you), how it copes with the guitar spilling into the microphone, and whether changing
Key mid-song is clean. Holding the harmony switch and turning the song encoder to change key
comes with the real board; for now Key is the box on screen.

### Part E: the bass chain (10 minutes)

1. Click a spare chain, tick **This chain on**, name it **Bass**. Set its **Input to the same
   socket as the Acoustic chain**: two chains can share a socket. Load
   **Starter - Bass (Bass9 copy)**.
2. Set **Looper hears it as** to socket 9 (where your real Bass9 is plugged in), so bass loops
   land on the track they always have. Out to = Returns 5+6.
3. To compare with the real Bass9: set Reference to the Bass9's socket and flip
   **Effects / Reference**. A take makes this easy: both hear the same notes.

The bass was reworked twice on 25 November: first for the ticking and static you found in
it, then for the warble, and to make Tone and Drive do something. What to know:

- **Tracking speed**: lower makes chords cleaner but low notes slower to speak, higher makes
  low notes quicker. It should no longer warble at any setting, so start in the middle (0.6)
  and go lower only if you like it better.
- **Drive** and **Compression** now work the same however quiet or hot the guitar arrives.
  Drive around 0.3 is a little warmth, 0.6 is plainly gritty, 1 is nearly a fuzz bass.
- **Tone** has more to work with, and two new sliders feed it: **String brightness** (how
  much top the "strings" have before the tone knob) and **Pluck (string noise)** (a short
  click from your own pick or finger attack on the front of each note; 0 turns it off).
- **A bass preset you saved before this build will come out about 8 dB quieter**, because the
  compressor no longer adds loudness of its own. Turn **Level** up by about 8 dB and save it again.

In the finished rig the bass chain only sounds while its switch is held or latched. For now
it is simply on or off with **This chain on** (or put **Chain on/off** on a pedal switch).

### What to send Claude

Press **Copy effects results** with all four chains switched on and a take playing, and paste
it: it now lists what each chain costs the processor. Say what you would change about each new
effect, attach any presets you save, and for the BodyRez paste the line the Amp capture tab
showed when it made the file. A **Capture** of the vocal chain with the harmony on (it holds
your dry voice and the result) tells Claude more about the harmony than any description.

## Songs: each song calls up its own sounds

This build links the effects to the songs. The full guide is [docs/songs.md](docs/songs.md).
In short: a song names the presets it uses, so changing a preset once changes every song that
uses it. Songs without sounds of their own use the **default sounds**. There is a new **Songs**
tab between Looper and Effects.

**Two screens.** On the Songs tab, **Open in its own window** lifts the Songs tab out into a
window you can drag to a second screen, so the Effects tab stays open while you go through the
songs. Closing that window (or **Put back as a tab**) puts it back.

### Part A: the default sounds (10 minutes)

1. Open the app and start the audio as usual. Click the **Effects** tab.
2. On chain 1 (your electric), choose your everyday comp and EQ preset in the Presets box and
   click **Load**. Then choose your everyday amp preset and click **Add to chain**. The line near
   the bottom now says "This chain was built from:" with both names.
3. Do the same on the acoustic and vocal chains with the presets you like there.
4. Click the **Songs** tab. Click **Keep these sounds as the default**.

### Part B: a song with sounds of its own (10 minutes)

1. On the Songs tab, choose **Wicked Game** in the Song box.
2. On the Effects tab, change chain 1 to what that song needs: **Load** and **Add to chain**
   different presets, and turn any knobs (more reverb, tremolo on).
3. Back on the Songs tab, in the Switches rows, set one row to **CTL4** (or any switch the
   song does not need for looping) and **next amp sound** on the chain your electric is on, and
   another to a switch you like and **delay throw while held** on the vocal chain. A switch
   given to the effects stops working for the looper, so do not use All start/stop (switch 9).
4. Click **Keep these sounds for this song**. The left box shows the file it wrote.
5. Choose another song in the Song box, then Wicked Game again. Its sounds and switches should
   come back each time, and the other song should have the default sounds.
6. Play and press that switch: the amp should step to the next amp sound each press.

### Part C: edit a preset once (5 minutes)

1. With Wicked Game loaded, go to the Effects tab and turn one knob on an effect that came from
   a preset another song also uses (for example the compressor).
2. Click **Update its presets**. The line under the buttons says which preset it saved into.
3. Load a different song that uses the same preset. The knob should be where you left it.

### Part D: tails, program numbers and the mixer coming back (10 minutes)

1. Load a song with a long delay or reverb, play a chord, and while it rings choose another
   song. The delay or reverb should die away naturally under the new sound, with no click.
2. On the Songs tab, type **11** in **Try a program number** and click **Load that program**.
   Wicked Game should load. Type a number no song uses (say **50**): the general set-up with the
   default sounds should load.
3. If you have a way to get Songbook Pro's MIDI into the laptop, tap a song there and watch the
   Song box follow.
4. With the audio running, pull the CQ's USB cable out, wait five seconds and plug it back in.
   A red line should appear on the first tab, and the sound should come back by itself within a
   few seconds.

### What to send Claude

Press **Copy report** after Part D and paste it. Say whether any tail was cut short or rang too
long, whether the amp switch should also change the amp's knobs, and send your list of which
songs use which presets (the format is at the end of docs/songs.md) so Claude can write all the
song files in one go.

## The mixer (first build)

The app now has its own mixer, on a new **Mixer** tab at the far right. The full guide is
[docs/mixer.md](docs/mixer.md). **It starts switched off, and switched off nothing has
changed**: use the app exactly as before until you want to try it.

### Setting up (5 minutes)

1. Plug in as for the effects tests: voice, acoustic and electric into the CQ sockets you
   used last time (voice 6, acoustic 7, electric 8), and start the audio as usual.
2. On the CQ, pull the faders **right down** on those three sockets, and on the channels
   the effect chains used to arrive on (channels 9 and 10, and ST1). Leave only the **USB
   stereo channel** up: that is where the app's main mix arrives.
3. In the app, click the **Mixer** tab and tick **Mixer on**. You should hear yourself
   through the app's mixer.
4. If a channel is on the wrong socket: click its name in the row of strips, then choose the
   socket under **Comes from**.

### Part A: channels (10 minutes)

1. Sing and play. Move the **Vocal**, **Acoustic** and **Electric** faders, and try **Mute**.
2. Click **Vocal**. Under the strips, drag **Band 2** up and sweep **Band 2 at** to hear the
   EQ move. Double-click a slider to put it back.
3. Click **Compressor: off**, tick **This block on**, and lower **Threshold** until the thin
   amber bar beside the Vocal meter moves when you sing loudly.
4. Still on Vocal: raise **To Vocal plate** to about -10 dB. You should hear reverb on the
   voice. Then click the **Vocal plate** strip and move **High cut on the wet sound** and
   **Low cut on the wet sound**: the reverb gets darker or thinner, the voice itself does not.
   (If your vocal effects chain still has its own Plate reverb switched on, switch that one
   off on the Effects tab first, or you will hear two.)

### Part B: loops come back on their own channel (5 minutes)

1. Record a short vocal loop on the looper as usual.
2. While it plays, move the **Vocal** fader: the loop and your live voice move together.
   Mute Vocal: both go. The reverb send works on the loop too.
3. Record a guitar loop and check it follows the guitar's channel, not the vocal's.

### Part C: the venue knob (5 minutes)

1. Press **Turn right** a few times: the line beside it shows venue names. Nothing changes.
2. Stop on **Own PA (limiter catches peaks first)** and press **Push**. The main fades to
   that venue over about a second (it is saved 6 dB quieter), with no click.
3. Turn to **Starting point (flat)** and **Push**. It is saved louder, so the level stays put
   and the line says by how much.
4. Move any slider on the **Main** strip, then turn and push again: the first push warns that
   tonight's changes are not saved, the second loads.

### Part D: a recording (3 minutes)

1. Press **Record**, play for half a minute with a loop running, press **Stop recording**.
2. Press **Open recordings**. The newest folder should hold one file per channel and a
   **Main mix** file. Play one channel's file: it should be you and your loops, with no reverb.

### Part E: the volume pedal as a blend (5 minutes, Effects tab)

1. On the **Effects** tab, in the electric chain, add **Volume pedal** and move it to sit
   between the drives and the amp. In the acoustic chain add another and switch its **Reversed (heel is loud)** on.
2. Move the **Pedal position** slider of either one: both move. At 100 you should hear only the
   electric, at 0 only the acoustic, and a blend between.

## The mixer, second build

Built from Tom's answers after the first build. Set up as above (**Mixer on**), then:

### Part F: the pedal as an even blend, and the toe switch on CTL4 (10 minutes, Effects tab)

1. With the two Volume pedals from Part E in place, set **Travel** to **Blend (even
   loudness)** on both.
2. Strum the acoustic and the electric in turn while moving a **Pedal position** slider slowly from 0
   to 100. The overall loudness should stay about the same, with no dip in the middle.
3. Put a **Wah** (or the **Whammy**) in the electric chain, switched off.
4. In the switches area of the Effects tab, give **CTL4** the action **toe switch** on the
   electric chain.
5. Press CTL4 on the RC-600. The electric should jump to full, the acoustic should go silent,
   and the wah should come on. Move the **Pedal position** slider: it now sweeps the wah.
6. Press CTL4 again: the wah goes off and the blend is back where it was.

### Part G: the studio-style blocks (15 minutes, Mixer tab)

1. Click **Vocal**. Click **LA-2A style: off** and tick **This block on**. Raise **Peak
   reduction** until the amber bar moves when you sing, then bring the level back with **Gain**.
2. Click **Warmth: off**, tick **This block on**, and raise **Drive**. It should get thicker,
   not louder. Try it on the acoustic and the bass too.
3. Click the **Main** strip. Switch on **Bus comp** and lower **Threshold** until the amber
   bar moves a little on the loud parts. Try **Mix** at 50.
4. Still on Main: switch on **Tape**, raise **Drive**, and try the three **Tape speed** choices.

### Part H: where a monitor takes its sends from (5 minutes)

1. Click **Monitor 1**. Under **From where in each channel** choose **Before its processing**,
   then **After its gate and EQ**, then **After its compressors**, listening to that monitor
   with a compressor working hard on the vocal. You should hear the EQ and the squeeze come
   and go in the monitor while the main mix stays the same.

### Part I: a stereo channel (5 minutes)

1. Plug something stereo (keys, or a phone playing music) into two sockets.
2. Click **Duo vocal**, tick **On** under In use, and choose the left socket under **Comes
   from**. Click **Duo instrument** and choose the right socket.
3. Click **Duo vocal** again and tick **Stereo with channel 7**. Move either fader: both move.
4. Click **Main** and untick **Mono** to hear it left and right; tick it again and both sides
   should be in both speakers.

### Part J: a song with its own reverb and delay sends (10 minutes)

1. On the **Effects** tab, load the preset **Vocal - for the mixer** on the vocal chain. It is
   your vocal EQ and compressor, the harmony and the delay throw, with no reverb and no
   everyday delay: those are the mixer's job now.
2. On the **Mixer** tab click **Vocal** and raise **To Vocal plate** to taste. That is the
   act's everyday reverb.
3. Load a song in the Song box. Click **Vocal**, tick **Belong to this song, not the act**,
   and raise **To Vocal delay (song)**.
4. On the **Songs** tab press **Keep these sounds for this song**.
5. Load another song: the delay send should drop back. Load the first again: it comes back.
6. Check the **Vocal delay** strip itself (its time, its feedback) is the same for both songs:
   a song only changes how much of a channel goes to it.

### Part K: the source enhancer, Warmth's Mix and the level marks (10 minutes)

Added after Tom's first test of the mixer.

1. Each channel's meter now has a lighter band between two marks. Sing your loudest and raise
   the Vocal channel's **Trim** until the peaks land inside the band. Do the same for each
   instrument. The limiter, tape and compressors should now work without extreme settings.
2. Click **Vocal**, click **Enhancer: off** and tick **This block on**. Stop singing and lower
   or raise **Threshold** until the room noise and spill drop when you are quiet but your
   quietest singing still comes through whole. **Depth** is how far it turns down in the gaps.
3. Try **Time** at A and at F: A closes quickly after each phrase, F slowly.
4. Click **Warmth**, switch it on with **Drive** high, and move **Mix** between 0 and 100.

### What to send Claude

Press **Copy report** (top right) and paste it, with anything that sounded or looked wrong.
The mixer's lines are near the end, starting with **MIXER**.

## The Device check tab

1. Plug the CQ12T into the laptop by USB and switch it on. Do the same with the RC-600.
2. The app scans by itself when it opens; **Scan again** rescans after plugging something in.
3. The two boxes at the top say whether the CQ is there with all 16 inputs and 16 outputs, and whether the RC-600 is there as a MIDI device.
4. The bottom panel shows any MIDI that arrives, live. With the RC-600's MIDI CTRL memory (slot 65) selected, each switch shows as CC 20 to 30.

**Copy report** copies the device list and the stage 1 results together.

## How the builds work

Every change to the code is built automatically by GitHub (the **Actions** tab) for
Windows and Linux. Each build also runs the app's self-test and a real device
scan, and the Linux build is installed on a fresh Ubuntu with the rig's set-up script. Changes that reach the `main` branch are published on the
[Releases page](https://github.com/TomBoothMusic/gig-rig/releases) as a new numbered build.
The build number shows in the app and at the top of every report.

## For developers

C++20, [JUCE](https://juce.com) 9 (fetched by CMake), CMake 3.22 or newer.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The app lands in `build/GigRig_artefacts/Release/`. On Linux, the packages
needed are listed in `.github/workflows/build.yml`.

```
GigRig --self-test result.txt   # run the built-in checks, no window
GigRig --report report.txt      # write the device report, no window
```

| Path | What it holds |
| --- | --- |
| `src/AudioEngine.*` | Opens the audio device, passes audio through, runs latency measurements and counts dropouts |
| `src/LatencyProbe.*` | Plays a chirp, records every input and finds how many samples later it came back |
| `src/GlitchWatch.*` | Sends a steady two-note tone, cancels it from what comes back and counts every break |
| `src/AudioPanel.*` | The stage 1 tab; remembers the audio device, rate and buffer for the next start |
| `src/Pedal.*` | The RC-600's switches: CC numbers to switch names, and press, hold and double-press |
| `src/PedalPanel.*` | The stage 2 tab, and the on-screen switch used by both new tabs |
| `src/Looper.*` | The looper core: tracks, bar timing, overdub, undo, and the RC-600's switch functions and linked actions |
| `src/TestSongs.cpp` | The built-in looper set-ups: Tom's songs from the RC-600 backup, and the fixtures the self-test uses |
| `tools/rc600_songs.py` | Writes those set-ups from an RC-600 backup's DATA folder |
| `src/LooperPanel.*` | The stage 3 tab |
| `src/LooperSelfTest.cpp` | Checks for the pedal and the looper, with loop timing checked to the sample |
| `src/SongBook.*`, `src/SongsPanel.*`, `src/SongSelfTest.cpp` | Songs calling up their sounds and switches, program changes, the Songs tab and its checks ([docs/songs.md](docs/songs.md)) |
| `src/effects/` | Every effect, one folder each, with the shared interface, the chain and the standard checks ([docs/effects.md](docs/effects.md)) |
| `src/EffectsRack.*`, `src/EffectsPanel.*`, `src/EffectsSelfTest.cpp` | The four effect chains inside the engine, presets, switch actions, the Effects tab and its checks |
| `src/AmpModels.*`, `src/Responses.*` | The built-in amp models, and response files made from a capture |
| `src/Mixer.*`, `src/MixerPanel.*`, `src/MixRecorder.*`, `src/MixerSelfTest.cpp` | The mixer, its tab, recording and its checks ([docs/mixer.md](docs/mixer.md)) |
| `src/KickPad.*`, `src/KickPadPanel.*`, `src/KickPadSelfTest.cpp` | The kick pad test ([docs/kick-pad.md](docs/kick-pad.md)) |
| `src/ReconnectWatch.h` | Notices a lost audio device and opens it again |
| `src/DeviceReport.*` | Scans audio and MIDI devices and judges whether the CQ and RC-600 are present |
| `src/MidiMonitor.*` | Listens to every MIDI input and describes what arrives |
| `src/DeviceCheckPanel.*` | The stage 0 tab |
| `src/MainComponent.*` | The window: title, Copy report and the tabs |
| `src/Style.*` | Colours and the small shared widgets |
| `src/SelfTest.*` | Checks run by the automatic builds, including a full latency measurement against a pretend cable |
| `src/Main.cpp` | App start-up and the two windowless modes |
| `linux/install.sh` | Sets up the Linux rig computer: realtime tuning, the app, start-up straight into it |
| `linux/rig-check.sh` | Health check of the rig computer; changes nothing |
| `linux/rollback.sh` | Switches the rig back to an earlier build |
| `docs/linux-setup.md` | Step-by-step guide to the rig computer, and the laptop USB-stick trial |
| `CLAUDE.md` | Notes for Claude: working rules, the rig computer, and how to add an effect |

## Licence note

This app is released under the GNU AGPLv3 (see `LICENSE`). JUCE is used under
its free AGPLv3 option and the ASIO headers bundled with JUCE are GPLv3, so the
app's own code must use a matching licence whenever builds are shared, and a
public repository with downloadable builds counts as sharing. Anyone may read,
build and change the code, as long as what they share stays under the same
licence with its source. Selling a closed-source copy would need JUCE's paid
licence instead.
