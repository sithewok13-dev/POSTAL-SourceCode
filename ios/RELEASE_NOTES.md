<!--
  The GitHub release text for a release run of .github/workflows/ios.yml: a
  manual run of branch ios with "release" set to a tag such as ios-v0.1. The
  workflow publishes this file, as it is in the commit the run builds, as the
  release body (body_path). Update it before each release: the run stops
  before building unless this file has a "### What's in <tag>" heading line
  for that tag (for example "### What's in ios-v0.1"), written exactly like
  that and on a line of its own. A '-' in the version after "ios-"
  (ios-v0.2-beta1) makes a prerelease; without one (ios-v0.1) the release is
  marked as the latest release. Only the .ipa files named in RELEASE_IPAS in
  ios.yml are attached (postal-ios.ipa). The release run also adds the
  release to the AltStore / SideStore source (ios/altstore-source.json),
  using the list under "What's in <tag>" as the short "what's new" text.
  Links must be absolute: relative links don't resolve on a release page.
  Keep each paragraph and list item on one line: a release page turns every
  line break into a visible one.
-->

**POSTAL for iOS v0.1**: an unofficial iPhone port of **POSTAL** (1997), the isometric shooter from Running With Scissors, built from the [POSTAL source code](https://github.com/RWS-Studios/POSTAL-SourceCode) they released to their fans, with on-screen touch controls made for it.

**You need your own copy of POSTAL: Classic and Uncut; no game files are included.** Buy it on [GOG](https://www.gog.com/game/postal_classic_and_uncut) or [Steam](https://store.steampowered.com/app/232770/POSTAL/), then copy its `res` folder onto your iPhone (see *Install*).

**One app:** **POSTAL** (`postal-ios.ipa`). It plays every level of POSTAL: Classic and Uncut, Special Delivery and Super Postal included.

**New here?** *Known limitations*, *Requirements* and *Install* are near the bottom of this page, just above the download.

### What's in ios-v0.1

The first numbered release. Everything is new:

- **Twin-stick touch controls:** your left thumb moves (push to the edge to run), your right thumb aims, and pushing it all the way fires. FIRE and DUCK sit bottom right, and EXECUTE shows up above FIRE only when it can do something.
- **The whole screen:** POSTAL draws its own wider view to fit your iPhone, so nothing is stretched. The game's status line, health and weapon are drawn clear of the notch and the home bar.
- **Weapon wheel:** slide NEXT WPN or PREV WPN and every weapon is there at once, with the game's own icons and ammo counts.
- **Quick save and quick load:** hold SAVE or LOAD.
- **A cheat button** (a hand grenade): you can't die, you get every weapon with endless ammo, and the secret Deathwad.
- **Jump to any level:** with the cheat on, LEVEL lists all 22 levels.
- **Slow motion** at half or quarter speed, and a **follow cam** that watches someone else in the level.
- **Camera button:** one tap saves a screenshot to your Photos.
- **Tap the game's menus**, with a BACK button, and type the game's own cheat codes on the iPhone keyboard.
- **Install and update from SideStore or AltStore** with the app's own source.

Thank you to Running With Scissors for giving POSTAL's source code to the fans; see *Credits* at the bottom.

---

### How to play: every control explained

```
NEXT PREV gren status cam SAVE LOAD MENU
WPN  WPN

  move              aim         EXECUTE
  stick             stick
                           DUCK   FIRE
health armor      weapon ammo
```

*gren* is the cheat's grenade button and *cam* the camera button (each only while shown). The two sticks appear wherever your thumbs land. Hold MENU for the tray of extra buttons.

**Moving.** Put your left thumb down anywhere on the left part of the screen: a stick appears where it lands. Push a little to walk, push to the edge to run. Lift your thumb and the stick goes away.

**Aiming and firing.** Put your right thumb down anywhere on the right that isn't a button: a second stick appears there. The Postal Dude turns to face the way you push it. Push it all the way out (a red ring shows where) and he fires too, as long as you hold it there. Or hold **FIRE**; a drag on FIRE also aims.

**DUCK** crouches while you hold it. **EXECUTE** appears above FIRE only when someone wounded is lying within reach and you have the machine gun; hold it to finish them off, as in the PC game.

**Changing weapons.** Tap **NEXT WPN** or **PREV WPN** for the next or previous weapon. Slide either one a little, or hold it for 0.3 seconds, to open the **weapon wheel**: one slice per weapon, with the game's own icon and the ammo you have. Slide toward a weapon and lift to take it (or, if you held it open, tap one). Weapons you haven't found yet are faint, and ones out of ammo are grey. The gap at the bottom cancels. The game holds still while the wheel is open.

**Saving.** Tap **SAVE** for the game's save screen, or hold it for 0.3 seconds (a ring fills) for a **quick save**. Tap **LOAD** for the load screen, or hold it for a **quick load**. Slide off before the ring fills to cancel. A POSTAL save keeps the level you're on with your health, weapons and ammo; loading one starts that level again from its beginning, as on PC.

**MENU.** Tap it for the game's own menu. Hold it for 0.45 seconds to open the **tray**, a row of extra buttons under it:

- **CHEAT** shows the grenade button in the top row and switches the cheat on (you'll hear a laugh). Tap the grenade to switch the cheat on or off. CHEAT in the tray again hides the grenade and switches the cheat off.
- **LEVEL** (only while the cheat is on) opens a list of all 22 levels; tap one to go there. The game holds still while the list is open.
- **FOLLOW** (only while the cheat is on) turns the follow cam on or off: the camera follows someone else in the level while the Postal Dude waits. The small arrow next to the camera button moves on to the next person.
- **SLOW** steps through half speed, quarter speed and normal. A gold label shows the speed; tap it to step too.
- The **camera** icon shows or hides the camera button.
- **FPS** shows or hides a frame rate readout.
- The **keyboard** icon opens the iPhone keyboard, so you can type the game's own cheat codes.

**The cheat.** While it's on, you can't die, you have every weapon with full ammo (kept full) and armor, and you get the Deathwad, a secret weapon that only cheats give. Switching it off keeps the weapons you have, but the Deathwad goes, and so does the follow cam. The cheat starts off each time you open the app.

**The camera button** (top row): tap it for a screenshot of the game without the buttons. It goes to your Photos app (the first time, iOS asks; the app can only add photos, never see them). If you say no, it goes to Files > On My iPhone > POSTAL > Screenshots.

**On screen.** The top row shows the game's status line (how many people are left, how many hostiles, and how many you have to kill to finish the level), and the level's goal at the start. Bottom left: your health (red when it's low) and armor. Bottom centre: your weapon and its ammo.

**Dying.** "You died: tap the screen to restart" appears. Tap anywhere, or let go of FIRE and press it again, to start the level again.

**Menus.** Tap a menu item to choose it. In every menu but the title screen, a **BACK** button top left does what Escape does on PC.

**Small print.** The app remembers whether the camera button and the FPS readout are shown.

### Known limitations

**Sorry about the bugs.** This is an unofficial beta, made by a fan. Expect bugs, and back up your saves.

- **Multiplayer isn't in the app yet.** In the released source code, network play only works on Windows. A first fix has run on a Linux computer (two copies playing co-op over the network), but it isn't in this app and hasn't been tried between phones. Sorry; it's being worked on.
- **Testing:** the touch controls and every feature above were tested with the app's game code running on a computer, with real game data and a stand-in for the iPhone's screen. The app has had little testing on a real iPhone so far, and none on an iPad.
- **Game controllers and keyboards:** the game has its own controller support, but it hasn't been tried on iPhone.
- **Saves** restart the level you saved on (that's how POSTAL saves work).
- **Steam copies** should have the same `res` folder as GOG's, but only the GOG version has been tried.
- The level editor isn't included.

### Requirements

- An iPhone (or iPad) with **iOS 15 or newer**. iPads have never been tried.
- Your own copy of **POSTAL: Classic and Uncut** (GOG, or Steam untried). The app needs its `res` folder, with `res/shell/shell.sak` inside. It isn't POSTAL Redux, which is a different game.
- A way to sideload: **SideStore** or **AltStore Classic** (both work with a free Apple ID: apps need re-signing every 7 days, and you can have 3 sideloaded apps at once), or **Feather**, **Sideloadly** or a paid signing service.

### Install

1. **Get the app.** In SideStore or AltStore, add the source `https://raw.githubusercontent.com/sithewok13-dev/POSTAL-SourceCode/ios/ios/altstore-source.json` and install POSTAL from it. Or download `postal-ios.ipa` from the bottom of this page and sign it with your tool.
2. **Open POSTAL once.** It says "POSTAL: game files not found" and tells you where they go. That's expected: it has just made its folder.
3. **Find your game files.** On your PC, open the folder POSTAL: Classic and Uncut is installed in (with GOG Galaxy or Steam: *Manage*, then *Show folder* or *Browse local files*). Inside is a folder called `res`.
4. **Copy the `res` folder** to your iPhone, into **Files > On My iPhone > POSTAL** (for example with iCloud Drive, AirDrop, a USB stick, or Finder or iTunes file sharing). You should end up with `POSTAL/res/shell/shell.sak`. Copy the whole folder, not what's inside it.
5. **Open POSTAL again.** The game starts.
6. **Updating** keeps your files and saves, as long as you update with the same tool and Apple ID you installed with.
7. **Saves** are in Files > On My iPhone > POSTAL > `savegame`. Copy that folder somewhere safe before you delete or reinstall the app: deleting the app deletes its folder.

Found a bug? Please report it here, not to Running With Scissors, and say you're on **ios-v0.1** (the app shows its version, v0.1 and a build number, under the MENU tray): https://github.com/sithewok13-dev/POSTAL-SourceCode/issues

---

### Credits

- **Running With Scissors** made POSTAL and gave its source code to the fans. Thank you to Vince Desi, Mike Riedel, Jeff Diamond, Bill Harman, Jonathan Isabelle, Randy Briley and the whole team named in the game's credits: https://runningwithscissors.com
- **Ryan C. Gordon** (icculus) moved POSTAL to SDL for Linux and Mac in the 2013 update; this port is built on that work: https://icculus.org
- **Emile Belanger** made the Android port, whose tap-to-choose menu code the iPhone app uses; **Lana Foltz** and Emile did the 2015 update.
- **Mystyle-48** brought the newest open-source changes into the source repository, and **OtterCodes101** fixed the credits: https://github.com/RWS-Studios/POSTAL-SourceCode
- **SDL** runs the game's window, sound and input: https://www.libsdl.org

**Licences:** the POSTAL source code is under the GNU General Public License v2: https://github.com/sithewok13-dev/POSTAL-SourceCode/blob/ios/LICENSE. SDL is under the zlib licence: https://www.libsdl.org/license.php. The game's data (everything in `res`) belongs to Running With Scissors and isn't part of this app.

Not affiliated with or endorsed by Running With Scissors.
